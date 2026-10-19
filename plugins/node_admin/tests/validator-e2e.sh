#!/usr/bin/env bash
# Scratch-chain end-to-end check for admin.validator.* on validator_guard.
#
# acceptance.sh proves the gate (no guard / guard without an active-key validator).
# This script proves the signing path itself, which needs a chain that produces blocks
# and a validator whose active key we hold. It builds a throwaway chain in a temp dir.
#
# Why the cold start is staged the way it is (testnet build, CHAIN_MAX_VALIDATORS = 1):
#   * before HF12 the only validator with a signing key is `committee`, and under a
#     one-slot schedule it wins the top slot by name order over a fresh account, so a
#     newly declared validator never gets scheduled — and HF9 (block 9) deletes every
#     validator whose last_confirmed_block_num is still 0;
#   * therefore `viz` is registered, vested (validator votes weigh vesting SHARES) and
#     voted for in the first blocks, so it outranks `committee`, produces before HF9
#     and stays in the schedule after HF12 excludes committee from selection;
#   * two more validators are created after HF12 (declaring one costs the
#     validator_declaration_fee in liquid VIZ): `backup` carries the operator
#     actions, `cold` only exists so a batch plan can name two different valida-
#     tors without touching the producing one.
#
# Why operator actions target `backup`, not `viz`: the schedule is rebuilt only at the
# end of an applied block. Nulling the signing key of the only *scheduled* validator
# leaves the stale schedule pointing at a keyless validator, so no further block is
# produced and the chain cannot rebuild its schedule — a single-slot chain bricks.
# `backup` is not scheduled while `viz` holds the top votes, so it can be disabled,
# held across a restart and re-enabled without stopping production.
#
# Why `viz`'s own signing key is never rotated here: the producer picks the key from
# the session-visible validator object (validator.cpp:1875), so a pending
# validator_update that rotates the producing validator's key makes it sign the block
# with the NEW key, while validate_block_header checks the signature against the
# pre-block (old) key. The block is rejected, the pending pool is cleared and the
# update is lost. Rotating a non-producing validator is unaffected.
#
# The chain is disposable; nothing outside the temp dir is touched.
#
# usage: plugins/node_admin/tests/validator-e2e.sh [--keep]
#        VIZD=... CLI_WALLET=... to point at other binaries (defaults: build/)
# exit:  0 all checks passed, 1 a check failed, 2 could not set up

set -u

KEEP=0
[ "${1:-}" = "--keep" ] && KEEP=1

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
VIZD=${VIZD:-$ROOT/build/programs/vizd/vizd}
CLI_WALLET=${CLI_WALLET:-$ROOT/build/programs/cli_wallet/cli_wallet}
for b in "$VIZD" "$CLI_WALLET"; do
    [ -x "$b" ] || { echo "missing binary: $b" >&2; exit 2; }
done

DIR=$(mktemp -d /tmp/validator-e2e.XXXXXX)
BASE=$(( 20000 + ($$ * 37) % 30000 ))
HTTP=$BASE
WS=$((BASE + 1))
ADM=$((BASE + 2))
TOKEN=validator-e2e-token
COMMITTEE_WIF=5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC
INITIATOR_WIF=5JabcrvaLnBTCkCVFX5r4rmeGGfuJuVp4NAKRNLTey6pxhRQmf4
COMMITTEE_PUB=VIZ6Yt7d6LsngBoXQr47aLv97bJVs7jyr7esZTM4UUSpLUf3nbRKS
INITIATOR_PUB=VIZ6MyX5QiXAXRZk7SYCiqpi6Mtm8UbHWDFSV8HPpt7FJyahCnc2T

PID=
stop_node() {
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null
        for _ in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.25; done
        kill -0 "$PID" 2>/dev/null && kill -9 "$PID" 2>/dev/null
        wait "$PID" 2>/dev/null
    fi
    PID=
}
cleanup() {
    stop_node
    if [ "$KEEP" = 1 ]; then
        echo "kept: $DIR"
    else
        rm -rf "$DIR"
    fi
}
trap cleanup EXIT

PASS=0
FAIL=0
ok()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad()  { FAIL=$((FAIL + 1)); echo "FAIL $*"; }
check() { # check <name> <expected> <actual>
    if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (expected '$2', got '$3')"; fi
}
check_contains() {
    case "$3" in *"$2"*) ok "$1" ;; *) bad "$1 (expected to contain '$2', got '$3')" ;; esac
}

# ── config ────────────────────────────────────────────────────────────────────
# $1 = 1 adds the backup/cold entries to the guard (the guard refuses to start for
# an account that does not exist yet, so this happens after the accounts are created).
write_config() {
    SHA=$(printf '%s' "$TOKEN" | sha256sum | cut -d' ' -f1)
    cat > "$DIR/config.ini" <<EOF
plugin = chain
plugin = json_rpc
plugin = p2p
plugin = webserver
plugin = validator
plugin = database_api
plugin = network_broadcast_api
plugin = validator_api
plugin = validator_guard
plugin = node_admin

validator = "committee"
validator = "viz"
validator = "backup"
private-key = $COMMITTEE_WIF
private-key = $INITIATOR_WIF
enable-stale-production = true
disable-minority-fork-detection = true

# viz is the only scheduled validator on this one-slot chain and produces every
# block; the anti-centralisation auto-disable would fire on the fifth block.
validator-guard-disable = 1000000
validator-guard-validator = ["viz", "$INITIATOR_WIF", "$INITIATOR_WIF"]
EOF
    if [ "${1:-0}" = 1 ]; then
        echo "validator-guard-validator = [\"backup\", \"$COMMITTEE_WIF\", \"$COMMITTEE_WIF\"]" >> "$DIR/config.ini"
        echo "validator-guard-validator = [\"cold\", \"$COMMITTEE_WIF\", \"$COMMITTEE_WIF\"]" >> "$DIR/config.ini"
    fi
    cat >> "$DIR/config.ini" <<EOF

webserver-http-endpoint = 127.0.0.1:$HTTP
webserver-ws-endpoint = 127.0.0.1:$WS

admin-endpoint = 127.0.0.1:$ADM
admin-token = $TOKEN:read,chain:$SHA
EOF
}

start_node() { # start_node <log>
    "$VIZD" --data-dir "$DIR" > "$1" 2>&1 &
    PID=$!
}

head_now() {
    curl -s -m 2 -X POST "http://127.0.0.1:$HTTP" \
        -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["database_api","get_dynamic_global_properties",[]]}' \
    | python3 -c 'import json,sys
try: print(json.load(sys.stdin)["result"]["head_block_number"])
except Exception: print(0)'
}

wait_head() { # wait_head <min-block> <seconds>
    local deadline=$(( $(date +%s) + $2 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        local h
        h=$(head_now)
        [ "${h:-0}" -ge "$1" ] 2>/dev/null && { echo "$h"; return 0; }
        sleep 0.5
    done
    echo 0
    return 1
}

vfield() { # vfield <name> <dotted-path> ; empty when the validator is not on chain
    curl -s -m 3 -X POST "http://127.0.0.1:$HTTP" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"call\",\"params\":[\"validator_api\",\"get_validator_by_account\",[\"$1\"]]}" \
    | python3 -c 'import json,sys
path = sys.argv[1]
try: d = json.load(sys.stdin).get("result")
except Exception: d = None
for part in path.split("."):
    if isinstance(d, dict): d = d.get(part)
    else: d = None
    if d is None: break
if d == "VIZ1111111111111111111111111111111114T1Anm":
    d = ""  # null_key as validator_api renders it; the admin list prints it empty
print("" if d is None else (json.dumps(d) if isinstance(d, (dict, list)) else d))' "$2"
}
wait_validator() { # wait_validator <name> <seconds> ; 0 once the validator object exists
    local deadline=$(( $(date +%s) + $2 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        [ -n "$(vfield "$1" owner)" ] && return 0
        sleep 0.5
    done
    return 1
}

admin() { # admin <method> <params-json> ; sets OUT and CODE
    OUT=$(mktemp)
    CODE=$(curl -s -m 10 -o "$OUT" -w '%{http_code}' -X POST "http://127.0.0.1:$ADM" \
        -H "Authorization: Bearer $TOKEN" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"$1\",\"params\":$2}")
}
field() { # field <dotted-path> [file] ; booleans print true/false, null prints empty
    python3 - "$1" "${2:-$OUT}" <<'PY'
import json, sys
path, name = sys.argv[1], sys.argv[2]
try:
    d = json.load(open(name))
except Exception:
    print(""); raise SystemExit
for part in path.split('.'):
    if isinstance(d, list):
        d = d[int(part)] if part.isdigit() and int(part) < len(d) else None
    elif isinstance(d, dict):
        d = d.get(part)
    else:
        d = None
    if d is None: break
if d is None:
    print("")
elif isinstance(d, bool):
    print("true" if d else "false")
elif isinstance(d, (dict, list)):
    print(json.dumps(d))
else:
    print(d)
PY
}
count_actions() { # results with a non-empty status
    python3 - "$1" <<'PY'
import json, sys
try: acts = json.load(open(sys.argv[1]))["result"]["results"]
except Exception: print(0); raise SystemExit
print(sum(1 for a in acts if a.get("status")))
PY
}

hold_count() { # hold_count <name>
    local f="$DIR/validator-guard-holds.txt"
    if [ -f "$f" ]; then awk -v n="$1" '$2 == n' "$f" | wc -l | tr -d ' '; else echo 0; fi
}

wallet() { # wallet <commands> ; sets WALLET_OUT
    WALLET_OUT=$(timeout 90 "$CLI_WALLET" -s "ws://127.0.0.1:$WS" -w "$DIR/wallet.json" \
        --logger.default.level=error --logger.rpc.level=off -C "$1" 2>&1)
}
wallet_block() { # print the last block_num mentioned by the wallet output
    # cli_wallet prints each command result as a JSON *string* whose body is the
    # serialized transaction (escaped newlines), so decode twice before looking.
    printf '%s' "$WALLET_OUT" | python3 -c 'import json,sys
best = ""
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        v = json.loads(line)
    except Exception:
        continue
    if isinstance(v, str):
        try:
            v = json.loads(v)
        except Exception:
            continue
    if isinstance(v, dict) and v.get("block_num"):
        best = str(v["block_num"])
print(best)'
}

# ── 1. cold start: get `viz` scheduled before HF9 ────────────────────────────
write_config 0
start_node "$DIR/n1.log"
H=$(wait_head 1 45) || { echo "chain did not produce its first block; see $DIR/n1.log" >&2; tail -20 "$DIR/n1.log" >&2; exit 2; }
ok "committee produces blocks (head $H)"

wallet "set_password pw && unlock pw && import_key $INITIATOR_WIF && import_key $COMMITTEE_WIF && update_validator \"viz\" \"https://viz.example\" $INITIATOR_PUB true && transfer_to_vesting \"viz\" \"viz\" \"1000.000 VIZ\" true && vote_for_validator \"viz\" \"viz\" true true"
REG_BLOCK=$(wallet_block)
[ -n "$REG_BLOCK" ] || { echo "could not register viz as validator: $WALLET_OUT" >&2; exit 2; }
if [ "$REG_BLOCK" -ge 9 ]; then
    echo "registration landed at block $REG_BLOCK, after HF9 (block 9) removes validators that never signed a block; the chain will halt" >&2
    exit 2
fi
ok "viz registered, vested and voted (block $REG_BLOCK)"

H=$(wait_head 12 90) || { echo "chain did not pass HF9/HF12; see $DIR/n1.log" >&2; tail -20 "$DIR/n1.log" >&2; exit 2; }
check "viz survived the HF9 spam cleanup" 0 "$(grep -c 'HF9 remove empty/spam validator viz' "$DIR/n1.log")"
check "committee stopped producing after HF12" 0 "$(grep -c 'Validated block #1[3-9] .* by committee' "$DIR/n1.log")"
ok "chain passed HF9 and HF12 (head $H)"

# ── 2. add two validators for operator actions ───────────────────────────────
# The declaration fee is paid from the account's LIQUID balance, so the account
# has to exist and be funded before validator_update; a transfer to a not-yet
# created account fails and a failed broadcast still prints block_num, so both
# steps are checked against chain state, not wallet output.
#
# `backup` carries disable/enable/set_key; `cold` exists only so that a batch plan
# can name two different validators without touching the producing one. Rotating
# the signing key of the validator that produces the next block does not land on a
# single-slot chain: the producer selects the key from the pending-transaction
# state (validator.cpp:1875 reads the session-visible validator object), signs the
# block with the new key, and validate_block_header then rejects it against the
# pre-block key; the pending pool is cleared and the update is lost.
for name in backup cold; do
    wallet "unlock pw && import_key $COMMITTEE_WIF && create_account_with_keys \"viz\" \"1000.000 VIZ\" \"0.000000 SHARES\" \"$name\" \"{}\" $COMMITTEE_PUB $COMMITTEE_PUB $COMMITTEE_PUB $COMMITTEE_PUB true"
    sleep 1
    wallet "unlock pw && transfer \"viz\" \"$name\" \"20000.000 VIZ\" \"declaration fee\" true"
    sleep 1
    wallet "unlock pw && update_validator \"$name\" \"https://$name.example\" $COMMITTEE_PUB true"
    if wait_validator "$name" 20; then
        ok "$name declared as validator"
    else
        echo "$name never appeared on chain; wallet output:" >&2
        printf '%s\n' "$WALLET_OUT" >&2
        exit 2
    fi
done

# The guard refuses to start for a configured validator whose account is missing,
# so the extra entries are added only now.
stop_node
write_config 1
start_node "$DIR/n2.log"
H=$(wait_head $((REG_BLOCK + 2)) 45) || { bad "chain did not come back after restart"; exit 1; }
ok "chain restarts with the guard watching three validators (head $H)"

# ── 3. list: two validators, viz is signing ─────────────────────────────────
admin admin.validator.list '{}'
check "list -> HTTP 200" 200 "$CODE"
check "list -> guard available" true "$(field result.guard.available)"
check "list -> backup is on chain" true "$(field result.validators.0.on_chain)"
check "list -> backup is enabled" true "$(field result.validators.0.enabled)"
check "list -> backup configured key" "$COMMITTEE_PUB" "$(field result.validators.0.configured_key)"
check "list -> cold is on chain" true "$(field result.validators.1.on_chain)"
check "list -> viz is on chain" true "$(field result.validators.2.on_chain)"
check "list -> viz signing key" "$INITIATOR_PUB" "$(field result.validators.2.signing_key)"
check "list -> viz active authority" true "$(field result.validators.2.active_authority)"

# ── 4. disable backup → hold file, chain keeps producing via viz ────────────
admin admin.validator.plan '{"actions":[{"name":"backup","action":"disable"}]}'
check "disable plan -> HTTP 200" 200 "$CODE"
PLAN=$(field result.plan_id)
[ -n "$PLAN" ] || { bad "disable plan produced no plan_id"; exit 1; }
admin admin.validator.apply "{\"plan_id\":\"$PLAN\"}"
check "disable apply -> one action" 1 "$(count_actions "$OUT")"
check "disable apply -> ok" "applied" "$(field result.results.0.status)"
check "disable apply -> transaction id" true \
    "$(python3 -c 'import json,sys; print(str(bool(json.load(open(sys.argv[1]))["result"]["results"][0].get("tx_id"))).lower())' "$OUT")"
admin admin.validator.list '{}'
check "after disable -> backup signing key is null" "" "$(field result.validators.0.signing_key)"
check "after disable -> backup hold is set" true "$(field result.validators.0.hold)"
check "after disable -> hold file has one line" 1 "$(hold_count backup)"
H=$(wait_head $((H + 2)) 30) || { bad "chain stopped after disabling a non-scheduled validator"; exit 1; }
ok "chain keeps producing (head $H)"

# ── 5. the hold survives a restart (auto-restore must not undo the operator) ──
stop_node
start_node "$DIR/n3.log"
H=$(wait_head $((H + 1)) 45) || { bad "chain did not come back after restart"; exit 1; }
ok "chain restarts (head $H)"
check_contains "restart restores the hold" "restored 1 operator hold" "$(cat "$DIR/n3.log")"
admin admin.validator.list '{}'
check "after restart -> backup still disabled" "" "$(field result.validators.0.signing_key)"
check "after restart -> hold still set" true "$(field result.validators.0.hold)"

# ── 6. enable clears the hold ────────────────────────────────────────────────
admin admin.validator.plan '{"actions":[{"name":"backup","action":"enable"}]}'
PLAN=$(field result.plan_id)
admin admin.validator.apply "{\"plan_id\":\"$PLAN\"}"
check "enable apply -> ok" "applied" "$(field result.results.0.status)"
admin admin.validator.list '{}'
check "after enable -> signing key back" "$COMMITTEE_PUB" "$(field result.validators.0.signing_key)"
check "after enable -> hold cleared" false "$(field result.validators.0.hold)"
check "after enable -> hold file empty" 0 "$(hold_count backup)"

# ── 7. set_key changes the on-chain key, configured key stays ───────────────
admin admin.validator.plan "{\"actions\":[{\"name\":\"backup\",\"action\":\"set_key\",\"signing_key\":\"$INITIATOR_PUB\"}]}"
PLAN=$(field result.plan_id)
admin admin.validator.apply "{\"plan_id\":\"$PLAN\"}"
check "set_key apply -> ok" "applied" "$(field result.results.0.status)"
admin admin.validator.list '{}'
check "set_key -> on-chain key changed" "$INITIATOR_PUB" "$(field result.validators.0.signing_key)"
check "set_key -> configured key untouched" "$COMMITTEE_PUB" "$(field result.validators.0.configured_key)"

# ── 8. a plan whose premise moved is refused, not signed ─────────────────────
admin admin.validator.plan '{"actions":[{"name":"backup","action":"disable"}]}'
PLAN=$(field result.plan_id)
wallet "unlock pw && update_validator \"backup\" \"https://backup.example\" $COMMITTEE_PUB true"
for _ in $(seq 1 40); do
    [ "$(vfield backup signing_key)" = "$COMMITTEE_PUB" ] && break
    sleep 0.5
done
check "external update applied on chain" "$COMMITTEE_PUB" "$(vfield backup signing_key)"
H=$(wait_head $((H + 2)) 30) || true
admin admin.validator.apply "{\"plan_id\":\"$PLAN\"}"
check "stale plan -> failed" "failed" "$(field result.results.0.status)"
check_contains "stale plan -> names the reason" "chain state changed" "$(field result.results.0.error)"
admin admin.validator.list '{}'
check "stale plan -> nothing was signed" "$COMMITTEE_PUB" "$(field result.validators.0.signing_key)"
check "stale plan -> no hold" false "$(field result.validators.0.hold)"

# ── 9. one plan, two validators ─────────────────────────────────────────────
# Each action becomes its own signed transaction (one signing key per op), so the
# plan reports two. The batch rotates `cold`, not the producing validator: see the
# self-rotation limitation in the section 2 comment.
admin admin.validator.plan "{\"actions\":[{\"name\":\"backup\",\"action\":\"disable\"},{\"name\":\"cold\",\"action\":\"set_key\",\"signing_key\":\"$INITIATOR_PUB\"}]}"
check "batch plan -> HTTP 200" 200 "$CODE"
check "batch plan -> two transactions" 2 "$(field result.transactions)"
PLAN=$(field result.plan_id)
admin admin.validator.apply "{\"plan_id\":\"$PLAN\"}"
check "batch apply -> two actions" 2 "$(count_actions "$OUT")"
check "batch apply -> backup disabled" "applied" "$(field result.results.0.status)"
check "batch apply -> cold key changed" "applied" "$(field result.results.1.status)"
H=$(wait_head $((H + 2)) 30) || { bad "chain stopped after the batch"; exit 1; }
for _ in $(seq 1 40); do
    [ -z "$(vfield backup signing_key)" ] && [ "$(vfield cold signing_key)" = "$INITIATOR_PUB" ] && break
    sleep 0.5
done
check "batch -> backup key null on chain" "" "$(vfield backup signing_key)"
check "batch -> cold key changed on chain" "$INITIATOR_PUB" "$(vfield cold signing_key)"
admin admin.validator.list '{}'
check "batch -> backup hold set" true "$(field result.validators.0.hold)"
check "batch -> cold configured key untouched" "$COMMITTEE_PUB" "$(field result.validators.1.configured_key)"
ok "chain keeps producing (head $H)"

# ── 10. contract corners ────────────────────────────────────────────────────
admin admin.validator.plan '{"actions":[{"name":"backup","action":"disable"},{"name":"backup","action":"enable"}]}'
check "duplicate names -> -32602" -32602 "$(field error.code)"
admin admin.validator.plan '{"actions":[{"name":"nope","action":"disable"}]}'
check "unknown name -> -32602" -32602 "$(field error.code)"
admin admin.validator.plan '{"actions":[{"name":"viz","action":"set_key","signing_key":"VIZinvalid"}]}'
check "bad key -> -32602" -32602 "$(field error.code)"
admin admin.validator.plan '{"actions":[{"name":"backup","action":"disable"}]}'
check "no-op -> -32602" -32602 "$(field error.code)"

# ── summary ──────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────"
echo "validator-e2e: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
