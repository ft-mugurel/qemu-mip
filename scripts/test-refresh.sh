#!/usr/bin/env bash
# PR3: shadow still works after halt; refresh:true times out when idle.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MUNUX="$ROOT/test/munux"
PLUGIN="$ROOT/build/libqemu-connect.so"
CLI="$ROOT/build/qemu-connect"
RUNTIME="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}"
SOCK="$RUNTIME/qemu-connect-refresh-$$.sock"
LOG="$RUNTIME/qemu-connect-refresh-$$.log"
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

make -C "$MUNUX" iso >/dev/null
ISO="$MUNUX/build/kernel.iso"

rm -f "$SOCK"
qemu-system-x86_64 \
  -display none -m 512M -accel tcg \
  -cdrom "$ISO" -boot order=d \
  -plugin "${PLUGIN},socket=${SOCK},vga=on,vga_refresh=on,vcpu_queue_timeout_ms=250" \
  -serial none -parallel none -monitor none -nographic \
  >"$LOG" 2>&1 &
QPID=$!

for _ in $(seq 1 100); do
  [[ -S "$SOCK" ]] && break
  sleep 0.1
done

# Try to catch a live refresh during early boot (soft).
got_refresh=0
for _ in $(seq 1 40); do
  out=$("$CLI" --socket "$SOCK" raw '{"cmd":"get_console","refresh":true}' 2>/dev/null || true)
  if echo "$out" | grep -q '"source":"refresh"'; then
    got_refresh=1
    echo "OK live refresh during boot"
    break
  fi
  sleep 0.15
done
if [[ "$got_refresh" -eq 0 ]]; then
  echo "WARN: no live refresh observed during boot (soft)"
fi

# Wait for panic (shadow)
"$CLI" --socket "$SOCK" expect '*** munux KERNEL PANIC ***' --timeout 60000

# (a) shadow still works after halt
json=$("$CLI" --socket "$SOCK" get_console)
echo "$json" | grep -q 'KERNEL PANIC' || { echo "FAIL shadow after halt"; exit 1; }
echo "$json" | grep -q '"source":"shadow"' || { echo "FAIL expected shadow source"; exit 1; }
echo "OK (a) shadow after halt"

# (b) refresh times out when permanently idle
start=$(date +%s%3N)
out=$("$CLI" --socket "$SOCK" raw '{"cmd":"get_console","refresh":true}' || true)
end=$(date +%s%3N)
elapsed=$((end - start))
echo "$out" | grep -q 'vcpu_idle_timeout' || {
  echo "FAIL expected vcpu_idle_timeout, got: $out" >&2
  exit 1
}
# should complete within ~2s for 250ms timeout (allow slack)
if (( elapsed > 3000 )); then
  echo "FAIL refresh hang: ${elapsed}ms" >&2
  exit 1
fi
echo "OK (b) refresh timeout in ${elapsed}ms"

echo "=== PASS test-refresh ==="
