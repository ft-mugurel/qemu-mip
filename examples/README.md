# Examples

Place guest-specific smoke scripts here once `expect` / console text land.

Planned:

- `munux-smoke.json` — boot munux/KFS, wait for shell prompt, run `help`
- `minimal-ping.sh` — only checks that the plugin socket answers `ping`

## minimal-ping.sh

```sh
#!/usr/bin/env bash
# Assumes QEMU is already running with the plugin.
set -euo pipefail
SOCK=${1:-/tmp/qemu-connect.sock}
./build/qemu-connect --socket "$SOCK" ping
```
