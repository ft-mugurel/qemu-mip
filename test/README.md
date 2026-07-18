# Test guests

Kernel trees used while developing **qemu-connect**. Nested clones under
`test/*/` are **gitignored**.

## munux

```sh
git clone git@github.com:ft-mugurel/munux.git test/munux
```

### Simple checks (from repo root)

```sh
make plugin cli
make -C test/munux iso disk

./build/qemu-connect guest              # boot + console
./build/qemu-connect guest help
make guest CMD=ls
```

### Make targets

| Target | What it does |
|--------|----------------|
| `make guest` / `CMD=…` | One-shot munux boot/type/show |
| `make test-munux-iso` | Build munux ISO |
| `make test-munux-shell` | Expect `munux>` shell ready |
| `make smoke` | Unit/QMP + munux shell checks when present |

### Success markers (current munux U6+)

1. `munux shell ready`
2. Prompt **`munux>`**
3. Optional: `guest help` shows `munux shell commands:`

Needs **ISO + disk.img** (ext2 rootfs).
