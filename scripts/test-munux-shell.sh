#!/usr/bin/env bash
# Primary munux smoke: boot to munux> via guest helper.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if [[ ! -d test/munux ]]; then
  echo "SKIP munux (test/munux missing)"
  exit 0
fi

make plugin cli
if [[ ! -f test/munux/build/kernel.iso || ! -f test/munux/build/disk.img ]]; then
  make -C test/munux iso disk
fi

./build/qemu-connect guest
echo "=== PASS test-munux-shell ==="
