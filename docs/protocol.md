# Control protocol (v0.2)

Transport: **Unix domain stream socket**, one JSON object per line (UTF-8),
request/response.

Default path: `/tmp/qemu-connect.sock`  
Override: `-plugin …,socket=/path/to.sock`

## Framing (PR1)

- One JSON object **per line**, terminated by `\n` (optional `\r` stripped)
- Maximum request line: **8192** bytes
- One response line per request (responses may be up to ~32 KiB for `get_console`)
- Partial writes that eventually form a full line are supported
- A line longer than the limit without `\n` drops the client

## Request

```json
{"cmd":"<command>", ...optional fields...}
```

## Response

```json
{"ok":true,"result":{...}}
{"ok":false,"error":"<message>"}
```

## Commands

| cmd | Description | Status |
|-----|-------------|--------|
| `ping` | Liveness | implemented |
| `version` | Name + protocol + mem counters | implemented |
| `get_console` | VGA text from store shadow | implemented (v0.2) |

### ping

```text
→ {"cmd":"ping"}
← {"ok":true,"result":{"pong":true,"name":"qemu-connect","proto":"0.2"}}
```

### version

```text
→ {"cmd":"version"}
← {"ok":true,"result":{"name":"qemu-connect","proto":"0.2","mem_cb_fired":…,"mem_cb_vga_hit":…}}
```

### get_console (shadow-only, default)

```text
→ {"cmd":"get_console"}
← {"ok":true,"result":{
     "cols":80,
     "rows":25,
     "writes":1234,
     "text_len":2024,
     "source":"shadow",
     "mem_cb_fired":…,
     "mem_cb_vga_hit":…,
     "text":"…80×25 with \\n between rows…"
   }}
```

| Field | Meaning |
|-------|---------|
| `cols` / `rows` | Geometry (80×25) |
| `writes` | Number of in-range store updates applied to the shadow |
| `text_len` | Length of `text` in characters (including newlines) |
| `source` | Always `"shadow"` in PR2 (no hwaddr refresh yet) |
| `text` | JSON-escaped console characters only (attributes discarded) |
| `mem_cb_*` | Instrumentation counters (debug / overhead) |

`dirty` is cleared on snapshot server-side; not currently exposed.

## Plugin arguments

| Arg | Default | Meaning |
|-----|---------|---------|
| `socket=PATH` | `/tmp/qemu-connect.sock` | Control socket |
| `socket_thread=on\|off` | `on` | Dedicated poll thread |
| `vga=on\|off` | `on` | Instrument guest stores for 0xB8000 scrape |

## Planned commands

| cmd | Description |
|-----|-------------|
| `expect` | Wait until console matches (CLI-side in PR4) |
| `get_console` + `refresh` | Optional hwaddr reread (PR3) |
| `mem_read` | Read guest phys/virt (PR7) |

## Versioning

- `QEMU_CONNECT_PROTO_MAJOR` / `MINOR` in `include/qemu-connect.h`
- v0.2 adds full `text` + `source` on `get_console`
