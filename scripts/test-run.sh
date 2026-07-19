#!/usr/bin/env bash
# run helper with disk + shell expects + type help
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI="$ROOT/build/qemu-connect"
GUEST="$ROOT/test/guest"
PLUGIN="$ROOT/build/libqemu-connect.so"

if [[ ! -d "$GUEST" ]]; then
  echo "SKIP guest"
  exit 0
fi

make -C "$ROOT" plugin cli
make -C "$GUEST" iso disk >/dev/null

out=$("$CLI" run \
  --iso "$GUEST/build/kernel.iso" \
  --disk "$GUEST/build/disk.img" \
  --plugin "$PLUGIN" \
  --expect '$' \
  --type help \
  --show \
  --timeout 60000)
echo "$out"
echo "$out" | grep -q '"ok":true' || { echo "FAIL run summary"; exit 1; }
echo "=== PASS test-run ==="
