# Snippet for a kernel repository

Paste into your kernel's **`AGENTS.md`** (or Cursor/Claude project rules) so
agents that only open the kernel tree still verify under QEMU.

```markdown
## Runtime verification (required)

Compile is not enough. After boot/shell/FS/syscall changes:

```sh
# Set to your qemu-connect / qemu-mip checkout
QC=/path/to/qemu-connect

make iso disk
make -C "$QC" plugin cli

# Preferred: one command boots guest, types shell input, prints console
"$QC/build/qemu-connect" guest help
# more examples:
# "$QC/build/qemu-connect" guest ls
# "$QC/build/qemu-connect" guest cat hello.txt
```

Or from the qemu-connect repo with this tree at `test/guest`:

```sh
make guest CMD=help
```

- Exit **0** + console showing expected output = verified
- Prompt to look for: **`$`**
- Full guide: `$QC/AGENTS.md`
```
