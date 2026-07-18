---
name: qemu-connect
description: >
  Verify freestanding kernels under QEMU using the qemu-connect TCG plugin and CLI
  (VGA console scrape, expect, run, QMP). Use when the user or task involves
  munux, KFS, hobby OS, kernel boot testing, QEMU smoke tests, "does it boot",
  runtime verification after kernel changes, or /qemu-connect. Prefer this over
  compile-only checks or interactive QEMU GUI.
---

# qemu-connect — agent skill

## Absolute rule

**Compile ≠ works.** After kernel/boot/console/exception changes, run the guest
with **qemu-connect** and require exit code **0**.

## Locate the tool

Prefer the repo that contains `build/libqemu-connect.so` and `build/qemu-connect`
(this project: **qemu-mip / qemu-connect**).

```sh
# From qemu-connect root
make plugin cli
```

Kernel ISO example (munux):

```sh
test -d test/munux || git clone git@github.com:ft-mugurel/munux.git test/munux
make -C test/munux iso
```

## Default verification command

```sh
./build/qemu-connect run \
  --iso test/munux/build/kernel.iso \
  --plugin ./build/libqemu-connect.so \
  --expect '*** munux KERNEL PANIC ***' \
  --expect 'Invalid opcode' \
  --expect 'System halted.' \
  --timeout 60000
```

Or: `make smoke`

### Exit codes

| Code | Meaning |
|-----:|---------|
| 0 | Pass |
| 1 | Expect failed / timeout |
| 2 | Missing ISO / usage |
| 3 | QEMU crash |
| 4 | Plugin/QMP connect fail |

Stdout JSON: `{"ok":true/false,"expects":[...],"exit_code":N}`

## munux success criteria (current)

x86_64 munux **intentionally** ends on VGA panic after `ud2`.  
Success = panic strings above — **not** a shell prompt (until munux adds one).

## On failure

1. Do not claim success.
2. Re-check ISO path and plugin path.
3. Capture console: `get_console --text-only` if you have a live socket, or re-run `run`.
4. Fix kernel or update expects if the kernel’s intentional end-state changed.

## Constraints

- Use **TCG** (`-accel tcg`), not KVM, for plugin checks.
- Plugin socket = console/status/mem_read; QMP = keys/quit.
- Full human doc: root `AGENTS.md` in the qemu-connect repo.

## When shell returns (future)

Replace expects with shell markers (e.g. `kfs>`) and optionally:

```sh
# after boot expect prompt
./build/qemu-connect --qmp ... type "help"
```
