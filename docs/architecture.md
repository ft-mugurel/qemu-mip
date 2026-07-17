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
