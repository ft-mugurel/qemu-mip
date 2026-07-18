#!/usr/bin/env bash
# Simple munux smoke: boot + optional shell command.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make plugin cli
if [[ ! -f test/munux/build/kernel.iso || ! -f test/munux/build/disk.img ]]; then
  make -C test/munux iso disk
fi
exec ./build/qemu-connect guest "$@"
