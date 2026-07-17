#!/usr/bin/env bash
# Assumes QEMU is already running with libqemu-connect.so loaded.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SOCK=${1:-/tmp/qemu-connect.sock}
exec "$ROOT/build/qemu-connect" --socket "$SOCK" ping
