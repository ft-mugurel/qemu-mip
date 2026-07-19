#!/usr/bin/env bash
# Generate MCP host config with absolute paths.
set -euo pipefail

PREFIX="${HOME}/.local"
ROOT=""
GUEST=""
OUT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --root) ROOT="$2"; shift 2 ;;
    --guest) GUEST="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: gen-mcp-config.sh --root TOOL_REPO [--guest GUEST_KERNEL_TREE] [--prefix ~/.local] [--out path]"
      exit 0
      ;;
    *) echo "unknown arg $1"; exit 1 ;;
  esac
done

if [[ -z "$ROOT" ]]; then
  ROOT=$(cd "$(dirname "$0")/.." && pwd)
fi
ROOT=$(cd "$ROOT" && pwd)

MCP_JS="$PREFIX/share/qemu-connect/mcp/dist/index.js"
if [[ ! -f "$MCP_JS" && -f "$ROOT/mcp/dist/index.js" ]]; then
  MCP_JS="$ROOT/mcp/dist/index.js"
fi
PLUGIN="$PREFIX/lib/qemu-connect/libqemu-connect.so"
if [[ ! -f "$PLUGIN" && -f "$ROOT/build/libqemu-connect.so" ]]; then
  PLUGIN="$ROOT/build/libqemu-connect.so"
fi

if [[ -z "$OUT" ]]; then
  OUT="$ROOT/mcp/mcp.generated.json"
fi

mkdir -p "$(dirname "$OUT")"

if [[ -n "$GUEST" ]]; then
  GUEST=$(cd "$GUEST" && pwd)
  ENV_EXTRA=",
        \"QEMU_CONNECT_GUEST\": \"${GUEST}\""
else
  ENV_EXTRA=""
fi

cat > "$OUT" <<JSON
{
  "mcpServers": {
    "qemu-connect": {
      "command": "node",
      "args": [
        "${MCP_JS}"
      ],
      "env": {
        "QEMU_CONNECT_ROOT": "${ROOT}",
        "QEMU_CONNECT_PLUGIN": "${PLUGIN}"${ENV_EXTRA}
      }
    }
  }
}
JSON

echo "Wrote $OUT"
echo "  QEMU_CONNECT_ROOT   = $ROOT"
echo "  QEMU_CONNECT_PLUGIN = $PLUGIN"
if [[ -n "$GUEST" ]]; then
  echo "  QEMU_CONNECT_GUEST  = $GUEST"
else
  echo "  QEMU_CONNECT_GUEST  = (unset — uses \$ROOT/test/guest)"
fi
