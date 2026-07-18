#!/usr/bin/env bash
# PR4 primary smoke: munux panic screen via CLI expect (shadow path).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MUNUX="$ROOT/test/munux"
PLUGIN="$ROOT/build/libqemu-connect.so"
CLI="$ROOT/build/qemu-connect"
RUNTIME="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}"
SOCK="$RUNTIME/qemu-connect-smoke-$$.sock"
LOG="$RUNTIME/qemu-connect-smoke-$$.log"
QPID=""

if [[ ! -d "$MUNUX" ]]; then
  echo "SKIP munux (test/munux missing)"
  exit 0
fi

cleanup() {
  if [[ -n "${QPID}" ]] && kill -0 "$QPID" 2>/dev/null; then
    kill "$QPID" 2>/dev/null || true
    wait "$QPID" 2>/dev/null || true
  fi
  rm -f "$SOCK" "$LOG"
}
trap cleanup EXIT

echo "=== munux ISO ==="
make -C "$MUNUX" iso
ISO="$MUNUX/build/kernel.iso"
[[ -f "$ISO" ]] || { echo "FAIL: no ISO"; exit 1; }

rm -f "$SOCK"
qemu-system-x86_64 \
  -display none -m 512M -accel tcg \
  -cdrom "$ISO" -boot order=d \
  -plugin "${PLUGIN},socket=${SOCK},vga=on,vga_refresh=on" \
  -serial none -parallel none -monitor none -nographic \
  >"$LOG" 2>&1 &
QPID=$!

for _ in $(seq 1 100); do
  [[ -S "$SOCK" ]] && break
  kill -0 "$QPID" 2>/dev/null || { echo "FAIL: QEMU died"; cat "$LOG"; exit 1; }
  sleep 0.1
done
[[ -S "$SOCK" ]] || { echo "FAIL: no socket"; exit 1; }

"$CLI" --socket "$SOCK" expect '*** munux KERNEL PANIC ***' --timeout 60000
"$CLI" --socket "$SOCK" expect 'Invalid opcode' --timeout 5000
"$CLI" --socket "$SOCK" expect 'System halted.' --timeout 5000

# Soft checks
text=$("$CLI" --socket "$SOCK" get_console --text-only || true)
echo "$text" | grep -q 'CPU exception' && echo "OK soft: CPU exception" || true
echo "$text" | grep -q 'vector=6' && echo "OK soft: vector=6" || true

echo "=== PASS test-munux-panic ==="
