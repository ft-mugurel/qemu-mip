#!/usr/bin/env bash
# Simple guest smoke: boot + optional shell command.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make plugin cli
if [[ ! -f test/guest/build/kernel.iso || ! -f test/guest/build/disk.img ]]; then
  make -C test/guest iso disk
fi
exec ./build/qemu-connect guest "$@"
