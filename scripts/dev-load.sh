#!/usr/bin/env bash
# Build and start a dummy machine with the plugin (no real guest).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make plugin
SOCK=${SOCK:-/tmp/qemu-connect.sock}
rm -f "$SOCK"
echo "Loading plugin; socket=$SOCK (thread on)"
echo "In another shell: ./build/qemu-connect --socket $SOCK ping"
exec qemu-system-x86_64 -display none -machine none -accel tcg \
  -plugin "$ROOT/build/libqemu-connect.so,socket=$SOCK,socket_thread=on" \
  -monitor stdio
