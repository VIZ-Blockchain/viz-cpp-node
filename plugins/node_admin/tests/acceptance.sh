#!/usr/bin/env bash
# Live acceptance for the node_admin plugin: starts a throwaway node (fresh data dir, no peers),
# drives the admin HTTP listener with curl and checks the contract from docs/plugins/node-admin.md.
#
# Not wired into CTest: it needs a built vizd and a free TCP port. Run it after changing the
# plugin:
#   ./plugins/node_admin/tests/acceptance.sh [path/to/vizd]
#
# Exit code: 0 = every check passed, 1 = at least one failed, 2 = the node never came up.
set -u

VIZD=${1:-${VIZD:-build/programs/vizd/vizd}}
PORT=${PORT:-18099}
P2P_PORT=${P2P_PORT:-$((PORT + 1))}
BASE="http://127.0.0.1:$PORT"
WORK=$(mktemp -d /tmp/vizd-node-admin.XXXXXX)

GOOD=accept-good-token
OPS=accept-ops-token
PEER=accept-peer-token
EXPIRED=accept-expired-token
sha() { printf %s "$1" | sha256sum | cut -d' ' -f1; }

cat > "$WORK/config.ini" <<EOF
plugin = chain
plugin = json_rpc
plugin = p2p
plugin = node_admin

admin-endpoint = 127.0.0.1:$PORT
admin-token = agent:read:$(sha "$GOOD")
admin-token = ops:process:$(sha "$OPS")
admin-token = peer:read,p2p:$(sha "$PEER")
admin-token = old:read:$(sha "$EXPIRED"):1000000000
admin-audit-log = $WORK/audit.log
admin-log-file = node=$WORK/node.log

p2p-endpoint = 127.0.0.1:$P2P_PORT
p2p-max-connections = 10

shared-file-size = 256M
private-key = 5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC
EOF
# A WIF in the log must never come back in clear text.
echo "boot ok, key 5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC" > "$WORK/node.log"
printf 'do not read me\n' > "$WORK/secret.log"

"$VIZD" --data-dir "$WORK" > "$WORK/console.log" 2>&1 &
NODE_PID=$!
cleanup() { kill "$NODE_PID" 2>/dev/null; wait "$NODE_PID" 2>/dev/null; }
trap cleanup EXIT

for _ in $(seq 1 120); do
    grep -q "node_admin: listening" "$WORK/console.log" && break
    kill -0 "$NODE_PID" 2>/dev/null || { echo "vizd exited during startup"; tail -20 "$WORK/console.log"; exit 2; }
    sleep 0.25
done
if ! grep -q "node_admin: listening" "$WORK/console.log"; then
    echo "node_admin never listened on $PORT"
    tail -20 "$WORK/console.log"
    exit 2
fi

FAIL=0
OUT="$WORK/out.json"
CODE=0

# call <curl args...> — sets CODE (http status) and OUT (body)
call() {
    CODE=$(curl -s -o "$OUT" -w '%{http_code}' -X POST "$BASE" \
        -H 'Content-Type: application/json' "$@")
}
auth() { call -H "Authorization: Bearer $1" -d "$2"; }
# jq-free field extraction: python is already a dependency of the build docs
field() { python3 -c 'import json,sys
try: v=json.load(open(sys.argv[1]))
except Exception: print("<unparsable>"); raise SystemExit
for k in sys.argv[2].split("."):
    if k.isdigit(): v=v[int(k)]
    else: v=v[k]
print(json.dumps(v))' "$OUT" "$1"; }

check() { # check <label> <expected> <actual>
    if [ "$2" = "$3" ]; then
        echo "ok   $1"
    else
        echo "FAIL $1: expected [$2], got [$3]"
        FAIL=$((FAIL + 1))
    fi
}
check_contains() {
    if printf '%s' "$3" | grep -q -- "$2"; then
        echo "ok   $1"
    else
        echo "FAIL $1: [$3] does not contain [$2]"
        FAIL=$((FAIL + 1))
    fi
}

# ── authentication ──────────────────────────────────────────────────────────────────────────
call -d '{"jsonrpc":"2.0","id":1,"method":"admin.ping"}'
check "no token -> HTTP 401" 401 "$CODE"
check "no token -> -32001" -32001 "$(field error.code)"

auth "not-a-token" '{"jsonrpc":"2.0","id":2,"method":"admin.ping"}'
check "bad token -> HTTP 401" 401 "$CODE"
check "bad token -> -32001" -32001 "$(field error.code)"

auth "$EXPIRED" '{"jsonrpc":"2.0","id":3,"method":"admin.ping"}'
check "expired token -> HTTP 401" 401 "$CODE"
check_contains "expired token says so" "token expired" "$(field error.data.reason)"

auth "$OPS" '{"jsonrpc":"2.0","id":4,"method":"admin.ping"}'
check "wrong scope -> HTTP 200" 200 "$CODE"
check "wrong scope -> -32002" -32002 "$(field error.code)"

# ── read methods ───────────────────────────────────────────────────────────────────────────
auth "$GOOD" '{"jsonrpc":"2.0","id":5,"method":"admin.ping"}'
check "ping -> HTTP 200" 200 "$CODE"
check "ping -> ok" true "$(field result.ok)"

auth "$GOOD" '{"jsonrpc":"2.0","id":6,"method":"admin.node_info"}'
check "node_info -> head_block_num is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["head_block_num"], int)).lower())' "$OUT")"
check "node_info -> head_age_sec null on an empty chain" null "$(field result.head_age_sec)"
check_contains "node_info -> version present" "4.2.0" "$(field result.version)"

auth "$GOOD" '{"jsonrpc":"2.0","id":7,"method":"admin.diagnostics"}'
check "diagnostics -> configured plugins" 4 "$(field result.plugins_configured)"
check "diagnostics -> observed plugins" 3 "$(field result.plugins_observed)"
check "diagnostics -> disk_free_bytes is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["disk_free_bytes"], int)).lower())' "$OUT")"
check "diagnostics -> p2p is loaded" true "$(field result.p2p_loaded)"

auth "$GOOD" '{"jsonrpc":"2.0","id":8,"method":"admin.plugins.list"}'
check "plugins.list -> chain state" '"started"' "$(field 'result.plugins.0.state')"
check "plugins.list -> chain observed" '"observed"' "$(field 'result.plugins.0.state_source')"
check "plugins.list -> json_rpc inferred" '"inferred"' "$(field 'result.plugins.1.state_source')"

auth "$GOOD" '{"jsonrpc":"2.0","id":9,"method":"admin.config.get"}'
check_contains "config.get -> private-key masked" 'private-key.*\*\*\*' \
    "$(tr -d '\n' < "$OUT")"
check_contains "config.get -> admin-token masked" 'admin-token.*\*\*\*' "$(tr -d '\n' < "$OUT")"
check_contains "config.get -> non-secret value intact" 'shared-file-size' "$(tr -d '\n' < "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":10,"method":"admin.logs.tail","params":{"name":"node"}}'
check "logs.tail -> one line" 1 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["lines"]))' "$OUT")"
check_contains "logs.tail -> WIF masked" '\*\*\*' "$(field 'result.lines.0')"
if grep -q '5Hw9YPABaFxa2Looi' "$OUT"; then
    echo "FAIL logs.tail leaked a WIF"; FAIL=$((FAIL + 1))
else
    echo "ok   logs.tail -> no WIF in the body"
fi

auth "$GOOD" '{"jsonrpc":"2.0","id":11,"method":"admin.logs.tail","params":{"name":"secret"}}'
check "logs.tail -> name outside the allow-list rejected" -32004 "$(field error.code)"

auth "$GOOD" '{"jsonrpc":"2.0","id":12,"method":"admin.nope"}'
check "unknown method -> -32601" -32601 "$(field error.code)"

# ── p2p status and operator actions ──────────────────────────────────────────────────────
auth "$GOOD" '{"jsonrpc":"2.0","id":30,"method":"admin.p2p.status"}'
check "p2p.status -> HTTP 200" 200 "$CODE"
check "p2p.status -> head_block_num is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["head_block_num"], int)).lower())' "$OUT")"
check "p2p.status -> peers is a list" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["peers"], list)).lower())' "$OUT")"
check "p2p.status -> uptime_sec is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["uptime_sec"], int)).lower())' "$OUT")"
check_contains "p2p.status -> node_status present" 'node_status' "$(tr -d '\n' < "$OUT")"
check "p2p.status -> trusted_peers starts at 0" 0 "$(field result.trusted_peers)"

# a read-only token must not be able to change peers
auth "$GOOD" '{"jsonrpc":"2.0","id":31,"method":"admin.p2p.add_node","params":{"endpoint":"127.0.0.1:1"}}'
check "p2p.add_node without 'p2p' scope -> -32002" -32002 "$(field error.code)"

auth "$PEER" '{"jsonrpc":"2.0","id":32,"method":"admin.p2p.add_node","params":{}}'
check "p2p.add_node -> missing endpoint" -32602 "$(field error.code)"

auth "$PEER" '{"jsonrpc":"2.0","id":33,"method":"admin.p2p.add_node","params":{"endpoint":"not-an-endpoint"}}'
check "p2p.add_node -> bad endpoint" -32602 "$(field error.code)"

auth "$PEER" '{"jsonrpc":"2.0","id":34,"method":"admin.p2p.add_node","params":{"endpoint":"127.0.0.1:1"}}'
check "p2p.add_node -> added" '"added"' "$(field result.status)"
check "p2p.add_node -> not trusted" false "$(field result.trusted)"

auth "$PEER" '{"jsonrpc":"2.0","id":35,"method":"admin.p2p.add_trusted","params":{"endpoint":"127.0.0.1:1"}}'
check "p2p.add_trusted -> added" '"added"' "$(field result.status)"
check "p2p.add_trusted -> trusted" true "$(field result.trusted)"

auth "$PEER" '{"jsonrpc":"2.0","id":36,"method":"admin.p2p.status"}'
check "p2p.status -> trusted peer counted" 1 "$(field result.trusted_peers)"
check "p2p.status -> added endpoint is in the peer list" true \
    "$(python3 -c 'import json,sys; ps=json.load(open(sys.argv[1]))["result"]["peers"]; print(str(any(p["endpoint"]=="127.0.0.1:1" for p in ps)).lower())' "$OUT")"
check "p2p.status -> the added peer is flagged trusted" true \
    "$(python3 -c 'import json,sys; ps=json.load(open(sys.argv[1]))["result"]["peers"]; print(str(any(p["endpoint"]=="127.0.0.1:1" and p["trusted"] for p in ps)).lower())' "$OUT")"

auth "$PEER" '{"jsonrpc":"2.0","id":37,"method":"admin.p2p.clear_banned"}'
check "p2p.clear_banned -> ok" '"ok"' "$(field result.status)"
check "p2p.clear_banned -> cleared is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["cleared"], int)).lower())' "$OUT")"


# ── MCP (Streamable HTTP) on the same listener ───────────────────────────────────────────
call -d '{"jsonrpc":"2.0","id":20,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}'
check "MCP initialize without a token -> HTTP 401" 401 "$CODE"

auth "$GOOD" '{"jsonrpc":"2.0","id":21,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"acceptance","version":"0"}}}'
check "MCP initialize -> HTTP 200" 200 "$CODE"
check "MCP initialize -> version echoed" '"2025-06-18"' "$(field result.protocolVersion)"
check "MCP initialize -> server name" '"viz-node-admin"' "$(field result.serverInfo.name)"
check "MCP initialize -> tools capability" true \
    "$(python3 -c 'import json,sys; print(str("tools" in json.load(open(sys.argv[1]))["result"]["capabilities"]).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":22,"method":"initialize","params":{"protocolVersion":"1999-01-01"}}'
check "MCP initialize -> unknown version falls back" '"2025-06-18"' "$(field result.protocolVersion)"

call -H "Authorization: Bearer $GOOD" -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
check "MCP notification -> HTTP 202" 202 "$CODE"
check "MCP notification -> empty body" 0 "$(wc -c < "$OUT" | tr -d ' ')"

auth "$GOOD" '{"jsonrpc":"2.0","id":23,"method":"tools/list"}'
check "MCP tools/list -> eleven tools" 11 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["tools"]))' "$OUT")"
check "MCP tools/list -> admin.node_info present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.node_info" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.p2p.status present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.p2p.status" for t in ts)).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":27,"method":"tools/call","params":{"name":"admin.p2p.add_node","arguments":{"endpoint":"127.0.0.1:1"}}}'
check "MCP mutating tool without 'p2p' -> isError" true "$(field result.isError)"
check_contains "MCP mutating tool -> scope named" "no 'p2p' scope" "$(field 'result.content.0.text')"

auth "$GOOD" '{"jsonrpc":"2.0","id":24,"method":"tools/call","params":{"name":"admin.node_info","arguments":{}}}'
check "MCP tools/call -> not an error" false "$(field result.isError)"
check "MCP tools/call -> text content" '"text"' "$(field 'result.content.0.type')"
check_contains "MCP tools/call -> tool result is JSON" 'head_block_num' "$(field 'result.content.0.text')"

auth "$GOOD" '{"jsonrpc":"2.0","id":25,"method":"tools/call","params":{"name":"admin.nope","arguments":{}}}'
check "MCP unknown tool -> -32602" -32602 "$(field error.code)"

auth "$GOOD" '{"jsonrpc":"2.0","id":26,"method":"tools/call","params":{"name":"admin.logs.tail","arguments":{"name":"secret"}}}'
check "MCP tool execution error -> isError" true "$(field result.isError)"
check_contains "MCP tool execution error -> reason in text" 'no admin-log-file entry' \
    "$(field 'result.content.0.text')"

# ── protocol edges ────────────────────────────────────────────────────────────────────────
CODE=$(curl -s -o "$OUT" -w '%{http_code}' "$BASE")
check "GET -> HTTP 405" 405 "$CODE"

# ── built-in UI ──────────────────────────────────────────────────────────────────────────
CODE=$(curl -s -o "$WORK/ui.html" -w '%{http_code}' -D "$WORK/ui.hdr" "$BASE/ui/")
check "GET /ui/ -> HTTP 200" 200 "$CODE"
check_contains "GET /ui/ -> HTML content type" 'Content-Type: text/html' "$(tr -d '\r' < "$WORK/ui.hdr")"
check_contains "GET /ui/ -> CSP header" 'Content-Security-Policy' "$(tr -d '\r' < "$WORK/ui.hdr")"
check_contains "GET /ui/ -> page title" '<title>VIZ node admin</title>' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> calls the p2p status method" 'admin.p2p.status' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> has the add-trusted action" 'admin.p2p.add_trusted' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> has the clear-banned action" 'admin.p2p.clear_banned' "$(cat "$WORK/ui.html")"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' "$BASE/ui")
check "GET /ui without slash -> HTTP 200" 200 "$CODE"


auth "$GOOD" '{"jsonrpc":"2.0","id":13,'
check "malformed JSON -> -32700" -32700 "$(field error.code)"

python3 -c 'print("{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"admin.ping\",\"pad\":\"" + "a"*70000 + "\"}")' > "$WORK/big.json"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' -X POST "$BASE" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $GOOD" --data-binary @"$WORK/big.json")
check "oversized body -> HTTP 413" 413 "$CODE"

# ── audit and rate limit (last: the burst trips the limiter on purpose) ───────────────────
auth "$GOOD" '{"jsonrpc":"2.0","id":15,"method":"admin.audit.tail","params":{"lines":200}}'
check "audit.tail -> readable" 200 "$CODE"
check_contains "audit has the rejected bad-token attempt" 'unauthorized' \
    "$(tr -d '\n' < "$OUT")"
if grep -q "$GOOD\|$(sha "$GOOD")" "$OUT"; then
    echo "FAIL audit log contains the token or its hash"; FAIL=$((FAIL + 1))
else
    echo "ok   audit -> token value and hash absent"
fi

LIMITED=0
for i in $(seq 1 40); do
    call -H "Authorization: Bearer $GOOD" -d '{"jsonrpc":"2.0","id":99,"method":"admin.ping"}'
    [ "$CODE" = 429 ] && { LIMITED=1; break; }
done
check "burst -> HTTP 429" 1 "$LIMITED"

echo
if [ "$FAIL" -eq 0 ]; then
    echo "PASS: all node_admin acceptance checks"
else
    echo "FAILED: $FAIL check(s); work dir kept at $WORK"
    exit 1
fi
