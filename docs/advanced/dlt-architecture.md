# DLT P2P Architecture

VIZ Ledger's P2P layer was redesigned from the legacy Graphene synopsis-based protocol (`node.cpp`) to a dedicated DLT-native protocol (`dlt_p2p_node.cpp`). The public plugin API is unchanged — only the internal implementation was replaced.

---

## Overview

```
Before:  p2p_plugin → graphene::network::node    (node.cpp, 6978 lines, STCP, inventory gossip)
After:   p2p_plugin → dlt_p2p_node               (dlt_p2p_node.cpp, 2627 lines, raw TCP, range-based sync)
```

The replacement is **in-place**: same plugin name `"p2p"`, same port (2001/4243), same public API. All dependent plugins (validator, snapshot, etc.) required zero changes.

---

## Wire Protocol

Raw TCP — no STCP encryption layer. Each message on the wire:

```
[4 bytes: data size (uint32_t)] [4 bytes: msg_type (uint32_t)] [N bytes: fc::raw::pack(T)]
```

### Message Types (5100–5116)

| Type | ID | Description |
|------|----|-------------|
| `dlt_hello_message` | 5100 | Handshake: protocol version, head/LIB, DLT range, node/fork status |
| `dlt_hello_reply_message` | 5101 | Handshake reply: exchange_enabled, fork_alignment |
| `dlt_range_request_message` | 5102 | Request a range of block IDs |
| `dlt_range_reply_message` | 5103 | Reply with available block range |
| `dlt_get_block_range_message` | 5104 | Fetch blocks start..end with prev_block_id check |
| `dlt_block_range_reply_message` | 5105 | Reply: blocks vector + is_last flag |
| `dlt_get_block_message` | 5106 | Fetch a single block by ID |
| `dlt_block_reply_message` | 5107 | Reply: block + next_available + is_last |
| `dlt_not_available_message` | 5108 | Block not available |
| `dlt_fork_status_message` | 5109 | Broadcast current fork/node status to peers |
| `dlt_peer_exchange_request` | 5110 | Request known peers list |
| `dlt_peer_exchange_reply` | 5111 | Reply with peers |
| `dlt_peer_exchange_rate_limited` | 5112 | Rate limit notice: wait N seconds |
| `dlt_transaction_message` | 5113 | Broadcast a signed transaction |
| `dlt_soft_ban_message` | 5114 | Notification before disconnecting a banned peer |
| `dlt_gap_fill_request` | 5115 | Request specific block numbers to fill a gap |
| `dlt_gap_fill_reply` | 5116 | Reply with requested blocks |

---

## Fiber Architecture

All I/O runs on a single `fc::thread` using cooperative fibers — no mutexes needed for shared state:

| Fiber | Role |
|-------|------|
| Accept loop | Waits for incoming connections; rejects duplicate IPs |
| Read loop (per peer) | Reads messages; dispatches to `on_message()` |
| Periodic task | Reconnects, checks stagnation, peer stats, mempool cleanup |

Fibers yield on blocking I/O (`readsome()`, `writesome()`), allowing multiple peers on one thread without contention.

---

## Node Status and Peer Lifecycle

**Node statuses:** `SYNC` (catching up) / `FORWARD` (live, exchanging blocks)

**Peer lifecycle states:**
```
CONNECTING → HANDSHAKING → SYNCING → ACTIVE → DISCONNECTED → BANNED
```

Timeouts: connecting=5s, handshaking=10s. Reconnect backoff: 30s → 60s → … → 3600s with ±25% jitter, reset after 5 minutes stable uptime. Peers removed after 8 hours of non-response.

---

## Block Sync: SYNC Mode

A node in SYNC mode fetches blocks sequentially from a peer with a higher head:

1. `request_blocks_from_peer()` — sends `dlt_get_block_range_message` for up to 200 blocks after our head.
2. `on_dlt_block_range_reply()` — validates `prev_block_id` hash chain, applies each block.
3. `check_sync_catchup()` — compares our head against all peers' heads; transitions to FORWARD when caught up.
4. `sync_stagnation_check()` — after 30s with no new block, retries up to 3 times then transitions to FORWARD with a warning.

### Gap Fill

When a contiguous gap exists between our head and the earliest available block on the syncing peer, `request_gap_fill()` sends a `dlt_gap_fill_request` (up to 100 blocks per request) to any peer whose DLT range covers the gap. Gap fill works in both SYNC and FORWARD modes:

- Triggered from `on_dlt_block_reply()` (out-of-order block detected) and `periodic_task()` (every 5s).
- Falls back from exchange-enabled peers to any active peer with a higher head.
- Falls back to SYNC mode if no peer has the needed blocks.
- Large gaps handled in 100-block chunks with a 5s cooldown between requests.

---

## Block Exchange: FORWARD Mode

In FORWARD mode, peers exchange live blocks and transactions:

- `exchange_enabled` flag controls whether a peer receives new blocks from us.
- On FORWARD transition, `dlt_fork_status_message` is sent to **all** peers (not just exchange-enabled) to notify them of our readiness.
- `on_dlt_fork_status()` re-evaluates `exchange_enabled` when a peer transitions from SYNC to FORWARD.
- `check_forward_stagnation()` — if head hasn't advanced in 30s AND at least one peer is ahead, transitions to SYNC.

---

## Fork Alignment and Exchange Eligibility

During the hello handshake, `check_fork_alignment()` performs multi-tier block ID matching to determine if peers are on the same fork:

| Check | Condition |
|-------|-----------|
| Empty peer | `head_block_num == 0` → aligned (new node) |
| Range overlap | Our DLT log covers peer's head → `is_block_known(head_id)` |
| Boundary link | `peer_head + 1 == our_earliest` → check our earliest block's `previous == peer_head_id` |
| LIB fallback | Always check `is_block_known(lib_id)` |

This multi-tier check prevents false "different fork" disconnections in DLT mode, where old blocks are pruned and the old single-ID check would fail for peers on the same chain.

---

## Fork Resolution

The fork resolution subsystem tracks competing chain tips:

- **Threshold:** 42 blocks of divergence triggers `resolve_fork()` (= `CHAIN_MAX_VALIDATORS × 2`, one full schedule rotation).
- **Selection:** Heaviest branch by vote weight.
- **Hysteresis:** 6 consecutive blocks as winner before switching (`CONFIRMATION_BLOCKS`).
- **Status:** `_fork_status` exposed via `is_on_majority_fork()` for the validator plugin to check before producing blocks.

---

## Anti-Spam

| Mechanism | Description |
|-----------|-------------|
| `spam_strikes` counter | Single counter per peer; reset on good packet; soft-ban at threshold=10 |
| Soft ban | Sets BANNED state for 3600s; sends `dlt_soft_ban_message` before closing |
| Per-IP dedup | Rejects duplicate connections from the same IP (both inbound and outbound) |
| Broadcast dedup | `send_to_all_our_fork_peers()` tracks `std::set<ip::address>` to skip duplicate IPs |

Duplicate blocks and out-of-order blocks from range replies are silently skipped — not counted as spam. Deserialization errors do not increment spam strikes.

---

## P2P Mempool

A separate in-process mempool (distinct from the chain's `_pending_tx`) provides early transaction filtering before chain acceptance:

- **Dedup** by `tx_id`.
- **Eviction** by oldest expiry when limits are reached.
- **Limits** (configurable): max 10,000 entries, 100 MB total, 64 KB per transaction.
- **Provisional entries** tagged during SYNC mode; revalidated on FORWARD transition.
- **Cleanup** on block receipt (`remove_transactions_in_block`) and fork switch (`prune_mempool_on_fork_switch`).

---

## Peer Exchange

Rate-limited peer discovery:

- Max 3 requests per 5-minute window per peer.
- Subnet diversity filter: max 2 peers per `/24` prefix in each reply.
- Only peers with ≥600s uptime are shared.
- Inbound peers (ephemeral ports) excluded from exchange replies.

---

## Recovery Mechanisms

### Peer isolation (P53)

When zero active peers exist for 60 seconds, `emergency_peer_reset()`:
- Clears all soft bans (BANNED → DISCONNECTED, resets spam strikes).
- Resets all disconnected peer backoffs to minimum with immediate reconnect.

### Block processing pause/resume

`pause_block_processing()` / `resume_block_processing()` allow the snapshot plugin to halt P2P block intake during state serialization. The periodic task skips DB-accessing operations while paused.

### Startup grace period (P22)

For the first 60 seconds after startup, blocks within 10 of the head are treated as `FORK_DB_ONLY` instead of `DEAD_FORK` — preventing cascade rejections while the fork_db rebuilds from the block log.

---

## Block Accept Results

`dlt_block_accept_result` enum replaces the old boolean return:

| Value | Meaning |
|-------|---------|
| `ACCEPTED` | Block applied to chain (became new head) |
| `FORK_DB_ONLY` | Stored in fork_db but not applied (unlinkable, competing fork) |
| `DEAD_FORK` | Block at/below head from a dead fork — peer is soft-banned |
| `ALREADY_KNOWN` | Already have this block (duplicates, `block_too_old_exception`) |
| `REJECTED` | Failed validation entirely |

---

## Configuration Reference

| Option | Default | Description |
|--------|---------|-------------|
| `dlt-block-log-max-blocks` | 100,000 | Max blocks in DLT rolling block log |
| `dlt-peer-max-disconnect-hours` | 8 | Remove peer after this many hours non-response |
| `dlt-mempool-max-tx` | 10,000 | Hard cap on mempool entries |
| `dlt-mempool-max-bytes` | 100 MB | Hard cap on total mempool memory |
| `dlt-mempool-max-tx-size` | 64 KB | Reject oversized transactions |
| `dlt-mempool-max-expiration-hours` | 24 | Reject far-future expiration |
| `dlt-peer-exchange-max-per-reply` | 10 | Max peers per exchange reply |
| `dlt-peer-exchange-max-per-subnet` | 2 | Anti-sybil: max 2 peers per /24 |
| `dlt-peer-exchange-min-uptime-sec` | 600 | Min uptime before peer is shared |
| `dlt-stats-interval-sec` | 300 | Peer stats log interval (min 30) |

---

## Color-Coded Logging

| Color | Meaning |
|-------|---------|
| Green | Sync progress and block production |
| White | Normal block exchange |
| Red | Fork events |
| Dark gray | Transaction handling |
| Orange | Warnings (soft bans, stagnation, gaps) |
| Cyan | Peer statistics output |

---

## Delegate Pattern

The network library links only `fc` and `graphene_protocol` — not `graphene_chain`. The `dlt_p2p_delegate` abstract interface bridges this gap:

```
dlt_p2p_node (network lib)  ←→  dlt_p2p_delegate (interface)  ←→  dlt_delegate (p2p_plugin)
```

The `dlt_delegate` in `p2p_plugin.cpp` implements:
- `read_block_by_num()` — checks dlt_block_log, then fork_db.
- `accept_block()` — calls `push_block()`; catches `unlinkable_block_exception` → stores in fork_db.
- `get_fork_branch_tips()` — fetches from fork_db around current head.
- `is_tapos_block_known()` — delegates to `db.is_known_block()`.

---

See also: [P2P Overview](../p2p/overview.md), [Sync Scenarios](../p2p/sync-scenarios.md), [Snapshot Plugin](../storage/snapshots.md), [Block Log](../storage/block-log.md).
