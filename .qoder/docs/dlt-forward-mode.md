# DLT Forward Mode — Block & Transaction Exchange

## Overview

Forward mode (`DLT_NODE_STATUS_FORWARD`) is the normal operating state of a DLT P2P node after it catches up with the network. In this mode, nodes actively **push** new blocks and transactions to each other as they arrive, rather than pulling them via range requests.

This document describes how exchange works in forward mode: what gets sent, to whom, and the filtering mechanisms that control it.

---

## Two-Phase Lifecycle

A DLT node operates in one of two modes:

| Mode | Enum Value | Behavior |
|------|-----------|----------|
| **SYNC** | `DLT_NODE_STATUS_SYNC = 0` | Pull-based: requests block ranges from peers to catch up. Mempool entries are tagged *provisional* (not forwarded). |
| **FORWARD** | `DLT_NODE_STATUS_FORWARD = 1` | Push-based: broadcasts new blocks/transactions to all fork-aligned peers. Mempool entries are validated and forwarded. |

The transition from SYNC → FORWARD happens via `transition_to_forward()` (see [SYNC→FORWARD Transition](#syncforward-transition) below).

---

## Core Mechanism: `send_to_all_our_fork_peers`

All broadcasting in forward mode funnels through a single method:

```cpp
// dlt_p2p_node.cpp
void dlt_p2p_node::send_to_all_our_fork_peers(const message& msg, peer_id exclude = INVALID_PEER_ID, const block_id_type& block_id = block_id_type()) {
    for (auto& [id, state] : _peer_states) {
        if (id == exclude) continue;
        if (state.exchange_enabled && state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE) {
            if (block_id != block_id_type() && state.has_block(block_id)) continue;  // echo suppression
            send_message(id, msg);
            if (block_id != block_id_type()) state.record_known_block(block_id);
        }
    }
}
```

**Two filters control delivery:**

| Filter | Source | Meaning |
|--------|--------|---------|
| `exchange_enabled == true` | Set during hello handshake, combined via OR in hello_reply (P27 fix); re-evaluated on block accept and FORWARD transition (P25 fix) | Peer is on our fork — its head block is known to us |
| `lifecycle_state == ACTIVE` | Peer lifecycle FSM | Peer has completed handshake and is in normal operation |

**`exclude` parameter:** When relaying a message received from a peer, that peer's ID is passed as `exclude` to avoid echoing the message back.

**`block_id` parameter (echo suppression):** When broadcasting or relaying a block, the block's ID is passed as `block_id`. The function checks each peer's `known_blocks` ring buffer — if the peer already has this block, the send is skipped and the peer is counted in the `skipped_echo` diagnostic. After a successful send, the block ID is recorded in the peer's `known_blocks`.

---

## Block Echo Suppression

### Problem

In a multi-peer mesh, a block can echo back to the node that produced it:

```
1. Master A generates block #N, broadcasts to B, V, G
2. V receives #N first, accepts it, retransmits to B and G (standard relay)
3. B receives #N from V, accepts it, retransmits to A and G
4. A receives its own block #N back from B — wasted bandwidth + log noise
```

The `exclude` parameter only filters the **direct sender**. It cannot filter peer B, which received the block from V (not from A) and relayed it to A.

### Solution: Per-Peer `known_blocks` Ring Buffer

Each peer state maintains a small ring buffer of recent block IDs that the peer is **known to have**:

```cpp
// dlt_p2p_peer_state.hpp
static constexpr size_t KNOWN_BLOCKS_WINDOW = 20;  // ~60 seconds of blocks at 3s/block
std::vector<block_id_type> known_blocks;

bool has_block(const block_id_type& id) const;
void record_known_block(const block_id_type& id);
```

A peer is recorded as having a block in two situations:

| Signal | Why it means the peer has the block |
|--------|-------------------------------------|
| **We sent the block to the peer** | `send_to_all_our_fork_peers` records the block ID after each successful send |
| **The peer sent the block to us** | `on_dlt_block_reply` records the block ID from the incoming message |

Before sending a block to a peer, `send_to_all_our_fork_peers` checks `has_block()`. If the peer already has it, the send is skipped.

### Scope

Echo suppression only applies to **block_reply** messages (the primary broadcast vector in FORWARD mode). Transactions, fork_status messages, and other P2P messages are not affected — the `block_id` parameter defaults to `block_id_type()` (null) for those calls.

### Diagnostics

The relay log line now includes echo-filtered count:
```
Relay block_reply to 3 peers (0 skipped: no_exchange, 0 skipped: not_active, 1 skipped: echo)
```

---

## Block Broadcasting

### Self-Produced Blocks

When a validator produces a block, the flow is:

```
validator.cpp:1081    p2p().broadcast_block(block)
  → p2p_plugin.cpp:482   my->node->broadcast_block(block)
    → dlt_p2p_node.cpp:1117
```

```cpp
void dlt_p2p_node::broadcast_block(const signed_block& block) {
    dlt_block_reply_message reply;
    reply.block = block;
    reply.next_available = 0;
    reply.is_last = true;
    send_to_all_our_fork_peers(message(reply), INVALID_PEER_ID, block.id());  // NO exclude, WITH echo suppression
}
```

The block goes to **all** ACTIVE peers with `exchange_enabled=true`, **except** peers that already have this block (echo suppression).

### Relaying Received Blocks

When a block arrives from a peer (via `on_dlt_block_reply`), the node applies it and **retransmits** to all other fork-aligned peers:

```cpp
// dlt_p2p_node.cpp
// Record that sender has this block (echo suppression)
state.record_known_block(reply.block.id());
// Retransmit to our-fork peers (with echo suppression)
send_to_all_our_fork_peers(message(dlt_block_reply_message(reply)), peer, reply.block.id());
```

The `peer` (sender) is excluded via the `exclude` parameter. Additionally, peers that already have this block in their `known_blocks` are skipped via echo suppression. The sender's `known_blocks` is updated so that if this block is later received from another peer, it won't be sent back to the original sender.

### Block Post-Validation Broadcast

`broadcast_block_post_validation()` sends a lightweight fork-status message (block ID + validator + signature) instead of the full block. This is called by the Validator Plugin for each block in the production round after validation completes.

---

## Transaction Broadcasting

### Self-Originated Transactions

When a transaction arrives via API (`network_broadcast_api`):

```cpp
// dlt_p2p_node.cpp
void dlt_p2p_node::broadcast_transaction(const signed_transaction& trx) {
    add_to_mempool(trx, /*from_peer=*/false, INVALID_PEER_ID);
    dlt_transaction_message msg;
    msg.trx = trx;
    dlog(DLT_LOG_DGRAY "Broadcasting transaction ${id} to fork peers" DLT_LOG_RESET,
         ("id", trx.id()));
    send_to_all_our_fork_peers(message(msg));  // NO exclude
}
```

The transaction is added to the local mempool and broadcast to **all** fork-aligned peers.

### Relaying Received Transactions

When a transaction arrives from a peer (via `on_dlt_transaction` → `add_to_mempool`):

```cpp
// dlt_p2p_node.cpp — in add_to_mempool()
// Retranslate to our-fork peers (if from peer)
if (from_peer && sender != INVALID_PEER_ID) {
    dlog(DLT_LOG_DGRAY "Relaying transaction ${id} to fork peers (excluding sender)" DLT_LOG_RESET,
         ("id", trx_id));
    dlt_transaction_message msg;
    msg.trx = trx;
    send_to_all_our_fork_peers(message(msg), sender);  // exclude sender
}
```

The sender is excluded; all other fork-aligned peers receive the relay.

### Transaction Diagnostic Logging

All transaction exchange events produce dark-gray `dlog` messages (visible at debug log level):

| Event | Log Message | Level |
|-------|-------------|-------|
| Self-originated send (API) | `Broadcasting transaction ${id} to fork peers` | `dlog` DGRAY |
| Peer relay (retransmit) | `Relaying transaction ${id} to fork peers (excluding sender)` | `dlog` DGRAY |
| Relay stats in `send_to_all_our_fork_peers` | `Relay transaction to ${e} peers (${nx} skipped: no_exchange, ${na} skipped: not_active)` | `dlog` DGRAY |
| Received from peer (new) | `Got transaction ${id} from peer ${ep}` | `dlog` DGRAY |
| Received duplicate | *Silent — no log emitted* | — |

Duplicate transactions (already in `_mempool_by_id`) are silently ignored in both `on_dlt_transaction` and `add_to_mempool`, same as duplicate blocks — no console spam.

### Mempool Validation (Pre-Forwarding)

Before a transaction is added to mempool or forwarded, it passes these checks:

| Check | Failure Action |
|-------|---------------|
| **Dedup** (`_mempool_by_id`) | Silently skip |
| **Expired** (`trx.expiration < now`) | Reject, increment spam strike if from peer |
| **Expiration too far** (>24h) | Reject, increment spam strike if from peer |
| **Too large** (>64KB) | Reject, increment spam strike if from peer |
| **TaPoS invalid** (ref block unknown) | Reject, increment spam strike if from peer |
| **Mempool full** | Evict oldest-expiry entry, retry |

During SYNC mode, accepted transactions are tagged `is_provisional = true` — they are stored but NOT forwarded to peers. On transition to FORWARD, provisional entries are revalidated (TaPoS check against current head) and the invalid ones are purged.

---

## SYNC→FORWARD Transition

The transition from SYNC to FORWARD is governed by `transition_to_forward()`:

```cpp
// dlt_p2p_node.cpp:1247
void dlt_p2p_node::transition_to_forward() {
    if (_node_status == DLT_NODE_STATUS_FORWARD) return;
    _node_status = DLT_NODE_STATUS_FORWARD;
    _sync_stagnation_retries = 0;
    // ...
}
```

### Transition Triggers

| Trigger | Location | Condition |
|---------|----------|------------|
| **Block range complete** | `on_dlt_block_range_reply()` | `is_last == true` AND `any_block_applied == true` (P20 guard) |
| **Sync catchup** | `check_sync_catchup()` | `our_head >= all_active_peer_heads` AND at least one active peer exists |
| **Stagnation timeout** | `sync_stagnation_check()` | 30s no-block, 3 retries exhausted → FORWARD with warning |

`check_sync_catchup()` is called from two places (P26 fix):
- `on_dlt_block_reply()` — after accepting a single block
- `periodic_task()` — every ~5 seconds

**Isolation guard (P53 fix):** `check_sync_catchup()` does NOT claim "caught up" when zero active peers exist. With no peers to compare against, the node cannot determine whether it has actually caught up. Instead, it tracks isolation via `_isolation_detected_time` and after 60 seconds calls `emergency_peer_reset()` to force reconnection. See [Peer Isolation Recovery](#peer-isolation-recovery) below.

### What Happens on Transition

1. **Notify all connected peers**: Send `dlt_fork_status_message` with `node_status=FORWARD` to ALL active/syncing peers (not just exchange-enabled). This lets peers know we're now in FORWARD mode so they can re-evaluate `exchange_enabled` for us.
2. **Re-evaluate `exchange_enabled` for all peers** (P25 fix): peers whose head block is now recognized (because we synced past it) get `exchange_enabled = true`
3. **Revalidate provisional mempool entries**: entries tagged during SYNC are checked for TaPoS validity against the current head; invalid ones are purged
4. **Reset stagnation retries**: counter set to 0 for clean slate

---

## FORWARD→SYNC Transition (P27)

| Trigger | Location | Condition |
|---------|----------|------------|
| **Peer ahead in hello_reply** | `on_dlt_hello_reply()` | `peer_head_num > our_head + FORWARD_FALLBEHIND_THRESHOLD` while in FORWARD mode |
| **Periodic fallbehind check** | `check_forward_behind()` | Any active peer is ahead by > `FORWARD_FALLBEHIND_THRESHOLD` (2) blocks |
| **FORWARD stagnation** | `check_forward_stagnation()` | Head stuck for 30s with active peers and at least one peer ahead → SYNC (P37). No peer ahead → reset stagnation timer, stay in FORWARD (P55). Isolated (no active peers) → emergency reset after 60s (P53) |

---

## The `exchange_enabled` Flag

`exchange_enabled` is the primary gatekeeper for forward-mode traffic. It is set during hello handshake, combined via logical OR in hello_reply, and re-evaluated at key lifecycle events.

### Initial Setting (Hello Handshake)

During `build_hello_reply()`, `check_fork_alignment()` determines whether the peer's head/LIB blocks are known to us:

| Check | Condition | Result |
|-------|-----------|--------|
| Empty peer (`head==0`) | Always | Aligned (new node) |
| Range overlap | `peer_head_num ∈ [our_earliest, our_latest]` | Use `is_block_known(head_id)` |
| Boundary link | `peer_head_num + 1 == our_earliest` | Check `our_earliest_block.previous == head_id` |
| LIB fallback | Always | `is_block_known(lib_id)` |

If any check passes → `exchange_enabled = true`, `fork_alignment = true`.

### P27 Fix: OR Combination in `on_dlt_hello_reply`

When two nodes connect, **both sides send hello messages** to each other. Each side computes its own `exchange_enabled` in `on_dlt_hello` (local determination), then receives the other side's determination in `on_dlt_hello_reply`.

**The bug (P27):** `on_dlt_hello_reply` used to **overwrite** `state.exchange_enabled` with the remote side's determination. This caused a critical failure:

```
Slave (head=79673001) connects to Master (head=79673101)

1. Master's on_dlt_hello:
   - check_fork_alignment(slave_head) → true (master has block 79673001)
   - state.exchange_enabled = true  ✅

2. Slave's on_dlt_hello:
   - check_fork_alignment(master_head) → false (slave doesn't have block 79673101)
   - state.exchange_enabled = false
   - Sends hello_reply with exchange_enabled=false to master

3. Master's on_dlt_hello_reply:
   - state.exchange_enabled = reply.exchange_enabled  (overwrite!)
   - state.exchange_enabled = false  ❌  BUG!
   - Master stops broadcasting blocks to slave!
```

**The fix:** Use `state.exchange_enabled = state.exchange_enabled || reply.exchange_enabled`. If **either** side considers the peer fork-aligned, exchange is enabled:
- If we think the peer is on our fork → we should send blocks to them
- If they think we're on their fork → we should receive blocks from them
- If both are false → truly different forks, no exchange

### Receiving FORWARD Transition from a Peer

When `on_dlt_fork_status()` receives a status update from a peer that just transitioned SYNC→FORWARD, the node re-evaluates `exchange_enabled` for that peer. The peer's head block may now be within our known chain, meaning we should enable block/transaction exchange with it.

### Re-Evaluation Triggers (P25 Fix)

The original implementation set `exchange_enabled` once and never updated it, causing slave-produced blocks to be ignored by the master. Re-evaluation points:

1. **`transition_to_forward()`**: Re-checks `is_block_known(peer_head_id)` for all peers with `exchange_enabled=false`
2. **`on_dlt_fork_status()`**: When a peer transitions SYNC→FORWARD, re-checks if the peer's head block is now known to us
3. **`on_dlt_block_range_reply()`**: When a non-exchange-enabled peer's block is ACCEPTED, enables exchange for that peer
4. **`on_dlt_block_reply()`**: Same — when a single block from a non-exchange-enabled peer is ACCEPTED

---

## FORWARD→SYNC Fallback (P27 Fix)

In FORWARD mode, blocks arrive via broadcast from fork-aligned peers. But if broadcast blocks are missed (e.g., connection dropped, `exchange_enabled` was incorrectly false), the node can fall behind with no recovery mechanism.

Two detection points were added:

### 1. In `on_dlt_hello_reply` (Reactive)

When a FORWARD node receives a hello_reply from a peer that is significantly ahead (`peer_head_num > our_head + FORWARD_FALLBEHIND_THRESHOLD`), it transitions to SYNC and requests the missing range:

```cpp
if (_node_status == DLT_NODE_STATUS_FORWARD) {
    uint32_t our_head = _delegate->get_head_block_num();
    if (state.peer_head_num > our_head + FORWARD_FALLBEHIND_THRESHOLD) {
        transition_to_sync();
        request_blocks_from_peer(peer);
    }
}
```

### 2. In `check_forward_behind()` (Periodic)

Called every ~5 seconds from `periodic_task()`. Iterates all active peers and checks if any is ahead by more than `FORWARD_FALLBEHIND_THRESHOLD` blocks:

```cpp
void dlt_p2p_node::check_forward_behind() {
    if (_node_status != DLT_NODE_STATUS_FORWARD) return;
    for (const auto& [id, state] : _peer_states) {
        if (state.peer_head_num > our_head + FORWARD_FALLBEHIND_THRESHOLD) {
            transition_to_sync();
            // Request blocks from ALL exchange-enabled ahead peers
            break;
        }
    }
}
```

The threshold is `FORWARD_FALLBEHIND_THRESHOLD = 2` (3+ blocks behind = ~9s at 3s/block). This avoids false triggers from normal 1-block broadcast latency.

---

## Peer Isolation Recovery (P53)

### Problem

When all peers are disconnected or banned (e.g., after a snapshot pause), the node becomes **isolated** — no active peer connections exist. This caused a SYNC↔FORWARD oscillation loop:

```
1. All peers DISC → check_sync_catchup() sees 0 active peers → all_caught_up=true → FORWARD
2. In FORWARD with no connections → check_forward_stagnation() after 30s → SYNC
3. In SYNC with no connections → check_sync_catchup() sees 0 active → FORWARD
4. Repeat forever, head never advances, backoffs never expire (up to 3600s)
```

The root cause: `check_sync_catchup()` treated zero active peers as "caught up" (vacuously true for the `all_caught_up` check), and `check_forward_stagnation()` transitioned to SYNC without any peer to request blocks from.

### Solution: Isolation Detection + Emergency Reset

A new field `_isolation_detected_time` tracks when isolation was first detected. After 60 seconds (`ISOLATION_RESET_SEC`) of continuous isolation, `emergency_peer_reset()` fires:

1. **Clears all soft bans** — BANNED peers are moved to DISCONNECTED state, `ban_reason` and `spam_strikes` are reset.
2. **Resets all backoffs** — Every DISCONNECTED peer gets `reconnect_backoff_sec = INITIAL_RECONNECT_BACKOFF_SEC` (30s) and `next_reconnect_attempt = now` (immediate).
3. **Resets stagnation counters** — `_sync_stagnation_retries` is cleared.
4. **Clears isolation timer** — `_isolation_detected_time` is reset so the timer can re-trigger if isolation recurs.

On the next `periodic_task()` tick (5 seconds), `periodic_reconnect_check()` will attempt immediate reconnection to all peers.

### Where Isolation Is Handled

| Function | Behavior When Isolated |
|----------|----------------------|
| `check_sync_catchup()` | Returns early (does not claim caught up). Starts isolation timer or calls `emergency_peer_reset()` after 60s. |
| `check_forward_stagnation()` | Returns early (does not transition to SYNC). Starts isolation timer or calls `emergency_peer_reset()` after 60s. |
| `emergency_peer_reset()` | Clears bans, resets backoffs, enables immediate reconnection. |

---

## FORWARD Stagnation When No Peer Is Ahead (P55)

A second oscillation pattern was observed when the head is stuck but no connected peer has a higher block number:

```
1. Head stuck at block N, all 5 peers also at block N → check_forward_stagnation() after 30s → SYNC
2. In SYNC, sync_stagnation_check() fires immediately (stale timer, see below)
3. check_sync_catchup() sees our_head >= all peers → FORWARD
4. Repeat — SYNC never actually requests or processes any blocks
```

**Two root causes:**

1. `transition_to_sync()` did not reset `_last_block_received_time`, so the sync stagnation timer inherited the stale timestamp from the last block received in FORWARD mode (~30s ago). `sync_stagnation_check()` fired on the very next periodic tick.

2. `check_forward_stagnation()` transitioned to SYNC even when no peer was ahead. With no peer to sync from, SYNC mode was useless — `check_sync_catchup()` immediately returned to FORWARD.

**Fixes:**

1. `transition_to_sync()` now resets `_last_block_received_time = fc::time_point::now()`, giving the sync phase a full 30s window.

2. `check_forward_stagnation()` now checks for peers ahead before transitioning to SYNC. If no peer has `peer_head_num > our_head`, the function resets the stagnation timer and stays in FORWARD instead of oscillating.

---

## What Does NOT Get Forwarded

### Peers on a Different Fork

Peers whose `exchange_enabled=false` (fork not aligned) receive nothing. This is by design — blocks and transactions from one fork are meaningless on another.

### SYNC-Mode Nodes

While in SYNC mode, a node does NOT broadcast blocks or relay transactions to peers. The only outbound traffic is block range requests (`dlt_get_block_range_message`) and gap fill requests (`dlt_gap_fill_request`). Gap fill works in both SYNC and FORWARD modes — in SYNC mode, it provides an alternative path to request missing blocks when `request_blocks_from_peer()` cannot bridge a gap (e.g., blocks below the syncing peer's DLT range). Large gaps are served in 100-block chunks.

### Block Processing Paused

When `_block_processing_paused == true` (snapshot in progress), periodic tasks skip DB-accessing operations, but the node can still receive and broadcast blocks. The flag primarily prevents sync-stagnation false positives and lock contention.

---

## Comparison: Old Graphene vs. DLT Forward Mode

| Aspect | Old Graphene (`node.cpp`) | DLT (`dlt_p2p_node.cpp`) |
|--------|--------------------------|--------------------------|
| **Broadcast trigger** | Inventory gossip → peer requests items | Direct push of full block/transaction |
| **Filtering** | `peer_needs_sync_items_from_us` / `we_need_sync_items_from_peer` per-peer flags | `exchange_enabled` (fork alignment) + `lifecycle_state == ACTIVE` |
| **Block format** | `block_message` with synopsis negotiation | `dlt_block_reply_message` — full block, always |
| **Transaction format** | `trx_message` via inventory | `dlt_transaction_message` — direct push |
| **Relay exclusion** | Complex inventory tracking | Simple `exclude` parameter on `send_to_all_our_fork_peers` |
| **Mempool** | Chain's `_pending_tx` | Separate P2P mempool with expiry/TaPoS/size filtering |
| **Anti-spam** | Multiple counters (`unlinkable_block_strikes`, `sync_spam_strikes`, etc.) | Single `spam_strikes` counter, reset on good packet |

---

## Summary: Who Gets What in Forward Mode

| Event | Sent to | Excluded | Echo Filtered |
|-------|---------|----------|---------------|
| Node produces a block | All ACTIVE peers with `exchange_enabled=true` | *none* | Peers that already have the block (from a previous relay) |
| Node originates a transaction | All ACTIVE peers with `exchange_enabled=true` | *none* | N/A |
| Node receives a block from peer X | All ACTIVE peers with `exchange_enabled=true` | X | Peers that already have the block |
| Node receives a transaction from peer X | All ACTIVE peers with `exchange_enabled=true` | X | N/A |
| Peer has `exchange_enabled=false` | *nothing* | — | — |
| Node is in SYNC mode | *nothing* (only range requests and gap fill) | - | - |

---

## Peer Stats: Stale `peer_head_num` Caveat

The `peer_head_num` shown in the P2P stats table is **not real-time**. It is a snapshot from the last communication event:

| Update Source | When `peer_head_num` Gets Updated |
|-------------|----------------------------------|
| `dlt_hello_message` | Initial handshake when connection is established |
| `dlt_fork_status_message` | Periodic fork_status exchanges between peers |
| `dlt_block_reply_message` | Updated if a peer sends us block #N, its head must be ≥ N |

Between these events, the peer's actual chain head may advance significantly (e.g., the peer produces or receives blocks via broadcast). **Do not treat `peer_head_num` in the stats table as the peer's current chain state.** It is useful for relative ordering and sync progress estimation, but not as a real-time block height monitor.

---

## Relevant Source Files

| File | Content |
|------|---------|
| `libraries/network/dlt_p2p_node.cpp` | `broadcast_block()`, `broadcast_transaction()`, `send_to_all_our_fork_peers()`, `transition_to_forward()`, `check_sync_catchup()`, `check_forward_behind()`, `check_forward_stagnation()`, `emergency_peer_reset()`, `add_to_mempool()` |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | `dlt_p2p_node` class declaration, `dlt_node_status` enum |
| `libraries/network/include/graphene/network/dlt_p2p_messages.hpp` | `dlt_block_reply_message`, `dlt_transaction_message`, `dlt_message_type_enum` |
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp` | `dlt_peer_state` (contains `exchange_enabled`, `fork_alignment`, `known_blocks` for echo suppression) |
| `plugins/p2p/p2p_plugin.cpp` | `dlt_delegate::accept_block()`, `p2p_plugin::broadcast_block()`, `p2p_plugin::broadcast_transaction()` |
| `plugins/validator/validator.cpp` | Calls `p2p().broadcast_block()` after block production |

---

## Related Documents

- [DLT P2P Network Redesign](./dlt-p2p-network-redesign.md) — Full implementation overview
- [DLT 4-Node Sync Scenarios](./dlt-4-node-sync-scenarios.md) — Problem analysis for SYNC/FORWARD edge cases
- [P2P Sync Workflow](./p2p-sync-workflow.md) — Old Graphene synopsis-based sync (pre-DLT, for comparison)
