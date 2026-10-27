#!/usr/bin/env python3
"""Hardfork activation readiness, counted the way the chain counts it.

The chain promotes next_hardfork in update_validator_schedule (database.cpp): it walks
the SCHEDULED slots current_shuffled_validators[0 .. num_scheduled_validators), skips the
committee validator while emergency consensus is active, groups the slot owners by
(hardfork_version_vote, hardfork_time_vote) and promotes the first group with at least
CHAIN_HARDFORK_REQUIRED_VALIDATORS (17) slots. Otherwise next_hardfork falls back to the
current version.

The recount runs every CHAIN_MAX_VALIDATORS * REPEAT (21) blocks, and a promoted fork whose
time has passed applies in that same block (process_hardforks runs after the schedule update).

A validator with a null signing key is not scheduled, so its vote does not count. At the
time of writing (2026-10-26) mainnet scheduled 13 validators: 17 votes for 4.2.0 were
unreachable however many of them had upgraded. Counting "upgraded validators in the top 21
by vote" hides this; this script counts slots.

The quorum constants are the production ones from protocol/config.hpp, not read from the
node: against a testnet node (quorum 1) the cross-check honestly reports DISAGREE.

Usage:
    python3 scripts/hardfork_readiness.py [--url URL]
    python3 scripts/hardfork_readiness.py --selftest

Exit codes:
    0  a vote group has the quorum and the chain agrees
    1  no group has the quorum (the chain agrees: next_hardfork is the current version)
    2  the recount disagrees with the chain's next_hardfork, or the API answer is unusable.
       Right after a vote change this can be transient: the chain recounts every 21
       blocks; rerun after the printed recount block.
    4  --selftest failed: the check itself is broken (not a readiness verdict)
"""

import argparse
import json
import os
import re
import sys
import urllib.request

REQUIRED = 17                 # CHAIN_HARDFORK_REQUIRED_VALIDATORS, protocol/config.hpp
REPEAT = 1                    # CHAIN_BLOCK_VALIDATOR_REPEAT
MAX_VALIDATORS = 21           # CHAIN_MAX_VALIDATORS
COMMITTEE = "committee"       # CHAIN_EMERGENCY_VALIDATOR_ACCOUNT
NULL_KEY = "VIZ1111111111111111111111111111111114T1Anm"
NAME_BYTES = 32               # account_name_type is a fixed_string<32>


def vkey(v):
    return tuple(int(x) for x in v.split("."))


def decode_slots(raw, n):
    """current_shuffled_validators comes back as one hex blob of 32-byte names (or a list)."""
    if isinstance(raw, list):
        names = list(raw)
    else:
        b = bytes.fromhex(raw)
        names = [b[i:i + NAME_BYTES].rstrip(b"\0").decode() for i in range(0, len(b), NAME_BYTES)]
    return names[:n]


def tally(slots, votes, emergency):
    """slots: scheduled names; votes: name -> (version, time). Returns (groups, promoted)."""
    groups = {}
    for i in range(0, len(slots), REPEAT):
        name = slots[i]
        if emergency and name == COMMITTEE:
            continue
        groups[votes[name]] = groups.get(votes[name], 0) + 1
    # flat_map<tuple<hardfork_version, time_point_sec>> iterates ascending; first >= quorum wins.
    for g in sorted(groups, key=lambda g: (vkey(g[0]), g[1])):
        if groups[g] >= REQUIRED:
            return groups, g
    return groups, None


def call(url, api, method, params):
    req = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "call", "params": [api, method, params]})
    with urllib.request.urlopen(urllib.request.Request(url, req.encode()), timeout=20) as r:
        body = json.load(r)
    if "error" in body:
        raise RuntimeError(f"{api}.{method}: {body['error'].get('message', body['error'])}")
    return body["result"]


def check(url):
    wso = call(url, "validator_api", "get_validator_schedule", [])
    dgp = call(url, "database_api", "get_dynamic_global_properties", [])
    nxt = call(url, "database_api", "get_next_scheduled_hardfork", [])
    cur = call(url, "database_api", "get_hardfork_version", [])
    n = wso["num_scheduled_validators"]
    slots = decode_slots(wso["current_shuffled_validators"], n)
    if len(slots) != n or not all(slots):
        print(f"BLIND: decoded {len(slots)} names for {n} scheduled slots: {slots}")
        return 2
    emergency = bool(dgp.get("emergency_consensus_active"))

    info = {}
    for name in set(slots) - ({COMMITTEE} if emergency else set()):
        w = call(url, "validator_api", "get_validator_by_account", [name])
        info[name] = w
    votes = {k: (w["hardfork_version_vote"], w["hardfork_time_vote"]) for k, w in info.items()}
    groups, promoted = tally(slots, votes, emergency)

    print(f"head #{dgp['head_block_number']}, current hardfork {cur}, emergency: {'yes' if emergency else 'no'}")
    # update_validator_schedule returns early unless head % (MAX_VALIDATORS * REPEAT) == 0.
    # (wso.next_shuffle_block_num is write-only in the chain and is not this block.)
    period = MAX_VALIDATORS * REPEAT
    next_recount = (dgp["head_block_number"] // period + 1) * period
    print(f"scheduled slots: {n}, quorum {REQUIRED}, next recount at #{next_recount}")
    if n < REQUIRED:
        more = REQUIRED - n
        print(f"  !! only {n} slots: no vote can reach {REQUIRED} until "
              f"{more} more validator{'s' if more > 1 else ''} set{'' if more > 1 else 's'} a signing key")
    lead = max(groups, key=lambda g: (groups[g], vkey(g[0]))) if groups else None
    for g in sorted(groups, key=lambda g: -groups[g]):
        short = f"  short {REQUIRED - groups[g]}" if groups[g] < REQUIRED else "  QUORUM"
        print(f"  {g[0]:>7} @ {g[1]}: {groups[g]:2}/{REQUIRED}{short}")

    if lead:
        off = sorted(k for k in votes if votes[k] != lead)
        if off:
            print(f"scheduled, not voting {lead[0]} @ {lead[1]}:")
            for k in off:
                print(f"  {k}: runs {info[k]['running_version']}, votes {votes[k][0]} @ {votes[k][1]}")
        idle = [w for w in call(url, "validator_api", "get_validators_by_vote", ["", 100])
                if w["signing_key"] == NULL_KEY
                and (w["hardfork_version_vote"], w["hardfork_time_vote"]) == lead]
        if idle:
            # get_validators_by_vote stops at the first validator with zero votes, so a null-key
            # validator with no votes is missing here even though it can hold a support slot.
            print(f"vote {lead[0]} but unscheduled (null signing key, votes > 0 only; "
                  f"a validator_update brings them back):")
            print("  " + ", ".join(w["owner"] for w in idle))

    chain = (nxt["hf_version"], nxt["live_time"])
    if promoted:
        agree = chain == promoted
    else:
        agree = nxt["hf_version"] == cur
    print(f"chain next_hardfork: {chain[0]} @ {chain[1]}; recount: "
          f"{'%s @ %s' % promoted if promoted else 'none (stays ' + cur + ')'} -> "
          f"{'agree' if agree else 'DISAGREE'}")
    if not agree:
        return 2
    return 0 if promoted else 1


def selftest():
    hf16 = ("4.2.0", "2026-10-31T00:00:00")
    hf15 = ("4.1.0", "2026-10-05T00:00:00")
    names = [f"v{i}" for i in range(21)]
    fails = []

    def arm(label, got, want):
        ok = got == want
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")
        if not ok:
            fails.append(f"{label}: got {got!r}, want {want!r}")

    votes = {**{k: hf16 for k in names[:16]}, **{k: hf15 for k in names[16:]}}
    arm("16 of 21 -> no promotion", tally(names, votes, False)[1], None)
    votes[names[16]] = hf16
    arm("17 of 21 -> promoted", tally(names, votes, False)[1], hf16)
    arm("13 slots all voting -> unreachable", tally(names[:13], {k: hf16 for k in names[:13]}, False)[1], None)
    # Emergency: committee holds several slots; those slots must not count (or crash: it has no vote here).
    slots = names[:17] + [COMMITTEE] * 4
    arm("emergency skips committee slots", tally(slots, {k: hf16 for k in names[:17]}, True), ({hf16: 17}, hf16))
    # A name scheduled twice counts per slot, as the chain loop does.
    arm("duplicate slot counts twice", tally(["a", "a"], {"a": hf16}, False)[0], {hf16: 2})
    # Live encoding sample (mainnet, 2026-10-26): 32-byte zero-padded names, unused tail empty.
    blob = "64656e69732d736b7269706e696b" + "00" * 18 + "727573696e6975" + "00" * 25 + "00" * 32
    arm("decode hex schedule", decode_slots(blob, 2), ["denis-skripnik", "rusiniu"])
    arm("decode list schedule", decode_slots(["a", "b", ""], 2), ["a", "b"])
    # The constants above are copies; fail if the production config moves.
    cfg_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "../libraries/protocol/include/graphene/protocol/config.hpp")
    if not os.path.exists(cfg_path):
        print("  skip constants vs config.hpp (script is outside the repo tree)")
    else:
        cfg = open(cfg_path).read()

        def define(macro):
            m = re.search(rf"#define\s+{macro}\s+(\d+)", cfg)
            return int(m.group(1)) if m else None

        arm("CHAIN_HARDFORK_REQUIRED_VALIDATORS matches config.hpp",
            define("CHAIN_HARDFORK_REQUIRED_VALIDATORS"), REQUIRED)
        arm("CHAIN_BLOCK_VALIDATOR_REPEAT matches config.hpp", define("CHAIN_BLOCK_VALIDATOR_REPEAT"), REPEAT)
        top, sup = define("CHAIN_MAX_TOP_VALIDATORS"), define("CHAIN_MAX_SUPPORT_VALIDATORS")
        arm("CHAIN_MAX_VALIDATORS matches config.hpp", top + sup if top and sup else None, MAX_VALIDATORS)

    for f in fails:
        print(f, file=sys.stderr)
    return 4 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", default="https://api.viz.world")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    try:
        return check(args.url)
    except (OSError, RuntimeError, KeyError, ValueError) as e:
        print(f"BLIND: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
