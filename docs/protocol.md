# Control protocol (v0.1)

Transport: **Unix domain stream socket**, one JSON object per line (UTF-8),
request/response.

Default path: `/tmp/qemu-connect.sock`  
Override: `-plugin …,socket=/path/to.sock`

## Request

```json
{"cmd":"<command>", ...optional fields...}
```

## Response

```json
{"ok":true,"result":{...}}
{"ok":false,"error":"<message>"}
```

## Commands (v0.1)

| cmd | Description | Status |
|-----|-------------|--------|
| `ping` | Liveness | implemented |
| `version` | Name + protocol version | implemented |
| `get_console` | VGA text summary | stub (metadata only) |

### ping

```text
→ {"cmd":"ping"}
← {"ok":true,"result":{"pong":true,"name":"qemu-connect","proto":"0.1"}}
```

### version

```text
→ {"cmd":"version"}
← {"ok":true,"result":{"name":"qemu-connect","proto":"0.1"}}
```

### get_console

```text
→ {"cmd":"get_console"}
← {"ok":true,"result":{"cols":80,"rows":25,"writes":0,"text_len":…}}
```

Full text payload will be added once VGA store instrumentation lands.

## Planned commands

| cmd | Description |
|-----|-------------|
| `expect` | Wait until console matches pattern / timeout |
| `mem_read` | Read guest phys/virt memory |
| `mem_write` | Write guest memory |
| `status` | Plugin + rough guest health |
| `subscribe` | Stream console deltas (later framing) |

## Versioning

- `QEMU_CONNECT_PROTO_MAJOR` / `MINOR` in `include/qemu-connect.h`
- Breaking wire changes bump major; additive fields bump minor
