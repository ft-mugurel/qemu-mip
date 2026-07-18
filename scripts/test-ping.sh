#!/usr/bin/env bash
# PR1 acceptance: plugin control socket works with zero guest code.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLUGIN="$ROOT/build/libqemu-connect.so"
CLI="$ROOT/build/qemu-connect"
SOCK="${TMPDIR:-/tmp}/qemu-connect-test-ping-$$.sock"
LOG="${TMPDIR:-/tmp}/qemu-connect-test-ping-$$.log"
QPID=""

cleanup() {
  if [[ -n "${QPID}" ]] && kill -0 "$QPID" 2>/dev/null; then
    kill "$QPID" 2>/dev/null || true
    wait "$QPID" 2>/dev/null || true
  fi
  rm -f "$SOCK" "$LOG"
}
trap cleanup EXIT

rm -f "$SOCK"

# -machine none: no guest firmware / no TB work required for our thread.
qemu-system-x86_64 \
  -display none \
  -machine none \
  -accel tcg \
  -plugin "${PLUGIN},socket=${SOCK}" \
  -monitor none \
  -serial none \
  -parallel none \
  -nic none \
  -nographic \
  >"$LOG" 2>&1 &
QPID=$!

# Wait for the control socket (plugin install + bind).
for _ in $(seq 1 50); do
  if [[ -S "$SOCK" ]]; then
    break
  fi
  if ! kill -0 "$QPID" 2>/dev/null; then
    echo "FAIL: QEMU exited before creating socket" >&2
    cat "$LOG" >&2 || true
    exit 1
  fi
  sleep 0.1
done

if [[ ! -S "$SOCK" ]]; then
  echo "FAIL: socket not created: $SOCK" >&2
  cat "$LOG" >&2 || true
  exit 1
fi

# Socket mode should be 0600 when the FS honors chmod on AF_UNIX.
mode=$(stat -c '%a' "$SOCK" 2>/dev/null || echo "?")
if [[ "$mode" != "600" && "$mode" != "?" ]]; then
  echo "WARN: socket mode is $mode (expected 600); continuing" >&2
fi
if [[ "$mode" == "600" ]]; then
  echo "OK socket mode 0600"
fi

echo "=== ping / version (x3 connect-disconnect) ==="
for i in 1 2 3; do
  out=$("$CLI" --socket "$SOCK" ping)
  echo "$out" | grep -q '"ok":true' || { echo "FAIL ping: $out" >&2; exit 1; }
  echo "$out" | grep -q '"pong":true' || { echo "FAIL pong: $out" >&2; exit 1; }
  out=$("$CLI" --socket "$SOCK" version)
  echo "$out" | grep -q 'qemu-connect' || { echo "FAIL version: $out" >&2; exit 1; }
  echo "  round $i OK"
done

echo "=== partial line framing ==="
python3 - "$SOCK" <<'PY'
import socket, sys, time
path = sys.argv[1]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(3.0)
s.connect(path)
s.sendall(b'{"cmd":"pi')
time.sleep(0.05)
s.sendall(b'ng"}\n')
data = b""
while b"\n" not in data:
    chunk = s.recv(4096)
    if not chunk:
        break
    data += chunk
s.close()
text = data.decode()
if '"ok":true' not in text or "pong" not in text:
    print("FAIL partial-line response:", text, file=sys.stderr)
    sys.exit(1)
print("OK partial-line:", text.strip())
PY

echo "=== oversized line drops client cleanly ==="
python3 - "$SOCK" <<'PY'
import socket, sys
path = sys.argv[1]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(3.0)
s.connect(path)
# 9000 bytes without newline — server max is 8192
s.sendall(b"x" * 9000)
# Server should close; recv may return b"" or raise.
try:
    r = s.recv(64)
except OSError:
    r = b""
s.close()
# Reconnect and ping must still work
s2 = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s2.settimeout(3.0)
s2.connect(path)
s2.sendall(b'{"cmd":"ping"}\n')
data = b""
while b"\n" not in data:
    chunk = s2.recv(4096)
    if not chunk:
        break
    data += chunk
s2.close()
if b'"ok":true' not in data:
    print("FAIL after oversized:", data, file=sys.stderr)
    sys.exit(1)
print("OK oversized-line recovery")
PY

echo "=== PASS test-ping ==="
