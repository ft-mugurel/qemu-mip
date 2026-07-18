# qemu-connect MCP server (v0.1 scaffold)

Thin **MCP** wrapper around the **qemu-connect CLI** (`v1.0+`).

Does **not** reimplement QEMU. It only:

1. Speaks MCP over **stdio**
2. Exposes tools
3. Runs `build/qemu-connect …` and returns output

## Tools (scaffold)

| Tool | Status | Maps to |
|------|--------|---------|
| `qemu_connect_info` | ✅ | path checks |
| `qemu_guest` | ✅ | `qemu-connect guest [cmd…]` |
| `qemu_run` | planned | `qemu-connect run …` |
| `qemu_build_guest` | planned | `make -C test/munux iso disk` |

## Setup

```sh
# From repo root
make plugin cli
make -C test/munux iso disk   # for qemu_guest

cd mcp
npm install
npm run build
```

## Run manually (stdio)

```sh
export QEMU_CONNECT_ROOT=/path/to/qemu-connect
node dist/index.js
# or: npm run dev
```

You normally do **not** run this by hand — the **AI host** spawns it.

## Connect an AI host

### Cursor / Claude Desktop style

Add to MCP config (path adjusted):

```json
{
  "mcpServers": {
    "qemu-connect": {
      "command": "node",
      "args": [
        "/ABSOLUTE/PATH/to/qemu-connect/mcp/dist/index.js"
      ],
      "env": {
        "QEMU_CONNECT_ROOT": "/ABSOLUTE/PATH/to/qemu-connect"
      }
    }
  }
}
```

Dev without build step:

```json
{
  "mcpServers": {
    "qemu-connect": {
      "command": "npx",
      "args": [
        "tsx",
        "/ABSOLUTE/PATH/to/qemu-connect/mcp/src/index.ts"
      ],
      "env": {
        "QEMU_CONNECT_ROOT": "/ABSOLUTE/PATH/to/qemu-connect"
      }
    }
  }
}
```

Restart the host; tools `qemu_connect_info` and `qemu_guest` should appear.

## Environment

| Variable | Meaning |
|----------|---------|
| `QEMU_CONNECT_ROOT` | Repo root (recommended in host config) |

If unset, the server walks up from `mcp/` to find the repo.

## Layout

```text
mcp/
  package.json
  tsconfig.json
  src/
    index.ts      # MCP server + tools
    paths.ts      # repo root resolution
    run-cli.ts    # spawn build/qemu-connect
  dist/           # after npm run build
  README.md
```

## Next steps (phase 2+)

- `qemu_run` tool with expect/type lists
- `qemu_build_guest` to build ISO/disk
- Tighter schemas and structured JSON content
- Optional install script for hosts

## License

GPL-2.0-or-later (same project).
