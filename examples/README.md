# Examples

| Script | Purpose |
|--------|---------|
| `minimal-ping.sh` | Ping a running plugin socket |
| `munux-panic.sh` | Build plugin + run primary munux panic smoke |

Primary automated guest check:

```sh
make smoke
# or
./examples/munux-panic.sh
```

CLI expect (against a live socket):

```sh
./build/qemu-connect --socket /tmp/qemu-connect.sock \
  expect '*** munux KERNEL PANIC ***' --timeout 60000
```
