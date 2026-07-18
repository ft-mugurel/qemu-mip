# Examples

| Script | Purpose |
|--------|---------|
| `munux-guest.sh` | Thin wrapper around `qemu-connect guest` |
| `minimal-ping.sh` | Ping a *already running* plugin socket |

```sh
# Preferred
./examples/munux-guest.sh
./examples/munux-guest.sh help
./examples/munux-guest.sh ls

# Same thing
make guest CMD=help
```

You do **not** need to build QEMU command lines by hand for normal tests.
