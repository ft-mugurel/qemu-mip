# qemu-connect

**Agent-facing control plane for QEMU guests**, delivered as an official-style
**out-of-tree TCG plugin** (`.so`) plus a small host CLI.

Coding agents can boot a kernel under QEMU and **observe / drive** it without a
human watching the VGA window — and without forking QEMU.

> Status: **PR7+PR8** — `mem_read`/`status`/discon stats, optional hypercall at `0xFEE1DEAD`.
> Optional later: packaging polish (PR9).

## Why a plugin?

| Approach | Shareable | Guest changes | Deep visibility |
|----------|-----------|---------------|-----------------|
| Host serial + expect only | Yes | Usually need UART | Weak on VGA-only kernels |
| QEMU device in-tree | Hard (rebuild QEMU) | Driver optional | Medium |
| **TCG plugin (this)** | **`.so` anyone can load** | **Optional** | **Excellent** |

QEMU’s plugin API is the supported out-of-tree extension point. Combined with
**QMP** (keys / power) it forms a complete agent surface.

See [docs/architecture.md](docs/architecture.md).

**Implementation plan:** [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) (PR-ordered roadmap).

**For AI agents:** start at **[AGENTS.md](AGENTS.md)** (mandatory runtime checks). Grok skill: `.grok/skills/qemu-connect/` (`/qemu-connect`).

## Requirements

- QEMU built with **plugins** (`-plugin` works; QEMU **≥ 8** recommended, **11** tested)
- System emulation via **TCG** (not KVM) while the plugin is loaded
- `gcc`, `make`, `pkg-config`, **glib-2.0** headers
- `qemu-plugin.h` (often `/usr/include/qemu-plugin.h`)

```sh
# Arch example
sudo pacman -S qemu-system-x86 base-devel glib2

# Debian/Ubuntu example
sudo apt install qemu-system-x86 build-essential libglib2.0-dev
```

## Build

```sh
make          # → build/libqemu-connect.so  and  build/qemu-connect
make plugin
make cli
make clean
```

## Quick start

```sh
# Terminal A — boot any x86 guest with the plugin
qemu-system-x86_64 -display none -m 512M -cdrom your-kernel.iso \
  -plugin ./build/libqemu-connect.so,socket=/tmp/qemu-connect.sock

# Terminal B — agent / human CLI
./build/qemu-connect --socket /tmp/qemu-connect.sock ping
./build/qemu-connect version
```

Plugin arguments:

| Arg | Default | Meaning |
|-----|---------|---------|
| `socket=PATH` | `/tmp/qemu-connect.sock` | Unix domain control socket |
| `socket_thread=on\|off` | `on` | Dedicated poll thread (needed while guest is idle/`hlt`) |
| `vga=on\|off` | `on` | Instrument stores to scrape VGA text at `0xB8000` |
| `vga_refresh=on\|off` | `on` | Allow `get_console` with `refresh:true` |
| `vcpu_queue_timeout_ms=N` | `250` | Max wait for vCPU refresh/mem_read |
| `hypercall=on\|off` | `on` | Scrape guest stores to `0xFEE1DEAD` |

## Layout

```text
.
├── plugin/           # TCG plugin sources → libqemu-connect.so
│   ├── agent.c       # qemu_plugin_install entry
│   ├── mem.c         # store callbacks → VGA scrape
│   ├── queue.c       # vCPU work queue (refresh/mem_read)
│   ├── hypercall.c  # 0xFEE1DEAD agent messages
│   ├── vga.c         # VGA text shadow (+ mutex)
│   ├── server.c      # Unix socket server (thread)
│   └── protocol.c    # request/response handlers
├── cli/              # Host CLI → qemu-connect
├── include/          # Shared headers / protocol constants
├── docs/             # Architecture + wire protocol
├── examples/         # Sample agent scripts (later)
└── Makefile
```

## Protocol (v0.1)

Line-oriented JSON over the Unix socket. See [docs/protocol.md](docs/protocol.md).

```text
→ {"cmd":"ping"}
← {"ok":true,"result":{"pong":true,"name":"qemu-connect","proto":"0.1"}}
```

## Roadmap

- [x] Repo skeleton, plugin load, control socket, CLI `ping`
- [x] Dedicated socket thread + framing + `make test-ping` (PR1)
- [x] Instrument stores to VGA text RAM (`0xB8000`) → real `get_console` (PR2)
- [x] `expect` / timeout helpers in CLI (PR4)
- [x] QMP helper for `send-key` / quit (PR5)
- [ ] Optional guest hypercall (`0xFEE1DEAD`) for exit codes
- [x] Example smoke scripts for hobby kernels
- [ ] Optional MCP server for coding agents

## License

**GPL-2.0-or-later** — required for compatibility with `qemu-plugin.h`.

## Remote

Add your GitHub/GitLab remote after you create it:

```sh
git remote add origin git@github.com:YOU/qemu-connect.git
git push -u origin main
```
