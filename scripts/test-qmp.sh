#!/usr/bin/env bash
# PR5: QMP greeting + capabilities + query-status + quit on -machine none.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI="$ROOT/build/qemu-connect"
RUNTIME="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}"
QSOCK="$RUNTIME/qemu-connect-qmp-test-$$.qmp"
LOG="$RUNTIME/qemu-connect-qmp-test-$$.log"
QPID=""

cleanup() {
  if [[ -n "${QPID}" ]] && kill -0 "$QPID" 2>/dev/null; then
    kill "$QPID" 2>/dev/null || true
    wait "$QPID" 2>/dev/null || true
  fi
  rm -f "$QSOCK" "$LOG"
}
trap cleanup EXIT

rm -f "$QSOCK"
qemu-system-x86_64 -display none -machine none -accel tcg \
  -qmp "unix:${QSOCK},server,nowait" \
  -monitor none -serial none -parallel none -nographic \
  >"$LOG" 2>&1 &
QPID=$!

for _ in $(seq 1 50); do
  [[ -S "$QSOCK" ]] && break
  sleep 0.05
done
[[ -S "$QSOCK" ]] || { echo "FAIL: no qmp socket"; exit 1; }

"$CLI" --qmp "$QSOCK" qmp-ping | grep -q running
echo "OK qmp-ping"

# send-key ret should succeed (return {})
# Use raw via key command
"$CLI" --qmp "$QSOCK" key ret
echo "OK key ret"

"$CLI" --qmp "$QSOCK" quit
echo "OK quit"

# QEMU should exit
for _ in $(seq 1 30); do
  if ! kill -0 "$QPID" 2>/dev/null; then
    QPID=""
    echo "=== PASS test-qmp ==="
    exit 0
  fi
  sleep 0.1
done
echo "FAIL: QEMU still running after quit" >&2
exit 1
