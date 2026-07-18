# Paste into munux `AGENTS.md`

```markdown
## Runtime checks

```sh
QC=/path/to/qemu-connect   # ft-mugurel/qemu-mip checkout
make iso disk
make -C "$QC" plugin cli
"$QC/build/qemu-connect" guest help
# or from QC with munux at test/munux:  make guest CMD=help
```

Exit 0 + console showing the command output = verified.
```
