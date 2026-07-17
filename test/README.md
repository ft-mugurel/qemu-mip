# Test guests

Kernel trees used while developing **qemu-connect**. They are **not** shipped
inside this repository (see root `.gitignore`).

## munux

```sh
git clone git@github.com:ft-mugurel/munux.git test/munux
# or HTTPS:
# git clone https://github.com/ft-mugurel/munux.git test/munux
```

Build / boot (from `test/munux`):

```sh
make iso          # or make help
# then load with the plugin, e.g.:
qemu-system-x86_64 -display none -m 512M -cdrom build/kernel.iso \
  -plugin ../../build/libqemu-connect.so,socket=/tmp/qemu-connect.sock
```

See munux `README.md` and `SMOKE.md` for targets and checklists.
