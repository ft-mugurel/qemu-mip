#!/usr/bin/env bash
# Agent-oriented wrapper: boot munux and assert panic screen.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
make plugin cli
exec make test-munux-panic
