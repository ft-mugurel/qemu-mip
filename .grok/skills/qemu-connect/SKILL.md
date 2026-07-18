---
name: qemu-connect
description: >
  Verify freestanding kernels under QEMU with qemu-connect (guest/run helpers).
  Use for munux/KFS boot tests, shell commands under QEMU, "does it boot",
  runtime checks after kernel edits, or /qemu-connect. Prefer guest over raw QEMU flags.
---

# qemu-connect skill

## Rule

**Compile ≠ works.** Use:

```sh
make plugin cli
make -C test/munux iso disk   # if needed
./build/qemu-connect guest              # boot + show console
./build/qemu-connect guest help         # type help
./build/qemu-connect guest ls
```

Or: `make guest` / `make guest CMD=help`

Exit **0** required. Console is printed on stderr; JSON summary on stdout.

## Full control

```sh
./build/qemu-connect run --iso … --disk … --expect 'munux>' --type help --show
```

## munux baseline

Expect **`munux>`** shell (not KERNEL PANIC). See root `AGENTS.md`.
