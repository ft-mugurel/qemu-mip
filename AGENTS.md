# Instructions for AI coding agents

**Read this before claiming a kernel change "works".**

This repository is **qemu-connect**: a QEMU TCG plugin + CLI so agents can
**boot a guest and check the console** without a human watching VGA.

## Rule (mandatory)

| Step | Required? |
|------|-----------|
| Edit kernel / build | Yes |
| **`make build` / `cargo build` only** | **Not enough** |
| **Run guest via qemu-connect and check exit code** | **Yes** for boot/console/exception work |

If you only compile, you have **not** verified runtime behavior.

## What this tool is

```text
Agent  →  qemu-connect CLI  →  plugin socket (console, expect, status, mem_read)
                           →  QMP socket (keys, quit)   [used by `run`]
                           →  QEMU (TCG) + guest kernel (e.g. munux)
```

- **Plugin** scrapes VGA text at physical `0xB8000` (no serial required).
- **QMP** injects keys / clean quit.
- Always use **TCG** (`-accel tcg`). Do **not** use KVM with this plugin path.

## Quick setup

```sh
# In this repo
make plugin cli

# Kernel under test (example: munux)
# Already may exist as test/munux (gitignored clone)
test -d test/munux || git clone git@github.com:ft-mugurel/munux.git test/munux
make -C test/munux iso
```

Paths below assume you are in the **qemu-connect** repo root.

## Preferred agent workflow (one-shot)

```sh
make plugin cli
make -C test/munux iso

./build/qemu-connect run \
  --iso test/munux/build/kernel.iso \
  --plugin ./build/libqemu-connect.so \
  --expect '*** munux KERNEL PANIC ***' \
  --expect 'Invalid opcode' \
  --expect 'System halted.' \
  --timeout 60000
```

Or full suite:

```sh
make smoke
```

### Exit codes (`run`)

| Code | Meaning | What you should do |
|-----:|---------|-------------------|
| **0** | All expects matched | Safe to report success for those asserts |
| **1** | Expect timeout / wrong console | Read failure text; fix kernel or expects |
| **2** | ISO missing / bad usage | Build ISO; fix paths |
| **3** | QEMU crashed | Check QEMU log under `$XDG_RUNTIME_DIR` / `/tmp` |
| **4** | Plugin or QMP connect failed | Rebuild plugin; check QEMU has `-plugin` |

### JSON summary (stdout)

```json
{"ok":true,"duration_ms":702,"expects":[{"pattern":"...","ok":true}],"exit_code":0}
```

Treat `"ok":true` and exit 0 as the success signal. On failure, run
`get_console --text-only` on a live session or re-run `run` and paste the JSON
into your reasoning.

## Current munux baseline (important)

The **x86_64 munux** tree under `test/munux` currently:

1. Prints early banners
2. Deliberately executes **`ud2`**
3. Exception handler paints a **stable panic screen** on VGA

**Success today** means the scraped console contains:

1. `*** munux KERNEL PANIC ***`
2. `Invalid opcode` (or `#UD`)
3. `System halted.`

This is **not** a bug in qemu-connect. Do **not** wait for `kfs>` until munux
grows an interactive shell again. When the shell returns, **update expects**.

## Other useful commands

```sh
# Live session (two concepts: plugin socket vs QMP)
./build/qemu-connect --socket /tmp/p.sock ping
./build/qemu-connect --socket /tmp/p.sock expect 'KERNEL PANIC' --timeout 60000
./build/qemu-connect --socket /tmp/p.sock get_console --text-only
./build/qemu-connect --socket /tmp/p.sock status
./build/qemu-connect --socket /tmp/p.sock raw '{"cmd":"mem_read","phys":0xb8000,"len":32}'

./build/qemu-connect --qmp /tmp/q.qmp qmp-ping
./build/qemu-connect --qmp /tmp/q.qmp key ret
./build/qemu-connect --qmp /tmp/q.qmp type "help"
./build/qemu-connect --qmp /tmp/q.qmp quit
```

Manual QEMU (if not using `run`):

```sh
qemu-system-x86_64 -display none -m 512M -accel tcg \
  -cdrom test/munux/build/kernel.iso -boot order=d \
  -plugin ./build/libqemu-connect.so,socket=/tmp/p.sock \
  -qmp unix:/tmp/q.qmp,server,nowait \
  -nographic
```

## Decision tree

```text
Did you change boot / VGA / exceptions / early init?
  YES → make iso + qemu-connect run (or make smoke)
  NO  → compile may be enough; still run smoke if unsure

Did run exit 0?
  YES → you may say the checked console conditions hold
  NO  → do not claim success; inspect expects / console / logs
```

## What not to do

- Do not open an interactive GUI QEMU and “assume” it booted.
- Do not use `-enable-kvm` for plugin-based checks.
- Do not require serial UART for basic console scrape (VGA scrape is the path).
- Do not treat compile success as runtime success.
- Do not kill QEMU with only `kill -9` when QMP `quit` is available (`run` already quits cleanly).

## Optional guest hypercall (advanced)

Guests may store a 16-byte message at phys `0xFEE1DEAD` (magic `0x544E4351` /
`QCNT`). See [docs/protocol.md](docs/protocol.md). munux does not need this.

## More docs

| Doc | Use |
|-----|-----|
| [README.md](README.md) | Build, status, layout |
| [docs/protocol.md](docs/protocol.md) | JSON wire protocol |
| [docs/architecture.md](docs/architecture.md) | Plugin + QMP design |
| [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) | Full roadmap |
| [test/README.md](test/README.md) | munux clone under `test/` |

## Skill (Grok)

If using Grok Build, project skill: `.grok/skills/qemu-connect/SKILL.md`  
Invoke with `/qemu-connect` or when verifying a kernel under QEMU.
