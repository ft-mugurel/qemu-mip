#!/usr/bin/env bash
# Prove MCP tools/list and tools/call work end-to-end.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export QEMU_CONNECT_ROOT="$ROOT"

cd "$ROOT/mcp"
npm run build --silent

MCP_JS="$ROOT/mcp/dist/index.js"
if [[ ! -f "$ROOT/build/qemu-connect" ]]; then
  echo "Building CLI..."
  make -C "$ROOT" plugin cli
fi

mcp_session() {
  local reqs=("$@")
  {
    echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"mcp-smoke","version":"0.1"}}}'
    echo '{"jsonrpc":"2.0","method":"notifications/initialized"}'
    local r
    for r in "${reqs[@]}"; do
      echo "$r"
    done
  } | timeout 300 node "$MCP_JS" 2>/tmp/mcp-smoke-err.txt
}

extract_id() {
  local id="$1"
  python3 -c '
import sys, json
want = int(sys.argv[1])
for line in sys.stdin:
    line=line.strip()
    if not line: continue
    o=json.loads(line)
    if o.get("id")==want:
        json.dump(o, sys.stdout)
        break
' "$id"
}

echo "======== 1) tools/list ========"
out=$(mcp_session '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}')
echo "$out" | extract_id 2 | python3 -c '
import sys, json
o=json.load(sys.stdin)
names=[t["name"] for t in o["result"]["tools"]]
print("tools:", ", ".join(names))
need={"qemu_connect_info","qemu_build_guest","qemu_guest","qemu_run"}
missing=need-set(names)
if missing:
    print("FAIL missing:", missing); sys.exit(1)
print("OK all expected tools present")
'

echo ""
echo "======== 2) tools/call qemu_connect_info ========"
out=$(mcp_session '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"qemu_connect_info","arguments":{}}}')
echo "$out" | extract_id 3 | python3 -c '
import sys, json
o=json.load(sys.stdin)
text=o["result"]["content"][0]["text"]
print(text)
assert "repo_root:" in text and "cli:" in text
print("OK qemu_connect_info")
'

echo ""
echo "======== 3) tools/call qemu_build_guest what=tool ========"
out=$(mcp_session '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"qemu_build_guest","arguments":{"what":"tool"}}}')
echo "$out" | extract_id 4 | python3 -c '
import sys, json
o=json.load(sys.stdin)
text=o["result"]["content"][0]["text"]
print(text[-600:] if len(text)>600 else text)
if o["result"].get("isError"):
    print("FAIL"); sys.exit(1)
print("OK qemu_build_guest")
'

echo ""
echo "======== 4) tools/call qemu_guest cmd=help ========"
out=$(mcp_session '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"qemu_guest","arguments":{"cmd":"help","timeout_ms":180000}}}')
echo "$out" | extract_id 5 | python3 -c '
import sys, json
o=json.load(sys.stdin)
text=o["result"]["content"][0]["text"]
print(text)
if o["result"].get("isError") or "exit_code: 0" not in text:
    print("FAIL qemu_guest"); sys.exit(1)
if "munux shell commands" not in text and "munux>" not in text:
    print("FAIL expected shell/help in output"); sys.exit(1)
print("OK qemu_guest help")
'

echo ""
echo "======== 5) tools/call qemu_run (expect munux> + type about) ========"
out=$(mcp_session '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"qemu_run","arguments":{"steps":[{"op":"expect","text":"munux>"},{"op":"type","text":"about"}],"show":true,"timeout_ms":60000}}}')
echo "$out" | extract_id 6 | python3 -c '
import sys, json
o=json.load(sys.stdin)
text=o["result"]["content"][0]["text"]
print(text)
if o["result"].get("isError") or "exit_code: 0" not in text:
    print("FAIL qemu_run"); sys.exit(1)
print("OK qemu_run")
'

echo ""
echo "======== ALL MCP SMOKE PASSED ========"
