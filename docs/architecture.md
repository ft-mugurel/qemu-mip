# Architecture

## Goals

1. Let **coding agents** verify that a kernel **boots and behaves**, not only that it compiles.
2. Stay **out-of-tree**: ship a `.so` + CLI, no QEMU rebuild for users.
3. Work for **VGA-only hobby kernels** (no serial required for console scrape).
4. Stay **shareable**: documented protocol, stable-ish CLI, versioned plugin args.

## Components

```text
  Agent / CI / human
          │
          │  Unix socket (JSON lines)     optional QMP
          ▼                                    │
  ┌───────────────────┐                        │
  │  qemu-connect CLI │────────────────────────┤
  └─────────┬─────────┘                        │
            │                                  ▼
            │                         QEMU monitor / QMP
            ▼                         (keys, quit, status)
  ┌───────────────────────────────────────────────┐
  │  libqemu-connect.so  (TCG plugin)             │
  │    • control server                           │
  │    • VGA text shadow                          │
  │    • mem / reg peek (later)                   │
  │    • hang / exception hooks (later)           │
  └───────────────────────┬───────────────────────┘
                          │ qemu-plugin API
                          ▼
                       QEMU (TCG)
                          │
                     guest kernel
```

## Why TCG plugins

- Official dynamic extension point (`-plugin file=…`).
- Can observe translation, execution, memory, discontinuities.
- Modern QEMU also exposes guest mem/register read-write APIs.
- Any host code (sockets, threads) can live inside the `.so`.

## Why also QMP

Plugins are **not** a first-class virtual keyboard/UART.

| Concern | Owner |
|---------|--------|
| Console text (VGA scrape) | Plugin |
| Hang / panic observation | Plugin |
| Guest memory inspect | Plugin |
| Inject keypresses | QMP `send-key` / `input-send-event` |
| Clean process control | QMP `quit` + host process |

## Constraints

- **TCG only** while the plugin is loaded (no KVM).
- Plugin API version must match QEMU (`QEMU_PLUGIN_VERSION`).
- Distro QEMU must be built with plugins enabled.

## Guest opt-in (later)

Optional magic physical address `0xFEE1DEAD` for structured guest→host
messages (`AGENT_READY`, `AGENT_EXIT`, …) without requiring serial.

## Control socket (PR1)

The plugin opens a **Unix domain stream socket** on the host (default
`/tmp/qemu-connect.sock`, override with `socket=PATH`).

### Thread model

| Mode | Plugin arg | Behavior |
|------|------------|----------|
| **Default** | `socket_thread=on` | Dedicated **pthread** runs `poll()` on the listen (and client) FDs. Agents can `ping` even if the guest is in `hlt` / not translating any code. |
| Fallback | `socket_thread=off` | No thread; `qc_server_poll()` is driven from a TB-translate callback. Only useful for debugging; **not** reliable under idle/halt. |

Unload order: set stop flag → `shutdown`/`close` FDs (wakes `poll`) → `pthread_join` → free server state.

### Framing

- One JSON object per line (UTF-8), newline-terminated.
- Max line length: **8192** bytes; oversized lines without `\n` close the client.
- Sequential request/response; no pipelining required from clients.
- After `bind`, the socket path is `chmod` **0600** (best-effort).

### Plugin load example

```bash
qemu-system-x86_64 -display none -machine none -accel tcg \
  -plugin ./build/libqemu-connect.so,socket=/tmp/qemu-connect.sock,socket_thread=on
./build/qemu-connect --socket /tmp/qemu-connect.sock ping
```

## VGA text scrape (PR2)

Classic PC text mode lives at physical **`0xB8000`**: 80×25 cells, 2 bytes each
(`char` + attribute). Hobby kernels (including munux) write this region with
normal stores (often `u16` cells).

The plugin, on every translation block:

1. Registers a **store** memory callback on each instruction.
2. At runtime, if the store’s **physical** address falls in the VGA window,
   updates a host-side **shadow** of the character bytes (mutex-protected).
3. `get_console` returns that shadow as JSON (`source:"shadow"`).

**Important:** do **not** ignore `qemu_plugin_hwaddr_is_io()` — VGA may be
device memory and still be a valid text buffer.

Plugin arg `vga=off` disables instrumentation (shadow stays blank/spaces).
