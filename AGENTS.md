# Instructions for AI coding agents

**Read this before claiming a kernel change "works".**

This repo is **qemu-connect**: a QEMU TCG plugin + CLI so agents can **boot a
guest, read the VGA console, and type shell commands** without a human GUI.

## Mandatory rule

| | |
|--|--|
| `make build` / `cargo build` only | **Not enough** |
| Runtime check with exit **0** | **Required** for boot / shell / FS work |

Preferred checks:

```sh
# single command
./build/qemu-connect guest help

# several commands (faster)
./build/qemu-connect session start --prompt '$'   # KFS shell
./build/qemu-connect session cmd help
./build/qemu-connect session type ':w' --no-enter # vi ex-mode chars (maps ':')
./build/qemu-connect session key esc
./build/qemu-connect session stop
```

Typing: **Enter is the default** for `type` / `session type` / `session cmd`.
Use `--no-enter` / MCP `enter: false` only for partial input (vi insert).
Punctuation map includes **`:` `!`** and common shell/vi symbols.

## If munux lives in another folder (recommended)

**Do not** rely on `$QEMU_CONNECT_ROOT/test/munux` — that is often a stale clone.

**Preferred (survives reinstall / missing shell env):** pin the tree in
`.qemu-connect.local` at the qemu-connect repo root (gitignored):

```sh
cat > .qemu-connect.local <<'EOF'
QEMU_CONNECT_ROOT=/absolute/path/to/qemu-connect
QEMU_CONNECT_MUNUX=/absolute/path/to/YOUR/munux-or-KFS
QEMU_CONNECT_PLUGIN=$HOME/.local/lib/qemu-connect/libqemu-connect.so
EOF
```

Also supported: process env, `~/.config/qemu-connect/env`, and Grok
`[mcp_servers.qemu-connect.env]` (see INSTALL.md).

```sh
export QEMU_CONNECT_MUNUX=/absolute/path/to/YOUR/munux
export QEMU_CONNECT_ROOT=/absolute/path/to/qemu-connect
make -C "$QEMU_CONNECT_MUNUX" iso disk
```

`guest` / `session` print the resolved ISO path on stderr — **check it** before
trusting results.

## One-time / after kernel changes

```sh
# From qemu-connect repo root
make plugin cli
make -C "$QEMU_CONNECT_MUNUX" iso disk   # or: make -C /path/to/KFS iso disk
```

## Simplest commands (use these)

```sh
./build/qemu-connect guest              # boot, wait for munux>, print console
./build/qemu-connect guest help         # type `help` + Enter, print console
./build/qemu-connect guest ls
./build/qemu-connect guest cat hello.txt
./build/qemu-connect guest about

# Make wrappers
make guest
make guest CMD=help
make guest CMD='ls bin'
```

### What `guest` does for you

1. Starts QEMU with munux **ISO + disk** + plugin + QMP (TCG)
2. Waits for prompt **`munux>`**
3. Types your shell line + Enter (if you passed one)
4. Waits for **`munux>`** again
5. Prints the guest console on **stderr**
6. Quits QEMU cleanly
7. Prints JSON on **stdout**: `{"ok":true/false,...,"exit_code":N}`

### Exit codes

| Code | Meaning |
|-----:|---------|
| **0** | Success |
| **1** | expect/type failed (wrong console / timeout) |
| **2** | missing ISO/disk or bad usage |
| **3** | QEMU crashed |
| **4** | plugin or QMP connect failed |

Success = exit **0** and console text matches what you intended (e.g. help list).

## More control: `run`

Use when you need custom ISO/disk paths or multi-step scripts:

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
| `--iso PATH` | CD image (**required** for `run`) |
| `--disk PATH` | IDE disk (munux rootfs) |
| `--expect TEXT` | Wait until console contains TEXT (order preserved) |
| `--type TEXT` | Type TEXT then Enter |
| `--show` | Print console when finished |
| `--timeout MS` | Per-expect timeout (default 60000) |
| `--plugin PATH` | default `build/libqemu-connect.so` |

Do **not** hand-roll long `qemu-system-x86_64 …` lines for normal checks.

## Current munux baseline (U6+)

munux boots to an **interactive shell** (not a panic screen):

| Signal | Meaning |
|--------|---------|
| `munux shell ready` | Shell started |
| **`munux>`** | Prompt (primary success marker) |
| FS lines like `ext2 mounted` | Disk attached correctly |

Typical boot scrape includes:

```text
munux x86_64
long mode OK
…
fs: ext2 mounted root=2
munux shell ready. Type `help`.
munux>
```

After `guest help`, console should include `munux shell commands:` and a new `munux>`.

## What not to do

- Stop at compile success
- Open interactive QEMU GUI for automation
- Use KVM for plugin checks (`guest`/`run` already use TCG)
- Expect old panic strings (`KERNEL PANIC` / `kfs>`) — outdated
- Manually juggle sockets/QMP unless debugging the tool itself

## Advanced (optional)

```sh
# Live sockets (debug only)
./build/qemu-connect --socket /tmp/p.sock status
./build/qemu-connect --socket /tmp/p.sock get_console --text-only
./build/qemu-connect --qmp /tmp/q.qmp quit
```

Protocol details: [docs/protocol.md](docs/protocol.md).

## More docs

| File | Content |
|------|---------|
| [README.md](README.md) | Build + overview |
| [docs/architecture.md](docs/architecture.md) | Plugin + QMP design |
| [docs/protocol.md](docs/protocol.md) | JSON wire protocol |
| [docs/munux-AGENTS-snippet.md](docs/munux-AGENTS-snippet.md) | Paste into munux repo |
| [test/README.md](test/README.md) | Local munux clone |

Grok skill: `.grok/skills/qemu-connect/` → `/qemu-connect`

## Multi-command without reboot (`session`) — preferred for agents

Boot **once**, run many shell commands, then stop:

```sh
./build/qemu-connect session start
./build/qemu-connect session cmd help
./build/qemu-connect session cmd ls
./build/qemu-connect session cmd about
./build/qemu-connect session console   # JSON with console field
./build/qemu-connect session stop
```

Every subcommand prints **one JSON object** on stdout (`ok`, `exit_code`, `console`, …).

| | one-shot `guest` | **`session`** |
|--|------------------|---------------|
| Boot cost | every call | **once** |
| Best for | single check | multi-step agent loops |
| Speed | ~1s+ per cmd | ~200ms per cmd after start |

MCP tools: `qemu_session_start`, `qemu_session_cmd`, `qemu_session_console`, `qemu_session_stop`.

