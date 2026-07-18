# Control protocol (v0.3)

Transport: **Unix domain stream socket**, one JSON object per line (UTF-8).

Default path: `/tmp/qemu-connect.sock`  
Override: `-plugin …,socket=/path/to.sock`

## Framing

- One JSON object **per line** (`\n`)
- Max request line: **8192** bytes
- Responses for `get_console` may be up to ~32 KiB

## Commands

| cmd | Status |
|-----|--------|
| `ping` | ok |
| `version` | ok (+ refresh flags) |
| `get_console` | shadow default; optional `refresh` |

### get_console (shadow — default)

```text
→ {"cmd":"get_console"}
← {"ok":true,"result":{...,"source":"shadow","text":"..."}}
```

Never blocks on the vCPU. Safe after permanent `hlt`.

### get_console (refresh — PR3)

```text
→ {"cmd":"get_console","refresh":true}
← {"ok":true,"result":{...,"source":"refresh","text":"..."}}
← {"ok":false,"error":"vcpu_idle_timeout"}
← {"ok":false,"error":"hwaddr_read_failed","result":{"code":"INVALID_ADDRESS","code_num":4}}
← {"ok":false,"error":"refresh disabled"}
```

Requires plugin arg `vga_refresh=on` (default). Uses a vCPU-side queue with
timeout (`vcpu_queue_timeout_ms`, default **250**).

| `code` | Meaning |
|--------|---------|
| `OK` | success |
| `ERROR` | unexpected |
| `DEVICE_ERROR` | device fault |
| `ACCESS_DENIED` | permission |
| `INVALID_ADDRESS` | bad phys |
| `INVALID_ADDRESS_SPACE` | wrong AS |

## Plugin arguments

| Arg | Default | Meaning |
|-----|---------|---------|
| `socket=PATH` | `/tmp/qemu-connect.sock` | Control socket |
| `socket_thread=on\|off` | `on` | Dedicated poll thread |
| `vga=on\|off` | `on` | Store scrape at `0xB8000` |
| `vga_refresh=on\|off` | `on` | Allow `refresh:true` |
| `vcpu_queue_timeout_ms=N` | `250` | Max wait for refresh drain |

## CLI helpers (host)

```sh
qemu-connect expect 'substring' --timeout 60000 --socket PATH
qemu-connect get_console --text-only
qemu-connect get_console --refresh
```
