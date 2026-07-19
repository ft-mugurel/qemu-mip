---
name: qemu-connect
description: >
  Verify freestanding kernels under QEMU with qemu-connect. Prefer the simple
  `guest` helper (boot guest, type shell commands, print console). Use for
  guest kernel, boot tests, shell under QEMU, "does it boot", runtime checks after
  kernel edits, make guest, or /qemu-connect. Never treat compile-only as success.
---

# qemu-connect

## Rule

**Compile ≠ works.** After kernel/boot/shell/FS changes, run a guest check with **exit 0**.

## Kernel path (critical)

Guest defaults come from, in order:

1. `QEMU_CONNECT_ISO` / `QEMU_CONNECT_DISK`
2. `QEMU_CONNECT_GUEST` env
3. **`$QEMU_CONNECT_ROOT/.qemu-connect.local`** (project pin)
4. `~/.config/qemu-connect/env`
5. Fallback only: `$ROOT/test/guest` (often a stale clone — **avoid**)

This machine’s dev kernel is pinned in `.qemu-connect.local`:

`QEMU_CONNECT_GUEST=/home/mtu/MTU/xAI/trace/test1-2/guest`

Before testing, confirm the boot line:

```text
guest: guest  iso=.../test1-2/guest/build/kernel.iso  disk=.../test1-2/guest/build/disk.img
```

If you see `.../qemu-connect/test/guest/...`, the pin is missing — fix `.qemu-connect.local` or env.

Rebuild guest after kernel edits:

```sh
make -C /home/mtu/MTU/xAI/trace/test1-2/guest iso disk
```

## Default commands (preferred)

### Multi-command (P0 — use this for several shell cmds)

```sh
qemu-connect session start --prompt '$'   # guest; or omit if .qemu-connect.local sets it
qemu-connect session cmd help
qemu-connect session cmd ls
# vi-style: keys without shell prompt wait
qemu-connect session key esc
qemu-connect session type ':wq' --enter
qemu-connect session stop
```

MCP: `qemu_session_start` → `qemu_session_cmd` / `type` / `key` / **`script`** → `stop`.  
- `session_start`: **`iso`**, **`disk`**, **`prompt`**
- `session_cmd`: **`wait: false`** for vi/top (no prompt wait)
- `session_type`: **Enter by default**; expect timeout still returns **console**
- `session_script`: batch steps in one call (vi recipes)
- `guest`/`run`: success JSON **always includes `console`**
- disk overlap: error **`disk locked by session X`**
- `console_lines: N` for last N non-blank lines (shell). **In vi: omit / `0` (full)** — `~` lines dominate a non-blank tail
- Prefer **j/k** over arrow keys in vi  
Results are **JSON** (`ok`, `console`, `exit_code`).

### One-shot

```sh
qemu-connect guest              # boot + show console
qemu-connect guest help         # type help
qemu-connect guest ls
qemu-connect guest cat hello.txt
```

- Console text → **stderr**
- JSON summary → **stdout** (`"ok":true`, `"exit_code":0`)
- Prompt to wait for: **`$`** (not `kfs>`, not KERNEL PANIC)
- First stderr line shows **which ISO/disk** was used — verify it is guest

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
qemu-connect run \
  --iso /home/mtu/MTU/xAI/trace/test1-2/guest/build/kernel.iso \
  --disk /home/mtu/MTU/xAI/trace/test1-2/guest/build/disk.img \
  --expect '$' \
  --type help \
  --show
```

## Do not

- Hand-assemble long QEMU CLI for normal verification
- Use interactive GUI as the agent test
- Gate on obsolete panic-era strings
- Use `test/guest` when the real tree is guest

Full detail: root **`AGENTS.md`**.
