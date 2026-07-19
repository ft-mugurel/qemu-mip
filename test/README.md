# Test guests

Optional kernel trees used while developing **qemu-connect**. Nested clones under
`test/*/` are **gitignored**.

## Layout

Place any freestanding kernel that builds an ISO (and optional disk) here:

```sh
# example — use your own kernel remote
git clone <your-kernel-repo-url> test/guest
make -C test/guest iso disk   # or whatever your Makefile targets are
```

Expected artifacts (defaults):

- `test/guest/build/kernel.iso`
- `test/guest/build/disk.img` (if your guest needs a root disk)

Or skip `test/guest` entirely and set:

```sh
export QEMU_CONNECT_GUEST=/absolute/path/to/your/kernel
export QEMU_CONNECT_PROMPT='$'   # match your shell prompt
```

### Simple checks (from repo root)

```sh
make plugin cli
# with QEMU_CONNECT_GUEST or test/guest present:
./build/qemu-connect guest
./build/qemu-connect guest help
make guest CMD=ls
```

### Make targets

| Target | What it does |
|--------|----------------|
| `make guest` / `CMD=…` | One-shot boot/type/show |
| `make test-guest-iso` | Build ISO under `test/guest` if present |
| `make test-guest-shell` | Boot and expect shell prompt |
| `make smoke` | Unit/QMP + guest checks when a guest tree is present |

### Success markers

Configure **`QEMU_CONNECT_PROMPT`** (or `--prompt`) to match your guest.
Common values: `$`, `#`, `>`, or a custom banner string.

Needs **ISO** (and **disk.img** if your kernel mounts one).
