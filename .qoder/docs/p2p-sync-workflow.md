# P2P Synchronization & Block Push Workflow

## Overview

The VIZ P2P network uses two distinct modes for block propagation between nodes:

1. **Sync Mode** — Active pull-based synchronization for catching up with the network
2. **Broadcast/Inventory Mode** — Passive push-based delivery of new blocks in real-time

A node transitions from sync mode to broadcast mode once it catches up to the network head. Both modes operate per-peer — a node can be syncing from one peer while receiving broadcasts from another.

---

## Architecture

### Key Components

| Component | File | Role |
|-----------|------|------|
| `node_impl` | `libraries/network/node.cpp` | Core P2P engine: sync state machine, message handlers, peer management |
| `peer_connection` | `libraries/network/include/graphene/network/peer_connection.hpp` | Per-peer state: sync flags, item queues, soft-ban timers |
| `p2p_plugin_impl` | `plugins/p2p/p2p_plugin.cpp` | Bridge between P2P network and blockchain: block handling, sync initiation |
| `chain::database` | `libraries/chain/database.cpp` | Blockchain state: push_block, fork_db, block validation |

### Per-Peer Sync State Flags

Each `peer_connection` carries two critical boolean flags:

```
peer_needs_sync_items_from_us   — Does the peer need blocks from us? (controls our OUTBOUND sync)
we_need_sync_items_from_peer    — Do we need blocks from them? (controls our INBOUND sync)
```

These flags determine which protocol mode is active for each peer.

---

## Mode 1: Sync Mode (Pull-Based)

### When Active

`we_need_sync_items_from_peer = true`

Active when:
- A new peer connection is established (`new_peer_just_added()` → `start_synchronizing_with_peer()`)
- After minority fork recovery (`resync()` resets all peers)
- When a peer is detected to be ahead of us

### Flow

```
Our Node                              Peer
   |                                    |
   |  1. fetch_blockchain_item_ids      |
   |  (our blockchain synopsis)         |
   |----------------------------------->|
   |                                    |
   |  2. blockchain_item_ids_inventory  |
   |  (list of block IDs + remaining)   |
   |<-----------------------------------|
   |                                    |
   |  3. fetch_items_message            |
   |  (request actual block data)       |
   |----------------------------------->|
   |                                    |
   |  4. block_message                  |
   |  (signed block data)               |
   |<-----------------------------------|
   |                                    |
   |  [repeat 3-4 for each block]       |
   |                                    |
   |  5. fetch_blockchain_item_ids      |
   |  (request next batch of IDs)       |
   |----------------------------------->|
   |                                    |
   |  [repeat until remaining=0]        |
```

### Detailed Steps

#### Step 1: Build and Send Synopsis
**Function:** `fetch_next_batch_of_item_ids_from_peer()` (node.cpp L2748)

The node builds a "blockchain synopsis" — a logarithmically-spaced list of block IDs from its chain — and sends it to the peer via `fetch_blockchain_item_ids_message`. The synopsis allows the peer to find the most recent common block efficiently.

#### Step 2: Receive Block ID Inventory
**Handler:** `on_blockchain_item_ids_inventory_message()` (node.cpp L2787)

The peer responds with:
- `item_hashes_available` — sequential list of block IDs the peer has after the common point
- `total_remaining_item_count` — how many more block IDs the peer can provide beyond this batch

The node validates the response (sequential block numbers, valid link to synopsis) and adds unknown block IDs to `peer->ids_of_items_to_get`.

#### Step 3-4: Fetch Actual Blocks
**Function:** `fetch_sync_items_loop()` (node.cpp ~L1100)

Runs periodically. For each peer with `we_need_sync_items_from_peer = true` and `inhibit_fetching_sync_blocks = false`:
- Picks blocks from `ids_of_items_to_get` that aren't already requested from other peers
- Sends `fetch_items_message` to request the block data
- Tracks requests in `_active_sync_requests` to avoid duplicate fetches

#### Step 5: Batch Continuation
If `total_remaining_item_count > 0`, the node sends another `fetch_blockchain_item_ids_message` to get the next batch of block IDs. This continues until the peer reports `remaining = 0`.

### Sync Completion

When the peer responds with `remaining = 0` and all offered items are already known:

```cpp
// node.cpp L2967 or L3181
originating_peer->we_need_sync_items_from_peer = false;
```

The node transitions this peer to **broadcast mode**.

---

## Mode 2: Broadcast/Inventory Mode (Push-Based)

### When Active

`we_need_sync_items_from_peer = false`

Active after sync completes — the node is caught up and receives new blocks in real-time.

### Flow: Block Production and Propagation

```
Witness Node                    Peer A                     Peer B (us)
     |                            |                            |
     | 1. generate_block()        |                            |
     | 2. broadcast_block()       |                            |
     |--------------------------->|                            |
     |                            |                            |
     |                            | 3. item_ids_inventory_msg  |
     |                            | ("I have block #N")        |
     |                            |--------------------------->|
     |                            |                            |
     |                            | 4. fetch_items_message     |
     |                            | ("send me block #N")       |
     |                            |<---------------------------|
     |                            |                            |
     |                            | 5. block_message           |
     |                            | (signed block data)        |
     |                            |--------------------------->|
     |                            |                            |
     |                            |                6. push_block()
     |                            |                7. broadcast to
     |                            |                   other peers
```

### Detailed Steps

#### Step 1-2: Block Production
A witness node produces a block and calls `p2p_plugin::broadcast_block()`, which sends the block to all connected peers via `node::broadcast()`.

#### Step 3: Inventory Advertisement
When a peer receives a new block (either via broadcast or sync), it advertises it to all its OTHER connected peers via `item_ids_inventory_message`. This is the gossip protocol — blocks propagate through the network hop by hop.

#### Step 4-5: Block Request and Delivery
**Handler:** `on_item_ids_inventory_message()` (node.cpp L3339)

When we receive an inventory message:
1. Check we're NOT in sync mode (skip if `we_need_sync_items_from_peer = true`)
2. Check no global sync in progress (skip if ANY peer has sync flag)
3. Check our head block is recent (skip if >30 seconds behind)
4. For each advertised item we don't have, request it via `fetch_items_message`
5. Peer responds with `block_message` containing the actual block

#### Step 6-7: Block Application
**Handler:** `p2p_plugin_impl::handle_block()` (p2p_plugin.cpp L145)

The block is pushed to the chain via `chain.accept_block()` → `database::push_block()`. If accepted, the node broadcasts it to its other peers, continuing propagation.

### Broadcast Gate (Critical)

At L3358 of node.cpp:
```cpp
if (originating_peer->we_need_sync_items_from_peer) {
    // skip broadcast inventory — we're syncing from this peer
    return;
}
```

**Broadcast inventory is completely suppressed during sync mode.** This prevents the 1-second inactivity timeout from killing sync connections (requesting tip-of-chain items during sync would time out before they arrive, disconnecting the peer).

---

## Peer Blocking Mechanisms

### Soft-Ban (`fork_rejected_until`)

A time-based ban that silently discards incoming sync requests from the peer:

```cpp
// node.cpp L2462
if (originating_peer->fork_rejected_until > fc::time_point::now()) {
    // silently discard sync request
    return;
}
```

**Triggered by:**
- 50 competing-fork sync spam strikes → 300 second ban (L2600, L2637)
- 20 unlinkable block strikes → dynamic duration ban (L3721, L3837)
- Peer on a dead/old fork → dynamic duration ban (L3823)
- Item not available from peer → 30 second ban (L3311, L3325)

### Sync Inhibition (`inhibit_fetching_sync_blocks`)

Prevents fetching sync blocks from a specific peer without fully banning them:

```cpp
// node.cpp L1158
if (!peer->inhibit_fetching_sync_blocks) {
    // fetch sync blocks from this peer
}
```

**Triggered by:**
- Peer can't advance our sync (returns only known blocks) — L3152
- Item not available from peer — L3310, L3324
- During soft-ban (always set alongside `fork_rejected_until`)

### 30-Second Stuck Flag Auto-Clear

**Location:** `terminate_inactive_connections_loop()` (node.cpp L1547-1556)

Runs every 1 second. If `peer_needs_sync_items_from_us = true` but the peer hasn't sent a sync request in 30+ seconds, auto-clears the flag to `false`. This prevents inventory starvation when a race condition leaves the flag stuck.

**Important:** There is NO equivalent auto-clear for `we_need_sync_items_from_peer`. If this flag gets stuck at `false` or the node needs to re-enter sync mode, only `start_synchronizing_with_peer()` or `resync()` can set it back to `true`.

---

## Minority Fork Recovery

### Detection

In `witness_plugin::impl::maybe_produce_block()` (witness.cpp), if the last 21 blocks in `fork_db` were ALL produced by the node's own configured witnesses, the node is likely on a minority fork (isolated from the network).

### Recovery Flow

```
1. MINORITY FORK DETECTED
   ↓
2. resync_from_lib()  (p2p_plugin.cpp)
   ├── Pop all reversible blocks back to LIB
   ├── Reset fork_db, seed with LIB block
   ├── node->sync_from(LIB block ID)
   ├── node->resync()  ← full peer state reset
   └── Reconnect seed nodes
   ↓
3. _production_enabled = false
   ↓
4. Production loop returns not_synced every 250ms
   (waiting for get_slot_time(1) >= now)
   ↓
5. P2P sync delivers blocks from peers
   (head advances toward real time)
   ↓
6. Once head catches up: get_slot_time(1) >= now
   → _production_enabled = true
   ↓
7. Block production resumes
```

### Full Peer State Reset

The peer state reset logic lives in `node_impl::reset_active_peer_states()` (node.cpp) and is shared by two callers:

1. **`resync()`** — called during minority fork recovery via `resync_from_lib()`. Resets all peer state, clears `_active_sync_requests`, then calls `start_synchronizing()`.
2. **`reconnect_seeds()`** — called by the witness plugin when producing a block with <2 peers. Resets all peer state, then force-reconnects seed nodes.

The reset clears:

```
For each active peer:
  - fork_rejected_until = epoch     (lift soft-ban)
  - unlinkable_block_strikes = 0    (clear strike counter)
  - sync_spam_strikes = 0           (clear spam counter)
  - inhibit_fetching_sync_blocks = false
  - peer_needs_sync_items_from_us = true
  - we_need_sync_items_from_peer = true
  - Clear: ids_of_items_to_get, ids_of_items_being_processed,
           sync_items_requested_from_peer
  - Reset: last_block_delegate_has_seen
```

This ensures no stale soft-bans, strike counters, or "already synced" markers prevent the node from re-syncing with available peers.

---

## Connection Retry & Seed Reconnection

### Connection Loop

`p2p_network_connect_loop()` (node.cpp) runs continuously, every **10 seconds**. It:

1. Processes `_add_once_node_list` — priority peers (seeds added via `add_node()`) that bypass connection limits
2. Checks `is_wanting_new_connections()` — true if `active_connections < desired_connections` (default 20)
3. Iterates `_potential_peer_db` with exponential backoff: `(failed_attempts + 1) * 30s`
4. Skips peers in disconnect cooldown (30 seconds after disconnect)

### Backoff Cap

`number_of_failed_connection_attempts` is capped at `GRAPHENE_NET_MAX_FAILED_CONNECTION_ATTEMPTS` (5) in `config.hpp`. This limits the maximum retry delay to `(5+1) * 30 = 180 seconds = 3 minutes`.

When a peer reaches the maximum failure count and still fails to connect, an info-level log is emitted:
```
P2P seed node <ip:port> not responding (5 consecutive failures), check config and remove if not needed
```

### Low-Peer Seed Reconnection

The witness plugin checks the connection count after each successfully produced block. If fewer than 2 peers are connected:

```
1. Witness produces block, broadcasts it
2. Check: get_connections_count() < 2?
3. YES → p2p_plugin::reconnect_seeds()
   ├── node->reset_active_peer_states()  (clear all blocking state)
   └── For each seed: add_node() + connect_to_endpoint()
       (bypasses exponential backoff via timer reset)
```

`add_node()` resets the `last_connection_attempt_time` to allow immediate retry, and adds the peer to `_add_once_node_list` for priority processing in the next connect loop iteration. This means the node retries seeds every block interval (~3 seconds) when isolated, rather than waiting for backoff.

---

## State Diagram

```
                    ┌─────────────────────────┐
                    │   Connection Established │
                    └────────────┬────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────┐
                    │  start_synchronizing_    │
                    │  with_peer()             │
                    │  we_need_sync = true     │
                    └────────────┬────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────┐
                    │     SYNC MODE           │
                    │  Pull block IDs + data  │◄──── resync() on
                    │  from peer              │      minority fork
                    └────────────┬────────────┘      recovery
                                 │
                                 │ remaining=0 &&
                                 │ all items known
                                 ▼
                    ┌─────────────────────────┐
                    │  we_need_sync = false    │
                    └────────────┬────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────┐
                    │   BROADCAST MODE        │
                    │  Receive inventory ads   │
                    │  Request unknown blocks  │
                    └─────────────────────────┘
```

---

## DLT Mode Considerations

In DLT mode (node loaded from snapshot), several sync behaviors are adjusted:

1. **Block serving is clamped** to the available range (dlt_block_log + fork_db). The node won't advertise blocks it can't serve.
2. **Synopsis matching** tolerates gaps between the anchor block and continuation blocks (a DLT node may not have all historical blocks).
3. **Peer ahead detection**: If all peer synopsis entries are above our head, return empty (peer is ahead, not on a fork).
4. **Broadcast inventory** is suppressed when head block is >30 seconds behind real time, even if no peer has `we_need_sync_items_from_peer = true`.

---

## Stale Sync Detection

A background safety mechanism that detects when the node has stopped receiving blocks from the network and automatically triggers recovery.

### Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `p2p-stale-sync-detection` | `true` | Enable/disable the feature |
| `p2p-stale-sync-timeout-seconds` | `120` | Seconds without any block before triggering recovery |

### How It Works

A scheduled task runs every **30 seconds** (`stale_sync_check_task()` in p2p_plugin.cpp L935):

1. Computes `elapsed = now - _last_block_received_time`
2. If `elapsed > timeout` (default 120s = 2 minutes):

```
Stale sync detected!
  │
  ├── 1. Get LIB number from database
  ├── 2. node->sync_from(LIB block ID)     ← reset sync start point
  ├── 3. node->resync()                    ← full peer state reset + start_synchronizing()
  ├── 4. For each seed node:
  │       add_node() + connect_to_endpoint()  ← force reconnect
  └── 5. Reset _last_block_received_time    ← prevent immediate retry
```

3. Reschedules itself for another check in 30 seconds

### Timer Reset Points

The `_last_block_received_time` is reset to `now` in these situations:

| Location | When |
|----------|------|
| `handle_block()` (L148) | Every time a block is received from any peer |
| `stale_sync_check_task()` (L984) | After recovery triggers (prevents immediate re-trigger) |
| `resync_from_lib()` (L1361) | After minority fork recovery |
| `trigger_resync()` (L1422) | After snapshot hot-reload |
| Plugin startup (L1236) | Initial value when node starts |

### Interaction with Other Recovery Mechanisms

The stale sync detector acts as a **last-resort safety net**. It complements:

- **Minority fork detection** (witness plugin) — triggers faster (after 21 own-witness blocks), but only if the node is actively producing. Stale sync covers the case where the node is NOT a witness or production is already disabled.
- **Low-peer seed reconnection** (witness plugin) — triggers per-block when <2 peers, but only while producing. Stale sync covers periods when production is halted.
- **Connection loop backoff** (node.cpp) — handles normal reconnection with exponential backoff. Stale sync overrides this by calling `resync()` which does a full peer state reset + `add_node()` on seeds.
- **Snapshot stalled sync detection** (snapshot plugin) — a separate, heavier mechanism described below.

---

## Snapshot Stalled Sync Detection

A separate stalled sync detector lives in the **snapshot plugin** (`plugins/snapshot/plugin.cpp`). Unlike the P2P-level detector (which resets sync and reconnects seeds), this one downloads a **newer snapshot** from trusted peers — a much heavier recovery action designed for DLT mode nodes that are hopelessly behind.

### Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `enable-stalled-sync-detection` | `false` | Enable/disable snapshot-level stall detection |
| `stalled-sync-timeout-minutes` | `5` | Minutes without any block before triggering snapshot re-download |
| `trusted-snapshot-peer` | (none) | Trusted peer endpoints for snapshot download (can specify multiple) |

Requires `trusted-snapshot-peer` to be configured — without trusted peers, the detection won't start even if enabled.

### How It Works

A background thread runs `check_stalled_sync_loop()` (plugin.cpp L1682), checking every **30 seconds**:

1. Computes `elapsed = now - last_block_received_time`
2. If `elapsed > stalled_sync_timeout_minutes` (default 5 min), uses a two-stage escalation:

```
Stalled sync detected!
  │
  ├── First trigger (P2P recovery):
  │     ├── p2p_plugin->trigger_resync()   ← resync + reconnect seeds
  │     ├── Set _p2p_recovery_attempted = true
  │     └── Delay timer by 1 minute (give P2P recovery time to work)
  │
  └── Second trigger (snapshot download):
        ├── 1. Query trusted peers for a newer snapshot
        │       download_snapshot_from_peers()
        │
        ├── 2a. Newer snapshot found:
        │     ├── load_snapshot()                   ← replace chain state
        │     ├── set_dlt_mode(true)
        │     ├── initialize_hardforks()
        │     ├── Replay dlt_block_log              ← apply local blocks beyond snapshot
        │     ├── p2p_plugin->trigger_resync()      ← resync + reconnect seeds
        │     └── Reset timer + guard, restart loop
        │
        └── 2b. No newer snapshot available:
              └── Reset timer, continue with P2P sync
```

The `_p2p_recovery_attempted` guard resets to `false` whenever a block is received (`on_applied_block`), so each new stall starts fresh with P2P recovery.

### Difference from P2P Stale Sync Detection

| | P2P Stale Sync (p2p_plugin) | Snapshot Stalled Sync (snapshot plugin) |
|---|---|---|
| **Default** | Enabled (`true`) | Disabled (`false`) |
| **Timeout** | 120 seconds (2 min) | 5 minutes |
| **Recovery action** | Reset sync to LIB + reconnect seeds | Download entire snapshot from trusted peer |
| **Requires** | Nothing (works with any peers) | `trusted-snapshot-peer` configured |
| **Severity** | Lightweight (P2P-level reset) | Heavy (full chain state replacement) |
| **Use case** | Temporary network issues, soft-bans | Node hopelessly behind, DLT mode bootstrap |

The P2P detector fires first (2 min) and attempts a soft recovery. If that doesn't work and blocks still don't arrive, the snapshot detector fires later (5 min) and does a hard recovery by re-downloading state.

---

## Key Configuration Constants

| Constant | File | Default | Description |
|----------|------|---------|-------------|
| `GRAPHENE_NET_DEFAULT_PEER_CONNECTION_RETRY_TIME` | `config.hpp` | 30s | Base retry interval per failed attempt |
| `GRAPHENE_NET_MAX_FAILED_CONNECTION_ATTEMPTS` | `config.hpp` | 5 | Cap on failure counter (max backoff = 180s) |
| `GRAPHENE_NET_DEFAULT_DESIRED_CONNECTIONS` | `config.hpp` | 20 | Target number of active connections |
| `GRAPHENE_NET_DEFAULT_MAX_CONNECTIONS` | `config.hpp` | 200 | Maximum allowed connections |
| `DISCONNECT_RECONNECT_COOLDOWN_SEC` | `node.cpp` | 30s | Per-IP cooldown after disconnect |
| `GRAPHENE_PEER_DATABASE_RETRY_DELAY` | `config.hpp` | 15s | (unused, replaced by 10s sleep) |
