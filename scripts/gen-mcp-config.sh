#!/usr/bin/env bash
# Generate an MCP host config with absolute paths.
set -euo pipefail

PREFIX="${HOME}/.local"
ROOT=""
OUT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --root) ROOT="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: gen-mcp-config.sh --root REPO [--prefix ~/.local] [--out path]"
      exit 0
      ;;
    *) echo "unknown arg $1"; exit 1 ;;
  esac
done

if [[ -z "$ROOT" ]]; then
  ROOT=$(cd "$(dirname "$0")/.." && pwd)
fi
ROOT=$(cd "$ROOT" && pwd)
PREFIX=$(cd "$PREFIX" 2>/dev/null && pwd || echo "$PREFIX")

MCP_JS="$PREFIX/share/qemu-connect/mcp/dist/index.js"
if [[ ! -f "$MCP_JS" ]]; then
  # dev fallback
  if [[ -f "$ROOT/mcp/dist/index.js" ]]; then
    MCP_JS="$ROOT/mcp/dist/index.js"
  fi
fi

if [[ -z "$OUT" ]]; then
  OUT="$ROOT/mcp/mcp.generated.json"
fi

mkdir -p "$(dirname "$OUT")"
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
        "QEMU_CONNECT_PLUGIN": "${PREFIX}/lib/qemu-connect/libqemu-connect.so"
      }
    }
  }
}
JSON

echo "Wrote $OUT"
echo "  QEMU_CONNECT_ROOT=$ROOT"
echo "  MCP entry: $MCP_JS"
