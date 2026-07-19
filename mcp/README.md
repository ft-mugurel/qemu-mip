# qemu-connect MCP server (v0.5)

Thin **MCP** wrapper around the **qemu-connect CLI**.

## Tools

| Tool | Maps to |
|------|---------|
| `qemu_connect_info` | Paths + **vi recipe** + agent notes |
| `qemu_build_guest` | `make` tool and/or guest (`guest_path` optional) |
| `qemu_guest` | One-shot boot+cmd — **JSON always has `console`** |
| `qemu_run` | Custom expect/type — **JSON always has `console`** |
| `qemu_session_start` | Boot once — `iso`/`disk`/`prompt`; clear **disk locked** errors |
| `qemu_session_cmd` | Shell cmd; **`wait:false`** for vi/top |
| `qemu_session_type` | Type text; `enter` default true; console on expect timeout |
| `qemu_session_key` | Single QMP key (`esc`, `j`, `ret`… prefer j/k over arrows) |
| `qemu_session_script` | **Batch** of type/key/cmd/expect/console steps |
| `qemu_session_console` | VGA text; optional `console_lines` |
| `qemu_session_status` | Includes **`prompt`** + **`last_expect`** |
| `qemu_session_stop` | Tear down |

### Agent notes (v0.5)

- **Enter by default** on type/cmd — use `enter:false` / `wait:false` for vi.
- **Char map** includes `:` `!` and shell/vi punctuation.
- **`console_lines: N`** → last N non-blank lines (shell/help). **Inside vi: omit or `0` (full console)** — the editor fills with `~` lines that count as non-blank, so a tail can drop the real buffer.
- **Disk overlap** → `"disk locked by session X"` + stop hint (not opaque exit 3).
- **`qemu_session_script`** for multi-step vi without many MCP round-trips.
- Call `qemu_connect_info` for the copy-paste **vi recipe**.

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
