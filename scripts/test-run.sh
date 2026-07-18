#!/usr/bin/env bash
# run helper with disk + shell expects + type help
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI="$ROOT/build/qemu-connect"
MUNUX="$ROOT/test/munux"
PLUGIN="$ROOT/build/libqemu-connect.so"

if [[ ! -d "$MUNUX" ]]; then
  echo "SKIP munux"
  exit 0
fi

make -C "$ROOT" plugin cli
make -C "$MUNUX" iso disk >/dev/null

out=$("$CLI" run \
  --iso "$MUNUX/build/kernel.iso" \
  --disk "$MUNUX/build/disk.img" \
  --plugin "$PLUGIN" \
  --expect 'munux>' \
  --type help \
  --show \
  --timeout 60000)
echo "$out"
echo "$out" | grep -q '"ok":true' || { echo "FAIL run summary"; exit 1; }
echo "=== PASS test-run ==="
