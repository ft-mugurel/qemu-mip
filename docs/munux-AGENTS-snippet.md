# Snippet to paste into munux `AGENTS.md` (or Cursor/Claude rules)

Copy the block below into the **munux** repository so agents working only in
munux still verify under QEMU.

```markdown
## Runtime checks (qemu-connect)

After changes to boot, VGA, exceptions, or early bring-up:

1. Build ISO: `make iso`
2. Verify with **qemu-connect** (clone or path to ft-mugurel/qemu-mip):

```sh
# Adjust QC to your qemu-connect checkout
QC=/path/to/qemu-connect
make -C "$QC" plugin cli
make iso
"$QC/build/qemu-connect" run \
  --iso "$PWD/build/kernel.iso" \
  --plugin "$QC/build/libqemu-connect.so" \
  --expect '*** munux KERNEL PANIC ***' \
  --expect 'Invalid opcode' \
  --expect 'System halted.' \
  --timeout 60000
```

- Exit 0 = current bring-up panic path OK (intentional `ud2` end-state).
- Compile-only is not a runtime success.
- Agent guide: `$QC/AGENTS.md`
```
