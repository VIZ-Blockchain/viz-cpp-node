# DLT P2P Network Redesign — Plan vs. Implementation Review

Review of [implementation status](dlt-p2p-network-redesign.md) against the [design plan](../plans/dlt-p2p-network-redesign_91a7ca29.md).

## Summary

The implementation closely follows the plan across all 5 phases. Core architecture, message types, connection management, sync logic, mempool, fork resolution, anti-spam, and in-place replacement are correctly implemented. Snapshot download on empty state is verified as preserved (lives in chain + snapshot plugins, untouched by P2P redesign). **6 unreported gaps** were found beyond the 6 already-documented known limitations.

---

## Items Fully Matching the Plan (29 items)

| Area | Plan Requirement | Status |
|------|-----------------|--------|
| Message types | 14 types (5100-5113), all structs with exact fields | ✅ Match |
| Delegate pattern | `dlt_p2p_delegate` bridging network→chain | ✅ Match |
| Fiber architecture | Accept loop, read loop, periodic task on `fc::thread` | ✅ Match |
| Wire format | 8-byte header (size+type) + raw data, no STCP | ✅ Match |
| Per-peer state | All fields: lifecycle, chain state, spam, exchange, reconnect | ✅ Match |
| Peer lifecycle | 6 states: connecting(5s)→handshaking(10s)→syncing→active→disconnected→banned | ✅ Match |
| Reconnection | Backoff 30s→…→3600s, ±25% jitter, reset on stable>5min | ✅ Match |
| 8h peer removal | Permanently remove after `dlt-peer-max-disconnect-hours` of non-response | ✅ Match |
| Node status | SYNC / FORWARD, transitions at catchup completion | ✅ Match |
| Hello handshake | `protocol_version` check, fork alignment via `is_block_known()` | ✅ Match |
| Block sync | Bulk range (200 blocks), single block, `not_available` | ✅ Match |
| P2P mempool | Separate index: dedup, expiry check, TaPoS check, size limits | ✅ Match |
| Mempool eviction | Oldest-expiry first when caps hit | ✅ Match |
| Provisional entries | Tagged during SYNC, revalidated on SYNC→FORWARD | ✅ Match |
| Fork threshold | 42 blocks (= `CHAIN_MAX_WITNESSES * 2`) | ✅ Match |
| Fork hysteresis | `CONFIRMATION_BLOCKS = 6`, tracked via `dlt_fork_resolution_state` | ✅ Match |
| Anti-spam | Single `spam_strikes` counter, reset on good, threshold=10, ban=3600s | ✅ Match |
| Spam reset-on-good | Valid block/transaction/hello → reset counter to 0 | ✅ Match |
| Peer exchange rate-limit | 10-min cooldown per peer | ✅ Match |
| Peer exchange subnet diversity | /24 subnet, max 2 per subnet | ✅ Match |
| Peer exchange min uptime | 600s before sharing | ✅ Match |
| Peer exchange cap | Max 10 peers per reply | ✅ Match |
| Color logging | GREEN/WHITE/RED/DGRAY/ORANGE | ✅ Match |
| Sync stagnation | 30s no-block, 3 retries, then FORWARD with warning | ✅ Match |
| Plugin replacement | Same `"p2p"` name, same port, same public API | ✅ Match |
| Old files removed | 12 files (node.cpp, peer_connection.cpp, stcp_socket.cpp, etc.) deleted from build | ✅ Match |
| Config | 9 new DLT options added, 4 old options removed | ✅ Match |
| Plugin startup | Deadlock fixed — `.async([setup]).wait()` instead of infinite loop block | ✅ Match |
| Snapshot download on empty state | Chain plugin detects `head_block_num == 0`, snapshot plugin downloads via raw TCP, P2P starts after import. Entirely in chain + snapshot plugins — untouched by P2P redesign. `trigger_resync()` bridge preserved. | ✅ Verified |

---

## Known Gaps (Documented in implementation status)

These are acknowledged in `dlt-p2p-network-redesign.md` § "Known Limitations / Future Work":

| # | Gap | Severity |
|---|-----|----------|
| 1 | `periodic_dlt_prune_check()` is a no-op | P2 |
| 2 | `dlt_delegate::has_emergency_private_key()` returns `false` | P2 |
| 3 | `dlt_delegate::switch_to_fork()` simplified — only pops one block | P1 |
| 4 | `dlt_p2p_node::compute_branch_info()` returns `total_vote_weight=0` | P1 |
| 5 | No unit tests | P2 |
| 6 | Build not verified | P2 |

---

## Unreported Gaps (Not in docs — Discrepancies with Plan)

### GAP 1: `expected_next_block` tracking never used (P1 Security)

**Plan**: P1 security hardening (§3.7) — Per-peer `expected_next_block` tracking to reject blocks that skip too far ahead (hole-creation attack prevention).

**What exists**: The field `expected_next_block` is declared in `dlt_peer_state` ([dlt_p2p_peer_state.hpp:53](file:///d:/Work/viz-cpp-node/libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp#L53)) but **never set, updated, or validated anywhere** in `dlt_p2p_node.cpp`.

**Impact**: No protection against peers sending blocks out of order or creating gaps.

**Fix**: In `on_dlt_block_range_reply()` and `on_dlt_block_reply()`, after applying blocks, set `state.expected_next_block = last_applied_block_num + 1`. Before applying new blocks, validate that `first_block.block_num() == state.expected_next_block` (or 0 if unset). Reject with `record_packet_result(peer, false)` on mismatch.

---

### GAP 2: `pending_block_batch` timeout never activated (P1 Security)

**Plan**: P1 security hardening (§3.7) — Blocks received but not yet validated: track with `pending_block_batch` timeout (30s) — if validation doesn't complete, soft-ban the peer.

**What exists**: The field `pending_block_batch_time` and helper `has_pending_batch_timeout()` are declared in `dlt_peer_state` ([dlt_p2p_peer_state.hpp:54-55](file:///d:/Work/viz-cpp-node/libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp#L54-L55)) but **never set** when blocks are received (e.g., in `on_dlt_block_range_reply` line 615), and **never checked** in `periodic_task()` (line 1271).

**Impact**: A slow/malicious peer can send blocks that stall without consequence.

**Fix**: In `on_dlt_block_range_reply()`, before `_delegate->accept_block()` loop, set `state.pending_block_batch_time = fc::time_point::now()`. Add a `block_validation_timeout()` method. In `periodic_task()`, call `block_validation_timeout()` to check all peers with `has_pending_batch_timeout()` → soft-ban on timeout.

---

### GAP 3: `block_validation_timeout()` handler not implemented (P1 Security)

**Plan**: Phase 2 (§4) — `block_validation_timeout()` — if `pending_block_batch` not validated within 30s, soft-ban the peer that sent it.

**What exists**: Not implemented at all. No such function in `dlt_p2p_node.cpp`, not declared in the header.

**Impact**: Combined with GAP 2, block validation timeout enforcement is completely absent.

**Fix**: Add method declaration to `dlt_p2p_node.hpp` and implementation to `dlt_p2p_node.cpp`. Call from `periodic_task()`.

---

### GAP 4: Fork resolution resets 42-block window on non-confirmation (P1 Fork)

**Plan**: P1 hysteresis (§3.7) — After the 42-block window, compute the winner. Winner must maintain lead for 6 consecutive blocks. If lead flips, reset the **confirmation counter** — not the 42-block detection window.

**What exists**: In `track_fork_state()` ([dlt_p2p_node.cpp:1116-1120](file:///d:/Work/viz-cpp-node/libraries/network/dlt_p2p_node.cpp#L1116-L1120)):

```cpp
if (_fork_detected &&
    block.block_num() - _fork_detection_block_num >= FORK_RESOLUTION_BLOCK_THRESHOLD) {
    resolve_fork();
    _fork_detected = false;  // ← resets the 42-block window too!
}
```

When `resolve_fork()` returns early (hysteresis not met), `_fork_detected = false` causes a **fresh 42-block countdown** from the next block. The plan intended continuous retry without resetting the detection window.

The plan's pseudocode does NOT set `_fork_detected = false` inside `track_fork_state()` after calling `resolve_fork()`. The confirmation counter alone should gate resolution, not a fresh detection window.

**Impact**: Fork resolution can be delayed by 42 extra blocks per failed confirmation attempt. In the worst case (two forks with rapidly oscillating vote weight), resolution may never complete.

**Fix**: Move `_fork_detected = false` into `resolve_fork()` — only clear it when resolution actually completes (i.e., `is_confirmed()` returns true and the switch is executed). Keep the confirmation counter reset logic unchanged.

---

### GAP 5: Spam strikes not incremented for all mempool rejections (P0 Anti-Spam)

**Plan**: P0 DoS protection (§3.7) — "All rejections increment sender's `spam_strikes`".

**What exists**: In `add_to_mempool()` ([dlt_p2p_node.cpp:960-1012](file:///d:/Work/viz-cpp-node/libraries/network/dlt_p2p_node.cpp#L960-L1012)):

| Rejection reason | Line | `record_packet_result(sender, false)` |
|-----------------|------|---------------------------------------|
| Already in mempool (dedup) | 964 | Not called (acceptable — duplicates are not malicious) |
| Expired (`expiration < now`) | 967 | ❌ Not called |
| Expiration headroom exceeded | 972 | ✅ Called |
| Size exceeded | 979 | ✅ Called |
| TaPoS invalid (wrong fork) | 984 | ❌ Not called |

**Impact**: Peers can spam expired or wrong-fork transactions without accumulating strikes.

**Fix**: Add `record_packet_result(sender, false)` calls at lines 967 and 984 when `from_peer && sender != INVALID_PEER_ID`.

---

### GAP 6: Fork resolution winner always picks first branch (P1 Fork)

**Plan**: §2.2 — Fork resolution uses `compare_fork_branches()` (database.cpp line 1359-1417) which does vote-weighted comparison with +10% longer-chain bonus.

**What exists**: `compute_branch_info()` ([dlt_p2p_node.cpp:1172-1181](file:///d:/Work/viz-cpp-node/libraries/network/dlt_p2p_node.cpp#L1172-L1181)) returns `total_vote_weight = 0` and `block_count = 1` for every branch. In `resolve_fork()`, the comparison `info.total_vote_weight > winner.total_vote_weight` always compares 0 > 0, so the **first branch in the `tips` vector always wins**.

The delegate already provides `compare_fork_branches(a, b)` which returns the correct vote-weighted comparison — but `resolve_fork()` calls `compute_branch_info()` instead.

**Impact**: Fork resolution is non-functional — it picks the first branch arbitrarily, not the vote-weighted winner. This directly contradicts the plan's fork resolution design.

**Fix**: Replace `compute_branch_info()` iterations in `resolve_fork()` with calls to `_delegate->compare_fork_branches()`:

```cpp
void dlt_p2p_node::resolve_fork() {
    auto tips = _delegate->get_fork_branch_tips();
    if (tips.size() < 2) { _fork_status = DLT_FORK_STATUS_NORMAL; return; }

    block_id_type winner = tips[0];
    for (size_t i = 1; i < tips.size(); ++i) {
        if (_delegate->compare_fork_branches(tips[i], winner) > 0) {
            winner = tips[i];
        }
    }
    // ... hysteresis and switch logic using 'winner'
}
```

---

## Minor Observations (Non-Blocking)

| # | Observation |
|---|-------------|
| 1 | `dlt_range_request` / `dlt_range_reply` messages (5102/5103) are implemented but the sync flow uses bulk `get_block_range` directly after hello — the plan's improvement note says "range query step could be skipped". These messages exist but are not on the main code path. |
| 2 | `broadcast_block_post_validation()` sends a `dlt_fork_status_message` instead of a dedicated post-validation type — functional but semantically imprecise. |
| 3 | `dlt_delegate::accept_block()` (p2p_plugin.cpp:139-151) ignores the `sync_mode` parameter — always calls `push_block()` the same way, discards the return value with `return false`. |
| 4 | `dlt_delegate::is_head_on_branch()` (p2p_plugin.cpp:203-206) does a simple equality check against `head_block_id()` — will miss the case where our head IS on the branch but not at its tip. |
| 5 | `resync_from_lib()` in `dlt_delegate` (p2p_plugin.cpp:215-217) is empty — documented as "handled at plugin level", but the plugin-level `resync_from_lib()` (p2p_plugin.cpp:437-441) simply calls `node->resync_from_lib()` which just calls `transition_to_sync()` + re-requests blocks. No actual LIB-level resync logic. |

---

## Severity Summary

| Priority | Gaps |
|----------|------|
| **P0 (must fix)** | GAP 6 — Fork resolution non-functional (picks wrong branch) |
| **P1 (should fix)** | GAP 4 — Fork window reset; GAP 1 — missing block ordering validation; GAP 2/3 — missing block validation timeout |
| **P2 (nice to fix)** | GAP 5 — incomplete spam strikes; Known gaps 1-6 (already documented) |
