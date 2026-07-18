# qemu-connect

Boot a kernel in QEMU, **read the screen**, and **type shell commands** — from a script or an AI agent. No GUI required.

Built as a **QEMU TCG plugin** (`.so`) plus a small **CLI**, with an optional **MCP** server for tool-calling hosts.

| | |
|--|--|
| **Repo** | [github.com/ft-mugurel/qemu-mip](https://github.com/ft-mugurel/qemu-mip) |
| **Latest** | See tags (`v1.0` CLI baseline, newer tags include MCP) |
| **Agents** | Start at **[AGENTS.md](AGENTS.md)** |

---

## What it does

```text
You / AI agent
    →  qemu-connect guest help
    →  QEMU + plugin + munux
    →  console text + exit 0/1
```

- **Eyes:** scrape classic VGA text (`0xB8000`) inside the guest  
- **Hands:** type keys via QMP  
- **Simple path:** one command — `guest`  
- **AI path:** same CLI, or MCP tools (`qemu_guest`, `qemu_run`, …)

Compile-only is not enough for kernels. This closes that gap.

---

## Install / build

**Needs:** QEMU with plugins (TCG), `gcc`, `make`, glib, `qemu-plugin.h`  
Optional for MCP: Node.js 18+

```sh
git clone git@github.com:ft-mugurel/qemu-mip.git
cd qemu-mip   # local folder may be named qemu-connect

make                  # → build/libqemu-connect.so + build/qemu-connect
```

```sh
# Arch
sudo pacman -S qemu-system-x86 base-devel glib2

# Debian/Ubuntu
sudo apt install qemu-system-x86 build-essential libglib2.0-dev
```

---

## Quick start (munux)

```sh
# Guest kernel (once)
git clone git@github.com:ft-mugurel/munux.git test/munux
make -C test/munux iso disk

# Tool
make plugin cli

# Boot and show the screen
./build/qemu-connect guest

# Type a shell command
./build/qemu-connect guest help
./build/qemu-connect guest ls
./build/qemu-connect guest cat hello.txt

# Same via make
make guest
make guest CMD=help
```

**Success:** process exit code **0**, console shows `munux>` (and your command output).

```text
$ ./build/qemu-connect guest help
…
munux> help
munux shell commands:
  help            This list
  about           Kernel summary
  …
munux>
{"ok":true,"exit_code":0}
```

---

## Commands

### `guest` — simplest (recommended)

```sh
./build/qemu-connect guest [shell words…]
```

Boots munux (ISO + disk), waits for `munux>`, types the command if given, prints the console, quits QEMU.

### `run` — custom steps

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
| `--expect TEXT` | Wait until console contains TEXT |
| `--type TEXT` | Type TEXT, then Enter |
| `--show` | Print console at the end |
| `--timeout MS` | Per-expect timeout (default 60000) |

### Other CLI

```sh
./build/qemu-connect ping|version|status     # against a live plugin socket
./build/qemu-connect expect 'munux>' --timeout 60000
./build/qemu-connect --qmp /path.qmp quit    # clean power-off
```

### Exit codes (`guest` / `run`)

| Code | Meaning |
|-----:|---------|
| 0 | OK |
| 1 | Expect/type failed |
| 2 | Missing ISO/disk or bad usage |
| 3 | QEMU crashed |
| 4 | Plugin/QMP connect failed |

---

## MCP (for Cursor, Claude Desktop, …)

Optional layer: the model calls **tools** instead of inventing shell lines.

```sh
cd mcp && npm install && npm run build
./scripts/mcp-smoke.sh          # optional self-test
```

| Tool | Does |
|------|------|
| `qemu_connect_info` | Paths / binaries present? |
| `qemu_build_guest` | Build plugin + munux ISO/disk |
| `qemu_guest` | Same as `guest` |
| `qemu_run` | Same as `run` |

Host config: [mcp/mcp.example.json](mcp/mcp.example.json) and [mcp/README.md](mcp/README.md).

---

## How it works (short)

```text
CLI / MCP
  ├─ plugin socket  →  VGA scrape, status, mem_read
  └─ QMP socket     →  keyboard, quit
         ↓
   QEMU (TCG) + guest kernel
```

You almost never need plugin flags when using `guest`. Details: [docs/architecture.md](docs/architecture.md), [docs/protocol.md](docs/protocol.md).

---

## Project layout

```text
plugin/     QEMU TCG plugin → libqemu-connect.so
cli/        Host CLI → qemu-connect
mcp/        MCP server (Node/TypeScript)
docs/       Architecture & protocol
AGENTS.md   Instructions for coding agents
test/       Local munux clone (gitignored)
```

---

## Tests

```sh
make test-ping
make guest CMD=help
make smoke                 # broader suite if test/munux exists
./scripts/mcp-smoke.sh     # MCP tools end-to-end
```

---

## Docs

| Doc | For |
|-----|-----|
| **[AGENTS.md](AGENTS.md)** | AI agents (read this) |
| [mcp/README.md](mcp/README.md) | MCP setup |
| [docs/architecture.md](docs/architecture.md) | Design |
| [docs/protocol.md](docs/protocol.md) | JSON control protocol |
| [test/README.md](test/README.md) | munux under `test/` |

---

## License

**GPL-2.0-or-later** (compatible with QEMU’s `qemu-plugin.h`).
