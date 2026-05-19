# Forward Mode — Block and Transaction Exchange

Forward mode (`DLT_NODE_STATUS_FORWARD`) is the normal operating state once a node has caught up with the network. Instead of pulling block ranges from peers, the node **pushes** new blocks and transactions to all fork-aligned peers as they arrive.

---

## Delivery Gate: `exchange_enabled`

All forward-mode traffic is filtered by two flags per peer:

| Flag | What it means |
|------|--------------|
| `exchange_enabled == true` | Peer is fork-aligned — its head block is known to us (or ours to it) |
| `lifecycle_state == ACTIVE` | Peer has completed handshake |

Both must be true for a peer to receive block and transaction broadcasts.

The central broadcast function, `send_to_all_our_fork_peers()`, iterates all connected peers and skips those that fail either check.

---

## Setting `exchange_enabled`

### Initial setting (hello handshake)

During hello, the acceptor calls `check_fork_alignment()` — a multi-tier DLT-range-aware check:

| Case | Check |
|------|-------|
| Peer has no blocks (`head_num == 0`) | → aligned |
| Peer head in our DLT range | `is_block_known(peer.head_id)` |
| Peer head + 1 == our DLT earliest | Read our earliest block; verify `previous == peer.head_id` |
| LIB fallback | `is_block_known(peer.lib_id)` |

If any check passes → `exchange_enabled = true`.

### OR combination

Both sides send hello messages to each other and each independently computes `exchange_enabled`. The final value for a peer is the **logical OR** of both sides' determinations. If either side recognizes the other's chain, exchange is enabled.

A slave whose head is behind the master's DLT range fails its own `check_fork_alignment` (it hasn't yet applied the master's blocks), but the master's check succeeds (it knows the slave's head). The OR ensures exchange is enabled even in this asymmetric case.

### Re-evaluation triggers

`exchange_enabled` is re-evaluated whenever the node's knowledge of peer blocks changes:

| Trigger | When |
|---------|------|
| `transition_to_forward()` | On every peer with `exchange_enabled=false`; re-checks `is_block_known(peer_head_id)` |
| `on_dlt_fork_status()` | Peer transitions SYNC → FORWARD; re-checks fork alignment |
| Block accepted from peer | If block applies to our chain, immediately enables exchange for that peer |

---

## Block Broadcasting

### Self-produced blocks

When a validator produces a block:

```
validator.cpp → p2p_plugin.broadcast_block(block)
              → dlt_p2p_node.broadcast_block(block)
              → send_to_all_our_fork_peers(dlt_block_reply_message, exclude=none, block_id=block.id())
```

The block is sent to **all** ACTIVE exchange-enabled peers. Echo suppression prevents re-sending to peers that already have the block.

### Relaying received blocks

When a block arrives from peer X:

1. Record that X has this block (`state.record_known_block(block.id())`).
2. Apply the block to the chain.
3. `send_to_all_our_fork_peers(block_reply, exclude=X, block_id=block.id())` — sends to all other exchange-enabled ACTIVE peers.

---

## Block Echo Suppression

Without suppression, blocks loop back to their producer through relay chains:

```
A produces block N → sends to B, C
B relays N to A, C
C relays N to A, B
A receives its own block N back from B and C — wasted bandwidth
```

Each peer state maintains a **ring buffer of 20 recent block IDs** (`known_blocks`). Before sending a block to a peer, the node checks `peer.has_block(block_id)`. If already known, the send is skipped.

A peer is recorded as "having" a block in two cases:
- **We just sent it to them** — recorded in `send_to_all_our_fork_peers` after the send.
- **They sent it to us** — recorded in `on_dlt_block_reply` on receipt.

The relay log shows echo-filtered counts:
```
Relay block_reply to 3 peers (0 skipped: no_exchange, 0 skipped: not_active, 1 skipped: echo)
```

---

## Transaction Broadcasting

### Self-originated (via API)

Transaction submitted via `network_broadcast_api` → added to P2P mempool → `dlt_transaction_message` sent to all exchange-enabled ACTIVE peers.

### Relaying received transactions

Transaction arrives from peer X → added to mempool → relayed to all exchange-enabled ACTIVE peers **except X**.

### Mempool pre-filter

Before a transaction is accepted into the mempool or forwarded, it must pass:

| Check | Failure |
|-------|---------|
| Duplicate (`trx_id` already in mempool) | Silently skip |
| Expired (`expiration < now`) | Reject; increment spam strike if from peer |
| Expiration too far (>24 h in future) | Reject; increment spam strike |
| Oversized (>`dlt-mempool-max-tx-size`, default 64 KB) | Reject; increment spam strike |
| TaPoS invalid (reference block unknown) | Reject; increment spam strike |
| Mempool full | Evict oldest-expiry entry, then add |

**Provisional entries:** Transactions received during SYNC mode are tagged `is_provisional = true` — stored locally but not forwarded to peers. On transition to FORWARD, provisional entries are revalidated against the current head and invalid ones are purged.

---

## SYNC → FORWARD Transition

### Triggers

| Trigger | Condition |
|---------|-----------|
| Block range reply with `is_last=true` | AND at least one block was applied (not all dead-fork) |
| `check_sync_catchup()` | `our_head >= all active peer heads` AND at least one active peer |
| Stagnation timeout | 30 s without a block, 3 retries exhausted |

`check_sync_catchup()` runs after each block acceptance and every 5 seconds from the periodic task.

**Isolation guard:** `check_sync_catchup()` does NOT claim caught up when zero active peers exist. Instead it starts a 60-second isolation timer; after expiry, `emergency_peer_reset()` fires (see below).

### Actions on transition

1. Notify all connected peers: broadcast `dlt_fork_status_message` with `node_status=FORWARD` to every active/syncing peer (not just exchange-enabled ones). This lets peers re-evaluate `exchange_enabled` for us immediately.
2. Re-evaluate `exchange_enabled` for all peers.
3. Revalidate and purge invalid provisional mempool entries.
4. Reset `_sync_stagnation_retries = 0`.
5. Reset `_last_block_received_time = now` so the forward stagnation timer starts fresh.

---

## FORWARD → SYNC Fallback

If blocks stop arriving in FORWARD mode, the node falls back to SYNC:

| Trigger | Condition |
|---------|-----------|
| Hello reply shows peer far ahead | `peer_head_num > our_head + 2` on receiving hello_reply |
| Periodic check | `check_forward_behind()`: any active peer has `peer_head_num > our_head + 2` (skipped for 15 s after entering FORWARD) |
| Stagnation | `check_forward_stagnation()`: head stuck for 30 s AND at least one peer is ahead |

**No-op when no peer is ahead:** `check_forward_stagnation()` does NOT transition to SYNC when all connected peers have the same head. There is nothing to sync from; transitioning would just cause oscillation. The stagnation timer resets and the node stays in FORWARD.

On transition to SYNC, `_last_block_received_time` is reset to `now` so the sync stagnation timer starts fresh (not inherited from the FORWARD phase).

---

## Peer Isolation Recovery

When all peers are disconnected or banned (e.g., after a snapshot pause), the normal SYNC/FORWARD mode transitions loop pointlessly. After **60 seconds** with zero active connections:

`emergency_peer_reset()`:
1. Moves all BANNED peers back to DISCONNECTED state; clears `spam_strikes`.
2. Resets all DISCONNECTED peers' backoffs to 30 s (`INITIAL_RECONNECT_BACKOFF_SEC`) with `next_reconnect_attempt = now`.
3. Clears stagnation retry counters.
4. On the next periodic task tick (~5 s), `periodic_reconnect_check()` immediately reconnects.

---

## What Is Not Forwarded

| Scenario | Traffic |
|----------|---------|
| Peer has `exchange_enabled=false` | No blocks, no transactions |
| Node is in SYNC mode | No broadcasts; only range requests and gap fill requests |
| Block processing paused (`_block_processing_paused=true`) | Blocks are received and queued but periodic DB-accessing tasks are skipped |

---

## Delivery Summary

| Event | Recipients | Excluded | Echo-filtered |
|-------|-----------|----------|--------------|
| Node produces block | All ACTIVE `exchange_enabled=true` peers | (none) | Peers with block in `known_blocks` |
| Node receives block from X | All ACTIVE `exchange_enabled=true` peers | X | Peers with block in `known_blocks` |
| Node originates transaction | All ACTIVE `exchange_enabled=true` peers | (none) | (none) |
| Node receives transaction from X | All ACTIVE `exchange_enabled=true` peers | X | (none) |

---

## `peer_head_num` Is a Stale Snapshot

The `peer_head_num` shown in [stats](./stats-reference.md) is updated from:
- hello handshake
- `dlt_fork_status_message` exchanges
- Block relay (receiving block N implies `peer_head_num ≥ N`)

Between these events the peer's actual chain head may be significantly higher. Do not treat `peer_head_num` as real-time.

---

See also: [P2P Overview](./overview.md), [Sync Scenarios](./sync-scenarios.md), [Stats Reference](./stats-reference.md), [Messages](./messages.md).
