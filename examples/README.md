# Examples

| Script | Purpose |
|--------|---------|
| `minimal-ping.sh` | Ping a running plugin socket |
| `munux-panic.sh` | One-shot `qemu-connect run` against munux panic screen |

```sh
./examples/munux-panic.sh
# or
make smoke
```

QMP (separate from plugin socket):

```sh
qemu-system-x86_64 ... -qmp unix:/tmp/q.qmp,server,nowait \
  -plugin ./build/libqemu-connect.so,socket=/tmp/p.sock
./build/qemu-connect --qmp /tmp/q.qmp qmp-ping
./build/qemu-connect --qmp /tmp/q.qmp quit
```
