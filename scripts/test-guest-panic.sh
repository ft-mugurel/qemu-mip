#!/usr/bin/env bash
# Compat alias: panic-era smoke replaced by interactive shell smoke.
exec "$(dirname "$0")/test-guest-shell.sh"
