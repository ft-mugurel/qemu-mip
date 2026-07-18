# qemu-connect

**Agent-facing control plane for QEMU guests**: out-of-tree **TCG plugin** + host
CLI. Coding agents can boot a kernel, **read the VGA console**, and **type shell
commands** without a human watching a window.

> Status: usable end-to-end for munux (shell, disk, `guest` helper).  
> Protocol **0.4**. Optional later: packaging polish (PR9).

**For AI agents:** **[AGENTS.md](AGENTS.md)** — start there.  
Grok skill: `.grok/skills/qemu-connect/` (`/qemu-connect`).

## Why

| Approach | Shareable | Guest changes | Agent-friendly |
|----------|-----------|---------------|----------------|
| Compile only | — | — | Blind after link |
| Serial + expect | Yes | Often need UART | Weak on VGA-only kernels |
| **qemu-connect** | **`.so` + CLI** | **Optional** | **VGA scrape + keys** |

Plugin = **eyes** (console / status / mem_read).  
QMP = **hands** (type keys / quit).  
`guest` / `run` wire both for you.

## Requirements

- QEMU with **plugins** (`-plugin`; QEMU ≥ 8, **11** tested)
- **TCG** for plugin path (not KVM)
- `gcc`, `make`, `pkg-config`, **glib-2.0**, `qemu-plugin.h`

```sh
# Arch
sudo pacman -S qemu-system-x86 base-devel glib2

# Debian/Ubuntu
sudo apt install qemu-system-x86 build-essential libglib2.0-dev
```

## Build

```sh
make              # build/libqemu-connect.so + build/qemu-connect
make plugin cli
make clean
```

## Quick start (munux)

```sh
# optional: git clone git@github.com:ft-mugurel/munux.git test/munux
make plugin cli
make -C test/munux iso disk

./build/qemu-connect guest              # boot → show console
./build/qemu-connect guest help         # type a command
./build/qemu-connect guest ls
make guest CMD='cat hello.txt'
```

That’s the whole agent loop: **build → guest → exit 0**.

### Example success

```text
$ ./build/qemu-connect guest help
run: wait for munux>
run: type help
-------- guest console --------
…
munux> help
munux shell commands:
  help            This list
  …
munux>
-------------------------------
{"ok":true,"duration_ms":1165,"exit_code":0}
```

## `run` (custom steps)

```sh
./build/qemu-connect run \
  --iso test/munux/build/kernel.iso \
  --disk test/munux/build/disk.img \
  --expect 'munux>' \
  --type help \
  --show
```

| Flag | Meaning |
|------|---------|
| `--iso` | CD image |
| `--disk` | IDE disk |
| `--expect TEXT` | Wait for console text (ordered) |
| `--type TEXT` | Type + Enter (ordered) |
| `--show` | Print console when done |
| `--timeout MS` | Per-expect timeout |

## Plugin arguments

| Arg | Default | Meaning |
|-----|---------|---------|
| `socket=PATH` | `/tmp/qemu-connect.sock` | Control socket |
| `socket_thread=on\|off` | `on` | Poll thread (works while guest idle) |
| `vga=on\|off` | `on` | Scrape VGA text at `0xB8000` |
| `vga_refresh=on\|off` | `on` | Allow `get_console` `refresh:true` |
| `vcpu_queue_timeout_ms=N` | `250` | Wait for mem_read/refresh |
| `hypercall=on\|off` | `on` | Optional guest window at `0xFEE1DEAD` |

You rarely need these when using **`guest`**.

## Layout

```text
.
├── AGENTS.md           # AI agent instructions (read this)
├── plugin/             # → libqemu-connect.so
├── cli/                # → qemu-connect (guest, run, QMP, …)
├── docs/               # architecture, protocol, munux snippet
├── examples/
├── test/               # local munux clone (gitignored)
└── .grok/skills/       # Grok /qemu-connect skill
```

## Protocol

Line-oriented JSON over a Unix socket — see **[docs/protocol.md](docs/protocol.md)** (v0.4).

```text
→ {"cmd":"ping"}
← {"ok":true,"result":{"pong":true,"proto":"0.4",…}}
```

## Tests

```sh
make test-ping
make guest CMD=help
make smoke          # broader suite when munux is present
```

## MCP (experimental)

Thin MCP server wrapping this CLI — see **[mcp/README.md](mcp/README.md)**.

```sh
cd mcp && npm install && npm run build
# point Cursor/Claude at mcp/dist/index.js (see mcp/mcp.example.json)
```

## Docs

| Doc | Content |
|-----|---------|
| [AGENTS.md](AGENTS.md) | **Agent workflow** |
| [docs/architecture.md](docs/architecture.md) | Design |
| [docs/protocol.md](docs/protocol.md) | Wire protocol |
| [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) | Historical PR plan |
| [test/README.md](test/README.md) | munux under `test/` |

## License

**GPL-2.0-or-later** (compatible with `qemu-plugin.h`).

## Remote

```sh
git remote add origin git@github.com:ft-mugurel/qemu-mip.git
git push -u origin main
```
