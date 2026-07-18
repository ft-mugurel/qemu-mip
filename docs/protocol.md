# Control protocol (v0.4)

Unix stream socket, one JSON object per line. **Trusted local socket** — no auth.

Default: `/tmp/qemu-connect.sock`

## Commands

| cmd | Notes |
|-----|--------|
| `ping` | liveness |
| `version` | proto + counters |
| `status` | full health (discon, hypercall, flags) |
| `get_console` | shadow; optional `refresh:true` |
| `mem_read` | `phys` + `len` (1..4096); needs live vCPU |
| `get_agent_event` | pop last hypercall event |

### mem_read

```text
→ {"cmd":"mem_read","phys":753664,"len":16}
← {"ok":true,"result":{"phys":753664,"len":16,"hex":"2a07..."}}
← {"ok":false,"error":"vcpu_idle_timeout"}
```

`phys` may be decimal or `0x...`. After permanent guest `hlt`, expect timeout unless a drain path runs.

### status

Includes `discon.{exception,interrupt,hostcall,total}` and `agent.{count,last_cmd,last_name,pending_event}`.

### Hypercall ABI (optional guest)

Physical window **`0xFEE1DEAD`**, **16 bytes**, little-endian:

| Offset | Field | Value |
|--------|--------|--------|
| 0 | magic | `0x544E4351` (`QCNT`) |
| 4 | cmd | `1=READY`, `2=EXIT`, `3=LOG` |
| 8 | status | guest-defined |
| 12 | reserved | 0 |

Guest example (C):

```c
struct { uint32_t magic, cmd, status, reserved; } msg = {
  .magic = 0x544E4351u, .cmd = 2, .status = 0, .reserved = 0
};
*(volatile uint32_t *)0xFEE1DEADu = msg.magic; /* or memcpy to that address */
```

No guest pointers in v1. Plugin arg `hypercall=on|off` (default on).

## Plugin args

| Arg | Default |
|-----|---------|
| `socket=` | `/tmp/qemu-connect.sock` |
| `socket_thread=` | on |
| `vga=` | on |
| `vga_refresh=` | on |
| `hypercall=` | on |
| `vcpu_queue_timeout_ms=` | 250 |
