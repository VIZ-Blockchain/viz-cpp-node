#!/usr/bin/env python3
"""HF15 stale-state detector: per LMSR market, Sigma b_share over its active LP rows vs lmsr_b.

Post-audit-fix invariant (see docs/prediction-markets/pm-audit-fix-upgrade.md, section 4):

    Sigma b_share over a market's ACTIVE (status 0) LP rows == market.lmsr_b

The legacy partial-withdrawal path subtracted a floored b_remove from market.lmsr_b while leaving the
withdrawn row's own b_share untouched, so a row could end up claiming more curve than the market still
held. HF15 refuses such a withdrawal ("would drain the LMSR pricing curve") instead of clamping, because
lmsr_b <= 0 makes lmsr_q96 fail soft: every outcome prices at zero and a bet costs nothing while the
market still holds the LP capital and the bettors' stakes.

Run this against a snapshot BEFORE scheduling the fork. If no market diverges, the migration question
is moot and the fork can be scheduled as-is; a diverging market means at least one LP's early-exit
option is dead for a stale row (the principal still returns in full at settlement).

Usage:
    pm_stale_bshare_detect.py <snapshot-block-NNNN.vizjson>

Exit codes:
    0  every LMSR market satisfies the invariant (nothing to migrate)
    1  at least one market diverges (active Sigma b_share > lmsr_b) - see the printed list
    2  the snapshot could not be read or the expected sections were not found

A *.vizjson snapshot is zlib-compressed JSON ("78 01" magic, fc::compress, not gzip): {"header":...,
"state":{<sections>}}. Snapshots live in the node's snapshot directory (`snapshot-auto-latest` writes
/var/lib/vizd/snapshots/snapshot-block-*.vizjson inside the container, i.e. <vizhome>/snapshots/ on
the host); the object field names below are the raw JSON names, not the C++ ones.
"""

import json
import sys
import zlib


def load_state(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw[:2] != b"\x78\x01" and raw[:2] != b"\x78\x9c" and raw[:2] != b"\x78\xda":
        print("warning: %s does not start with a zlib header - not a .vizjson?" % path, file=sys.stderr)
    return json.loads(zlib.decompress(raw)).get("state", {})


def find_section(state, key):
    """Locate a snapshot section by a field its rows carry (section names are not part of the ABI)."""
    for name, rows in state.items():
        if isinstance(rows, list) and rows and isinstance(rows[0], dict) and key in rows[0]:
            return name, rows
    return None, None


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print("usage: %s <snapshot.vizjson>" % sys.argv[0], file=sys.stderr)
        return 2
    try:
        state = load_state(sys.argv[1])
    except Exception as exc:  # noqa: BLE001 - any read/parse failure is the same verdict
        print("cannot read snapshot: %s" % exc, file=sys.stderr)
        return 2

    mk_name, markets = find_section(state, "lmsr_b")
    lq_name, rows = find_section(state, "b_share")
    if markets is None or rows is None:
        print("cannot find the market/liquidity sections (no row carries lmsr_b / b_share)",
              file=sys.stderr)
        return 2
    print("sections: markets=%s (%d rows) liquidity=%s (%d rows)" % (mk_name, len(markets),
                                                                     lq_name, len(rows)))

    lmsr_b = {m["id"]: (m.get("lmsr_b") or 0) for m in markets if m.get("market_type") == 1}
    active, everything = {}, {}
    for row in rows:
        mid = row.get("market")
        if mid not in lmsr_b:
            continue
        share = row.get("b_share") or 0
        everything[mid] = everything.get(mid, 0) + share
        if row.get("status") == 0:
            active[mid] = active.get(mid, 0) + share

    diverging = sorted((mid, lmsr_b[mid], active.get(mid, 0)) for mid in lmsr_b
                       if active.get(mid, 0) > lmsr_b[mid])
    healthy = sum(1 for mid in lmsr_b if 0 < active.get(mid, 0) == lmsr_b[mid])
    idle = sum(1 for mid in lmsr_b if not active.get(mid, 0))

    print("LMSR markets: %d (with active LP rows: %d, without: %d)"
          % (len(lmsr_b), len(lmsr_b) - idle, idle))
    print("invariant holds (active Sigma b_share == lmsr_b): %d" % healthy)
    print("diverging (active Sigma b_share > lmsr_b): %d" % len(diverging))
    for mid, lb, act in diverging[:50]:
        print("  market %d: lmsr_b=%d active_sum=%d over_by=%d all_rows=%d"
              % (mid, lb, act, act - lb, everything.get(mid, 0)))
    if len(diverging) > 50:
        print("  ... %d more" % (len(diverging) - 50))
    return 1 if diverging else 0


if __name__ == "__main__":
    sys.exit(main())
