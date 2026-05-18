# DLT P2P Node — Code Audit vs. Documentation (2026-05-08)

Audit of the `fix-validator` branch against the problem/fix log in
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
| P28/P29/P30 | Build errors in p2p_plugin.cpp / multimap::erase / validator header | **DONE** |

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
| **P49** | Range request starts at `our_head + 1`, skipping our own head block. If two validators signed competing blocks at the same height, we'd never detect the divergence. Fixed: start at `our_head`. (`dlt_p2p_node.cpp:982-986`) |

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

---

### BUG-E: Spam strike on fork_db-only range response causes false soft-ban of master peer

**File:** [dlt_p2p_node.cpp:1288](libraries/network/dlt_p2p_node.cpp#L1288)

```cpp
record_packet_result(peer, any_block_applied);   // BUG: penalises legit fork_db batches
```

During a large-gap sync (`head=79740486`, `peer_head=79746356`, gap=5854), the node syncs
from LIB using `request_blocks_from_peer`. The peer is on the majority fork; the node's
head diverged. Every range response has `any_block_applied=false` (all blocks go to
`fork_db` as competing-fork candidates). After 10 such responses `spam_strikes` reaches
`SPAM_STRIKE_THRESHOLD` → soft-ban for 3600s.

The same `on_dlt_block_range_r` handler explicitly continues fetching for `fork_db`-only
batches (line 1304-1310, "competing fork? — continue fetch") while simultaneously
punishing the peer for sending them. Self-contradictory.

**Observed log:**
```
Soft-banning peer 185.146.232.170:2001 for 3600s (reason: spam strike threshold exceeded)
```

**Severity:** HIGH — the node's only sync peer gets banned mid-sync; gap never fills.

---

### BUG-F: DLT snapshot node cannot accept competing fork starting at snapshot LIB block

**Files:** [database.cpp:355-376](libraries/chain/database.cpp#L355), [database.cpp:1518-1522](libraries/chain/database.cpp#L1518), [fork_database.cpp](libraries/chain/fork_database.cpp)

After importing a DLT snapshot at block N (e.g. 79740482):
1. DLT block log starts at N+1; block N is **not stored** anywhere `fetch_block_by_id`
   can reach (no main block_log in DLT mode; DLT log starts at N+1; fork_db seeded
   with head-only via `start_block`).
2. Fork_db seeded top-down: only the head block (N+4) is inserted via `start_block`
   with `prev=null`; blocks N+1…N+3 are absent from fork_db entirely.
3. Master peer is on the majority fork diverging at block N+1.
4. Master sends sync range starting at N: block N arrives → `ALREADY_KNOWN`
   (same ID) → silently discarded. Block N+1_master (parent=N.id) arrives:
   - `is_known_block(N.id)` → **false** (N not in fork_db)
   - `fetch_block_by_id(N.id)` → **null** (not in any log)
   - → DEAD_FORK. Rejected forever.
5. Fork switch never triggers; node stays stuck at N+4 while master advances.

**Observed log:**
```
Rejecting block 79740483 from a different fork: parent not in fork_db and not on main chain (head=79740486)
Range stored in fork_db only (competing fork?), continuing fetch from #79740682
```
*(continues indefinitely, head never advances)*

**Root causes (two independent failures):**
- DLT seeding builds no `prev` chain; `fetch_branch_from` would crash walking the slave
  branch past the null `prev` on the `start_block` root.
- Snapshot block N is unreachable via `fetch_block_by_id`, so `_push_block` cannot seed
  fork_db with it when N+1_master arrives.

**Severity:** CRITICAL — node with a diverged head after snapshot is permanently stuck;
gap never fills regardless of how many peers are connected.

---

## Summary

```
BUG-A  [HIGH]     range_fallback_mode transition_to_forward() not guarded → FIXED (2026-05-08)
BUG-B  [LOW]      Empty peer gets exchange_enabled=true → broadcast waste   (accepted as-is)
BUG-C  [LOW]      check_fork_alignment range-overlap silent fail at window boundary (accepted as-is)
BUG-D  [MEDIUM]   check_sync_catchup ignores peer_head_num==0 peers → FIXED (2026-05-08)
BUG-E  [HIGH]     fork_db-only range response counted as spam → soft-ban    → FIXED (2026-05-08)
BUG-F  [CRITICAL] DLT snapshot: competing fork starting at LIB permanently rejected → FIXED (2026-05-08)

DOCS   [INFO]   P32-P49 implemented in code but not documented in dlt-4-node-sync-scenarios.md
```

### Fixes Applied (2026-05-08)

**BUG-A** — [dlt_p2p_node.cpp:1523-1527](libraries/network/dlt_p2p_node.cpp#L1523-L1527)

Replaced `transition_to_forward()` with `check_sync_catchup()` in the `range_fallback_mode` path. `check_sync_catchup()` already verifies that our head ≥ all known-head peers before transitioning, so it correctly handles the case where all single-block replies were ALREADY_KNOWN/FORK_DB_ONLY with no real progress.

**BUG-D** — [dlt_p2p_node.cpp:2420-2462](libraries/network/dlt_p2p_node.cpp#L2420-L2462)

Added `known_head_peers` counter alongside `active_peer_count`. Empty peers (`peer_head_num==0`) still count toward `active_peer_count` (so isolation detection works) but do not increment `known_head_peers`. The final transition guard now requires `known_head_peers > 0 && !has_peer_ahead`: a node surrounded only by empty peers will never claim "caught up" and stays in SYNC until the stagnation / snapshot-plugin recovery path fires.

**BUG-E** — [dlt_p2p_node.cpp:1290](libraries/network/dlt_p2p_node.cpp#L1290)

Changed `record_packet_result(peer, any_block_applied)` to
`record_packet_result(peer, any_block_applied || any_fork_db_only)`.

A peer that sends valid blocks landing in fork_db (normal during competing-fork or
LIB-based sync) provides useful data and must not be penalised. The existing continuation
path at line 1304 already recognises this ("competing fork — keep fetching"), making the
spam penalty a direct contradiction. With the fix, spam strikes only accumulate when the
peer sends batches that produce neither applied blocks nor fork_db entries (true spam or
dead-fork responses).

**BUG-F** — [database.cpp](libraries/chain/database.cpp), [fork_database.cpp](libraries/chain/fork_database.cpp), [fork_database.hpp](libraries/chain/include/graphene/chain/fork_database.hpp)

Three coordinated changes:

1. **DLT mode startup seeding** ([database.cpp:~355](libraries/chain/database.cpp#L355)):
   Replaced single-block `start_block(head)` with bottom-up seeding. Scans the DLT
   block log for the oldest block within a 100-block window, uses `start_block` on it,
   then pushes each successive block in order up to the head. Result: the slave's recent
   chain (e.g. N+1…N+4) is in fork_db with correct `prev` pointers so
   `fetch_branch_from` can walk the slave branch during fork switch.

2. **ALREADY_KNOWN path** ([database.cpp:1518](libraries/chain/database.cpp#L1518)):
   When block N arrives as ALREADY_KNOWN and is not yet in fork_db, call
   `_fork_db.insert_as_base(new_block)`. The peer sent us the full block data — this
   is the only opportunity to seed fork_db with the snapshot LIB block whose data is
   absent from every log file.

3. **`fork_database::insert_as_base` + `_repair_child_prev_links`**
   ([fork_database.cpp](libraries/chain/fork_database.cpp)):
   `insert_as_base(b)` inserts block `b` into `_index` without requiring its parent to
   be present (it is a known-chain anchor). Calls `_push_next` to link any blocks in
   `_unlinked_index` waiting for this parent, then calls `_repair_child_prev_links` which
   finds child blocks already in `_index` (inserted via `start_block` with null `prev`)
   and sets their `prev` pointer. This reconnects the slave chain so
   `fetch_branch_from` can traverse it: both the slave branch and the master's
   competing branch now walk back to block N as their common ancestor.
