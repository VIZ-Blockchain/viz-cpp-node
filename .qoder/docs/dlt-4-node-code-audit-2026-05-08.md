# DLT P2P Node — Code Audit vs. Documentation (2026-05-08)

Audit of the `fix-witness` branch against the problem/fix log in
`dlt-4-node-sync-scenarios.md`. Checks which fixes are actually in the code,
identifies logic errors not yet documented, and lists undocumented problems
found via code comments.

---

## Status of Documented Fixes

### Fixes (2026-05-05) — alignment & handshake

| # | Description | Status |
|---|-------------|--------|
| P1/P2/P6/P7/P8/P13/P15 | `check_fork_alignment` extended with boundary link + range overlap | **DONE** — `dlt_p2p_node.cpp:788-828` |
| P9 | Empty peer (`head_block_num==0`) returns `true` immediately | **DONE** — line 796-798 |
| P3/P14 | `on_dlt_hello` lifecycle: `hello.node_status == DLT_NODE_STATUS_SYNC` added | **DONE** — line ~867 |
| P4 | Block range start clamped to `peer_dlt_earliest` | **DONE** — line 1049 |

### Fixes (2026-05-06) — stability & crash protection

| # | Description | Status |
|---|-------------|--------|
| P20/P21 | `DEAD_FORK` enum + `accept_block` returns DEAD_FORK for unlinkable at/below head | **DONE** — `p2p_plugin.cpp:240-283` |
| P20/P21 | `on_dlt_block_range_reply` soft-bans on DEAD_FORK | **DONE** — line 1263-1271 |
| P20/P21 | `on_dlt_block_reply` soft-bans on DEAD_FORK | **DONE** — line 1456-1462 |
| P20/P21 | `transition_to_forward()` in range reply guarded by `any_block_applied` | **DONE** — line 1297 |
| P17 | `dlt_block_log::is_consistent_with()` + corruption auto-reset in `database::open()` | **DONE** |
| P22 | Grace period: near-head blocks → FORK_DB_ONLY not DEAD_FORK for first 60s | **DONE** — `p2p_plugin.cpp:248-277` |
| P24 | `_block_processing_paused` early-return in periodic task | **DONE** — line 598 |
| P24 | Per-block pause check inside range processing loop | **DONE** — line 1195 |
| P27 | Write lock diagnostic logging in `notify_applied_block` | **DONE** |
| P19 | Gap detection: log warning when our head < peer_earliest | **DONE** — line 1012-1014 |
| P25 | `exchange_enabled` re-evaluated when peer block accepted | **DONE** — line 1247-1249 |
| P26 | `check_sync_catchup()` called on single-block accept | **DONE** — line 1495 |
| P31 | `constexpr uint32_t dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC;` defined | **DONE** — line 25 |
| P28/P29/P30 | Build errors in p2p_plugin.cpp / multimap::erase / witness header | **DONE** |

---

## Logic Errors Found in Current Code

### BUG-A: `range_fallback_mode` transition to FORWARD without `any_block_applied` guard

**File:** [dlt_p2p_node.cpp:1513-1527](libraries/network/dlt_p2p_node.cpp#L1513-L1527)

```cpp
// on_dlt_block_reply — fallback path
if (state.range_fallback_mode && _node_status == DLT_NODE_STATUS_SYNC) {
    if (reply.next_available > 0) {
        // request next block
    } else if (reply.is_last) {
        state.range_fallback_mode = false;
        transition_to_forward();   // ← UNGUARDED
    }
}
```

**Problem:** When range deserialization fails, the node falls back to requesting blocks
one-at-a-time. If the peer's replies are all `ALREADY_KNOWN` or `FORK_DB_ONLY` (no
block actually applied to the chain) and the peer sends `is_last=true`, the node calls
`transition_to_forward()` without having made any real progress. The node can enter
FORWARD mode while still behind the network.

**Contrast:** The range reply handler at line 1297 has the correct guard:
```cpp
if (any_block_applied) {
    if (reply.is_last) { transition_to_forward(); }
}
```

**Fix:** Track `any_block_applied` in `on_dlt_block_reply` and gate the fallback
`transition_to_forward()` on it, OR replace the call with `check_sync_catchup()` which
already verifies all active peers' heads before transitioning.

**Severity:** HIGH — node can enter FORWARD without catching up, misses blocks, never
re-enters SYNC because it thinks it's caught up.

---

### BUG-B: `check_fork_alignment` returns `true` for empty peer but `exchange_enabled` becomes `true`

**File:** [dlt_p2p_node.cpp:792-798](libraries/network/dlt_p2p_node.cpp#L792-L798)

```cpp
if (hello.head_block_num == 0) {
    return true;  // recognized_head_out stays zero_id
}
```

For empty peers `fork_alignment=true` → `exchange_enabled=true`. This is intentional
(they stay connected). However, in `send_to_all_our_fork_peers`, block broadcasts are
sent to all `exchange_enabled` peers. An empty peer (slaveC) will receive forward blocks
it cannot process (its fork_db is empty). This wastes bandwidth and could fill its
fork_db with unlinked blocks if it has no snapshot yet.

**Current mitigation:** `request_blocks_from_peer` checks `peer_latest == 0` and
doesn't send range requests. But broadcast blocks still arrive.

**Severity:** LOW — bandwidth waste; not a correctness issue since blocks that can't
link go to `_unlinked_index` and are eventually pruned.

---

### BUG-C: `check_fork_alignment` range-overlap branch can silently fail for in-range peers

**File:** [dlt_p2p_node.cpp:803-808](libraries/network/dlt_p2p_node.cpp#L803-L808)

```cpp
if (hello.head_block_num >= our_earliest && hello.head_block_num <= our_latest) {
    if (_delegate->is_block_known(hello.head_block_id)) {
        recognized_head_out = hello.head_block_id;
    }
    // ← no else: if is_block_known returns false, we silently skip
}
```

If a peer's head is numerically within our DLT range but `is_block_known` returns
`false` (e.g., it's a competing fork tip within our range), `recognized_head_out` stays
zero. The code then falls to the LIB check. If LIB also fails, `fork_alignment=false`.
This is the **correct** behavior for a hostile fork peer.

However: in DLT mode, blocks at the boundary of the rolling window can be partially
pruned from the chain index. A legitimate peer at e.g. `our_earliest + 2` may have its
block ID not in `is_block_known` if the index was trimmed. This is a narrow window but
can cause a false `fork_alignment=false` for a valid peer.

**Severity:** LOW — rare; only during the first few blocks of the DLT window. The
boundary link check and LIB fallback provide redundancy.

---

### BUG-D: `transition_to_forward()` called from `check_sync_catchup()` without checking peers with `peer_head_num == 0`

**File:** [dlt_p2p_node.cpp:2406-2445](libraries/network/dlt_p2p_node.cpp#L2406-L2445)

```cpp
// check_sync_catchup:
for (const auto& _peer_item : _peer_states) {
    ...
    if (state.peer_head_num == 0) continue;  // skip peers with no head info
    if (our_head < state.peer_head_num) {
        has_peer_ahead = true;
        break;
    }
}
// If no peer is ahead → transition to FORWARD
```

Peers are skipped if `peer_head_num == 0`. Empty peers (slaveC) and peers whose hello
didn't include a head num will be skipped. If slaveC is the ONLY connected peer and our
head is 1500, `has_peer_ahead = false` → `transition_to_forward()`. The node enters
FORWARD mode "connected" to an empty peer, with no real connectivity to the network.

This can happen during initial startup or after all block-bearing peers disconnect while
only an empty peer remains.

**Severity:** MEDIUM — leads to an oscillation: FORWARD on an empty peer → no blocks
arrive → `check_forward_stagnation()` falls back to SYNC → requests from empty peer →
no reply → stagnation again.

---

## Undocumented Problems Fixed in Code (P32+)

The code comments reference problems not yet in `dlt-4-node-sync-scenarios.md`.

| Code tag | Description inferred from comments |
|----------|------------------------------------|
| **P36** | Out-of-order single block received in FORWARD mode triggers unnecessary SYNC→FORWARD oscillation. Fixed: gap fill requested instead of mode switch. (`dlt_p2p_node.cpp:1378-1383`) |
| **P37** | `peer_head_num` goes stale after hello — not updated when the peer sends us blocks. Fixed: `peer_head_num` updated from received block number in both range and single-block handlers. (`lines 1183-1185, 1339-1341`) |
| **P39** | `fork_db._head` jumps ahead of database head via `_push_next` cascade when a parent block arrives. The block being pushed can then appear "too old" (block_too_old_exception). Fixed in `_push_block`. (`p2p_plugin.cpp:185-192`) |
| **P40** | Iterator invalidation in `periodic_lifecycle_timeout_check()` when `handle_disconnect` erases entries from `_peer_states` during iteration. Fixed: collect timed-out peers into a vector first. (`dlt_p2p_node.cpp:417-418`) |
| **P42** | `request_blocks_from_peer` uses stale `peer_dlt_latest` as the peer's chain tip. Fixed: `peer_latest = max(peer_dlt_latest, peer_head_num)` to use whichever is fresher. (`dlt_p2p_node.cpp:956-960`) |
| **P49** | Range request starts at `our_head + 1`, skipping our own head block. If two witnesses signed competing blocks at the same height, we'd never detect the divergence. Fixed: start at `our_head`. (`dlt_p2p_node.cpp:982-986`) |

---

## Documentation Gaps

The `dlt-4-node-sync-scenarios.md` file ends at P31 but the codebase implements fixes
through at least P49. The following sections are absent from the documentation:

1. **P32-P35** — not referenced in any code comment; may be internal tracking numbers
   or obsolete entries.
2. **P36-P40** — out-of-order oscillation, stale peer head, fork_db cascade, iterator
   invalidation — all fixed in code but not in docs.
3. **P42, P49** — stale peer_dlt_latest, sync start offset — fixed in code, not in docs.

**Recommendation:** Update `dlt-4-node-sync-scenarios.md` with a "Post-P31 Fixes"
section covering P36-P49.

---

## Summary

```
BUG-A  [HIGH]   range_fallback_mode transition_to_forward() not guarded by any_block_applied  → FIXED
BUG-B  [LOW]    Empty peer gets exchange_enabled=true → receives broadcast blocks it can't use  (accepted as-is)
BUG-C  [LOW]    Range-overlap in check_fork_alignment can silently fail at rolling window boundary  (accepted as-is)
BUG-D  [MEDIUM] check_sync_catchup ignores peers with peer_head_num==0 → false "caught up"   → FIXED

DOCS   [INFO]   P32-P49 implemented in code but not documented in dlt-4-node-sync-scenarios.md
```

### Fixes Applied (2026-05-08)

**BUG-A** — [dlt_p2p_node.cpp:1523-1527](libraries/network/dlt_p2p_node.cpp#L1523-L1527)

Replaced `transition_to_forward()` with `check_sync_catchup()` in the `range_fallback_mode` path. `check_sync_catchup()` already verifies that our head ≥ all known-head peers before transitioning, so it correctly handles the case where all single-block replies were ALREADY_KNOWN/FORK_DB_ONLY with no real progress.

**BUG-D** — [dlt_p2p_node.cpp:2420-2462](libraries/network/dlt_p2p_node.cpp#L2420-L2462)

Added `known_head_peers` counter alongside `active_peer_count`. Empty peers (`peer_head_num==0`) still count toward `active_peer_count` (so isolation detection works) but do not increment `known_head_peers`. The final transition guard now requires `known_head_peers > 0 && !has_peer_ahead`: a node surrounded only by empty peers will never claim "caught up" and stays in SYNC until the stagnation / snapshot-plugin recovery path fires.
