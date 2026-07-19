# Examples

| Script | Purpose |
|--------|---------|
| `guest-shell.sh` | Thin wrapper around `qemu-connect guest` |
| `minimal-ping.sh` | Ping a *already running* plugin socket |

```sh
# Preferred
./examples/guest-shell.sh
./examples/guest-shell.sh help
./examples/guest-shell.sh ls

# Same thing
make guest CMD=help
```

You do **not** need to build QEMU command lines by hand for normal tests.
