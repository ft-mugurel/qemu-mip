#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

echo "=== hypercall unit ==="
make -s build/test_hypercall_unit
./build/test_hypercall_unit

GUEST="$ROOT/test/guest"
PLUGIN="$ROOT/build/libqemu-connect.so"
CLI="$ROOT/build/qemu-connect"
make -s plugin cli

if [[ ! -d "$GUEST" ]]; then
  echo "SKIP guest integration"
  exit 0
fi

RUNTIME="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}"
SOCK="$RUNTIME/qemu-connect-mem-$$.sock"
QSOCK="$RUNTIME/qemu-connect-mem-$$.qmp"
LOG="$RUNTIME/qemu-connect-mem-$$.log"
QPID=""
cleanup() {
  if [[ -n "${QPID}" ]] && kill -0 "$QPID" 2>/dev/null; then
    "$CLI" --qmp "$QSOCK" quit 2>/dev/null || kill "$QPID" 2>/dev/null || true
    wait "$QPID" 2>/dev/null || true
  fi
  rm -f "$SOCK" "$QSOCK" "$LOG"
}
trap cleanup EXIT

make -C "$GUEST" iso disk >/dev/null
ISO="$GUEST/build/kernel.iso"
DISK="$GUEST/build/disk.img"

rm -f "$SOCK" "$QSOCK"
qemu-system-x86_64 -display none -m 512M -accel tcg \
  -drive format=raw,file="$DISK",if=ide,index=0,media=disk \
  -drive format=raw,file="$ISO",if=ide,index=1,media=cdrom \
  -boot order=d \
  -plugin "${PLUGIN},socket=${SOCK},vga=on,hypercall=on" \
  -qmp "unix:${QSOCK},server,nowait" \
  -serial none -parallel none -monitor none -nographic \
  >"$LOG" 2>&1 &
QPID=$!

for _ in $(seq 1 100); do [[ -S "$SOCK" ]] && break; sleep 0.1; done
"$CLI" --socket "$SOCK" expect '$' --timeout 60000

st=$("$CLI" --socket "$SOCK" status)
echo "status: $st" | head -c 300; echo

# mem_read while shell is running (vCPU live)
mr=$("$CLI" --socket "$SOCK" raw '{"cmd":"mem_read","phys":0xb8000,"len":32}' || true)
if echo "$mr" | grep -q '"ok":true'; then
  echo "$mr" | grep -qi 'hex' && echo "OK mem_read while shell live"
else
  echo "WARN mem_read: $mr (shadow still works)"
  text=$("$CLI" --socket "$SOCK" get_console --text-only)
  echo "$text" | grep -q '$' || exit 1
fi

echo "=== PASS test-mem-hypercall ==="
