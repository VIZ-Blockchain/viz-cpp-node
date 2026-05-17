# DLT P2P `expected_next_block` — Design, Data Flow, and Fixes

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

This is the **per-peer advance** pattern. It appears in two places:

| Site | File:Line | Context |
|------|-----------|--------|
| `on_dlt_block_range_reply` | `dlt_p2p_node.cpp:1150` | After processing each block in a range reply |
| `on_dlt_block_reply` | `dlt_p2p_node.cpp:1381` | After processing a single block reply |

Both use `std::max()` so the value only moves forward, never backward.

### 2.3 Write: Bulk advance for ALL peers in `on_block_applied()` (Fix 8.1)

**File:** `dlt_p2p_node.cpp:2457-2469`

After any block is applied (from any source), all peers whose `expected_next_block` is behind the new chain head are advanced:

```cpp
uint32_t next = block.block_num() + 1;
for (auto& item : _peer_states) {
    if (item.second.expected_next_block != 0 &&
        item.second.expected_next_block < next) {
        item.second.expected_next_block = next;
    }
}
```

This is called from:
- `on_dlt_block_range_reply` (SYNC mode) — `dlt_p2p_node.cpp:1117`
- `on_dlt_block_reply` (FORWARD mode) — `dlt_p2p_node.cpp:1345`
- `on_dlt_gap_fill_reply` (gap fill) — `dlt_p2p_node.cpp:1678`
- `broadcast_block()` (self-produced blocks) — `dlt_p2p_node.cpp:1860` (Fix 8.2)

---

## 3. Lifecycle — Every Read Site (Decision Points)

### 3.1 Out-of-order check in `on_dlt_block_range_reply` (SYNC mode)

**File:** `dlt_p2p_node.cpp:1070-1094`

```
if (state.expected_next_block != 0 && block.block_num() != state.expected_next_block)
```

**Branches:**
- `expected_next_block == 0` → skip check (no tracking active)
- `block_num == expected_next_block` → in order, proceed normally
- `block_num < expected_next_block && block is known` → duplicate, skip with debug log
- `block_num != expected_next_block && (not duplicate)` → **3-tier log** (Fix 8.3, see §6):
  - `block_num <= head` → `dlog` (stale tracking)
  - `block_num == head + 1` → `dlog` (stale tracking)
  - `block_num > head + 1` → `wlog` (genuine gap)

### 3.2 Out-of-order check in `on_dlt_block_reply` (FORWARD mode single-block)

**File:** `dlt_p2p_node.cpp:1225-1291`

Same 3-tier log structure as 3.1 (Fix 8.3), plus additional logic:

- **Gap fill trigger (P36/P40/P54):** If `block_num > head + 1`, request gap fill. Works in both SYNC and FORWARD modes. If `block_num == head + 1`, there's no real gap - it's just stale `expected_next_block`.
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
    For each block:                        1. Out-of-order check (3-tier log)
    1. Out-of-order check (3-tier)           - duplicate? → return
    2. accept_block()                         - gap? → request_gap_fill()
    3. on_block_applied() ◄── Fix 8.1       - competing fork? → request parent
       └─ advances ALL peers' enb          2. accept_block()
    4. expected_next_block =                 3. on_block_applied() ◄── Fix 8.1
       max(enb, block_num + 1)                 └─ advances ALL peers' enb
                                            4. expected_next_block =
                                               max(enb, block_num + 1)
               │                                      │
               └──────────────────┬──────────────────┘
                                  │
               ┌──────────────────┴──────────────────┐
               │                                      │
    ┌──────────▼──────────┐              ┌────────────▼───────────┐
    │   broadcast_block()  │              │  on_dlt_gap_fill_reply │
    │  (self-produced)     │              │  (gap fill blocks)     │
    │  Fix 8.2: calls      │              │                        │
    │  on_block_applied()  │              │  on_block_applied()    │
    └──────────┬──────────┘              └────────────┬───────────┘
               │                                      │
               └──────────────────┬──────────────────┘
                                  │
                    ┌─────────────▼────────────────────────────┐
                    │        handle_disconnect()                │
                    │  expected_next_block = 0                  │
                    └──────────────────────────────────────────┘
```

---

## 5. Residual Gaps (after Fixes 8.1–8.3)

Fixes 8.1, 8.2, and 8.3 eliminate the primary sources of false "out of order" warnings. The following gaps still exist but have reduced impact:

### 5.1 ~~Self-produced blocks~~ FIXED (Fix 8.2)

`broadcast_block()` now calls `on_block_applied()`, which advances all peers' `expected_next_block`.

### 5.2 ~~Blocks received from other peers~~ FIXED (Fix 8.1)

`on_block_applied()` now iterates all peers and advances stale `expected_next_block` values.

### 5.3 ~~`on_block_applied()` does not touch peer states~~ FIXED (Fix 8.1)

`on_block_applied()` now includes the peer advancement loop at `dlt_p2p_node.cpp:2457-2469`.

### 5.4 Fork switches

When a fork switch happens (minority → majority), the fix 8.1 loop in `on_block_applied()` will advance peers to the new head **if** `on_block_applied()` is called during the fork switch path. This is handled by the chain layer calling `broadcast_block` or the P2P accept path after switch_to_fork.

### 5.5 `transition_to_forward()` / `transition_to_sync()`

Neither transition function updates `expected_next_block` for any peer. This is mitigated by fix 8.1 since any block application during/after transition will correct stale values.

### 5.6 Narrow race window

There is a small race between when a block is applied and when the next block arrives from another peer. If both blocks arrive nearly simultaneously (before the first block's `on_block_applied()` completes the peer iteration), a single stale "out of order" may still fire. Fix 8.3 demotes this to `dlog`.

---

## 6. Active Mitigations

| Mitigation | File:Line | Description |
|-----------|-----------|-------------|
| Bulk advance all peers (8.1) | `dlt_p2p_node.cpp:2457-2469` | `on_block_applied()` advances stale `expected_next_block` for all peers |
| `broadcast_block` → `on_block_applied` (8.2) | `dlt_p2p_node.cpp:1860` | Self-produced blocks trigger peer advancement |
| 3-tier log demotion (8.3) | `dlt_p2p_node.cpp:1083-1092, 1240-1249` | Stale tracking → `dlog`; genuine gap → `wlog` |
| Reset to 0 on range request | `dlt_p2p_node.cpp:945` | Fresh range request resets tracking |
| Duplicate detection | `dlt_p2p_node.cpp:1071,1227` | If block_num < expected and block is known → skip silently |
| P40 gap fill guard | `dlt_p2p_node.cpp:1258` | Only trigger gap fill if block_num > head + 1 (not just stale tracking) |
| std::max on write | `dlt_p2p_node.cpp:1150,1381` | Value only moves forward, never backward |

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

## 8. Implemented Fixes

### 8.1 IMPLEMENTED: Update all peers' `expected_next_block` in `on_block_applied()`

After any block is applied to our chain (from any source), advance `expected_next_block` for all peers whose value is behind our new head.

**Location:** `dlt_p2p_node.cpp:2457-2469` (`on_block_applied`)

```cpp
// Advance stale expected_next_block for all peers.
uint32_t next = block.block_num() + 1;
for (auto& item : _peer_states) {
    if (item.second.expected_next_block != 0 &&
        item.second.expected_next_block < next) {
        item.second.expected_next_block = next;
    }
}
```

**Call sites:** `on_dlt_block_range_reply:1117`, `on_dlt_block_reply:1345`, `on_dlt_gap_fill_reply:1678`, `broadcast_block:1860`.

### 8.2 IMPLEMENTED: Call `on_block_applied()` from `broadcast_block()`

Self-produced blocks now trigger mempool cleanup, fork state tracking, and peer `expected_next_block` advancement.

**Location:** `dlt_p2p_node.cpp:1856-1860`

```cpp
// Track our own block application: clean mempool of included
// transactions, advance fork state, and update all peers'
// expected_next_block so the next incoming block from any peer
// is not falsely flagged as "out of order".
on_block_applied(block, /*caused_fork_switch=*/false);
```

### 8.3 IMPLEMENTED: Demote stale "out of order" to debug level

Replaced unconditional `wlog` with 3-tier logic at both out-of-order check sites:

| Condition | Log level | Meaning |
|-----------|-----------|---------|
| `block_num <= head` | `dlog` | Block already applied — stale per-peer tracker |
| `block_num == head + 1` | `dlog` | Block links to head — stale per-peer tracker, no gap |
| `block_num > head + 1` | `wlog` | Genuine gap — real out-of-order concern |

**Locations:** `dlt_p2p_node.cpp:1083-1092` (SYNC), `dlt_p2p_node.cpp:1240-1249` (FORWARD)

---

## 9. Remaining Proposals (Not Yet Implemented)

### 9.1 Reset `expected_next_block` on `transition_to_forward()`

When we complete SYNC and transition to FORWARD, all peers' `expected_next_block` is based on the last range reply. But in FORWARD mode, blocks arrive via broadcast from any peer, not in sequence from one peer. Resetting to 0 (or to `head + 1`) on transition would eliminate stale values:

```cpp
// In transition_to_forward():
for (auto& item : _peer_states) {
    item.second.expected_next_block = 0;  // broadcast mode = no sequential tracking
}
```

### 9.2 Long-term: Replace per-peer `expected_next_block` with global head comparison

The fundamental design issue is that `expected_next_block` tries to track per-peer sequential ordering, but in FORWARD mode blocks come from multiple peers simultaneously. The real question is: "does this block follow our chain head?" — not "does this block follow what we last received from this peer?"

A simpler and more robust approach:

```cpp
// Replace out-of-order check with:
bool is_out_of_order = (block_num > _delegate->get_head_block_num() + 1);
bool is_behind = (block_num <= _delegate->get_head_block_num());
```

This eliminates the per-peer tracking entirely for FORWARD mode and uses the only authoritative source of truth — our chain head.

---

## 10. Source File Reference

| File | Relevance |
|------|-----------|
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp:65` | Field definition |
| `libraries/network/dlt_p2p_node.cpp:321` | Reset on disconnect |
| `libraries/network/dlt_p2p_node.cpp:945` | Reset on range request |
| `libraries/network/dlt_p2p_node.cpp:1070-1094` | Out-of-order check (range reply, 3-tier log) |
| `libraries/network/dlt_p2p_node.cpp:1117` | `on_block_applied()` call in range reply |
| `libraries/network/dlt_p2p_node.cpp:1150` | Per-peer advance after range block |
| `libraries/network/dlt_p2p_node.cpp:1225-1291` | Out-of-order check (single block, 3-tier log) |
| `libraries/network/dlt_p2p_node.cpp:1345` | `on_block_applied()` call in single block reply |
| `libraries/network/dlt_p2p_node.cpp:1381` | Per-peer advance after single block |
| `libraries/network/dlt_p2p_node.cpp:1678` | `on_block_applied()` call in gap fill reply |
| `libraries/network/dlt_p2p_node.cpp:1849-1861` | `broadcast_block()` — now calls `on_block_applied()` (Fix 8.2) |
| `libraries/network/dlt_p2p_node.cpp:2457-2469` | `on_block_applied()` — bulk peer advance (Fix 8.1) |
| `plugins/witness/witness.cpp:1092-1099` | Block production → broadcast |
