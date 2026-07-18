---
name: qemu-connect
description: >
  Verify freestanding kernels under QEMU with qemu-connect. Prefer the simple
  `guest` helper (boot munux, type shell commands, print console). Use for
  munux/KFS, boot tests, shell under QEMU, "does it boot", runtime checks after
  kernel edits, make guest, or /qemu-connect. Never treat compile-only as success.
---

# qemu-connect

## Rule

**Compile ≠ works.** After kernel/boot/shell/FS changes, run a guest check with **exit 0**.

## Default commands (preferred)

### Multi-command (P0 — use this for several shell cmds)

```sh
./build/qemu-connect session start
./build/qemu-connect session cmd help
./build/qemu-connect session cmd ls
./build/qemu-connect session stop
```

MCP: `qemu_session_start` → `qemu_session_cmd` → `qemu_session_stop`.  
Results are **JSON** (`ok`, `console`, `exit_code`).

### One-shot


From the **qemu-connect** repo root:

```sh
make plugin cli
make -C test/munux iso disk    # when kernel or rootfs changed

./build/qemu-connect guest              # boot + show console
./build/qemu-connect guest help         # type help
./build/qemu-connect guest ls
./build/qemu-connect guest cat hello.txt

make guest
make guest CMD=help
```

- Console text → **stderr**
- JSON summary → **stdout** (`"ok":true`, `"exit_code":0`)
- Prompt to wait for: **`munux>`** (not `kfs>`, not KERNEL PANIC)

## Exit codes

| Code | Meaning |
|-----:|---------|
| 0 | Pass |
| 1 | expect/type failed |
| 2 | missing ISO/disk |
| 3 | QEMU crash |
| 4 | connect failed |

## Custom scripts

```sh
./build/qemu-connect run \
  --iso test/munux/build/kernel.iso \
  --disk test/munux/build/disk.img \
  --expect 'munux>' \
  --type help \
  --show
```

## Do not

- Hand-assemble long QEMU CLI for normal verification
- Use interactive GUI as the agent test
- Gate on obsolete panic-era strings

Full detail: root **`AGENTS.md`**.
