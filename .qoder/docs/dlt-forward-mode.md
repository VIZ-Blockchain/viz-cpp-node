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
// dlt_p2p_node.cpp:358
void dlt_p2p_node::send_to_all_our_fork_peers(const message& msg, peer_id exclude = INVALID_PEER_ID) {
    for (auto& [id, state] : _peer_states) {
        if (id == exclude) continue;
        if (state.exchange_enabled && state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE) {
            send_message(id, msg);
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

---

## Block Broadcasting

### Self-Produced Blocks

When a witness produces a block, the flow is:

```
witness.cpp:1081    p2p().broadcast_block(block)
  → p2p_plugin.cpp:482   my->node->broadcast_block(block)
    → dlt_p2p_node.cpp:1117
```

```cpp
void dlt_p2p_node::broadcast_block(const signed_block& block) {
    dlt_block_reply_message reply;
    reply.block = block;
    reply.next_available = 0;
    reply.is_last = true;
    send_to_all_our_fork_peers(message(reply));  // NO exclude — send to EVERYONE
}
```

The block goes to **all** ACTIVE peers with `exchange_enabled=true`. There is no self-filtering — the producing node broadcasts its own block to all fork-aligned peers unconditionally.

### Relaying Received Blocks

When a block arrives from a peer (via `on_dlt_block_reply`), the node applies it and **retransmits** to all other fork-aligned peers:

```cpp
// dlt_p2p_node.cpp:965
// Retransmit to our-fork peers
send_to_all_our_fork_peers(message(dlt_block_reply_message(reply)), peer);
```

The `peer` (sender) is excluded to prevent echo. All other ACTIVE fork-aligned peers receive the relay.

### Block Post-Validation Broadcast

`broadcast_block_post_validation()` sends a lightweight fork-status message (block ID + witness + signature) instead of the full block. This is called by the witness plugin for each block in the production round after validation completes.

---

## Transaction Broadcasting

### Self-Originated Transactions

When a transaction arrives via API (`network_broadcast_api`):

```cpp
// dlt_p2p_node.cpp:1137
void dlt_p2p_node::broadcast_transaction(const signed_transaction& trx) {
    add_to_mempool(trx, /*from_peer=*/false, INVALID_PEER_ID);
    dlt_transaction_message msg;
    msg.trx = trx;
    send_to_all_our_fork_peers(message(msg));  // NO exclude
}
```

The transaction is added to the local mempool and broadcast to **all** fork-aligned peers.

### Relaying Received Transactions

When a transaction arrives from a peer (via `on_dlt_transaction_message` → `add_to_mempool`):

```cpp
// dlt_p2p_node.cpp:1413-1417
// Retranslate to our-fork peers (if from peer)
if (from_peer && sender != INVALID_PEER_ID) {
    dlt_transaction_message msg;
    msg.trx = trx;
    send_to_all_our_fork_peers(message(msg), sender);  // exclude sender
}
```

The sender is excluded; all other fork-aligned peers receive the relay.

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
| **Sync catchup** | `check_sync_catchup()` | `our_head >= all_active_peer_heads` |
| **Stagnation timeout** | `sync_stagnation_check()` | 30s no-block, 3 retries exhausted → FORWARD with warning |

`check_sync_catchup()` is called from two places (P26 fix):
- `on_dlt_block_reply()` — after accepting a single block
- `periodic_task()` — every ~5 seconds

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

## What Does NOT Get Forwarded

### Peers on a Different Fork

Peers whose `exchange_enabled=false` (fork not aligned) receive nothing. This is by design — blocks and transactions from one fork are meaningless on another.

### SYNC-Mode Nodes

While in SYNC mode, a node does NOT broadcast blocks or relay transactions to peers. The only outbound traffic is block range requests (`dlt_get_block_range_message`).

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

| Event | Sent to | Excluded |
|-------|---------|----------|
| Node produces a block | All ACTIVE peers with `exchange_enabled=true` | *none* |
| Node originates a transaction | All ACTIVE peers with `exchange_enabled=true` | *none* |
| Node receives a block from peer X | All ACTIVE peers with `exchange_enabled=true` | X |
| Node receives a transaction from peer X | All ACTIVE peers with `exchange_enabled=true` | X |
| Peer has `exchange_enabled=false` | *nothing* | — |
| Node is in SYNC mode | *nothing* (only range requests) | — |

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
| `libraries/network/dlt_p2p_node.cpp` | `broadcast_block()`, `broadcast_transaction()`, `send_to_all_our_fork_peers()`, `transition_to_forward()`, `check_sync_catchup()`, `check_forward_behind()`, `add_to_mempool()` |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | `dlt_p2p_node` class declaration, `dlt_node_status` enum |
| `libraries/network/include/graphene/network/dlt_p2p_messages.hpp` | `dlt_block_reply_message`, `dlt_transaction_message`, `dlt_message_type_enum` |
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp` | `dlt_peer_state` (contains `exchange_enabled`, `fork_alignment`) |
| `plugins/p2p/p2p_plugin.cpp` | `dlt_delegate::accept_block()`, `p2p_plugin::broadcast_block()`, `p2p_plugin::broadcast_transaction()` |
| `plugins/witness/witness.cpp` | Calls `p2p().broadcast_block()` after block production |

---

## Related Documents

- [DLT P2P Network Redesign](./dlt-p2p-network-redesign.md) — Full implementation overview
- [DLT 4-Node Sync Scenarios](./dlt-4-node-sync-scenarios.md) — Problem analysis for SYNC/FORWARD edge cases
- [P2P Sync Workflow](./p2p-sync-workflow.md) — Old Graphene synopsis-based sync (pre-DLT, for comparison)
