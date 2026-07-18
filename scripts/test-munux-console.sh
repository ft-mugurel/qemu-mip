#!/usr/bin/env bash
# Compat: console smoke → shell guest helper
exec "$(dirname "$0")/test-munux-shell.sh"
