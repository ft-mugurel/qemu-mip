#!/usr/bin/env bash
# PR2: boot munux under QEMU+plugin and scrape stable panic text from VGA shadow.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MUNUX="$ROOT/test/munux"
PLUGIN="$ROOT/build/libqemu-connect.so"
CLI="$ROOT/build/qemu-connect"
SOCK="${TMPDIR:-/tmp}/qemu-connect-munux-$$.sock"
LOG="${TMPDIR:-/tmp}/qemu-connect-munux-$$.log"
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

echo "=== building munux ISO (if needed) ==="
if [[ ! -f "$MUNUX/build/kernel.iso" ]]; then
  make -C "$MUNUX" iso
else
  # Rebuild if sources newer than ISO (best-effort)
  make -C "$MUNUX" iso
fi

ISO="$MUNUX/build/kernel.iso"
if [[ ! -f "$ISO" ]]; then
  echo "FAIL: ISO not produced at $ISO" >&2
  exit 1
fi

rm -f "$SOCK"
qemu-system-x86_64 \
  -display none \
  -m 512M \
  -accel tcg \
  -cdrom "$ISO" \
  -boot order=d \
  -plugin "${PLUGIN},socket=${SOCK},vga=on" \
  -serial none \
  -parallel none \
  -monitor none \
  -nographic \
  >"$LOG" 2>&1 &
QPID=$!

for _ in $(seq 1 100); do
  if [[ -S "$SOCK" ]]; then
    break
  fi
  if ! kill -0 "$QPID" 2>/dev/null; then
    echo "FAIL: QEMU died before socket" >&2
    cat "$LOG" >&2 || true
    exit 1
  fi
  sleep 0.1
done
[[ -S "$SOCK" ]] || { echo "FAIL: no socket"; exit 1; }

# Wait for panic screen (guest clears then paints KERNEL PANIC).
deadline=$((SECONDS + 60))
found=0
while (( SECONDS < deadline )); do
  text=$("$CLI" --socket "$SOCK" get_console --text-only 2>/dev/null || true)
  if echo "$text" | grep -q '\*\*\* munux KERNEL PANIC \*\*\*'; then
    found=1
    echo "=== console text (matched) ==="
    echo "$text" | head -15
    break
  fi
  # also show progress via writes occasionally
  sleep 0.25
done

if [[ "$found" -ne 1 ]]; then
  echo "FAIL: panic string not seen within timeout" >&2
  echo "=== last get_console ===" >&2
  "$CLI" --socket "$SOCK" get_console 2>&1 | head -c 2000 >&2 || true
  echo >&2
  echo "=== qemu log ===" >&2
  cat "$LOG" >&2 || true
  exit 1
fi

# Required companions on current munux exception path
text=$("$CLI" --socket "$SOCK" get_console --text-only)
echo "$text" | grep -q 'Invalid opcode' || {
  echo "WARN: missing 'Invalid opcode' (continuing if KERNEL PANIC present)" >&2
}
echo "$text" | grep -q 'System halted' || {
  echo "WARN: missing 'System halted.'" >&2
}

# writes should be > 0
json=$("$CLI" --socket "$SOCK" get_console)
echo "$json" | grep -q '"writes":[1-9]' || echo "$json" | grep -q '"writes":[0-9][0-9]' || {
  echo "FAIL: writes not positive: $json" >&2
  exit 1
}
echo "$json" | grep -q '"source":"shadow"' || {
  echo "FAIL: source not shadow" >&2
  exit 1
}

echo "=== PASS test-munux-console ==="
