# Instructions for AI coding agents

**Read this before claiming a kernel change "works".**

This repo is **qemu-connect**: a QEMU TCG plugin + CLI so agents can **boot a
guest, read the VGA console, and type shell commands** without a human GUI.

## Mandatory rule

| | |
|--|--|
| `make build` / `cargo build` only | **Not enough** |
| Runtime check with exit **0** | **Required** for boot / shell / FS work |

Preferred check:

```sh
./build/qemu-connect guest help
```

## One-time / after kernel changes

```sh
# From qemu-connect repo root
make plugin cli
make -C test/munux iso disk
```

Clone munux if missing:

```sh
git clone git@github.com:ft-mugurel/munux.git test/munux
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
