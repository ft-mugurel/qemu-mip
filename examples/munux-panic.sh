#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make plugin cli
make -C test/munux iso
exec ./build/qemu-connect run \
  --iso test/munux/build/kernel.iso \
  --plugin ./build/libqemu-connect.so \
  --expect '*** munux KERNEL PANIC ***' \
  --expect 'Invalid opcode' \
  --expect 'System halted.' \
  --timeout 60000
