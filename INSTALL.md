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
export QEMU_CONNECT_ROOT=/path/to/this/repo

# Pin your real kernel (preferred — survives reinstall; do not use test/guest)
cat > "$QEMU_CONNECT_ROOT/.qemu-connect.local" <<EOF
QEMU_CONNECT_ROOT=$QEMU_CONNECT_ROOT
QEMU_CONNECT_GUEST=/absolute/path/to/your/kernel
QEMU_CONNECT_PLUGIN=$HOME/.local/lib/qemu-connect/libqemu-connect.so
# QEMU_CONNECT_PROMPT=$   # or $ for shell-style shells
EOF

make -C /absolute/path/to/your/kernel iso disk
qemu-connect guest help   # stderr first line must show your iso path
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

## Point at your own guest tree (not the bundled test clone)

**Why env alone felt broken:** `make install-mcp` used to regenerate MCP config
without `QEMU_CONNECT_GUEST`, so the tool fell back to `$ROOT/test/guest`.
Also, shell env is not the same as Grok MCP process env.

**Durable pin (recommended):**

```sh
# project-local (gitignored), read by CLI + MCP wrapper
cat > .qemu-connect.local <<'EOF'
QEMU_CONNECT_GUEST=/absolute/path/to/your/kernel
QEMU_CONNECT_PROMPT=$
EOF
# optional global fallback:
# ~/.config/qemu-connect/env  (same KEY=VALUE format)
```

Or export for a single shell:

```sh
export QEMU_CONNECT_GUEST=/absolute/path/to/your/guest
export QEMU_CONNECT_ROOT=/absolute/path/to/qemu-connect
export QEMU_CONNECT_PLUGIN=$HOME/.local/lib/qemu-connect/libqemu-connect.so
make -C "$QEMU_CONNECT_GUEST" iso disk
qemu-connect guest help
```

### Grok MCP

Prefer the wrapper (loads `.qemu-connect.local` automatically):

```toml
[mcp_servers.qemu-connect]
command = "qemu-connect-mcp"
args = []
enabled = true
tool_timeout_sec = 300

[mcp_servers.qemu-connect.env]
QEMU_CONNECT_ROOT = "/home/YOU/path/to/qemu-connect"
QEMU_CONNECT_PLUGIN = "/home/YOU/.local/lib/qemu-connect/libqemu-connect.so"
QEMU_CONNECT_GUEST = "/home/YOU/path/to/your/guest"
QEMU_CONNECT_PROMPT = "$"
```

```sh
./scripts/gen-mcp-config.sh \
  --root /path/to/qemu-connect \
  --guest /path/to/your/guest \
  --out ~/.local/share/qemu-connect/mcp.json
```

Then: edit `~/.grok/config.toml`; in Grok `/mcps` press `r` to reload.
