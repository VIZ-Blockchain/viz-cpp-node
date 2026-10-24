#!/usr/bin/env bash
# Live acceptance for the node_admin plugin: starts a throwaway node (fresh data dir, no peers),
# drives the admin HTTP listener with curl and checks the contract from docs/plugins/node-admin.md.
#
# Not wired into CTest: it needs a built vizd and a free TCP port. Run it after changing the
# plugin:
#   ./plugins/node_admin/tests/acceptance.sh [path/to/vizd]
#
# The suite writes fixture files into the node's data dir (snapshots, config.ini edits, repair
# markers), so the node must run as the SAME user as the script. With Docker, pass --user:
#   docker run --rm --network host --user "$(id -u):$(id -g)" -v "$WORK:$WORK" \
#       --entrypoint /usr/local/bin/vizd vizblockchain/vizd:pm --data-dir "$WORK"
# Otherwise node-created files are root-owned and those fixtures fail with permission denied;
# the guard below stops the run instead of reporting the resulting noise as product failures.
#
# Exit code: 0 = every check passed, 1 = at least one failed, 2 = the node never came up or the
# data dir is not writable by this user (that is "not measured", not a regression).
set -u

VIZD=${1:-${VIZD:-build/programs/vizd/vizd}}
PORT=${PORT:-18099}
P2P_PORT=${P2P_PORT:-$((PORT + 1))}
WEB_PORT=${WEB_PORT:-$((PORT + 2))}
BASE="http://127.0.0.1:$PORT"
WEB="http://127.0.0.1:$WEB_PORT"
WORK=$(mktemp -d /tmp/vizd-node-admin.XXXXXX)

GOOD=accept-good-token
OPS=accept-ops-token
PEER=accept-peer-token
SNAP=accept-snap-token
WS=accept-webserver-token
ADM=accept-admin-token
ADM2=accept-admin2-token
CHN=accept-chain-token
EXPIRED=accept-expired-token
sha() { printf %s "$1" | sha256sum | cut -d' ' -f1; }

cat > "$WORK/config.ini" <<EOF
plugin = chain p2p
plugin = json_rpc webserver
plugin = snapshot
plugin = node_admin

admin-endpoint = 127.0.0.1:$PORT
admin-token = agent:read:$(sha "$GOOD")
admin-token = ops:process:$(sha "$OPS")
admin-token = peer:read,p2p:$(sha "$PEER")
admin-token = snap:read,snapshot:$(sha "$SNAP")
admin-token = ws:read,webserver:$(sha "$WS")
admin-token = adm:read,config,plugins,process:$(sha "$ADM")
admin-token = adm2:read,config,plugins,process:$(sha "$ADM2")
# The chain scope alone does not buy validator operations: the guard must be live.
admin-token = chn:read,chain:$(sha "$CHN")
admin-token = old:read:$(sha "$EXPIRED"):1000000000
# Short TTL so the expired-plan branch is reachable inside this script (a 4 s sleep).
admin-plan-ttl = 3
admin-audit-log = $WORK/audit.log
admin-log-file = node=$WORK/node.log

p2p-endpoint = 127.0.0.1:$P2P_PORT
p2p-max-connections = 10

webserver-http-endpoint = 127.0.0.1:$WEB_PORT
webserver-stats-enabled = true
webserver-limit-m = 5
webserver-limit-h = 100
webserver-limit-d = 1000
webserver-stats-log = $WORK/webserver-stats.log
webserver-stats-interval = 1
webserver-admin-state = $WORK/webserver-admin.json

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

# The node must run as this user: fixtures are written straight into its data dir below. A
# root-owned run (a container without --user) makes every such write fail with permission
# denied, and the resulting cascade would be reported as product failures.
if [ ! -w "$WORK/state" ]; then
    echo "FATAL: $WORK/state is not writable by $(id -un); the node runs as another user"
    echo "       Run the node as this user (docker: --user $(id -u):$(id -g)) or use a local binary."
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
# raw: same walk but prints the value unquoted (plan ids, paths)
raw() { python3 -c 'import json,sys
try: v=json.load(open(sys.argv[1]))
except Exception: print(""); raise SystemExit
for k in sys.argv[2].split("."):
    if k.isdigit(): v=v[int(k)]
    else: v=v[k]
print(v)' "$OUT" "$1"; }

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

# The UI asks who it is so it can hide actions the token cannot perform. The answer must name
# the label and the scopes, and must never carry the token or its hash.
auth "$GOOD" '{"jsonrpc":"2.0","id":55,"method":"admin.auth.me"}'
check "auth.me -> label" '"agent"' "$(field result.label)"
check "auth.me -> one scope" 1 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["scopes"]))' "$OUT")"
check "auth.me -> read scope" '"read"' "$(field 'result.scopes.0')"
check "auth.me -> no hash in the answer" false \
    "$(python3 -c 'import json,sys; print(str("hash" in json.dumps(json.load(open(sys.argv[1]))["result"])).lower())' "$OUT")"
auth "$SNAP" '{"jsonrpc":"2.0","id":56,"method":"admin.auth.me"}'
check "auth.me -> sees the snapshot scope" true \
    "$(python3 -c 'import json,sys; print(str("snapshot" in json.load(open(sys.argv[1]))["result"]["scopes"]).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":57,"method":"admin.logs.list"}'
check "logs.list -> one allow-listed file" 1 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["logs"]))' "$OUT")"
check "logs.list -> names it" '"node"' "$(field 'result.logs.0.name')"
check "logs.list -> file exists" true "$(field 'result.logs.0.exists')"

auth "$GOOD" '{"jsonrpc":"2.0","id":58,"method":"admin.validators.active"}'
check "validators.active -> count is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["count"], int)).lower())' "$OUT")"
check "validators.active -> emergency flag is a boolean" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["emergency_consensus_active"], bool)).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":6,"method":"admin.node_info"}'
check "node_info -> head_block_num is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["head_block_num"], int)).lower())' "$OUT")"
check "node_info -> head_age_sec null on an empty chain" null "$(field result.head_age_sec)"
check_contains "node_info -> version present" "4.2.0" "$(field result.version)"
check "node_info -> emergency consensus exposed" false "$(field result.emergency_consensus_active)"
check "node_info -> emergency key exposed" false "$(field result.emergency_key_configured)"

auth "$GOOD" '{"jsonrpc":"2.0","id":7,"method":"admin.diagnostics"}'
check "diagnostics -> configured plugins" 6 "$(field result.plugins_configured)"
check "diagnostics -> observed plugins" 5 "$(field result.plugins_observed)"
check "diagnostics -> disk_free_bytes is a number" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["disk_free_bytes"], int)).lower())' "$OUT")"
check "diagnostics -> p2p is loaded" true "$(field result.p2p_loaded)"

auth "$GOOD" '{"jsonrpc":"2.0","id":8,"method":"admin.plugins.list"}'
# The fixture writes two names on one line, as the live config.ini does. Rows are looked up
# by name, not by position: a positional check silently changed meaning the moment the
# fixture stopped using one name per line.
check "plugins.list -> chain state" '"started"' \
    "$(python3 -c 'import json,sys; p=json.load(open(sys.argv[1]))["result"]["plugins"]; print(json.dumps([x["state"] for x in p if x["name"]=="chain"][0]))' "$OUT")"
check "plugins.list -> chain observed" '"observed"' \
    "$(python3 -c 'import json,sys; p=json.load(open(sys.argv[1]))["result"]["plugins"]; print(json.dumps([x["state_source"] for x in p if x["name"]=="chain"][0]))' "$OUT")"
check "plugins.list -> json_rpc inferred" '"inferred"' \
    "$(python3 -c 'import json,sys; p=json.load(open(sys.argv[1]))["result"]["plugins"]; print(json.dumps([x["state_source"] for x in p if x["name"]=="json_rpc"][0]))' "$OUT")"
check "plugins.list -> multi-name line yields one row per name" true \
    "$(python3 -c 'import json,sys; n=[x["name"] for x in json.load(open(sys.argv[1]))["result"]["plugins"]]; print(str("chain" in n and "p2p" in n and "json_rpc" in n and "webserver" in n).lower())' "$OUT")"
check "plugins.list -> no row name contains a space" true \
    "$(python3 -c 'import json,sys; n=[x["name"] for x in json.load(open(sys.argv[1]))["result"]["plugins"]]; print(str(all(" " not in x for x in n)).lower())' "$OUT")"
check "plugins.list -> p2p names its status method" '"admin.p2p.status"' \
    "$(python3 -c 'import json,sys; p=json.load(open(sys.argv[1]))["result"]["plugins"]; print(json.dumps([x["status_method"] for x in p if x["name"]=="p2p"][0]))' "$OUT")"
check "plugins.list -> p2p carries its grouped parameters" true \
    "$(python3 -c 'import json,sys; p=json.load(open(sys.argv[1]))["result"]["plugins"]; print(str(len([x for x in p if x["name"]=="p2p"][0]["params"]) > 0).lower())' "$OUT")"

# ── per-plugin status ──────────────────────────────────────────────────────────────────────
# The page must be able to answer "what is this plugin doing", not just "is it configured".
auth "$GOOD" '{"jsonrpc":"2.0","id":171,"method":"admin.plugins.status","params":{"name":"p2p"}}'
check "plugins.status -> HTTP 200" 200 "$CODE"
check "plugins.status -> names the plugin" '"p2p"' "$(field result.name)"
check "plugins.status -> observed state" '"started"' "$(field result.state)"
check "plugins.status -> detail available" true "$(field result.detail_available)"
check "plugins.status -> names the detail method" '"admin.p2p.status"' "$(field result.detail_method)"
check "plugins.status -> detail is the p2p view" true \
    "$(python3 -c 'import json,sys; print(str(isinstance(json.load(open(sys.argv[1]))["result"]["detail"]["peers"], list)).lower())' "$OUT")"

# json_rpc is configured but its type is not linked into this binary: state is null with a
# reason, never an invented lifecycle.
auth "$GOOD" '{"jsonrpc":"2.0","id":172,"method":"admin.plugins.status","params":{"name":"json_rpc"}}'
check "plugins.status -> unobserved state is null" null "$(field result.state)"
check "plugins.status -> unobserved says why" true \
    "$(python3 -c 'import json,sys; print(str(bool(json.load(open(sys.argv[1]))["result"].get("reason"))).lower())' "$OUT")"
check "plugins.status -> unobserved has no detail" false "$(field result.detail_available)"

auth "$GOOD" '{"jsonrpc":"2.0","id":173,"method":"admin.plugins.status","params":{"name":"node_admin"}}'
check "plugins.status -> started without own status" '"started"' "$(field result.state)"
check "plugins.status -> no own status API" false "$(field result.detail_available)"
check "plugins.status -> absence is explained" true \
    "$(python3 -c 'import json,sys; print(str(bool(json.load(open(sys.argv[1]))["result"].get("reason"))).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":174,"method":"admin.plugins.status"}'
check "plugins.status -> name is required" -32602 "$(field error.code)"

# ── chainbase objects ─────────────────────────────────────────────────────────────────────
# One row per object type, plus the judge that may invite an operator to wipe state.
auth "$GOOD" '{"jsonrpc":"2.0","id":175,"method":"admin.objects.list"}'
check "objects.list -> HTTP 200" 200 "$CODE"
check "objects.list -> index count matches the array" true \
    "$(python3 -c 'import json,sys; r=json.load(open(sys.argv[1]))["result"]; print(str(r["index_count"] == len(r["indexes"])).lower())' "$OUT")"
check "objects.list -> at least one object type" true \
    "$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1]))["result"]["index_count"] > 0).lower())' "$OUT")"
check "objects.list -> rows carry a type and a count" true \
    "$(python3 -c 'import json,sys; r=json.load(open(sys.argv[1]))["result"]; print(str(bool(r["indexes"][0]["type"]) and isinstance(r["indexes"][0]["objects"], int)).lower())' "$OUT")"
check "objects.list -> schema matches this binary" true "$(field result.schema.match)"
# Both live under <data-dir>/state (next to shared_memory.bin); reading the app data dir
# reported them absent on every healthy node.
check "objects.list -> block log found where the chain keeps it" true "$(field result.block_log.present)"
check "objects.list -> read lock answered" null "$(field result.lock_error)"
check "objects.list -> index order is largest first" true \
    "$(python3 -c 'import json,sys; c=[i["objects"] for i in json.load(open(sys.argv[1]))["result"]["indexes"]]; print(str(c == sorted(c, reverse=True)).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":176,"method":"admin.objects.check"}'
check "objects.check -> HTTP 200" 200 "$CODE"
check "objects.check -> healthy scratch node is ok" '"ok"' "$(field result.verdict)"
check "objects.check -> no findings" 0 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["findings"]))' "$OUT")"
check "objects.check -> no repair recommended" null "$(field result.recommended_repair)"

# Repair is a write disguised as maintenance: the read token must not reach it, and the
# confirm word is not decoration. Both refusals happen before a marker is written, so this
# block is safe to run against the throwaway node.
auth "$GOOD" '{"jsonrpc":"2.0","id":87,"method":"admin.objects.repair","params":{"mode":"replay","confirm":"repair"}}'
check "objects.repair -> read token refused" -32002 "$(field error.code)"
# ADM carries read+process, so these reach the handler's own validation.
auth "$ADM" '{"jsonrpc":"2.0","id":181,"method":"admin.objects.repair","params":{"mode":"replay"}}'
check "objects.repair -> confirm is required" -32602 "$(field error.code)"
auth "$ADM" '{"jsonrpc":"2.0","id":182,"method":"admin.objects.repair","params":{"mode":"sideways","confirm":"repair"}}'
check "objects.repair -> unknown mode refused" -32602 "$(field error.code)"
auth "$ADM" '{"jsonrpc":"2.0","id":183,"method":"admin.objects.repair","params":{"mode":"replay","confirm":"repair"}}'
check "objects.repair -> replay without blocks refused, no marker written" -32000 "$(field error.code)"
check "objects.repair -> the refusal names the block log" true \
    "$(python3 -c 'import json,sys; print(str("block log" in json.load(open(sys.argv[1]))["error"]["message"]).lower())' "$OUT")"
if [ -e "$WORK/force_replay" ] || [ -e "$WORK/state/force_replay" ]; then
    echo "FAIL objects.repair -> a refused repair left a marker behind"
    FAIL=$((FAIL + 1))
else
    echo "ok   objects.repair -> no marker after a refusal"
fi

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


# ── snapshots: status, list, download, create/restore guards ─────────────────────────────
# The limiter allows 20 req/s per token; start this block in a fresh window so the extra
# requests below cannot trip it before the deliberate burst at the end.
sleep 1
auth "$GOOD" '{"jsonrpc":"2.0","id":40,"method":"admin.snapshot.status"}'
check "snapshot.status -> HTTP 200" 200 "$CODE"
check "snapshot.status -> empty directory" 0 "$(field result.snapshot_count)"
check "snapshot.status -> total equals count on the first page" 0 "$(field result.snapshot_total)"
check "snapshot.status -> default page size 20" 20 "$(field result.limit)"
check "snapshot.status -> no more after the last page" false "$(field result.has_more)"
check "snapshot.status -> latest_name null with no files" null "$(field result.latest_name)"
check "snapshot.status -> not restoring" false "$(field result.restoring)"
check "snapshot.status -> not creating" false "$(field result.in_progress)"
check "snapshot.status -> dir lives under the data dir" true \
    "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["dir"]; print(str(d.startswith(sys.argv[2])).lower())' "$OUT" "$WORK")"
SNAPDIR=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["result"]["dir"])' "$OUT")

# creation is refused on an empty chain (head 0), not queued
auth "$SNAP" '{"jsonrpc":"2.0","id":41,"method":"admin.snapshot.create"}'
check "snapshot.create on an empty chain -> -32000" -32000 "$(field error.code)"
check_contains "snapshot.create -> names the empty head" 'head is 0' "$(field error.data.reason)"

# create/restore additionally need the 'snapshot' scope; read is not enough
auth "$GOOD" '{"jsonrpc":"2.0","id":42,"method":"admin.snapshot.create"}'
check "snapshot.create with only 'read' -> -32002" -32002 "$(field error.code)"
auth "$GOOD" '{"jsonrpc":"2.0","id":43,"method":"admin.snapshot.restore","params":{}}'
check "snapshot.restore with only 'read' -> -32002" -32002 "$(field error.code)"

# restore with no files on disk fails before anything is touched
auth "$SNAP" '{"jsonrpc":"2.0","id":44,"method":"admin.snapshot.restore","params":{}}'
check "snapshot.restore with no snapshots -> -32000" -32000 "$(field error.code)"
check_contains "snapshot.restore -> says no snapshots found" 'no snapshots found' \
    "$(field error.data.reason)"

# download: auth and name validation
CODE=$(curl -s -o "$OUT" -w '%{http_code}' "$BASE/snapshot/latest")
check "GET /snapshot/latest without a token -> HTTP 401" 401 "$CODE"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' -H "Authorization: Bearer $OPS" "$BASE/snapshot/latest")
check "GET /snapshot/latest without 'read' -> HTTP 403" 403 "$CODE"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' -H "Authorization: Bearer $GOOD" "$BASE/snapshot/latest")
check "GET /snapshot/latest with no snapshot -> HTTP 404" 404 "$CODE"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' -H "Authorization: Bearer $GOOD" "$BASE/snapshot/config.ini")
check "GET /snapshot/<non-snapshot name> -> HTTP 400" 400 "$CODE"

# a real file in the snapshot directory must appear in the list and download verbatim
mkdir -p "$SNAPDIR"
printf 'dummy snapshot payload\n' > "$SNAPDIR/snapshot-block-123.vizjson"
auth "$GOOD" '{"jsonrpc":"2.0","id":45,"method":"admin.snapshot.status"}'
check "snapshot.status -> one file" 1 "$(field result.snapshot_count)"
check "snapshot.status -> latest is the only file" '"snapshot-block-123.vizjson"' "$(field result.latest_name)"
check "snapshot.status -> block number parsed from the name" 123 "$(field 'result.snapshots.0.block_num')"
check "snapshot.status -> file flagged latest" true "$(field 'result.snapshots.0.latest')"
check "snapshot.status -> size reported" 23 "$(field 'result.snapshots.0.size_bytes')"

# Cursor paging: an anchor must continue after THAT row, not after its index — a snapshot
# written between two pages shifts every offset below it, and retention prunes the tail.
for b in 120 121 122; do printf 'dummy snapshot payload\n' > "$SNAPDIR/snapshot-block-$b.vizjson"; done
auth "$GOOD" '{"jsonrpc":"2.0","id":46,"method":"admin.snapshot.status","params":{"limit":2}}'
N0=$(raw result.snapshots.0.name)
N1=$(raw result.snapshots.1.name)
auth "$GOOD" "{\"jsonrpc\":\"2.0\",\"id\":47,\"method\":\"admin.snapshot.status\",\"params\":{\"limit\":1,\"after\":\"$N0\"}}"
check "snapshot.status -> cursor anchor found" true "$(field result.after_found)"
check "snapshot.status -> cursor continues after the anchored row" "\"$N1\"" "$(field 'result.snapshots.0.name')"
auth "$GOOD" '{"jsonrpc":"2.0","id":48,"method":"admin.snapshot.status","params":{"limit":1,"offset":1,"after":"snapshot-block-0.vizjson"}}'
check "snapshot.status -> pruned anchor is reported" false "$(field result.after_found)"
check "snapshot.status -> pruned anchor falls back to offset" "\"$N1\"" "$(field 'result.snapshots.0.name')"

CODE=$(curl -s -o "$WORK/dl.bin" -D "$WORK/dl.hdr" -w '%{http_code}' \
    -H "Authorization: Bearer $GOOD" "$BASE/snapshot/latest")
check "GET /snapshot/latest -> HTTP 200" 200 "$CODE"
check "GET /snapshot/latest -> body is the file" "dummy snapshot payload" "$(cat "$WORK/dl.bin")"
check_contains "GET /snapshot/latest -> attachment disposition" \
    'Content-Disposition: attachment; filename="snapshot-block-123.vizjson"' "$(tr -d '\r' < "$WORK/dl.hdr")"
check_contains "GET /snapshot/latest -> octet stream" 'Content-Type: application/octet-stream' \
    "$(tr -d '\r' < "$WORK/dl.hdr")"

CODE=$(curl -s -I -o "$WORK/dl-head.hdr" -w '%{http_code}' \
    -H "Authorization: Bearer $GOOD" "$BASE/snapshot/snapshot-block-123.vizjson")
check "HEAD /snapshot/<name> -> HTTP 200" 200 "$CODE"
check_contains "HEAD /snapshot/<name> -> content length" 'Content-Length: 23' \
    "$(tr -d '\r' < "$WORK/dl-head.hdr")"

# Large files must stream byte-for-byte without buffering whole bodies or blocking RPC.
if ! python3 "$(dirname "$0")/snapshot_download.py" "$BASE" "$GOOD" "$SNAPDIR" "$NODE_PID"; then
    echo "FAIL snapshot streaming regression"
    FAIL=$((FAIL + 1))
fi

# restore path guards never start a restore from an unexpected file
auth "$SNAP" '{"jsonrpc":"2.0","id":46,"method":"admin.snapshot.restore","params":{"name":"missing.vizjson"}}'
check "snapshot.restore of a missing name -> -32602" -32602 "$(field error.code)"
auth "$SNAP" '{"jsonrpc":"2.0","id":47,"method":"admin.snapshot.restore","params":{"name":"../../config.ini"}}'
check "snapshot.restore with traversal -> -32602" -32602 "$(field error.code)"
auth "$SNAP" '{"jsonrpc":"2.0","id":48,"method":"admin.snapshot.restore","params":{"path":"'$WORK'/config.ini"}}'
check "snapshot.restore of a path outside the snapshot dir -> -32602" -32602 "$(field error.code)"
check_contains "snapshot.restore path guard names the directory" 'outside snapshot directory' \
    "$(field error.message)"
auth "$SNAP" '{"jsonrpc":"2.0","id":49,"method":"admin.snapshot.restore","params":{"latest":false}}'
check "snapshot.restore with latest:false and no name -> -32602" -32602 "$(field error.code)"


# ── webserver stats, limits and IP lists ────────────────────────────────────────────────
# The webserver runs on its own port; requests to it are what the counters and limits see.
for _ in $(seq 1 40); do
    grep -q "start listening for http requests" "$WORK/console.log" && break
    sleep 0.25
done
if ! grep -q "start listening for http requests" "$WORK/console.log"; then
    echo "webserver never listened on $WEB_PORT"; tail -20 "$WORK/console.log"; exit 2
fi

# webcall sets CODE and OUT; the body itself does not matter for admission
webcall() {
    CODE=$(curl -s -o "$OUT" -w '%{http_code}' -X POST "$WEB" \
        -H 'Content-Type: application/json' \
        -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["database_api","get_dynamic_global_properties",{}]}')
}

auth "$GOOD" '{"jsonrpc":"2.0","id":50,"method":"admin.webserver.status"}'
check "webserver.status -> HTTP 200" 200 "$CODE"
check "webserver.status -> stats enabled from config" true "$(field result.stats_enabled)"
check "webserver.status -> minute limit from config" 5 "$(field 'result.limits.per_minute')"
check "webserver.status -> hour limit from config" 100 "$(field 'result.limits.per_hour')"
check "webserver.status -> day limit from config" 1000 "$(field 'result.limits.per_day')"
check "webserver.status -> limits enforced" true "$(field 'result.limits.enabled')"
check "webserver.status -> no IPs seen yet" 0 "$(field result.tracked_ips)"

# burst: five requests pass, the sixth is rejected with 429 and Retry-After
for i in 1 2 3 4 5; do
    webcall
    check "webserver request $i -> HTTP 200" 200 "$CODE"
done
CODE=$(curl -s -o "$OUT" -D "$WORK/web.hdr" -w '%{http_code}' -X POST "$WEB" \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["database_api","get_dynamic_global_properties",{}]}')
check "webserver sixth request -> HTTP 429" 429 "$CODE"
check_contains "webserver 429 -> names the limit" 'rate limit exceeded' "$(cat "$OUT")"
check_contains "webserver 429 -> Retry-After header" 'Retry-After: 60' "$(tr -d '\r' < "$WORK/web.hdr")"

auth "$GOOD" '{"jsonrpc":"2.0","id":51,"method":"admin.webserver.ips","params":{"limit":10}}'
check "webserver.ips -> one tracked IP" 1 "$(field result.tracked_ips)"
check "webserver.ips -> loopback address" '"127.0.0.1"' "$(field 'result.ips.0.ip')"
check "webserver.ips -> total counted" 6 "$(field 'result.ips.0.total')"
check "webserver.ips -> one request limited" 1 "$(field 'result.ips.0.limited')"
check "webserver.ips -> minute counter includes the burst" true \
    "$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1]))["result"]["ips"][0]["minute"] >= 6).lower())' "$OUT")"

# the stats log is written on the stats interval and carries per-IP lines
for _ in $(seq 1 40); do
    grep -q 'ip=127.0.0.1' "$WORK/webserver-stats.log" 2>/dev/null && break
    sleep 0.25
done
check_contains "stats log -> per-IP line" 'ip=127.0.0.1' "$(cat "$WORK/webserver-stats.log" 2>/dev/null)"
check_contains "stats log -> summary line" 'summary' "$(cat "$WORK/webserver-stats.log" 2>/dev/null)"

# a read-only token cannot manage the webserver
auth "$GOOD" '{"jsonrpc":"2.0","id":52,"method":"admin.webserver.limits","params":{"per_minute":1000}}'
check "webserver.limits with only 'read' -> -32002" -32002 "$(field error.code)"
check_contains "webserver.limits -> scope named" "no 'webserver' scope" "$(field error.data.reason)"

# whitelisting bypasses the (already exceeded) limit; counting continues
auth "$WS" '{"jsonrpc":"2.0","id":53,"method":"admin.webserver.whitelist_add","params":{"ip":"127.0.0.1"}}'
check "whitelist_add -> added" '"added"' "$(field result.status)"
check "whitelist_add -> list contains the address" '"127.0.0.1"' "$(field 'result.whitelist.0')"
webcall
check "whitelisted IP bypasses the limit -> HTTP 200" 200 "$CODE"

auth "$GOOD" '{"jsonrpc":"2.0","id":54,"method":"admin.webserver.ips"}'
check "webserver.ips -> address flagged whitelisted" true "$(field 'result.ips.0.whitelisted')"

# the blacklist wins over the whitelist and rejects with 403
auth "$WS" '{"jsonrpc":"2.0","id":55,"method":"admin.webserver.blacklist_add","params":{"ip":"127.0.0.1"}}'
check "blacklist_add -> added" '"added"' "$(field result.status)"
check "blacklist_add -> removed the address from the whitelist" 0 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["whitelist"]))' "$OUT")"
webcall
check "blacklisted IP -> HTTP 403" 403 "$CODE"
check_contains "blacklisted IP -> body names the reason" 'forbidden' "$(cat "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":56,"method":"admin.webserver.ips"}'
check "webserver.ips -> address flagged blacklisted" true "$(field 'result.ips.0.blacklisted')"
check "webserver.ips -> blocked counter is 1" 1 "$(field 'result.ips.0.blocked')"

auth "$WS" '{"jsonrpc":"2.0","id":57,"method":"admin.webserver.blacklist_remove","params":{"ip":"127.0.0.1"}}'
check "blacklist_remove -> removed" '"removed"' "$(field result.status)"
webcall
check "blacklist removal lifts the 403 (still rate limited) -> HTTP 429" 429 "$CODE"

auth "$WS" '{"jsonrpc":"2.0","id":58,"method":"admin.webserver.limits","params":{"per_minute":1000,"per_hour":10000,"per_day":100000}}'
check "webserver.limits -> applied" '"applied"' "$(field result.status)"
check "webserver.limits -> per_minute updated" 1000 "$(field 'result.limits.per_minute')"
webcall
check "raised limit applies on the next request -> HTTP 200" 200 "$CODE"

# stats can be switched at runtime; the blacklist keeps working without them
auth "$WS" '{"jsonrpc":"2.0","id":59,"method":"admin.webserver.stats","params":{"enabled":false}}'
check "webserver.stats -> disabled" '"disabled"' "$(field result.status)"
check "webserver.stats -> stats flag is false" false "$(field result.stats_enabled)"
auth "$WS" '{"jsonrpc":"2.0","id":60,"method":"admin.webserver.blacklist_add","params":{"ip":"127.0.0.1"}}'
webcall
check "blacklist still enforced with stats off -> HTTP 403" 403 "$CODE"
auth "$WS" '{"jsonrpc":"2.0","id":61,"method":"admin.webserver.blacklist_remove","params":{"ip":"127.0.0.1"}}'
auth "$WS" '{"jsonrpc":"2.0","id":62,"method":"admin.webserver.stats","params":{"enabled":true}}'
check "webserver.stats -> re-enabled" '"enabled"' "$(field result.status)"

# changes are persisted, and reload re-reads the file from disk
check_contains "admin state file -> limits persisted" '"per_minute":1000' \
    "$(cat "$WORK/webserver-admin.json" 2>/dev/null)"
python3 - "$WORK/webserver-admin.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["per_minute"] = 7
json.dump(d, open(p, "w"))
PY
auth "$WS" '{"jsonrpc":"2.0","id":63,"method":"admin.webserver.reload"}'
check "webserver.reload -> HTTP 200" 200 "$CODE"
check "webserver.reload -> picked up the edited limit" 7 "$(field 'result.limits.per_minute')"
auth "$WS" '{"jsonrpc":"2.0","id":64,"method":"admin.webserver.limits","params":{"per_minute":1000}}'
check "webserver.limits -> per_minute restored" 1000 "$(field 'result.limits.per_minute')"

auth "$WS" '{"jsonrpc":"2.0","id":65,"method":"admin.webserver.reset"}'
check "webserver.reset -> status" '"reset"' "$(field result.status)"
auth "$GOOD" '{"jsonrpc":"2.0","id":66,"method":"admin.webserver.status"}'
check "webserver.reset -> counters zeroed" 0 "$(field result.total_requests)"
check "webserver.reset -> limits kept" 1000 "$(field 'result.limits.per_minute')"
check "webserver.reset -> whitelist kept empty" 0 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["whitelist"]))' "$OUT")"


# ── config/plugin edits: two-phase plan/apply ────────────────────────────────────────────
# Plans are single-use, token-bound and expire after admin-plan-ttl (3 s in this config);
# each plan+apply pair must fit that window. A fresh second keeps the 20 req/s limiter out.
sleep 1
CFG="$WORK/config.ini"

# scope gates: 'read' and unrelated scopes may not edit config or the plugin list
auth "$GOOD" '{"jsonrpc":"2.0","id":80,"method":"admin.config.plan","params":{"set":{"webserver-limit-m":42}}}'
check "config.plan with only 'read' -> -32002" -32002 "$(field error.code)"
check_contains "config.plan -> names the missing scope" "no 'config' scope" "$(field error.data.reason)"
auth "$GOOD" '{"jsonrpc":"2.0","id":81,"method":"admin.plugins.plan","params":{"add":["block_info"]}}'
check "plugins.plan with only 'read' -> -32002" -32002 "$(field error.code)"
check_contains "plugins.plan -> names the missing scope" "no 'plugins' scope" "$(field error.data.reason)"

# allow-list: identity, endpoints and out-of-range values never reach a plan
auth "$ADM" '{"jsonrpc":"2.0","id":82,"method":"admin.config.plan","params":{"set":{"private-key":"5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC"}}}'
check "config.plan -> identity key refused" -32602 "$(field error.code)"
check_contains "config.plan -> allow-list named" "admin allow-list" "$(field error.data.reason)"
auth "$ADM" '{"jsonrpc":"2.0","id":83,"method":"admin.config.plan","params":{"set":{"webserver-http-endpoint":"0.0.0.0:9999"}}}'
check "config.plan -> endpoint key refused" -32602 "$(field error.code)"
auth "$ADM" '{"jsonrpc":"2.0","id":84,"method":"admin.config.plan","params":{"set":{"p2p-max-connections":0}}}'
check "config.plan -> out-of-range value refused" -32602 "$(field error.code)"
check_contains "config.plan -> range named" "between" "$(field error.data.reason)"
auth "$ADM" '{"jsonrpc":"2.0","id":85,"method":"admin.config.plan","params":{"set":{}}}'
check "config.plan -> empty set refused" -32602 "$(field error.code)"

# a real plan: one key replaces an existing entry, one is appended
ORIG_SHA=$(sha256sum "$CFG" | cut -d' ' -f1)
auth "$ADM" '{"jsonrpc":"2.0","id":86,"method":"admin.config.plan","params":{"set":{"webserver-limit-m":42,"dlt-stats-interval-sec":60}}}'
check "config.plan -> HTTP 200" 200 "$CODE"
check "config.plan -> kind" '"config"' "$(field result.kind)"
check "config.plan -> two diff entries" 2 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["diff"]))' "$OUT")"
check "config.plan -> diff names the replaced key" true \
    "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["diff"]; print(str(any(e["key"]=="webserver-limit-m" and e["from"]=="5" and e["to"]=="42" for e in d)).lower())' "$OUT")"
check "config.plan -> new key has a null 'from'" true \
    "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["diff"]; print(str(any(e["key"]=="dlt-stats-interval-sec" and e["from"] is None for e in d)).lower())' "$OUT")"
check "config.plan -> needs a restart" true "$(field result.needs_restart)"
PLAN=$(raw result.plan_id)
check "config.plan -> plan id is 32 chars" 32 "$(printf %s "$PLAN" | wc -c | tr -d ' ')"
check "config.plan -> wrote nothing" "$ORIG_SHA" "$(sha256sum "$CFG" | cut -d' ' -f1)"

# a no-op plan is refused: apply would rewrite an identical file for nothing
auth "$ADM" '{"jsonrpc":"2.0","id":87,"method":"admin.config.plan","params":{"set":{"webserver-limit-m":5}}}'
check "config.plan -> no-op refused" -32602 "$(field error.code)"
check_contains "config.plan -> says nothing changes" "no changes" "$(field error.data.reason)"

# a plan belongs to the token that made it (and survives that refused attempt)
auth "$ADM2" '{"jsonrpc":"2.0","id":88,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "config.apply from another token -> -32002" -32002 "$(field error.code)"
check_contains "config.apply -> token binding named" "another token" "$(field error.data.reason)"

# apply: backup first, then an atomic replace
auth "$ADM" '{"jsonrpc":"2.0","id":89,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "config.apply -> HTTP 200" 200 "$CODE"
check "config.apply -> applied" true "$(field result.applied)"
BACKUP=$(raw result.backup)
if [ -f "$BACKUP" ]; then
    echo "ok   config.apply -> backup file exists"
else
    echo "FAIL config.apply -> backup file missing at [$BACKUP]"; FAIL=$((FAIL + 1))
fi
check_contains "config.apply -> replaced value in config.ini" '^webserver-limit-m = 42$' \
    "$(grep -E '^webserver-limit-m' "$CFG")"
check_contains "config.apply -> appended key in config.ini" '^dlt-stats-interval-sec = 60$' \
    "$(grep -E '^dlt-stats-interval-sec' "$CFG")"
check_contains "config.apply -> backup keeps the old value" '^webserver-limit-m = 5$' \
    "$(grep -E '^webserver-limit-m' "$BACKUP" 2>/dev/null)"
check_contains "config.apply -> comments survive" '# Short TTL' "$(cat "$CFG")"

# single-use: the same plan cannot be replayed
auth "$ADM" '{"jsonrpc":"2.0","id":90,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "config.apply replay -> -32004" -32004 "$(field error.code)"
check_contains "config.apply replay -> unknown plan" "unknown plan" "$(field error.message)"

# backups are listed newest first, under the plain 'read' scope
auth "$GOOD" '{"jsonrpc":"2.0","id":91,"method":"admin.config.backups"}'
check "config.backups -> one backup" 1 "$(field result.count)"
check_contains "config.backups -> backup name" '.bak-' "$(field 'result.backups.0.name')"
check "config.backups -> backup is not empty" true \
    "$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1]))["result"]["backups"][0]["bytes"] > 0).lower())' "$OUT")"

# cancel removes a plan without writing anything
auth "$ADM" '{"jsonrpc":"2.0","id":92,"method":"admin.config.plan","params":{"set":{"webserver-limit-h":200}}}'
PLAN=$(raw result.plan_id)
auth "$ADM2" '{"jsonrpc":"2.0","id":93,"method":"admin.plan.cancel","params":{"plan_id":"'$PLAN'"}}'
check "plan.cancel from another token -> -32002" -32002 "$(field error.code)"
auth "$ADM" '{"jsonrpc":"2.0","id":94,"method":"admin.plan.cancel","params":{"plan_id":"'$PLAN'"}}'
check "plan.cancel -> cancelled" true "$(field result.cancelled)"
auth "$ADM" '{"jsonrpc":"2.0","id":95,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "cancelled plan cannot be applied -> -32004" -32004 "$(field error.code)"
check_contains "cancel wrote nothing" '^webserver-limit-h = 100$' "$(grep -E '^webserver-limit-h' "$CFG")"

# plans expire: wait past admin-plan-ttl, then apply
auth "$ADM" '{"jsonrpc":"2.0","id":96,"method":"admin.config.plan","params":{"set":{"webserver-limit-d":2000}}}'
PLAN=$(raw result.plan_id)
sleep 4
auth "$ADM" '{"jsonrpc":"2.0","id":97,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "expired plan -> -32004" -32004 "$(field error.code)"
check_contains "expired plan -> says expired" "plan expired" "$(field error.message)"
check_contains "expired plan wrote nothing" '^webserver-limit-d = 1000$' "$(grep -E '^webserver-limit-d' "$CFG")"

# the plan carries the file hash: a hand edit in between makes it stale
auth "$ADM" '{"jsonrpc":"2.0","id":98,"method":"admin.config.plan","params":{"set":{"webserver-limit-m":43}}}'
PLAN=$(raw result.plan_id)
printf '# hand edit made outside the admin API\n' >> "$CFG"
auth "$ADM" '{"jsonrpc":"2.0","id":99,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "stale plan -> -32000" -32000 "$(field error.code)"
check_contains "stale plan -> says nothing was written" "nothing was written" "$(field error.data.reason)"
check_contains "stale plan -> config kept the hand edit" '# hand edit' "$(cat "$CFG")"
check_contains "stale plan -> value untouched" '^webserver-limit-m = 42$' "$(grep -E '^webserver-limit-m' "$CFG")"

# ── plugin list edits ────────────────────────────────────────────────────────────────────
sleep 1
auth "$ADM" '{"jsonrpc":"2.0","id":100,"method":"admin.plugins.plan","params":{"add":["bogus_plugin"]}}'
check "plugins.plan -> unknown plugin refused" -32602 "$(field error.code)"
check_contains "plugins.plan -> unknown name named" "unknown plugin" "$(field error.data.reason)"
auth "$ADM" '{"jsonrpc":"2.0","id":101,"method":"admin.plugins.plan","params":{"remove":["node_admin"]}}'
check "plugins.plan -> node_admin cannot be removed" -32602 "$(field error.code)"
check_contains "plugins.plan -> protection named" "cannot be disabled" "$(field error.data.reason)"
auth "$ADM" '{"jsonrpc":"2.0","id":102,"method":"admin.plugins.plan","params":{"remove":["chain"]}}'
check "plugins.plan -> chain cannot be removed" -32602 "$(field error.code)"
auth "$ADM" '{"jsonrpc":"2.0","id":103,"method":"admin.plugins.plan","params":{"remove":["validator"]}}'
check "plugins.plan -> removing an absent plugin refused" -32602 "$(field error.code)"
check_contains "plugins.plan -> absence named" "not configured" "$(field error.data.reason)"
auth "$ADM" '{"jsonrpc":"2.0","id":104,"method":"admin.plugins.plan","params":{"add":["block_info","validator_guard"],"remove":["snapshot"]}}'
check "plugins.plan -> HTTP 200" 200 "$CODE"
check "plugins.plan -> three diff entries" 3 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["diff"]))' "$OUT")"
check "plugins.plan -> adds block_info" true \
    "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["diff"]; print(str(any(e["op"]=="add" and e["name"]=="block_info" for e in d)).lower())' "$OUT")"
check "plugins.plan -> adds validator_guard" true \
    "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["diff"]; print(str(any(e["op"]=="add" and e["name"]=="validator_guard" for e in d)).lower())' "$OUT")"
check "plugins.plan -> removes snapshot" true \
    "$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["diff"]; print(str(any(e["op"]=="remove" and e["name"]=="snapshot" for e in d)).lower())' "$OUT")"
check_contains "plugins.plan -> nothing written" '^plugin = snapshot$' "$(grep -E '^plugin = ' "$CFG")"

# a config plan is not a plugin plan
PLAN=$(raw result.plan_id)
auth "$ADM" '{"jsonrpc":"2.0","id":105,"method":"admin.config.apply","params":{"plan_id":"'$PLAN'"}}'
check "config.apply of a plugin plan -> -32602" -32602 "$(field error.code)"
check_contains "config.apply -> kind mismatch named" "plan kind is 'plugins'" "$(field error.data.reason)"

auth "$ADM" '{"jsonrpc":"2.0","id":106,"method":"admin.plugins.apply","params":{"plan_id":"'$PLAN'"}}'
check "plugins.apply -> applied" true "$(field result.applied)"
check_contains "plugins.apply -> block_info in config.ini" '^plugin = block_info$' "$(grep -E '^plugin = ' "$CFG")"
if grep -qE '^plugin = snapshot$' "$CFG"; then
    echo "FAIL plugins.apply -> snapshot still present"; FAIL=$((FAIL + 1))
else
    echo "ok   plugins.apply -> snapshot removed"
fi
check "plugins.apply -> five plugin lines" 5 "$(grep -cE '^plugin = ' "$CFG")"
auth "$GOOD" '{"jsonrpc":"2.0","id":107,"method":"admin.config.backups"}'
check "plugins.apply -> wrote a second backup" 2 "$(field result.count)"

# ── MCP (Streamable HTTP) on the same listener ───────────────────────────────────────────
sleep 1
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
check "MCP schemas use named properties and valid required keys" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(all(isinstance(t["inputSchema"]["properties"],dict) and all(k in t["inputSchema"]["properties"] for k in t["inputSchema"].get("required",[])) for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> forty-one tools" 41 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["tools"]))' "$OUT")"
check "MCP tools/list -> admin.auth.me present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.auth.me" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.logs.list present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.logs.list" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.validators.active present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.validators.active" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.node_info present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.node_info" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.p2p.status present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.p2p.status" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.snapshot.status present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.snapshot.status" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.snapshot.create present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.snapshot.create" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.snapshot.restore present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.snapshot.restore" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.webserver.status present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.webserver.status" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.webserver.blacklist_add present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.webserver.blacklist_add" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.config.plan present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.config.plan" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.restart present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.restart" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.validator.list present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.validator.list" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.validator.plan present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.validator.plan" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.plugins.status present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.plugins.status" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.objects.list present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.objects.list" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.objects.check present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.objects.check" for t in ts)).lower())' "$OUT")"
check "MCP tools/list -> admin.objects.repair present" true \
    "$(python3 -c 'import json,sys; ts=json.load(open(sys.argv[1]))["result"]["tools"]; print(str(any(t["name"]=="admin.objects.repair" for t in ts)).lower())' "$OUT")"

auth "$GOOD" '{"jsonrpc":"2.0","id":70,"method":"tools/call","params":{"name":"admin.webserver.limits","arguments":{"per_minute":10}}}'
check "MCP webserver.limits without 'webserver' scope -> isError" true "$(field result.isError)"
check_contains "MCP webserver.limits -> scope named" "no 'webserver' scope" \
    "$(field 'result.content.0.text')"

auth "$WS" '{"jsonrpc":"2.0","id":71,"method":"tools/call","params":{"name":"admin.webserver.status","arguments":{}}}'
check "MCP webserver.status -> not an error" false "$(field result.isError)"
check_contains "MCP webserver.status -> returns limits" 'per_minute' "$(field 'result.content.0.text')"

auth "$GOOD" '{"jsonrpc":"2.0","id":27,"method":"tools/call","params":{"name":"admin.p2p.add_node","arguments":{"endpoint":"127.0.0.1:1"}}}'
check "MCP mutating tool without 'p2p' -> isError" true "$(field result.isError)"
check_contains "MCP mutating tool -> scope named" "no 'p2p' scope" "$(field 'result.content.0.text')"

auth "$GOOD" '{"jsonrpc":"2.0","id":28,"method":"tools/call","params":{"name":"admin.snapshot.create","arguments":{}}}'
check "MCP snapshot.create without 'snapshot' scope -> isError" true "$(field result.isError)"
check_contains "MCP snapshot.create -> scope named" "no 'snapshot' scope" \
    "$(field 'result.content.0.text')"

auth "$SNAP" '{"jsonrpc":"2.0","id":29,"method":"tools/call","params":{"name":"admin.snapshot.status","arguments":{}}}'
check "MCP snapshot.status -> not an error" false "$(field result.isError)"
check_contains "MCP snapshot.status -> returns the snapshot count" 'snapshot_count' \
    "$(field 'result.content.0.text')"

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
check_contains "GET /ui/ -> reads snapshot status" 'admin.snapshot.status' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> has the create-snapshot action" 'admin.snapshot.create' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> links plugins to their docs on GitHub Pages" 'https://viz-blockchain.github.io/viz-cpp-node/' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> per-plugin panel with status and parameters tabs" 'id="plugTabs"' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> has the restore action" 'admin.snapshot.restore' "$(cat "$WORK/ui.html")"
check_contains "GET /ui/ -> downloads snapshots by name" '/snapshot/' "$(cat "$WORK/ui.html")"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' "$BASE/ui")
check "GET /ui without slash -> HTTP 200" 200 "$CODE"


auth "$GOOD" '{"jsonrpc":"2.0","id":13,'
check "malformed JSON -> -32700" -32700 "$(field error.code)"

python3 -c 'print("{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"admin.ping\",\"pad\":\"" + "a"*70000 + "\"}")' > "$WORK/big.json"
CODE=$(curl -s -o "$OUT" -w '%{http_code}' -X POST "$BASE" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $GOOD" --data-binary @"$WORK/big.json")
check "oversized body -> HTTP 413" 413 "$CODE"

# ── validator operations need a live validator_guard ─────────────────────────────────────
# The plugin is not in the initial config, so the read answers "loaded: false" and the
# mutating methods are refused before any key is even considered. The happy path (signing
# through the guard) needs real validators and is covered by a scratch chain, not here.
auth "$GOOD" '{"jsonrpc":"2.0","id":120,"method":"admin.validator.list","params":{}}'
check "validator.list without the guard -> HTTP 200" 200 "$CODE"
check "validator.list -> guard not loaded" false "$(field result.guard.loaded)"
check_contains "validator.list -> names the plugin to add" 'plugin = validator_guard' \
    "$(field result.guard.reason)"
check "validator.list -> no validators" 0 \
    "$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["result"]["validators"]))' "$OUT")"
auth "$GOOD" '{"jsonrpc":"2.0","id":121,"method":"admin.validator.plan","params":{"actions":[{"name":"v1","action":"disable"}]}}'
check "validator.plan with only 'read' -> -32002" -32002 "$(field error.code)"
check_contains "validator.plan -> chain scope named" "no 'chain' scope" "$(field error.data.reason)"
auth "$CHN" '{"jsonrpc":"2.0","id":122,"method":"admin.validator.plan","params":{"actions":[{"name":"v1","action":"disable"}]}}'
check "validator.plan with 'chain' but no guard -> -32002" -32002 "$(field error.code)"
check_contains "validator.plan -> missing guard named" "validator-guard is not configured" \
    "$(field error.data.reason)"

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

# ── restart through the supervisor contract (last: the process really exits) ─────────────
sleep 1
auth "$ADM" '{"jsonrpc":"2.0","id":110,"method":"admin.restart","params":{"confirm":"please"}}'
check "restart -> confirm must be exact" -32602 "$(field error.code)"
check_contains "restart -> names the required word" "exactly the word 'restart'" \
    "$(field error.data.reason)"
auth "$GOOD" '{"jsonrpc":"2.0","id":111,"method":"admin.restart","params":{"confirm":"restart"}}'
check "restart without 'process' scope -> -32002" -32002 "$(field error.code)"
check_contains "restart -> scope named" "no 'process' scope" "$(field error.data.reason)"

auth "$ADM" '{"jsonrpc":"2.0","id":112,"method":"admin.restart","params":{"confirm":"restart"}}'
check "restart -> HTTP 200" 200 "$CODE"
check "restart -> ok" true "$(field result.ok)"
check "restart -> restarting" true "$(field result.restarting)"
check "restart -> replies before quitting" 0 "$(field result.exit_code)"

for _ in $(seq 1 80); do
    kill -0 "$NODE_PID" 2>/dev/null || break
    sleep 0.25
done
if kill -0 "$NODE_PID" 2>/dev/null; then
    echo "FAIL restart -> the process is still running after 20 s"; FAIL=$((FAIL + 1))
    kill "$NODE_PID" 2>/dev/null
else
    echo "ok   restart -> the process exited"
fi
wait "$NODE_PID"; NODE_RC=$?
NODE_PID=
check "restart -> clean exit code" 0 "$NODE_RC"

# the edits persist on disk; the supervisor-style start reads them back
check_contains "restart -> config.ini keeps webserver-limit-m = 42" '^webserver-limit-m = 42$' \
    "$(grep -E '^webserver-limit-m' "$CFG")"
check_contains "restart -> config.ini keeps plugin = block_info" '^plugin = block_info$' \
    "$(grep -E '^plugin = ' "$CFG")"

"$VIZD" --data-dir "$WORK" > "$WORK/console2.log" 2>&1 &
NODE_PID=$!
for _ in $(seq 1 120); do
    grep -q "node_admin: listening" "$WORK/console2.log" && break
    kill -0 "$NODE_PID" 2>/dev/null || break
    sleep 0.25
done
if grep -q "node_admin: listening" "$WORK/console2.log"; then
    echo "ok   restart -> the node is back after the supervisor-style start"
else
    echo "FAIL restart -> the node did not come back"
    tail -20 "$WORK/console2.log"
    FAIL=$((FAIL + 1))
fi
auth "$ADM" '{"jsonrpc":"2.0","id":113,"method":"admin.ping"}'
check "restart -> admin answers after the restart" true "$(field result.ok)"

auth "$GOOD" '{"jsonrpc":"2.0","id":126,"method":"admin.webserver.status"}'
check "restart -> config limit overrides stale runtime policy" 42 "$(field result.limits.per_minute)"

auth "$ADM" '{"jsonrpc":"2.0","id":114,"method":"admin.plugins.list"}'
check "restart -> block_info is configured now" true \
    "$(python3 -c 'import json,sys; ps=json.load(open(sys.argv[1]))["result"]["plugins"]; print(str(any(p["name"]=="block_info" for p in ps)).lower())' "$OUT")"
check "restart -> snapshot is gone from the plugin list" true \
    "$(python3 -c 'import json,sys; ps=json.load(open(sys.argv[1]))["result"]["plugins"]; print(str(not any(p["name"]=="snapshot" for p in ps)).lower())' "$OUT")"

# the restarted node loads validator_guard (added by the plan above) but has no validator
# configured with an active key, so the mutating methods stay refused — the scope alone
# never authorises a signature
auth "$GOOD" '{"jsonrpc":"2.0","id":123,"method":"admin.validator.list","params":{}}'
check "restart -> guard is loaded" true "$(field result.guard.loaded)"
check "restart -> guard is started" true "$(field result.guard.started)"
check "restart -> guard has no active-key validator" false "$(field result.guard.available)"
check_contains "restart -> guard says what is missing" 'no validator is configured with an active key' \
    "$(field result.guard.reason)"
auth "$CHN" '{"jsonrpc":"2.0","id":124,"method":"admin.validator.plan","params":{"actions":[{"name":"v1","action":"disable"}]}}'
check "restart -> validator.plan needs a live guard -> -32002" -32002 "$(field error.code)"
auth "$CHN" '{"jsonrpc":"2.0","id":125,"method":"tools/call","params":{"name":"admin.validator.list","arguments":{}}}'
check "restart -> MCP validator.list -> not an error" false "$(field result.isError)"
check_contains "restart -> MCP validator.list returns the guard block" 'no validator is configured with an active key' \
    "$(field 'result.content.0.text')"

# ── objects.repair: the action itself, not only its refusals ───────────────────────────────
# Everything above proves the gate; a gate that refused every repair would pass all of it.
# This node is throwaway and has no supervisor, so the script plays one: repair, wait for
# the clean exit, start it again and check that the marker drove the startup.
auth "$ADM" '{"jsonrpc":"2.0","id":131,"method":"admin.objects.repair","params":{"mode":"resync","confirm":"repair"}}'
check "objects.repair resync -> HTTP 200" 200 "$CODE"
check "objects.repair resync -> accepted" true "$(field result.ok)"
check "objects.repair resync -> effect names the resync" true \
    "$(python3 -c 'import json,sys; print(str("resyncs from peers" in json.load(open(sys.argv[1]))["result"]["effect"]).lower())' "$OUT")"
check "objects.repair resync -> snapshot mode reported off" false "$(field result.snapshot_replay)"
check "objects.repair resync -> force_resync marker written" 1 \
    "$(ls "$WORK/state/force_resync" 2>/dev/null | wc -l)"

for _ in $(seq 1 80); do
    kill -0 "$NODE_PID" 2>/dev/null || break
    sleep 0.25
done
if kill -0 "$NODE_PID" 2>/dev/null; then
    echo "FAIL objects.repair -> the process is still running after 20 s"; FAIL=$((FAIL + 1))
    kill "$NODE_PID" 2>/dev/null
else
    echo "ok   objects.repair -> the process exited"
fi
wait "$NODE_PID"; NODE_RC=$?
NODE_PID=
check "objects.repair -> clean exit code" 0 "$NODE_RC"

"$VIZD" --data-dir "$WORK" > "$WORK/console3.log" 2>&1 &
NODE_PID=$!
for _ in $(seq 1 120); do
    grep -q "node_admin: listening" "$WORK/console3.log" && break
    kill -0 "$NODE_PID" 2>/dev/null || break
    sleep 0.25
done
if grep -q "node_admin: listening" "$WORK/console3.log"; then
    echo "ok   objects.repair -> the node is back after the supervisor-style start"
else
    echo "FAIL objects.repair -> the node did not come back"
    tail -20 "$WORK/console3.log"
    FAIL=$((FAIL + 1))
fi
# The marker must be what the startup acted on, not a leftover that happens to be harmless.
check "objects.repair -> startup honoured the marker" 1 \
    "$(grep -c 'Detected force_resync marker' "$WORK/console3.log")"
check "objects.repair -> marker consumed at startup" 0 \
    "$(ls "$WORK/state/force_resync" 2>/dev/null | wc -l)"
auth "$GOOD" '{"jsonrpc":"2.0","id":132,"method":"admin.objects.check"}'
check "objects.repair -> state is healthy after the resync" '"ok"' "$(field result.verdict)"

# ── a marker from the other mode must not outrank the confirmed choice ─────────────────────
# Startup precedence is resync > replay: with force_resync present the chain plugin removes a
# force_replay marker. So a stale opposite marker would silently turn "replay (keeps blocks)"
# into a full resync. Reproducible here without a block log by the symmetric direction: a
# stale force_replay must be cleared and named in the reply, not left to outlive the request.
touch "$WORK/state/force_replay"
auth "$ADM" '{"jsonrpc":"2.0","id":140,"method":"admin.objects.repair","params":{"mode":"resync","confirm":"repair"}}'
check "objects.repair -> stale opposite marker is named" 1 \
    "$(python3 -c 'import json,sys; print(int(bool(json.load(open(sys.argv[1]))["result"].get("removed_marker"))))' "$OUT")"
check "objects.repair -> stale opposite marker is gone" 0 \
    "$(ls "$WORK/state/force_replay" 2>/dev/null | wc -l)"
check "objects.repair -> the requested marker is written" 1 \
    "$(ls "$WORK/state/force_resync" 2>/dev/null | wc -l)"

for _ in $(seq 1 80); do
    kill -0 "$NODE_PID" 2>/dev/null || break
    sleep 0.25
done
wait "$NODE_PID"; NODE_RC=$?
NODE_PID=
check "objects.repair -> clean exit on the second cycle" 0 "$NODE_RC"

"$VIZD" --data-dir "$WORK" > "$WORK/console4.log" 2>&1 &
NODE_PID=$!
for _ in $(seq 1 120); do
    grep -q "node_admin: listening" "$WORK/console4.log" && break
    kill -0 "$NODE_PID" 2>/dev/null || break
    sleep 0.25
done
if grep -q "node_admin: listening" "$WORK/console4.log"; then
    echo "ok   objects.repair -> the node is back after the second cycle"
else
    echo "FAIL objects.repair -> the node did not come back after the second cycle"
    tail -20 "$WORK/console4.log"
    FAIL=$((FAIL + 1))
fi
check "objects.repair -> only the requested marker was honoured" 1 \
    "$(grep -c 'Detected force_resync marker' "$WORK/console4.log")"
check "objects.repair -> no replay marker reached startup" 0 \
    "$(grep -c 'Detected force_replay marker' "$WORK/console4.log")"
auth "$GOOD" '{"jsonrpc":"2.0","id":141,"method":"admin.objects.check"}'
check "objects.repair -> state is healthy after the second cycle" '"ok"' "$(field result.verdict)"

echo
if [ "$FAIL" -eq 0 ]; then
    echo "PASS: all node_admin acceptance checks"
else
    echo "FAILED: $FAIL check(s); work dir kept at $WORK"
    exit 1
fi
