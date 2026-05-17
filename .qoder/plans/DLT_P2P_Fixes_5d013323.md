# DLT P2P Fixes — Implementation Plan

Fixes all issues found during review verification of `.qoder/docs/dlt-p2p-network-redesign-review.md`.

---

## Task 1: Compile Error — `peer_dlt_latest_block` field name mismatch (P0)

**File**: `libraries/network/dlt_p2p_node.cpp`

The code references `peer_dlt_latest_block` but the field in `dlt_peer_state` is named `peer_dlt_latest`.

**Changes**:
- Line 523: `it->second.peer_dlt_latest_block` → `it->second.peer_dlt_latest`
- Line 531: `s.peer_dlt_latest_block` → `s.peer_dlt_latest`

---

## Task 2: Functional Bug — `broadcast_block_post_validation()` corrupts peer_head_num (P0)

**File**: `libraries/network/dlt_p2p_node.cpp`

`broadcast_block_post_validation()` at line 805 sets `msg.head_block_num = 0` with comment "filled by receiver from block_id". But `on_dlt_fork_status()` at line 699 stores it as `peer_head_num = 0`, corrupting peer state.

**Fix**: Extract block number from the `block_id` using `block_header::num_from_id()`:
```cpp
// Line 804-805: replace
msg.head_block_id = block_id;
msg.head_block_num = 0; // filled by receiver from block_id
// With:
msg.head_block_id = block_id;
msg.head_block_num = block_header::num_from_id(block_id);
```

Also add the necessary include if not present: `#include <graphene/protocol/block_header.hpp>` (likely already available through `graphene/protocol/block.hpp`).

---

## Task 3: Fork Resolution Non-Functional — always picks first branch (P0)

**File**: `libraries/network/dlt_p2p_node.cpp`

`compute_branch_info()` returns `total_vote_weight = 0` for every branch. The comparison `0 > 0` is always false, so the first branch in `tips` always wins. The delegate already provides `compare_fork_branches()` which does the correct vote-weighted comparison with +10% longer-chain bonus.

**Fix**: Replace `compute_branch_info()` iterations in `resolve_fork()` with `compare_fork_branches()`:

Replace lines 1130-1139:
```cpp
// Find the heaviest branch
dlt_fork_branch_info winner;
bool first = true;
for (const auto& tip : tips) {
    auto info = compute_branch_info(tip);
    if (first || info.total_vote_weight > winner.total_vote_weight) {
        winner = info;
        first = false;
    }
}
```

With:
```cpp
// Find the heaviest branch using vote-weighted comparison
block_id_type winner_tip = tips[0];
for (size_t i = 1; i < tips.size(); ++i) {
    if (_delegate->compare_fork_branches(tips[i], winner_tip) > 0) {
        winner_tip = tips[i];
    }
}
dlt_fork_branch_info winner;
winner.tip = winner_tip;
```

The rest of `resolve_fork()` already uses `winner.tip` for hysteresis and fork switching, so it will work with the new `winner_tip`.

After this change, `compute_branch_info()` is no longer called from `resolve_fork()`. It can be kept for potential future use or removed.

---

## Task 4: `expected_next_block` tracking never used (P1)

**File**: `libraries/network/dlt_p2p_node.cpp`

The field `expected_next_block` is declared in `dlt_peer_state` but never set or validated. Per plan section 3.7: reject blocks that skip too far ahead (hole-creation attack prevention).

**Changes**:

In `on_dlt_block_range_reply()` (after line 633, inside the block loop), add ordering validation before `accept_block()`:
```cpp
// Validate block ordering
if (state.expected_next_block != 0 && block.block_num() != state.expected_next_block) {
    wlog(DLT_LOG_RED "Block #${n} from ${ep} out of order (expected #${e})" DLT_LOG_RESET,
         ("n", block.block_num())("ep", state.endpoint)("e", state.expected_next_block));
    record_packet_result(peer, false);
    continue;
}
```

After successful `accept_block()`, set the expected next block:
```cpp
state.expected_next_block = block.block_num() + 1;
```

Same for `on_dlt_block_reply()` (line 673): add the same validation before `accept_block()`.

On disconnect (`handle_disconnect()`), reset: `state.expected_next_block = 0;`

---

## Task 5: `pending_block_batch` timeout + `block_validation_timeout()` (P1)

**Files**: `libraries/network/include/graphene/network/dlt_p2p_node.hpp`, `libraries/network/dlt_p2p_node.cpp`

The field `pending_block_batch_time` and helper `has_pending_batch_timeout()` exist but are never used. The `block_validation_timeout()` method is not implemented.

**Changes**:

**Header** (`dlt_p2p_node.hpp`): Add method declaration in the private section (after `periodic_task()` line 194):
```cpp
void block_validation_timeout();
```

**Implementation** (`dlt_p2p_node.cpp`):

1. In `on_dlt_block_range_reply()` (line 615), before the block processing loop, set the batch start time:
```cpp
state.pending_block_batch_time = fc::time_point::now();
```

2. After the block processing loop completes successfully, clear it:
```cpp
state.pending_block_batch_time = fc::time_point();
```

3. Implement `block_validation_timeout()`:
```cpp
void dlt_p2p_node::block_validation_timeout() {
    for (auto& [id, state] : _peer_states) {
        if (state.has_pending_batch_timeout()) {
            wlog(DLT_LOG_RED "Block validation timeout for peer ${ep} (30s)" DLT_LOG_RESET,
                 ("ep", state.endpoint));
            record_packet_result(id, false);
            state.pending_block_batch_time = fc::time_point();
            // If already at threshold, soft_ban will happen via record_packet_result
        }
    }
}
```

4. Call from `periodic_task()` (after `sync_stagnation_check()` line 1274):
```cpp
block_validation_timeout();
```

---

## Task 6: Fork window reset on non-confirmation (P1)

**File**: `libraries/network/dlt_p2p_node.cpp`

In `track_fork_state()` (lines 1116-1120), `_fork_detected = false` is set after `resolve_fork()` regardless of whether resolution succeeded. When hysteresis is not met, this causes a fresh 42-block countdown instead of continuous retry.

**Fix**: Move `_fork_detected = false` into `resolve_fork()` — only clear it when resolution actually completes.

Change `track_fork_state()` (lines 1116-1120):
```cpp
// Before:
if (_fork_detected &&
    block.block_num() - _fork_detection_block_num >= FORK_RESOLUTION_BLOCK_THRESHOLD) {
    resolve_fork();
    _fork_detected = false;
}

// After:
if (_fork_detected &&
    block.block_num() - _fork_detection_block_num >= FORK_RESOLUTION_BLOCK_THRESHOLD) {
    resolve_fork();
    // _fork_detected is cleared inside resolve_fork() only when resolution completes
}
```

In `resolve_fork()`, add `_fork_detected = false` at the two exit points where resolution actually completes:

1. After line 1127 (`_fork_status = DLT_FORK_STATUS_NORMAL; return;` — branch count < 2, fork is over):
```cpp
if (tips.size() < 2) {
    _fork_status = DLT_FORK_STATUS_NORMAL;
    _fork_detected = false;  // fork resolved: only 1 branch remains
    return;
}
```

2. At the end of the function (after the hysteresis is confirmed and fork switch is executed, line 1169):
```cpp
// Reset hysteresis
_fork_resolution_state = dlt_fork_resolution_state();
_fork_detected = false;  // fork resolution completed
```

Do NOT set `_fork_detected = false` at the early return (line 1153) where hysteresis is not confirmed — that's the whole point.

---

## Task 7: `switch_to_fork()` in delegate — only pops one block (P1)

**File**: `plugins/p2p/p2p_plugin.cpp` (lines 189-201)

Current implementation calls `pop_block()` once but never re-pushes blocks from the new fork. The chain's `_push_block()` already has a full fork-switch implementation (database.cpp:1590-1730) that handles pop-until-common-ancestor + re-apply-new-branch + LIB guard + DLT crash prevention.

**Fix**: Replace the simplified `switch_to_fork()` with a call to `push_block()` which triggers the chain's built-in fork switch:

```cpp
void switch_to_fork(const block_id_type& new_head) override {
    try {
        auto& fdb = chain.db().get_fork_db();
        auto block = fdb.fetch_block(new_head);
        if (block) {
            ilog("Switching to fork with head ${id}", ("id", new_head));
            // The chain's push_block() handles full fork switch:
            // pop-until-common-ancestor, re-apply new branch,
            // LIB guard, DLT crash prevention
            chain.db().push_block(*block);
        }
    } catch (const fc::exception& e) {
        wlog("Error switching to fork: ${e}", ("e", e.to_detail_string()));
    }
}
```

This requires adding the include for the block type (already available through `graphene/chain/database.hpp`).

---

## Task 8: `is_head_on_branch()` too simplistic (P1)

**File**: `plugins/p2p/p2p_plugin.cpp` (lines 203-206)

Current implementation does `tip == head_block_id()` — misses the case where our head IS on the branch but not at its tip (e.g., our head is block 100, tip is block 105 on the same branch).

**Fix**: Use `fork_db.fetch_branch_from()` to check if our head is an ancestor of the tip:

```cpp
bool is_head_on_branch(const block_id_type& tip) const override {
    if (tip == chain.db().head_block_id()) return true;
    try {
        auto& fdb = chain.db().get_fork_db();
        if (!fdb.is_known_block(tip) || !fdb.is_known_block(chain.db().head_block_id()))
            return false;
        auto branches = fdb.fetch_branch_from(tip, chain.db().head_block_id());
        // If our head is in the "old" branch (branches.second), we're on the same branch
        // as the tip — they share a common ancestor and our head is below the tip
        return !branches.second.empty();
    } catch (...) {
        return false;
    }
}
```

---

## Task 9: Spam strikes not incremented for expired/TaPoS-invalid rejections (P2)

**File**: `libraries/network/dlt_p2p_node.cpp`

In `add_to_mempool()`:
- Line 967: expired transaction returns false without `record_packet_result(sender, false)`
- Line 984: TaPoS-invalid transaction returns false without `record_packet_result(sender, false)`

**Fix**:

Line 967, replace:
```cpp
if (trx.expiration < fc::time_point_sec(fc::time_point::now())) return false;
```
With:
```cpp
if (trx.expiration < fc::time_point_sec(fc::time_point::now())) {
    if (from_peer && sender != INVALID_PEER_ID) record_packet_result(sender, false);
    return false;
}
```

Line 984, replace:
```cpp
if (!is_tapos_valid(trx)) return false;
```
With:
```cpp
if (!is_tapos_valid(trx)) {
    if (from_peer && sender != INVALID_PEER_ID) record_packet_result(sender, false);
    return false;
}
```

---

## Task 10: `periodic_dlt_prune_check()` is a no-op (P2)

**File**: `libraries/network/dlt_p2p_node.cpp` (lines 1224-1227)

Currently empty. Should trigger batch pruning when the DLT block log exceeds `_dlt_block_log_max_blocks`.

**Fix**:
```cpp
void dlt_p2p_node::periodic_dlt_prune_check() {
    if (!_delegate) return;
    uint32_t earliest = _delegate->get_dlt_earliest_block();
    uint32_t latest = _delegate->get_dlt_latest_block();
    if (latest == 0 || earliest == 0) return;

    uint32_t current_range = latest - earliest + 1;
    if (current_range <= _dlt_block_log_max_blocks) return;

    // Only prune in batches of DLT_PRUNE_BATCH_SIZE (10000)
    if (latest - _last_prune_block_num < DLT_PRUNE_BATCH_SIZE) return;

    ilog(DLT_LOG_GREEN "DLT block log exceeds max (${r} > ${m}), pruning ${b} blocks" DLT_LOG_RESET,
         ("r", current_range)("m", _dlt_block_log_max_blocks)("b", DLT_PRUNE_BATCH_SIZE));

    // The actual pruning is done at the chain level via truncate_before()
    // We signal the delegate to prune, passing the new start block number
    uint32_t new_start = earliest + DLT_PRUNE_BATCH_SIZE;
    // Delegate method needed — for now, log the intent
    // TODO: Add dlt_p2p_delegate::prune_dlt_block_log(uint32_t new_start)
    _last_prune_block_num = latest;
}
```

This is a partial implementation — a new delegate method `prune_dlt_block_log()` would be needed for the full chain. Mark as partial with a TODO.

---

## Task 11: `has_emergency_private_key()` returns false (P2)

**File**: `plugins/p2p/p2p_plugin.cpp` (lines 80-83)

Currently always returns `false`. Should check if the witness plugin has an emergency private key configured.

**Fix**: Query the witness plugin:
```cpp
bool has_emergency_private_key() const override {
    auto* wit_plug = appbase::app().find_plugin<graphene::plugins::witness::witness_plugin>();
    if (wit_plug) {
        return wit_plug->is_emergency_key_configured();
    }
    return false;
}
```

This requires:
1. Adding `#include <graphene/plugins/witness/witness_plugin.hpp>` to p2p_plugin.cpp
2. Adding `bool is_emergency_key_configured() const;` to the witness_plugin public API
3. Implementing it in the witness plugin (check if emergency master key is set)

Since this crosses plugin boundaries and requires changes to the witness plugin, mark as requiring coordination. The minimal change is to add the include and the query, with a stub on the witness side that returns `true` if the key is in the config.

---

## Task 12: `accept_block()` ignores `sync_mode` parameter (P2)

**File**: `plugins/p2p/p2p_plugin.cpp` (lines 139-151)

Currently always calls `push_block()` the same way regardless of `sync_mode`. In sync mode, blocks are being applied in bulk and certain expensive checks could be skipped.

**Fix**: Pass `skip` flags to `push_block()` based on sync mode:
```cpp
bool accept_block(const signed_block& block, bool sync_mode) override {
    try {
        uint32_t skip = graphene::chain::database::skip_nothing;
        if (sync_mode) {
            // During bulk sync, skip expensive checks that are redundant
            // for blocks we trust from our fork peers
            skip = graphene::chain::database::skip_witness_signature
                 | graphene::chain::database::skip_transaction_signatures;
        }
        chain.db().push_block(block, skip);
        return false; // fork detection done via on_block_applied callback
    } catch (const graphene::chain::unlinkable_block_exception&) {
        wlog("Unlinkable block #${n}, storing in fork_db", ("n", block.block_num()));
        chain.db().get_fork_db().push_block(block);
        return false;
    } catch (const fc::exception& e) {
        wlog("Error accepting block #${n}: ${e}", ("n", block.block_num())("e", e.to_detail_string()));
        return false;
    }
}
```

Note: The `skip_witness_signature` flag must be used carefully — it should only be applied for blocks from fork-aligned peers. The current design already ensures this because only fork-aligned peers exchange blocks.

---

## Task 13: `resync_from_lib()` is shallow (P2)

**Files**: `plugins/p2p/p2p_plugin.cpp` (lines 215-217, 437-441), `libraries/network/dlt_p2p_node.cpp` (lines 847-856)

The delegate-level `resync_from_lib()` is empty. The node-level version just calls `transition_to_sync()` + re-requests blocks. A proper resync from LIB should:
1. Pop blocks back to LIB
2. Reset fork tracking state
3. Re-request blocks from LIB+1

**Fix for dlt_p2p_node::resync_from_lib()** (line 847):
```cpp
void dlt_p2p_node::resync_from_lib(bool force_emergency) {
    ilog(DLT_LOG_GREEN "DLT P2P: resync from LIB requested (force_emergency=${f})" DLT_LOG_RESET,
         ("f", force_emergency));
    
    // Reset fork tracking
    _fork_detected = false;
    _fork_detection_block_num = 0;
    _fork_resolution_state = dlt_fork_resolution_state();
    _fork_status = DLT_FORK_STATUS_NORMAL;
    
    transition_to_sync();
    
    // Re-send hello to all peers to get updated chain state
    auto hello = build_hello_message();
    for (auto& [id, state] : _peer_states) {
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
            state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
            send_message(id, message(hello));
            request_blocks_from_peer(id);
        }
    }
}
```

The delegate-level `resync_from_lib()` can remain empty since the P2P node handles the logic internally. The chain-level block popping is handled by the caller (witness plugin) before invoking `resync_from_lib()`.

---

## Task 14: Review Document Fixes

**File**: `.qoder/docs/dlt-p2p-network-redesign-review.md`

### 14a: Factual error in GAP 4 (line 116)

Current text:
> "The plan's pseudocode does NOT set `_fork_detected = false` inside `track_fork_state()` after calling `resolve_fork()`."

This is incorrect — the plan's pseudocode (plan lines 643-647) DOES set `_fork_detected = false`. Both the plan's pseudocode and implementation share the same issue.

Replace line 116 with:
> "Both the plan's pseudocode and the implementation set `_fork_detected = false` after `resolve_fork()` — but this contradicts the plan's own design intent in section 3.7, which specifies that only the confirmation counter should reset on lead flip, not the 42-block detection window."

### 14b: Severity inconsistency — GAP 5

- Line 124 heading: "(P0 Anti-Spam)"
- Line 191 summary: "P2 (nice to fix)"

Align both to P0 (matches plan's P0 rating for mempool DoS protection):
Change line 191 from:
```
| **P2 (nice to fix)** | GAP 5 — incomplete spam strikes; Known gaps 1-6 (already documented) |
```
To:
```
| **P0 (must fix)** | GAP 5 — incomplete spam strikes (plan rates this P0) |
| **P2 (nice to fix)** | Known gaps 1-6 (already documented) |
```

### 14c: Severity inconsistency — GAP 6

- Line 144 heading: "(P1 Fork)"
- Line 189 summary: "P0 (must fix)"

Align heading to P0 (fork resolution is non-functional):
Change line 144 from `### GAP 6: Fork resolution winner always picks first branch (P1 Fork)` to `### GAP 6: Fork resolution winner always picks first branch (P0 Fork)`

### 14d: Upgrade minor observation #2

Minor observation #2 says `broadcast_block_post_validation()` sends a `dlt_fork_status_message` "functional but semantically imprecise." But `msg.head_block_num = 0` actually corrupts `peer_head_num` in the receiver — this is a functional bug, not just semantic imprecision.

Replace line 178 with:
> `broadcast_block_post_validation()` sends a `dlt_fork_status_message` with `head_block_num = 0` — the receiver stores this as `peer_head_num = 0`, corrupting peer state tracking. This is a functional bug, not just semantic imprecision.

---

## Execution Order

Tasks should be executed in this order to minimize conflicts:

1. **Task 1** (compile error) — must be first, code won't compile without it
2. **Task 2** (head_block_num = 0) — simple one-line fix
3. **Task 3** (fork resolution) — highest-impact logic fix
4. **Task 6** (fork window reset) — related to Task 3, same area
5. **Task 5** (block validation timeout) — new method + wiring
6. **Task 4** (expected_next_block) — validation additions
7. **Task 7** (switch_to_fork delegate) — delegate fix
8. **Task 8** (is_head_on_branch) — delegate fix
9. **Task 9** (spam strikes) — simple additions
10. **Task 10** (prune check) — partial implementation
11. **Task 11** (emergency key) — cross-plugin, partial
12. **Task 12** (accept_block sync_mode) — optimization
13. **Task 13** (resync_from_lib) — node logic
14. **Task 14** (review doc) — documentation fixes, last