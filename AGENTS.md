# Instructions for AI coding agents

**Read this before claiming a kernel change "works".**

This repository is **qemu-connect**: boot a guest under QEMU, scrape the VGA
console, type shell commands, without a human watching a window.

## Rule

| | |
|--|--|
| Compile only | **Not enough** |
| **`./build/qemu-connect guest …` (or `run`)** with exit **0** | Required for boot/shell work |

## Simplest usage (munux under `test/munux`)

```sh
make plugin cli
make -C test/munux iso disk   # once / after kernel changes

# Just boot and print the console
./build/qemu-connect guest

# Run a shell command (types it, waits for prompt, prints console)
./build/qemu-connect guest help
./build/qemu-connect guest ls
./build/qemu-connect guest cat hello.txt

# Same via make
make guest              # boot only
make guest CMD=help
make guest CMD='ls bin'
```

That one command:

1. Starts QEMU (ISO + disk + plugin + QMP)
2. Waits for `munux>`
3. Types your command + Enter (if given)
4. Waits for `munux>` again
5. Prints the guest console to stderr
6. Quits QEMU cleanly
7. Prints a small JSON summary on stdout (`"ok":true/false`)

### Exit codes

| Code | Meaning |
|-----:|---------|
| 0 | Success |
| 1 | expect/type failed |
| 2 | missing iso/disk/usage |
| 3 | QEMU crash |
| 4 | plugin/QMP connect failed |

## Slightly more control (`run`)

```sh
./build/qemu-connect run \
  --iso test/munux/build/kernel.iso \
  --disk test/munux/build/disk.img \
  --expect 'munux>' \
  --type help \
  --show
```

Steps run **in order**. Flags:

| Flag | Meaning |
|------|---------|
| `--iso` | CD image (required) |
| `--disk` | IDE disk (munux rootfs) |
| `--expect TEXT` | Wait until console contains TEXT |
| `--type TEXT` | Type TEXT then Enter |
| `--show` | Print console when done |
| `--timeout MS` | Per-expect wait (default 60000) |

## Current munux success signals

After pull (U6+), the guest should reach an interactive shell:

- `munux shell ready`
- prompt **`munux>`**

Example agent check:

```sh
./build/qemu-connect guest help
# console should list shell commands; exit 0
```

## What not to do

- Don’t stop at `make build` / `cargo build`.
- Don’t use interactive GUI QEMU for automation.
- Don’t use KVM for the plugin path (use TCG; `run`/`guest` already do).
- Don’t hand-assemble long QEMU flag lines — use **`guest`** or **`run`**.

## More docs

| File | Content |
|------|---------|
| [README.md](README.md) | Build overview |
| [docs/protocol.md](docs/protocol.md) | JSON protocol |
| [docs/architecture.md](docs/architecture.md) | Plugin + QMP |
| [docs/munux-AGENTS-snippet.md](docs/munux-AGENTS-snippet.md) | Paste into munux repo |

Grok skill: `.grok/skills/qemu-connect/` → `/qemu-connect`
