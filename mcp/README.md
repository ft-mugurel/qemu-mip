# qemu-connect MCP server (v0.3)

Thin **MCP** wrapper around the **qemu-connect CLI**.

## Tools

| Tool | Maps to |
|------|---------|
| `qemu_connect_info` | Path / binary diagnostics |
| `qemu_build_guest` | `make plugin cli` and/or `make -C test/munux iso disk` |
| `qemu_guest` | `qemu-connect guest [cmd…]` |
| `qemu_run` | `qemu-connect run …` |
| `qemu_session_start` | Boot once |
| `qemu_session_cmd` | Shell cmd without reboot |
| `qemu_session_console` | Read VGA text |
| `qemu_session_status` | Session + guest status |
| `qemu_session_stop` | Tear down |

## Setup

```sh
# Repo root
make plugin cli
cd mcp && npm install && npm run build
```

## Host config

See `mcp.example.json`. Set absolute paths:

```json
{
  "mcpServers": {
    "qemu-connect": {
      "command": "node",
      "args": ["/ABS/qemu-connect/mcp/dist/index.js"],
      "env": { "QEMU_CONNECT_ROOT": "/ABS/qemu-connect" }
    }
  }
}
```

## Smoke test (without an IDE)

```sh
export QEMU_CONNECT_ROOT=/path/to/qemu-connect
./scripts/mcp-smoke.sh   # from repo root after build
```

## License

GPL-2.0-or-later
