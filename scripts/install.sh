#!/usr/bin/env bash
# Easy install: CLI + plugin (+ optional MCP) into ~/.local by default.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PREFIX="${PREFIX:-$HOME/.local}"
WITH_MCP="${WITH_MCP:-1}"

echo "==> Building qemu-connect"
make -j"$(nproc 2>/dev/null || echo 2)" plugin cli

echo "==> Installing to PREFIX=$PREFIX"
make install PREFIX="$PREFIX"

if [[ "$WITH_MCP" == "1" ]]; then
  if command -v npm >/dev/null && command -v node >/dev/null; then
    echo "==> Installing MCP"
    make install-mcp PREFIX="$PREFIX"
  else
    echo "==> Skipping MCP (node/npm not found). Install later: make install-mcp"
  fi
fi

BIN="$PREFIX/bin"
echo ""
echo "=========================================="
echo "  qemu-connect installed"
echo "=========================================="
echo ""
echo "1) Ensure PATH includes:  $BIN"
echo "   (add to ~/.bashrc if needed:)"
echo "     export PATH=\"$BIN:\$PATH\""
echo ""
echo "2) Point at this tool checkout:"
echo "     export QEMU_CONNECT_ROOT=$ROOT"
echo ""
echo "3) Point at your freestanding kernel (ISO + optional disk):"
echo "     export QEMU_CONNECT_GUEST=/path/to/your/kernel"
echo "     export QEMU_CONNECT_PROMPT='\$'   # match your guest shell prompt"
echo "     make -C \"\$QEMU_CONNECT_GUEST\" iso disk   # or your build system"
echo ""
echo "4) Try:"
echo "     qemu-connect guest help"
echo "     qemu-connect session start && qemu-connect session cmd help && qemu-connect session stop"
echo ""
if [[ -f "$PREFIX/share/qemu-connect/mcp.json" ]]; then
  echo "5) MCP: merge this into Cursor/Claude config:"
  echo "     $PREFIX/share/qemu-connect/mcp.json"
  echo "   Or run:  qemu-connect-mcp   (stdio server)"
  echo ""
fi
echo "Uninstall:  make uninstall PREFIX=$PREFIX"
