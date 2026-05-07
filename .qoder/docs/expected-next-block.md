# DLT P2P `expected_next_block` — Design, Data Flow, and Known Gaps

## 1. Overview

`expected_next_block` is a per-peer tracking field in `dlt_peer_state` that records the next sequential block number we expect to receive **from this specific peer**. Its purpose is:

1. **Detect out-of-order blocks** — when a peer sends a block whose `block_num` doesn't match `expected_next_block`, something unexpected happened (duplicate, gap, competing fork).
2. **Distinguish harmless duplicates from suspicious out-of-order** — if the block is a known duplicate from another peer, it's harmless; otherwise it may indicate a fork or gap.

**Key invariant (design intent):** `expected_next_block` should always equal `head_block_num + 1` for peers that are in sync with us. In practice, it only tracks what *this peer* has sent us, not our global chain head.

**Field definition:** `dlt_p2p_peer_state.hpp:65`
```cpp
uint32_t expected_next_block = 0;  // 0 = "not tracking" (no active sync session)
```

---

## 2. Lifecycle — Every Write Site

### 2.1 Initialization to 0 (tracking disabled)

| Site | File:Line | When |
|------|-----------|------|
| Struct default | `dlt_p2p_peer_state.hpp:65` | Peer state constructed |
| `connect_to_peer` | `dlt_p2p_node.cpp:222` | `state = dlt_peer_state()` — full zero-init on reconnect |
| `handle_disconnect` | `dlt_p2p_node.cpp:321` | `state.expected_next_block = 0` on disconnect |
| `request_blocks_from_peer` | `dlt_p2p_node.cpp:945` | Reset before sending range request |

**Meaning of `0`:** We are not actively tracking block ordering from this peer. The out-of-order check is skipped entirely (see §3.1).

### 2.2 Write: `expected_next_block = max(expected_next_block, block_num + 1)`

This is the **only pattern** used to advance the value. It appears in two places:

| Site | File:Line | Context |
|------|-----------|---------|
| `on_dlt_block_range_reply` | `dlt_p2p_node.cpp:1136` | After processing each block in a range reply |
| `on_dlt_block_reply` | `dlt_p2p_node.cpp:1354` | After processing a single block reply |

Both use `std::max()` so the value only moves forward, never backward.

---

## 3. Lifecycle — Every Read Site (Decision Points)

### 3.1 Out-of-order check in `on_dlt_block_range_reply` (SYNC mode)

**File:** `dlt_p2p_node.cpp:1070-1080`

```
if (state.expected_next_block != 0 && block.block_num() != state.expected_next_block)
```

**Branches:**
- `expected_next_block == 0` → skip check (no tracking active)
- `block_num == expected_next_block` → in order, proceed normally
- `block_num < expected_next_block && block is known` → duplicate, skip with debug log
- `block_num != expected_next_block && (not duplicate)` → warn "out of order", fall through to accept_block

### 3.2 Out-of-order check in `on_dlt_block_reply` (FORWARD mode single-block)

**File:** `dlt_p2p_node.cpp:1211-1264`

Same structure as 3.1, plus additional logic:

- **Gap fill trigger (P36/P40):** If FORWARD mode and `block_num > head + 1`, request gap fill. If `block_num == head + 1`, there's no real gap — it's just stale `expected_next_block`.
- **Competing fork detection:** If the block's `previous` hash differs from our head at the same height, request the competing parent block.

---

## 4. Data Flow Diagram

```
                    ┌──────────────────────────────────────────┐
                    │            Peer Connect / Reconnect       │
                    │  expected_next_block = 0                  │
                    └─────────────┬────────────────────────────┘
                                  │
                    ┌─────────────▼────────────────────────────┐
                    │      request_blocks_from_peer()           │
                    │  expected_next_block = 0  (reset again)  │
                    └─────────────┬────────────────────────────┘
                                  │
               ┌──────────────────┴──────────────────┐
               │                                      │
    ┌──────────▼──────────┐              ┌────────────▼───────────┐
    │  Range Reply (SYNC)  │              │  Single Block Reply    │
    │  (batch of blocks)   │              │  (FORWARD / broadcast) │
    └──────────┬──────────┘              └────────────┬───────────┘
               │                                      │
    For each block:                        1. Out-of-order check
    1. Out-of-order check                     - duplicate? → return
    2. accept_block()                         - gap? → request_gap_fill()
    3. expected_next_block =                 - competing fork? → request parent
       max(enb, block_num + 1)             2. accept_block()
                                            3. expected_next_block =
                                               max(enb, block_num + 1)
               │                                      │
               └──────────────────┬──────────────────┘
                                  │
                    ┌─────────────▼────────────────────────────┐
                    │        handle_disconnect()                │
                    │  expected_next_block = 0                  │
                    └──────────────────────────────────────────┘
```

---

## 5. What Does NOT Update `expected_next_block`

This is the critical section — these are the **gaps** in the tracking that cause false "out of order" warnings.

### 5.1 Self-produced blocks (BUG)

When the witness plugin generates a block:

1. `witness.cpp:1092-1099`: `db.generate_block()` → `p2p().broadcast_block(block)`
2. `broadcast_block()` (`dlt_p2p_node.cpp:1822-1828`): sends block to peers via `send_to_all_our_fork_peers`
3. **Neither `broadcast_block()` nor `on_block_applied()` updates `expected_next_block` for any peer**

**Impact:** If our node produces block #N and peer A sends us block #N+1, peer A's `expected_next_block` is still #N → false "out of order (expected #N)".

### 5.2 Blocks received from other peers

When we accept block #N from peer A:
- Peer A's `expected_next_block` → N+1
- Peer B's `expected_next_block` → unchanged (still stale)

If peer B then sends #N+1, we log "out of order (expected #N)" for peer B.

### 5.3 `on_block_applied()` does not touch peer states

**File:** `dlt_p2p_node.cpp:2409-2423`

`on_block_applied()` only:
1. Removes transactions from mempool
2. Tracks fork state
3. Prunes DLT block log

It does **not** iterate over `_peer_states` to update `expected_next_block`.

### 5.4 Fork switches

When a fork switch happens (minority → majority), all peer `expected_next_block` values remain from the old chain, becoming stale.

### 5.5 `transition_to_forward()` / `transition_to_sync()`

Neither transition function updates `expected_next_block` for any peer.

---

## 6. Current Mitigations

The code has several mitigations for stale `expected_next_block`:

| Mitigation | File:Line | Description |
|-----------|-----------|-------------|
| Reset to 0 on range request | `dlt_p2p_node.cpp:945` | Fresh range request resets tracking |
| Duplicate detection | `dlt_p2p_node.cpp:1071,1213` | If block_num < expected and block is known → skip silently |
| P40 gap fill guard | `dlt_p2p_node.cpp:1231` | Only trigger gap fill if block_num > head + 1 (not just stale tracking) |
| std::max on write | `dlt_p2p_node.cpp:1136,1354` | Value only moves forward, never backward |

These mitigations prevent the worst outcomes (infinite gap fill loops, punishing innocent peers) but do **not** prevent the false "out of order" warning log messages.

---

## 7. Concrete Bug Scenario (from production logs)

```
206774ms  witness.cpp:431    Generated block #79720273 ... by creativity
...
209818ms  dlt_p2p_node.cpp:1208  Block #79720274 from 80.87.202.57 out of order (expected #79720273)
209822ms  dlt_p2p_node.cpp:1298  Got block #79720274 ... by witness m0ssa99 [80.87.202.57]
```

**Trace:**
1. Peer 80.87.202.57 sent us block #79720272 → its `expected_next_block` = 79720273
2. We generated #79720273 ourselves → `expected_next_block` for this peer stays at 79720273
3. Peer sends #79720274 → `79720274 != 79720273` → false "out of order"
4. Block is still accepted (no real gap, `block_num == head + 1`) — but the warning is misleading noise

---

## 8. Proposed Improvements

### 8.1 Fix: Update all peers' `expected_next_block` in `on_block_applied()` (RECOMMENDED)

After any block is applied to our chain (from any source), advance `expected_next_block` for all peers whose value is behind our new head.

**Location:** `dlt_p2p_node.cpp:2409` (`on_block_applied`)

```cpp
void dlt_p2p_node::on_block_applied(const signed_block& block, bool caused_fork_switch) {
    remove_transactions_in_block(block);
    track_fork_state(block);
    if (caused_fork_switch) {
        prune_mempool_on_fork_switch();
    }
    periodic_dlt_prune_check();

    // NEW: Advance stale expected_next_block for all peers.
    uint32_t next = block.block_num() + 1;
    for (auto& item : _peer_states) {
        if (item.second.expected_next_block != 0 &&
            item.second.expected_next_block < next) {
            item.second.expected_next_block = next;
        }
    }
}
```

**Pros:** Single-line fix, covers all sources (self-produced, other peers, gap fill, fork switch).
**Cons:** O(peers) per block — trivial cost (typically < 20 peers).

### 8.2 Fix: Call `on_block_applied()` from `broadcast_block()` (BUG FIX — secondary)

Currently `broadcast_block()` does not call `on_block_applied()`. This means:
- DLT P2P mempool is not cleaned of transactions in self-produced blocks
- Fork state is not tracked for own blocks

```cpp
void dlt_p2p_node::broadcast_block(const signed_block& block) {
    dlt_block_reply_message reply;
    reply.block = block;
    reply.next_available = 0;
    reply.is_last = true;
    send_to_all_our_fork_peers(message(reply), INVALID_PEER_ID, block.id());

    // NEW: Track our own block application (mempool cleanup, fork state,
    // and expected_next_block advancement for all peers).
    on_block_applied(block, /*caused_fork_switch=*/false);
}
```

**Caution:** `on_block_applied` calls `remove_transactions_in_block` which iterates mempool. Need to verify this is safe when called from witness thread context (should be fine since P2P operations are single-threaded on the p2p fiber).

### 8.3 Improvement: Demote stale "out of order" to debug level when block_num == head + 1

Even with fix 8.1, there's a narrow race window. When `block_num == head + 1`, the block links directly to our head — it's not truly out of order. The warning should be debug level:

**Location:** `dlt_p2p_node.cpp:1221` and `dlt_p2p_node.cpp:1077`

```cpp
// Before:
wlog("Block #${n} from ${ep} out of order (expected #${e})", ...);

// After: only warn if there's a genuine gap
if (block_num <= _delegate->get_head_block_num()) {
    dlog("Block #${n} from ${ep} out of order (expected #${e}) but at or behind head — stale tracking",
         ...);
} else if (block_num == _delegate->get_head_block_num() + 1) {
    dlog("Block #${n} from ${ep} matches head+1 but expected #${e} — stale tracking",
         ...);
} else {
    wlog("Block #${n} from ${ep} out of order (expected #${e})", ...);
}
```

### 8.4 Improvement: Reset `expected_next_block` on `transition_to_forward()`

When we complete SYNC and transition to FORWARD, all peers' `expected_next_block` is based on the last range reply. But in FORWARD mode, blocks arrive via broadcast from any peer, not in sequence from one peer. Resetting to 0 (or to `head + 1`) on transition would eliminate stale values:

```cpp
// In transition_to_forward():
for (auto& item : _peer_states) {
    item.second.expected_next_block = 0;  // broadcast mode = no sequential tracking
}
```

### 8.5 Long-term: Replace per-peer `expected_next_block` with global head comparison

The fundamental design issue is that `expected_next_block` tries to track per-peer sequential ordering, but in FORWARD mode blocks come from multiple peers simultaneously. The real question is: "does this block follow our chain head?" — not "does this block follow what we last received from this peer?"

A simpler and more robust approach:

```cpp
// Replace out-of-order check with:
bool is_out_of_order = (block_num > _delegate->get_head_block_num() + 1);
bool is_behind = (block_num <= _delegate->get_head_block_num());
```

This eliminates the per-peer tracking entirely for FORWARD mode and uses the only authoritative source of truth — our chain head.

---

## 9. Source File Reference

| File | Relevance |
|------|-----------|
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp:65` | Field definition |
| `libraries/network/dlt_p2p_node.cpp:321` | Reset on disconnect |
| `libraries/network/dlt_p2p_node.cpp:945` | Reset on range request |
| `libraries/network/dlt_p2p_node.cpp:1070-1080` | Out-of-order check (range reply) |
| `libraries/network/dlt_p2p_node.cpp:1136` | Advance after range block |
| `libraries/network/dlt_p2p_node.cpp:1211-1264` | Out-of-order check (single block) |
| `libraries/network/dlt_p2p_node.cpp:1354` | Advance after single block |
| `libraries/network/dlt_p2p_node.cpp:1822-1828` | `broadcast_block()` — missing update |
| `libraries/network/dlt_p2p_node.cpp:2409-2423` | `on_block_applied()` — missing update |
| `plugins/witness/witness.cpp:1092-1099` | Block production → broadcast |
