#!/usr/bin/env bash
# Primary guest smoke: boot to $ via guest helper.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if [[ ! -d test/guest ]]; then
  echo "SKIP guest (test/guest missing)"
  exit 0
fi

make plugin cli
if [[ ! -f test/guest/build/kernel.iso || ! -f test/guest/build/disk.img ]]; then
  make -C test/guest iso disk
fi

./build/qemu-connect guest
echo "=== PASS test-guest-shell ==="
