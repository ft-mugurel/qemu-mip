# Install qemu-connect

## One command (recommended)

From a clone of this repo:

```sh
./scripts/install.sh
```

Installs into **`~/.local`** by default:

| Path | Content |
|------|---------|
| `~/.local/bin/qemu-connect` | CLI |
| `~/.local/lib/qemu-connect/libqemu-connect.so` | QEMU plugin |
| `~/.local/share/qemu-connect/` | docs + `env.sh` + `mcp.json` |
| `~/.local/bin/qemu-connect-mcp` | MCP stdio server (if Node available) |

### Then

```sh
export PATH="$HOME/.local/bin:$PATH"
export QEMU_CONNECT_ROOT=/path/to/this/repo   # where test/munux lives

# once
make -C "$QEMU_CONNECT_ROOT/test/munux" iso disk

qemu-connect guest help
```

### Custom prefix

```sh
PREFIX=/usr/local ./scripts/install.sh
# or
make install PREFIX=/opt/qemu-connect
make install-mcp PREFIX=/opt/qemu-connect
```

### MCP only / config

```sh
make install-mcp
# → ~/.local/share/qemu-connect/mcp.json
# merge into Cursor/Claude Desktop MCP settings
```

Regenerate config anytime:

```sh
./scripts/gen-mcp-config.sh --root "$PWD" --prefix "$HOME/.local"
```

### Uninstall

```sh
make uninstall
# PREFIX=/usr/local make uninstall
```

### Requirements

- QEMU with plugin support (`qemu-system-x86_64 -plugin help`)
- gcc, make, glib-2.0, `qemu-plugin.h`
- Optional: Node.js 18+ for MCP
