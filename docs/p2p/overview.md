# P2P Network Overview

VIZ Ledger uses a custom DLT P2P protocol that replaced the legacy graphene synopsis-based networking layer. The new design is optimized for DLT mode (snapshot-based nodes with rolling block logs) and removes the complex graphene ancestry synopsis in favour of a simpler range-based block exchange.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│ p2p_plugin (AppBase plugin)                             │
│   └─ dlt_delegate (implements dlt_p2p_delegate)         │
│        └─ bridges chain state: db(), fork_db, block_log │
├─────────────────────────────────────────────────────────┤
│ dlt_p2p_node                                            │
│   ├─ accept loop (incoming TCP connections)             │
│   ├─ periodic task (5s tick: reconnect, stats, gaps)    │
│   └─ dlt_peer_state × N (one per connected peer)        │
├─────────────────────────────────────────────────────────┤
│ Wire format: raw TCP, header (type + length) + data     │
│ Fiber model: all I/O on one fc::thread, cooperative     │
└─────────────────────────────────────────────────────────┘
```

### Design decisions

| Decision | Rationale |
|----------|-----------|
| Delegate pattern | `dlt_p2p_node` links only `fc` + `graphene_protocol`. Direct chain access is exposed via `dlt_p2p_delegate` to avoid circular dependency. |
| Raw TCP (no STCP encryption) | DLT emergency mode flips all validators simultaneously — no backward-compatible encryption needed. Simpler wire protocol. |
| Cooperative fibers (fc::thread) | All I/O uses `readsome()`/`writesome()` which yield the fiber. Multiple peers on one thread without mutexes. |
| Separate P2P mempool | Chain's `_pending_tx` only takes effect after acceptance. The P2P mempool filters by expiry, TaPoS, and size before pushing to chain, reducing wasted evaluation. |
| In-place plugin replacement | Plugin name is still `"p2p"`, port still `2001`/`4243`, public API unchanged. Old and new protocols are incompatible; dual-mode creates isolated sub-networks. |

---

## Peer Lifecycle

Each peer connection goes through the following states:

```
CONNECTING ──(TCP established)──► HANDSHAKING
              5s timeout ↓           ↓ 10s timeout
              DISCONNECTED      hello/hello_reply
                    ▲               ↓
                    │          SYNCING ──(caught up)──► ACTIVE
                    │                                      │
                    └──(disconnect/error)──────────────────┘
                                                           │
                    BANNED ◄──(spam_strikes ≥ 10)──────────┘
```

**Timeout values:**
- Connecting → DISCONNECTED: **5 seconds**
- Handshaking → DISCONNECTED: **10 seconds**

**Reconnection backoff:** 30 s → 60 s → … → 3600 s with ±25% jitter. Backoff resets after a stable connection of >5 minutes. Peers with no response for 8 hours are permanently removed.

**Emergency peer reset:** If all peers are isolated (zero active connections) for 60 seconds, `emergency_peer_reset()` clears all soft bans and resets all backoffs to their initial value with immediate reconnect attempts.

---

## Hello Handshake

On connection the initiating peer sends `dlt_hello_message` containing:

- `head_block_num` / `head_block_id`
- `lib_block_num` / `lib_block_id`
- `dlt_earliest_block_num` — oldest block available in the peer's rolling DLT block log
- `node_status` — SYNC or FORWARD

The receiving peer responds with `dlt_hello_reply_message` containing:

- `fork_alignment` — whether the blocks overlap on the same fork
- `exchange_enabled` — whether the responding peer considers the sender caught up

### Fork alignment check (DLT-range-aware)

Because DLT nodes prune old blocks, naive head-ID comparison would falsely flag same-chain peers as "different fork". The check is multi-tiered:

| Case | Check |
|------|-------|
| Peer has no blocks (`head_num == 0`) | Aligned |
| Peer head is in our DLT range | `is_block_known(peer.head_id)` |
| Peer head + 1 == our earliest block | Read our earliest block, verify `previous == peer.head_id` |
| Fallback | `is_block_known(peer.lib_id)` |

---

## Sync Modes

Each node is in one of two modes at any time:

### SYNC mode (pull-based)

Used when the node is behind the network. The node requests blocks in ranges of up to **200 blocks** from a peer:

```
us                          peer
 │──dlt_get_block_range──►│
 │◄──dlt_block_range_reply─│
 │   (up to 200 blocks)    │
 │──apply each block──►chain│
 │                          │
 │  (when is_last=true)     │
 │──transition_to_forward   │
```

**Gap detection:** If `our_head + 1 < peer.dlt_earliest` (the missing blocks are no longer in the peer's rolling log), the node searches for another peer that can bridge the gap. If no peer can serve the gap, a snapshot import is recommended.

**Stagnation protection:** If no block is received for 30 seconds, the node retries up to 3 times, then transitions to FORWARD mode with a warning.

### FORWARD mode (push-based)

Used when the node is caught up. Blocks are gossiped via `dlt_block_message`. Each block is broadcast to all **exchange-enabled** peers that share the same fork.

**FORWARD → SYNC fallback:** If the node's head does not advance for **30 seconds** (`check_forward_stagnation`) and at least one peer is ahead, the node re-enters SYNC mode.

### SYNC ↔ FORWARD transitions

| Transition | Trigger |
|------------|---------|
| SYNC → FORWARD | Block range reply with `is_last=true` |
| SYNC → FORWARD | `check_sync_catchup()`: our head ≥ all peers |
| SYNC → FORWARD | Stagnation after 3 retries |
| FORWARD → SYNC | `check_forward_stagnation()`: head stuck for 30s and a peer is ahead |
| FORWARD → SYNC | `check_forward_behind()`: peer ahead by >2 blocks (15 s grace after entering FORWARD) |
| FORWARD → SYNC | Gap fill fails and no peer is available |

On SYNC → FORWARD, the node broadcasts a `dlt_fork_status_message` with `node_status=FORWARD` to all connected peers, enabling them to re-evaluate `exchange_enabled` for this node.

---

## Gap Fill

Gap fill is a lightweight mechanism to fetch a small number of specific blocks without entering full SYNC mode. It uses two dedicated message types (`dlt_gap_fill_request` / `dlt_gap_fill_reply`) and triggers in three places:

1. When an out-of-order block arrives (`on_dlt_block_reply`)
2. Every 5 seconds from `periodic_task()`
3. After snapshot pause completes (`resume_block_processing()`)

**Rules:**
- Maximum **100 blocks per request** (`GAP_FILL_MAX_BLOCKS`); larger gaps use chunked requests.
- **5-second cooldown** between gap fill requests.
- The requesting peer selects the active peer with the highest head block number.
- The serving peer reads blocks from its DLT block log; requests outside the log range are rejected.
- SYNCING lifecycle peers are eligible candidates (not just ACTIVE).
- If no suitable peer is found, the node transitions immediately to SYNC mode.

---

## Mempool

The DLT P2P layer maintains its own mempool separate from the chain's `_pending_tx`. This allows early filtering before pushing transactions to the chain evaluator.

**Admission checks:**
- Duplicate by `tx_id` — deduplicated on receipt
- Expiry — reject if already expired
- TaPoS (`tapos_block_num`) — reject if reference block is unknown
- Size — reject if `tx.size > dlt-mempool-max-tx-size` (default 64 KB)
- Expiration horizon — reject if expiry is more than `dlt-mempool-max-expiration-hours` (default 24 h) in the future

**Eviction:** When the mempool exceeds `dlt-mempool-max-tx` (default 10 000) or `dlt-mempool-max-bytes` (default 100 MB), the entry with the nearest expiration is evicted first.

**Lifecycle:**
- Transactions received during SYNC are tagged **provisional** and revalidated on transition to FORWARD (TaPoS blocks may now be known).
- On block application, included transactions are pruned (`remove_transactions_in_block`).
- On fork switch, TaPoS-invalid entries are pruned (`prune_mempool_on_fork_switch`).
- `periodic_mempool_cleanup()` removes expired and TaPoS-invalid entries every cycle.

---

## Fork Resolution

The DLT P2P layer tracks fork state with a **42-block threshold** (2 full validator rounds = `CHAIN_MAX_WITNESSES × 2`).

`track_fork_state()` is called after each block application. When a competing fork is detected and sustained for ≥ 42 blocks, `resolve_fork()` computes the **heaviest branch** by total vote weight. A candidate branch must accumulate **6 consecutive confirmation blocks** (`dlt_fork_resolution_state::CONFIRMATION_BLOCKS`) before the node switches to it (hysteresis).

The current fork status is exposed via `is_on_majority_fork()`, which the Validator Plugin uses to decide whether to produce blocks.

---

## Anti-Spam

Each peer has a single **`spam_strikes`** counter:

- Incremented on: invalid block, invalid transaction, protocol violation
- Reset on: any valid packet
- Soft-ban threshold: **10 strikes**

A soft-banned peer receives `dlt_soft_ban_message` (containing `ban_duration_sec` and a human-readable reason) before the connection is closed. The banned peer enters BANNED state for the specified duration and will not reconnect until it expires.

**Per-IP connection dedup** prevents multiple connections from the same node:
- `accept_loop()` rejects incoming connections from IPs with an existing active entry.
- `connect_to_peer()` skips outbound connections if the target IP already has an active entry.
- Broadcast (`send_to_all_our_fork_peers`) tracks a `set<ip::address>` and skips IPs already sent to in that broadcast.

**Duplicate / out-of-order block tolerance:**
- Already-applied blocks are silently skipped (not counted as spam).
- Out-of-order blocks in range replies fall through to `fork_db` instead of triggering a soft ban.
- Deserialization errors do not increment spam strikes.
- Oversized messages from old-protocol peers trigger a disconnect without increasing backoff.

---

## Peer Exchange

Nodes share peer addresses to assist discovery.

**Rate limit:** **3 requests per 5-minute window** per peer.

**Filters applied before sharing a peer address:**
- Minimum uptime: **600 seconds**
- Subnet diversity: maximum **2 peers per /24** subnet
- Ephemeral-port exclusion: `is_incoming` peers are never shared (their port is temporary)

**Limits per reply:** `dlt-peer-exchange-max-per-reply` (default 10).

---

## Block Processing Pause/Resume

The snapshot plugin (and other plugins requiring exclusive access) can halt P2P block intake via `pause_block_processing()`. While paused:

- `periodic_task()` skips operations that need database read locks: `sync_stagnation_check()`, `periodic_peer_exchange()`, `log_peer_stats()`.
- Stale sync and forward stagnation timers are reset so the node does not enter unnecessary mode transitions.
- Non-DB housekeeping continues: reconnect, lifecycle management, mempool cleanup, banned-peer unban.

On `resume_block_processing()`, the node attempts gap fill before falling back to SYNC mode.

---

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `p2p-endpoint` | `0.0.0.0:2001` | Listen address and port |
| `seed-node` | — | Static seed peer address(es) |
| `p2p-max-connections` | — | Maximum simultaneous peer connections |
| `dlt-block-log-max-blocks` | 100000 | Rolling DLT block log capacity |
| `dlt-peer-max-disconnect-hours` | 8 | Remove non-responding peer after N hours |
| `dlt-mempool-max-tx` | 10000 | Hard cap on mempool entry count |
| `dlt-mempool-max-bytes` | 104857600 | Hard cap on total mempool memory (100 MB) |
| `dlt-mempool-max-tx-size` | 65536 | Reject transactions larger than this (64 KB) |
| `dlt-mempool-max-expiration-hours` | 24 | Reject transactions expiring more than N hours in future |
| `dlt-peer-exchange-max-per-reply` | 10 | Max addresses returned per peer-exchange reply |
| `dlt-peer-exchange-max-per-subnet` | 2 | Max peers shared per /24 subnet |
| `dlt-peer-exchange-min-uptime-sec` | 600 | Min peer uptime before sharing address |
| `dlt-stats-interval-sec` | 300 | Interval between peer stats log output (min 30 s) |

---

## Peer Statistics Log

Every `dlt-stats-interval-sec` (default 5 minutes) the node logs a peer statistics summary:

```
[DLT-P2P] node=FORWARD head=#79274318 lib=#79274297 fork=MAJORITY
  peer 192.168.1.10:2001 ACTIVE  head=#79274318 exch=YES  dlt=[79174319..79274318] strikes=0
  peer 192.168.1.11:2001 SYNCING head=#79274100 exch=no   dlt=[79174319..79274100] strikes=0
  peer 192.168.1.12:2001 BANNED  ban_remaining=3540s
```

Fields:
- `exch=YES/no` — whether block/transaction exchange is enabled with this peer
- `dlt=[min..max]` — DLT block log range the peer can serve
- `strikes` — current spam strike count (resets on any valid packet)
- `ban_remaining` — seconds until soft ban expires

The stats interval can be updated at runtime via `set_stats_log_interval()`.

---

## Diagnostic Summary

| Symptom | Likely cause |
|---------|--------------|
| Node stuck in SYNC, head not advancing | Gap between our head and peer's DLT range — peer cannot bridge; consider snapshot import |
| Rapid SYNC ↔ FORWARD oscillation | No peer is ahead, or all peers isolated — check `emergency_peer_reset` log entries |
| All peers show `exch=no` | FORWARD transition did not notify peers; should self-resolve on next `broadcast_chain_status` cycle |
| `spam_strikes` growing on all peers | Likely fork divergence — check fork alignment via hello logs |
| `unlinked_size` growing in fork_db | Parent blocks are not arriving; gap fill should recover within 5s |
| `peer_head_num` appears stale in stats | Expected — `peer_head_num` is a snapshot from the last hello/fork_status exchange, not real-time |

---

See also: [Messages](./messages.md), [Sync Scenarios](./sync-scenarios.md), [Forward Mode](./forward-mode.md), [Stats Reference](./stats-reference.md), [Snapshot](../node/snapshot.md), [Fork Resolution](../consensus/fork-resolution.md).
