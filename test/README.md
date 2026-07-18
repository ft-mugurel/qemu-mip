# Test guests

Kernel trees used while developing **qemu-connect**. Nested clones under
`test/*/` are **gitignored** (see root `.gitignore`).

## munux

```sh
git clone git@github.com:ft-mugurel/munux.git test/munux
```

### Make targets (from repo root)

| Target | What it does |
|--------|----------------|
| `make test-munux-iso` | `make -C test/munux iso` |
| `make test-munux-panic` | Boot ISO + plugin; `expect` panic strings |
| `make test-refresh` | Shadow after halt + `refresh` timeout |
| `make smoke` | ping + vga-unit + panic + refresh |

Primary assertions (current munux end-state):

1. `*** munux KERNEL PANIC ***`
2. `Invalid opcode` / `#UD`
3. `System halted.`
