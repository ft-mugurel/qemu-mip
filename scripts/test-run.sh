#!/usr/bin/env bash
# PR6: one-shot run against munux panic strings.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI="$ROOT/build/qemu-connect"
MUNUX="$ROOT/test/munux"
PLUGIN="$ROOT/build/libqemu-connect.so"

if [[ ! -d "$MUNUX" ]]; then
  echo "SKIP munux"
  exit 0
fi

make -C "$MUNUX" iso >/dev/null
ISO="$MUNUX/build/kernel.iso"

out=$("$CLI" run \
  --iso "$ISO" \
  --plugin "$PLUGIN" \
  --expect '*** munux KERNEL PANIC ***' \
  --expect 'System halted.' \
  --timeout 60000)
echo "$out"
echo "$out" | grep -q '"ok":true' || { echo "FAIL run summary"; exit 1; }
echo "=== PASS test-run ==="
