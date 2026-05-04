# DLT P2P Network Redesign — Analysis & Implementation Plan

## 1. Current System Analysis

### 1.1 Existing P2P Architecture (node.cpp — 6978 lines)
The current P2P layer is a general-purpose synopsis-based protocol from Graphene/BitShares with many layers of backwards compatibility:

- **Hello handshake**: Exchanges node identity, chain state, DLT mode, emergency status via `user_data` fields (already extended for DLT in `generate_hello_user_data` around line 2297).
- **Synopsis sync**: `get_blockchain_synopsis()` builds exponential backoff hash list → peer returns which blocks it doesn't know → `fetch_items` requests batches → `send_sync_block_to_node_delegate` processes them.
- **Inventory gossip**: Normal operation uses `item_ids_inventory_message` to advertise new blocks/transactions.
- **Fork handling**: `fork_rejected_until` soft-ban, `unlinkable_block_strikes`, `sync_spam_strikes` counters.
- **Chain status announcements**: Type 5018 — already carries head, LIB, DLT range, emergency flags.
- **Post-validation**: `block_post_validation_message` (type 6009) lets witnesses signal block confirmation.

### 1.2 Existing DLT Infrastructure (already built)
| Component | What exists |
|-----------|-------------|
| `dlt_block_log` | Memory-mapped rolling block log with index, `read_block_by_num()`, `head()`, `start_block_num()`, `truncate_before()` |
| `fork_db` | `fetch_block_by_number()`, `fetch_branch_from()`, `compare_fork_branches()` (vote-weighted) |
| `node_delegate` | `is_dlt_mode()`, `get_dlt_earliest_block_num()`, `is_emergency_consensus_active()`, `has_emergency_private_key()` |
| `chain_status_announcement` | Message type 5018 with head/LIB/DLT/emergency fields |
| `witness_plugin` | Minority fork detection, `resync_from_lib()`, fork collision deferral with Level 1 (vote-weight) and Level 2 (timeout) |

### 1.3 What's Broken/Problematic in node.cpp
- 6978 lines of mostly irrelevant Graphene inheritance
- Synopsis-based sync is overengineered for DLT where blocks are contiguous
- Complex state machine (synopsis→fetch_ids→fetch_items→process) prone to ping-pong loops
- Peer discovery is overcomplicated (potential_peer_db, firewall checks, address gossip)
- Inventory advertisement adds complexity without benefit for block-only DLT
- Soft-ban/strike logic scattered across multiple locations
- Thread model (`P2P_IN_DEDICATED_THREAD`) adds overhead

---

## 2. Proposed Design Analysis

### 2.1 Core Protocol (Normal Mode)

**Step 1: Peer registration + discovery**
- **Bootstrap**: Read seed peer endpoints from config (`p2p-seed-node`, already exists in p2p_plugin)
- **Peer discovery via "friends" exchange**: After a node transitions from sync/catchup to forward mode, it becomes eligible to participate in peer discovery. Once every 10 minutes per peer, a node may send a `dlt_peer_exchange_request` ("send me your 'our fork' friends") to any connected peer. The responder replies with `dlt_peer_exchange_reply` containing its known "our fork" peer endpoints (IP:port + node_id). If a peer asks too frequently, the responder sends `dlt_peer_exchange_rate_limited` ("you already asked, wait N seconds").
- **Peer database**: Simple in-memory set of known peer endpoints, pruned on disconnect. No persistent potential_peer_db, no firewall checks — simpler than the old `peers.json` + `potential_peer_db` machinery.
- **Assessment**: Controlled peer discovery without the complexity of the existing address-gossip system. Rate-limiting (10 min cooldown per peer) prevents flooding while allowing the network to gradually learn about new nodes that join.

**Step 2: Hello with DLT range + status**
- Send: `[dlt_start, dlt_end]`, head block (num + hash), LIB (num + hash), emergency status, fork status, node status (sync/forward)
- Receiver checks if initiator's head/LIB are in our dlt_block_log or fork_db → flags as "our fork" (exchange enabled, `fork_alignment=true`) or "not our fork" (`fork_alignment=false`)
- **Node status model**: The node has 2 statuses:
  - **SYNC**: Catching up — send hello, try to find next continuation after our head, request blocks in ranges. When we reach the top → transition to FORWARD.
  - **FORWARD**: Caught up — exchange data with "our fork" peers. If fork detected, get competing blocks and mark peer as "our" or "not our". Don't care if peer's head is above ours.
- **Assessment**: This replaces the existing synopsis approach. Better for DLT. Simplified from the previous 3-state model:
  - We don't care about initiator's head being above ours — they're just ahead.
  - In sync mode: just get blocks after our head.
  - In forward mode: mark peer as "our" or "not our" based on chain link.
  - LIB below dlt_start = "unknown" during sync (allow exchange), re-evaluate in forward mode.
  - **Keep**: "fork status (not used, but visible in logs)" — valuable for debugging.

**Step 3: Range query + get request**
- Peer asks: "do you have block X with hash H?"
- We check dlt_block_log: if known, respond with actual available range + wait for specific `get` packet
- `get` packet: block_num + prev_hash (to verify chain link)
- We send: the block + next_available_num (or 0 if none) + sync_status bool
- If sync_status says "last block", flag peer as "our fork" (exchange enabled)
- **Assessment**: This is essentially a binary-search-free sync. Good for DLT. But:
  - **Improvement**: The "range query" step could be skipped. Just include the range in hello response.
  - **Improvement**: The "prev_hash" check is a good anti-corruption measure. Keep it.
  - **Issue**: What if we don't have the requested block? Current design says "we saying that we not have it" — need a `not_available` response type.

**Step 4: Exchange + retranslation**
- When we receive a block/transaction from "our fork" peer → add to chain + retranslate to all "our fork" peers
- Witnesses send post-validation to accelerate LIB
- **Assessment**: Already implemented via `broadcast_block` + `broadcast_block_post_validation`. The retranslation to "our fork" peers only is a good optimization over broadcast-to-all.

### 2.2 Fork Resolution (Fork Mode)

**Key design**: Wait until end of schedule round, then tally vote weights per fork branch.

**Analysis of the proposed fork resolution**:
- The existing system already has `compare_fork_branches()` in database.cpp (line 1359-1417) which does vote-weighted comparison with +10% longer-chain bonus.
- The user wants to defer fork resolution to schedule round boundaries — this is MORE conservative than the existing Level 1+Level 2 system.
- **Resolution trigger**: Use `num_scheduled_witnesses * 2` blocks (e.g., 21 × 2 = 42 blocks = 2 full rounds) as the fork resolution threshold — same as the existing emergency DLT minority fork detection in witness.cpp (line 722: `CHAIN_MAX_WITNESSES * 2`). This avoids the ambiguity of "end of schedule round" when different forks have diverged at different block heights. When 42 blocks have been produced since the fork was first detected, resolution fires.
- **Improvement needed**: None — this is a clean, proven threshold already used in production code.

**Data structure: fork_db with `get_all_active_forks()`**:
- The fork_db already supports multiple forks at same height (`fetch_block_by_number()` returns a vector).
- Add `fork_database::get_all_active_branch_tips()` — returns distinct tip IDs for all currently tracked fork branches.
- Each branch is then resolved via the existing `compare_fork_branches()` vote-weighted comparison (database.cpp line 1359-1417).
- **Design decision confirmed**: Use fork_db as the sole fork data structure; no separate "array of arrays".

**Issue: "Stop producing blocks if in minority fork"**
- Already implemented in witness.cpp minority fork detection (line 648-666)
- The new P2P layer needs to expose a `bool is_on_majority_fork()` query

### 2.3 Emergency Mode

The user's emergency design is mostly aligned with existing code:
- Emergency activates when no blocks processed for timeout period (already in `_apply_block` emergency check, database.cpp ~line 5200)
- Reset witness signing keys (already done: `w.signing_key = public_key_type()` at line 5001)
- Free slots go to `committee` account (already: hybrid schedule override at line 2733)
- Nodes with emergency key produce blocks (already: witness_plugin emergency master check)

**Issue**: "If node in emergency got blocks with emergency witness — auto choose this fork as main head"
- The existing `compare_fork_branches()` already does this: branch with emergency committee blocks wins unconditionally (line 1395-1397)
- **Good**: Aligned with existing logic.

**Issue**: "LIB moves faster" during emergency
- Already implemented: during emergency, LIB advances every block capped at HEAD-1 (line 5737-5782)
- **Good**: No changes needed.

### 2.4 Anti-Spam

"Good packet resets spam counter" — **This is a significant improvement over existing logic.**

Current system has independent strike counters that only decrement on certain actions. The user's proposal is:
- Each peer has a `spam_strikes` counter
- Any valid block, valid transaction, or valid hello response → reset counter to 0
- Invalid/duplicate/wrong-fork packets → increment counter
- Counter exceeds threshold → soft-ban

**Assessment**: Much simpler and more effective than the current multi-counter system. The reset-on-good behavior naturally recovers from transient issues.

**Recommendation**: Use a single `spam_strikes` per peer instead of the current multiple counters (`unlinkable_block_strikes`, `sync_spam_strikes`, `fetch_ids_rate_limit_strikes`).

### 2.5 DLT Pruning

"Remove old blocks every 1000 blocks, keep only `dlt-block-log-max-blocks` (default 100000)"

The existing `dlt_block_log::truncate_before()` works but is expensive (copies all retained data).

**Decision: Modify `truncate_before()` to batch-prune 10000 blocks at once instead of 1000.** This reduces copy frequency by 10x while keeping the implementation simple (no ring-buffer refactor needed). The pruning trigger checks every 10000 blocks produced — when `current_head - dlt_start > dlt-block-log-max-blocks`, prune the oldest 10000 blocks in one call.

---

## 3. Design Issues & Improvements

### 3.1 Peer Discovery via "Friends" Exchange — Design Details

The user's proposal is:
- After catchup → forward mode, exchange "our fork" peers
- `dlt_peer_exchange_request` message: "send me your friends"
- `dlt_peer_exchange_reply` message: list of known peer endpoints (IP:port + node_id)
- Rate limit: once per 10 minutes per peer
- If asked too soon: `dlt_peer_exchange_rate_limited` ("you already asked, wait N seconds")

**Improvements over old system**: The existing node.cpp uses:
- `potential_peer_db` (persistent JSON file `peers.json`)
- `address_request_message`/`address_message` gossip
- `firewall_check_state_data` for NAT detection
- Connection disposition tracking (last_connection_succeeded/failed)

All of this is unnecessary for DLT where we only care about "our fork" peers. The proposed friends exchange is simpler:
- No persistence — peers are discovered through live exchange only
- No firewall checks — DLT nodes are assumed reachable (config-controlled)
- No address gossip — only intentional peer exchange requests
- Rate-limiting prevents abuse (spam counter also applies)

**Edge cases**:
- New node with only seed peers: after catchup, sends `dlt_peer_exchange_request` to a seed. Seed responds with its known peers. Node connects to new ones.
- Peer disconnects: NOT immediately removed from `_known_peers` set. Instead, the peer is flagged as `disconnected` and reconnection is attempted with an incrementing backoff step (starting at ~30s, doubling each retry, capped at 3600s max). If the peer does not respond for 8 hours (configurable), it is permanently removed from `_known_peers`.
- Self in peer list: receiver filters out its own node_id and already-connected peers.
- All peers exhausted: periodic timer (every ~12 min) picks a random connected peer to re-request. If no new peers found, nothing happens.

### 3.2 Transaction Propagation Design

Transactions follow a clear lifecycle:
1. **Ingest**: Transactions enter the system via the `jsonrpc` plugin (API endpoint).
2. **Broadcast**: The node translates (retransmits) the transaction to all "our fork" peers.
3. **Mempool**: A separate in-memory transaction index (mempool) collects and deduplicates pending transactions.
4. **Witness inclusion**: When a witness creates a block and includes the transaction, the block is broadcast.
5. **Dedup on block receipt**: When we receive a block containing a transaction that is in our mempool, we remove that transaction from the mempool (we have a separate index for working with transactions).
6. **Retranslation**: If we receive a new transaction from an "our fork" peer, we retranslate it to all other "our fork" peers (excluding the sender).

This provides natural deduplication: transactions are tracked in the mempool until they appear in a confirmed block, then pruned.

**Transaction expiry pruning**: Transactions have an `expiration` field (`fc::time_point_sec`). Expired transactions MUST be pruned from the P2P mempool — they will never be included in a block. The P2P layer checks mempool entries periodically and removes any where `expiration < now`.

**Transaction fork linkage (TaPoS)**: Transactions reference a specific block via `ref_block_num` + `ref_block_prefix` (TaPoS — Transactions as Proof of Stake). The chain validates this in `_apply_transaction()` (database.cpp:4547-4554) by checking against `block_summary_object`. If `ref_block_num` points to a block NOT in our fork (e.g., on a competing fork), the TaPoS check fails → the transaction is invalid for us. **Therefore**: when we switch forks or detect that a transaction's `ref_block_num` references a block not on our chain, we MUST prune that transaction from the P2P mempool.

**Current system already has these mechanisms (but at the chain level, not P2P level)**:
- `_pending_tx` (database.hpp:504): `vector<signed_transaction>` — the witness mempool. Used during `generate_block()` to select transactions for the next block.
- `transaction_object` / `transaction_index` (transaction_object.hpp): Chainbase persistent index with `by_trx_id` (hashed, for dedup) and `by_expiration` (ordered, for cleanup). Created when a transaction is included in a block.
- `clear_expired_transactions()` (database.cpp:5944-5953): Already prunes expired `transaction_object` entries during block processing.
- TaPoS check (database.cpp:4547-4554): Verifies `ref_block_num`/`ref_block_prefix` against `block_summary_object`. Rejects transactions referencing unknown/fork blocks.

**The new DLT P2P mempool is SEPARATE from the chain's `_pending_tx`**: The P2P mempool sits at the network layer (before chain acceptance). It needs its own expiry + fork-alignment pruning because the chain's mechanisms only apply after a transaction is accepted into `_pending_tx`. A transaction that arrives from a peer might be expired or reference a fork block — we should prune it at the P2P layer before even trying to push it to the chain.

**Comparison with old P2P transaction flow**: The old P2P does NOT work the same way. The old flow uses:
- **Inventory gossip**: Advertises transaction IDs via `item_ids_inventory_message` → peer requests if missing. Not direct relay.
- **No separate mempool**: Chain's `accept_transaction()` handles dedup internally — no explicit P2P-level mempool index.
- **Broadcast to ALL peers**: `node->broadcast(trx_message(tx))` sends to every connected peer, not just "our fork".
- **No mempool pruning**: When a transaction appears in a block, there's no explicit removal from a P2P mempool — the chain just won't accept it again.
- **No expiry pruning at P2P level**: Expired transactions are only cleaned up by the chain's `clear_expired_transactions()`, not at the P2P layer.

The new DLT P2P flow is simpler and more efficient: direct relay (no inventory gossip), separate P2P mempool index (explicit dedup, expiry pruning, and fork-alignment pruning), and targeted broadcast ("our fork" peers only).

### 3.3 Initial Sync / Gap Filling

**DLT stores only the top head block range** — older blocks are pruned. For example, if the node holds blocks [200..500], blocks [1..199] are already pruned and NOT stored. This is how DLT works: only the latest `dlt-block-log-max-blocks` range is retained.

**Bulk get with last-block-info**: The `get` request works in bulk (request a range of blocks). The reply includes the requested blocks PLUS additional info only about the **last block in the bulk** (its hash, next_available, sync_status). This keeps replies compact.

**Gap handling for new/recovery nodes**:
- A new or recovering node tries to fetch all needed blocks from known peers.
- If a gap is discovered (missing blocks that no connected peer has), the node MUST **reset its state** and go forward by **importing a snapshot from trusted peers**.
- There is no partial gap-filling across disconnected ranges — either the chain is contiguous from the peer's perspective, or the node resyncs from a snapshot.

### 3.4 IMPORTANT: Multiple Simultaneous Peers
The proposal describes 1:1 exchange but not multi-peer coordination:
- What if peer A gives us block 100 and peer B gives us a different block 100?
- The fork_db handles this, but the P2P layer needs to know when to stop requesting from a peer
- **Recommendation**: Track per-peer "last good block" and prefer peers that give us chain-extending blocks

### 3.5 IMPORTANT: "Our fork" determination and Node Sync/Forward Model

The node operates in two primary statuses:

**1. SYNC status** (catching up):
- We send hello to peers and try to find the next continuation after our head block.
- We request blocks in ranges after our head from connected peers.
- When we reach the top (head = no more blocks available from any peer) → transition to FORWARD status.
- During sync, we don't care about fork alignment — we just need blocks that continue our chain.

**2. FORWARD status** (caught up, exchanging data):
- We exchange blocks and transactions with "our fork" peers.
- If a fork is detected (block doesn't link to our head), we try to get the competing blocks.
- The competing blocks may be true for our consensus or false — it's OK, we just need to **mark this peer as "our" or "not our"**.
- "Our fork" peer = their head or LIB hash is in our `dlt_block_log` or `fork_db`.
- "Not our fork" peer = their blocks don't link to anything we know.

**We don't care if initiator's head is above ours** — they're just ahead, that's fine. Strange things happen on the network.

**Fork status during forward mode**:
- `fork_status = 0` (normal): Peer's blocks extend our chain → "our fork", full exchange.
- `fork_status = 1` (looking_resolution): Peer's blocks create a fork → still exchange, but mark peer. Let `fork_db` + vote-weight resolution determine winner.
- `fork_status = 2` (minority): We're on the losing fork → stop producing, switch.

**Edge case: initiator's LIB is below our dlt_start**: We can't verify their fork alignment → treat as "unknown" during sync (allow exchange to get blocks). Once in forward mode, re-evaluate after we have their blocks.

### 3.6 Color-Coded Logging — Good Design
Existing color codes already defined (line 79-83 in node.cpp):
```
CLOG_RED    = fork blocks
CLOG_ORANGE = warnings
CLOG_GRAY   = transactions
CLOG_GREEN  = sync/production
CLOG_CYAN   = diagnostics
```
The user's proposal adds:
- Green: sync start, catchup, block production
- White: normal block exchange from our fork
- Red: fork block exchange
- Dark gray: transaction exchange

**Assessment**: These map well to existing colors. Just need consistency.

### 3.7 Security Hardening (from AI Review)

**P0 — Mempool DoS protection**: Hard mempool size limits with eviction policy:
- `max_transactions` (default 10000) — hard cap on mempool entries
- `max_memory_bytes` (default 100MB) — hard cap on total mempool memory
- `max_tx_size` (default 64KB) — reject oversized transactions at P2P layer
- `max_expiration_headroom` (default 24h) — reject transactions with `expiration` too far in the future
- Eviction policy: when caps are hit, evict oldest-expiry transactions first
- All rejections increment sender's `spam_strikes`

**P1 — Peer exchange poisoning protection**: Anti-sybil diversity requirements:
- `max_peers_per_subnet` (default 2 per /24) — prevent same-subnet domination
- `max_accept_from_exchange` (default 10 peers per exchange reply) — cap to prevent single-reply flooding
- `min_uptime_for_exchange` (default 600s) — peer must be connected and stable before its endpoint is shared in exchange replies
- Filter exchange replies: remove peers from same subnet if limit reached, cap reply size

**P1 — Fork resolution hysteresis**: The 42-block window alone can create a decision race at the boundary where both forks briefly appear as winners during network partition. Add confirmation requirement:
- After the 42-block window, compute the vote-weight winner
- The winner must maintain its lead for `CONFIRMATION_BLOCKS` (6) consecutive blocks before we execute the fork switch
- If the lead flips during confirmation, reset the counter — prevents premature switching

**P1 — Block validation ordering**: The `prev_block_id` check prevents chain link corruption, but we also need:
- `block.previous` must exist in our `fork_db` or `dlt_block_log` — reject blocks that reference unknown ancestors (prevents circular dependencies and future-reference attacks)
- Per-peer `expected_next_block` tracking — reject blocks that skip too far ahead (prevents hole-creation attacks)
- Blocks received but not yet validated: track with `pending_block_batch` timeout (30s) — if validation doesn't complete, soft-ban the peer

**P2 — Sync-to-forward transition race**: "No more blocks available from any peer" is ambiguous during temporary disconnects. Add sync stagnation detection:
- Track `last_block_received_time` during sync
- If no new block for `SYNC_STAGNATION_SEC` (30s), re-request from all connected peers (up to 3 retries)
- If still stagnated after retries → transition to FORWARD with warning log
- Prevents getting stuck in SYNC due to transient network issues

**P2 — Reconnection backoff amplification**: Add jitter and reset conditions:
- Add random jitter (±25% of backoff interval) to prevent synchronized reconnection storms
- Reset `reconnect_backoff_sec` to initial value (30s) when a connection stays stable for > 5 minutes — prevents persistent long backoff from transient disconnects

**P2 — Transaction fork-awareness during sync**: Transactions received during SYNC mode reference a chain that may get reorganized during catchup. Tag them as provisional:
- `mempool_entry.is_provisional = true` for transactions received during sync
- `mempool_entry.expected_head` = our head at time of receipt
- On SYNC→FORWARD transition: revalidate all provisional entries against final chain, prune those whose TaPoS is now invalid

**P3 — Protocol version negotiation**: Add `protocol_version` field to `dlt_hello_message` (start at 1). Peers with different major versions disable exchange. Allows future protocol upgrades without breaking compatibility.

**P3 — Peer lifecycle state machine**: Explicit states with timeouts:
- `connecting` (5s timeout) → `handshaking` (10s timeout) → `syncing` → `active` → `disconnected` (backoff) → `banned` (duration from spam threshold)
- Each transition has a timeout — if stuck in any intermediate state, disconnect and move to `disconnected`

---

## 4. Implementation Plan

### Phase 1: New Message Types (libraries/network/include/graphene/network/)

**Task 1.1**: Define new message types in `core_messages.hpp` (after type 5018):
```
dlt_hello_message_type               = 5100  // replaces hello for DLT peers
dlt_hello_reply_message_type         = 5101  // response to dlt_hello
dlt_range_request_message_type       = 5102  // "do you have block N?"
dlt_range_reply_message_type         = 5103  // "yes, my range is [S..E]"
dlt_get_block_range_message_type     = 5104  // "send me blocks [N..M]" (bulk fetch)
dlt_block_range_reply_message_type   = 5105  // blocks [N..M] + last-block info only
dlt_get_block_message_type           = 5106  // "send me block N, prev=H" (single block)
dlt_block_reply_message_type         = 5107  // block + next_available + sync_status
dlt_not_available_message_type       = 5108  // "I don't have that block"
dlt_fork_status_message_type         = 5109  // fork resolution status
dlt_peer_exchange_request_type       = 5110  // "send me your 'our fork' friends"
dlt_peer_exchange_reply_type         = 5111  // list of known peer endpoints
dlt_peer_exchange_rate_limited_type  = 5112  // "you already asked, wait N sec"
dlt_transaction_message_type         = 5113  // broadcast new transaction to peers
```

**Task 1.2**: Define message structs:

```cpp
// DLT Hello — sent on connection
struct dlt_hello_message {
    uint16_t       protocol_version;    // start at 1; major version mismatch disables exchange
    block_id_type  head_block_id;
    uint32_t       head_block_num;
    block_id_type  lib_block_id;
    uint32_t       lib_block_num;
    uint32_t       dlt_earliest_block;  // our dlt_block_log start
    uint32_t       dlt_latest_block;    // our dlt_block_log end (head)
    bool           emergency_active;
    bool           has_emergency_key;
    uint8_t        fork_status;  // 0=normal, 1=looking_resolution, 2=minority
    uint8_t        node_status;  // 0=sync (catching up), 1=forward (caught up, exchanging)
};

// DLT Hello Reply — response to hello
struct dlt_hello_reply_message {
    bool           exchange_enabled;      // "our fork" = true
    bool           fork_alignment;       // true = peer's blocks link to ours, false = not our fork
    block_id_type  initiator_head_seen;   // which of initiator's blocks we recognize
    block_id_type  initiator_lib_seen;
    uint32_t       our_dlt_earliest;
    uint32_t       our_dlt_latest;
    uint8_t        our_fork_status;
    uint8_t        our_node_status;      // 0=sync, 1=forward
};

// Block range request (bulk fetch)
struct dlt_get_block_range_message {
    uint32_t       start_block_num;
    uint32_t       end_block_num;
    block_id_type  prev_block_id;  // hash of block (start-1) for chain link verification
};

// Block range reply — reply includes all blocks + info only about the LAST block
struct dlt_block_range_reply_message {
    std::vector<signed_block> blocks;
    uint32_t       last_block_next_available;  // next block after last in range (0=none)
    bool           is_last;                    // true = last block we have, peer fully synced
};

// Transaction broadcast
struct dlt_transaction_message {
    signed_transaction trx;
};

// Single block request (non-bulk, for individual blocks)
struct dlt_get_block_message {
    uint32_t       block_num;
    block_id_type  prev_block_id;  // to verify chain link
};

// Single block reply
struct dlt_block_reply_message {
    signed_block   block;
    uint32_t       next_available;  // 0 = no more blocks
    bool           is_last;         // true = last block we have, peer fully synced
};

// Not available
struct dlt_not_available_message {
    uint32_t       block_num;
};

// Peer exchange request — "send me your 'our fork' friends"
// Rate-limited: responder ignores if asked more than once per 10 min
struct dlt_peer_exchange_request {
    // empty — the request itself implies intent
};

// Peer exchange reply — list of known "our fork" peer endpoints
struct dlt_peer_exchange_reply {
    struct peer_endpoint_info {
        fc::ip::endpoint  endpoint;
        node_id_t         node_id;
    };
    std::vector<peer_endpoint_info> peers;
};

// Rate-limit response — "you already asked, wait N seconds"
struct dlt_peer_exchange_rate_limited {
    uint32_t       wait_seconds;  // how long until next allowed request
};
```

### Phase 2: New DLT P2P Node Class (libraries/network/)

**Task 2.1**: Create `dlt_p2p_node.hpp` — new class `dlt_p2p_node`:
- Peer array from config (seeds) + dynamically discovered peers
- Per-peer state: `dlt_peer_state` struct
  - `node_status` (0=sync, 1=forward)
  - `exchange_enabled` (our fork)
  - `fork_alignment` (true = blocks link to ours)
  - `peer_head_num`, `peer_head_id`, `peer_lib_num`, `peer_lib_id`
  - `peer_dlt_range_start`, `peer_dlt_range_end`
  - `peer_emergency_active`, `peer_has_emergency_key`
  - `peer_node_status` (0=sync, 1=forward)
  - `spam_strikes` (single counter, reset on good packet)
  - `last_good_packet_time`
  - `last_peer_exchange_request_time` (per-peer rate-limit, 600s cooldown)
  - `pending_requests` (what we asked this peer for)
  - `expected_next_block` (for block ordering validation)
  - `pending_block_batches` (blocks received but not yet validated, with 30s timeout)
  - `last_connection_duration` (for backoff reset on stable connections > 5min)
  - **Peer lifecycle state machine**:
    - `peer_lifecycle_state` (connecting → handshaking → syncing → active → disconnected → banned)
    - Each intermediate state has a timeout (connecting=5s, handshaking=10s)
    - Timeout → disconnect and move to `disconnected`
  - **Reconnection tracking**:
    - `connection_state` (connected / disconnected / banned)
    - `disconnected_since` (fc::time_point, set on disconnect)
    - `next_reconnect_attempt` (fc::time_point, incrementing backoff)
    - `reconnect_backoff_sec` (starts at 30s, doubles each retry, capped at 3600s)
- `_known_peers`: in-memory set of `{fc::ip::endpoint, node_id_t}` — non-persistent. Peers are NOT immediately removed on disconnect; they are flagged `disconnected` and reconnection is attempted with backoff. A peer is permanently removed only after **8 hours of continuous non-response** (configurable via `dlt-peer-max-disconnect-hours`).
- Main loop: process incoming messages, periodic peer discovery (every ~10 min send exchange request to one peer), periodic reconnection attempts for disconnected peers.
- **Node status transitions**:
  - Start in SYNC mode: send hello to peers, request blocks after our head.
  - SYNC → FORWARD: when no more blocks available from any peer (reached the top), transition to FORWARD and start exchanging data + enable peer discovery.
  - FORWARD → SYNC: if we detect a long fork or need to resync (rare, e.g. after snapshot import).
- No dedicated thread — use fc::asio or simple poll loop

**Task 2.2**: Create `dlt_p2p_node.cpp` — implement:

**Normal mode handlers**:
```
on_dlt_hello()                  → check peer's chain link to ours; set fork_alignment; if our node is SYNC, request blocks after our head; if FORWARD, mark peer as "our"/"not our"; check protocol_version compatibility
on_dlt_hello_reply()            → if exchange_enabled && fork_alignment, start catching up (if SYNC) or begin exchange (if FORWARD)
on_dlt_get_block_range()        → bulk fetch: request blocks [N..M], reply with blocks + last-block info only
on_dlt_block_range_reply()      → validate prev_hash link on first block, validate block ordering (previous must be known), push all to chain, retranslate; if last block reached and we're SYNC → transition to FORWARD
on_dlt_get_block()              → read from dlt_block_log, send block_reply or not_available
on_dlt_block_reply()            → validate prev_hash link, validate block ordering, push to chain, retranslate
on_dlt_transaction()            → add to P2P mempool (dedup by tx_id, check expiry, check TaPoS fork alignment, check size limits), retranslate to all "our fork" peers (excl. sender)
on_dlt_peer_exchange_request()  → if peer asked in last 600s, send rate_limited; else send our known "our fork" peers (filtered: min_uptime, subnet diversity, cap reply size)
on_dlt_peer_exchange_reply()    → add received peer endpoints to our known-peers set (filtered: subnet diversity, cap per reply), attempt connections to new ones
on_block_added_to_chain()       → remove any transactions in this block from P2P mempool (separate mempool index); if fork switch occurred, prune transactions whose `ref_block_num` is not on our fork
transition_to_forward()         → when sync reaches top (no more blocks from any peer, or sync stagnation timeout): set node_status=FORWARD, start exchange, enable peer discovery; revalidate provisional mempool entries
periodic_mempool_cleanup()      → prune expired transactions (`expiration < now`) and TaPoS-invalid transactions (`ref_block_num` not on our fork) from P2P mempool; enforce mempool size limits with eviction
sync_stagnation_check()         → if no new block for 30s during sync, re-request from all peers (up to 3 retries); if still stagnated → transition_to_forward with warning
peer_lifecycle_timeout()        → check peer lifecycle state timeouts (connecting=5s, handshaking=10s); if stuck, disconnect and move to disconnected
block_validation_timeout()      → if pending_block_batch not validated within 30s, soft-ban the peer that sent it
```

**Peer discovery logic**:
```cpp
// Per-peer rate-limit tracking
fc::time_point _last_peer_exchange_request_time;
static constexpr uint32_t PEER_EXCHANGE_COOLDOWN_SEC = 600; // 10 minutes

void dlt_p2p_node::on_dlt_peer_exchange_request(peer_id peer) {
    auto now = fc::time_point::now();
    if (_last_peer_exchange_request_time != fc::time_point() &&
        now - _last_peer_exchange_request_time < fc::seconds(PEER_EXCHANGE_COOLDOWN_SEC)) {
        uint32_t wait = PEER_EXCHANGE_COOLDOWN_SEC -
            (now - _last_peer_exchange_request_time).count() / 1000000;
        send_message(peer, dlt_peer_exchange_rate_limited{wait});
        return;
    }
    _last_peer_exchange_request_time = now;

    // Collect "our fork" peers (exchange_enabled=true, in FORWARD mode)
    std::vector<peer_endpoint_info> friends;
    for (auto& [id, state] : _peer_states) {
        if (state.exchange_enabled && state.node_status == NODE_STATUS_FORWARD) {
            friends.push_back({state.endpoint, id});
        }
    }
    send_message(peer, dlt_peer_exchange_reply{friends});
}

void dlt_p2p_node::on_dlt_peer_exchange_reply(const dlt_peer_exchange_reply& reply) {
    for (auto& info : reply.peers) {
        if (!is_known_peer(info.node_id) && !is_connected_to(info.node_id)) {
            _known_peers.insert(info);
            // Attempt connection if under max_connections
            if (_active_connections.size() < _max_connections) {
                connect_to_peer(info.endpoint);
            }
        }
    }
}

// Reconnection logic for disconnected peers
void dlt_p2p_node::handle_disconnect(peer_id peer) {
    auto& state = _peer_states[peer];
    state.peer_lifecycle_state = PEER_LIFECYCLE_DISCONNECTED;
    state.disconnected_since = fc::time_point::now();

    // Reset backoff to initial if the connection was stable (>5 min)
    if (state.last_connection_duration > 300) {  // 5 minutes stable
        state.reconnect_backoff_sec = INITIAL_RECONNECT_BACKOFF_SEC;  // 30s
    }

    state.reconnect_backoff_sec = std::min(state.reconnect_backoff_sec * 2, MAX_RECONNECT_BACKOFF_SEC); // capped at 3600s

    // Add random jitter (±25%) to prevent synchronized reconnection storms
    uint32_t jitter = (rand() % (state.reconnect_backoff_sec / 2)) - (state.reconnect_backoff_sec / 4);
    state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(state.reconnect_backoff_sec + jitter);
    // Do NOT remove from _known_peers — peer stays known for reconnection
}

void dlt_p2p_node::periodic_reconnect_check() {
    auto now = fc::time_point::now();
    auto expire_threshold = now - fc::hours(PEER_MAX_DISCONNECT_HOURS); // 8 hours default

    for (auto it = _known_peers.begin(); it != _known_peers.end(); ) {
        auto& state = _peer_states[it->node_id];
        if (state.peer_lifecycle_state == PEER_LIFECYCLE_DISCONNECTED) {
            // Permanently remove if no response for 8 hours
            if (state.disconnected_since < expire_threshold) {
                wlog("Removing peer ${p} after ${h}h of non-response", ("p", it->node_id)("h", PEER_MAX_DISCONNECT_HOURS));
                it = _known_peers.erase(it);
                _peer_states.erase(it->node_id);
                continue;
            }
            // Attempt reconnection if backoff timer has elapsed
            if (now >= state.next_reconnect_attempt && _active_connections.size() < _max_connections) {
                ilog("Attempting reconnect to peer ${p} (backoff=${b}s)", ("p", it->node_id)("b", state.reconnect_backoff_sec));
                connect_to_peer(it->endpoint);
            }
        }
        ++it;
    }
}
```

**Fork mode handlers**:
```
check_fork_resolution_needed()  → called each block; if multiple forks exist, set fork_status=looking_resolution
resolve_fork_at_round_end()     → called when schedule round completes; tally vote weights per fork branch
```

**Emergency mode handlers**:
```
on_emergency_status_change() → update peer flags, adjust fork choice priority
```

**Task 2.3**: Anti-spam implementation:
```cpp
bool dlt_p2p_node::record_packet_result(peer_id peer, bool is_good) {
    if (is_good) {
        peer_state[peer].spam_strikes = 0;
        peer_state[peer].last_good_packet_time = now();
        return true;
    }
    peer_state[peer].spam_strikes++;
    if (peer_state[peer].spam_strikes >= SPAM_THRESHOLD) {
        soft_ban_peer(peer, BAN_DURATION);
        return false;
    }
    return true;
}
```

### Phase 3: P2P Plugin Replacement (plugins/p2p/)

**Task 3.1**: Replace `p2p_plugin_impl` in `p2p_plugin.cpp` — swap internal implementation:
- Appbase plugin requiring `chain::plugin` (same as current)
- Replace internal `graphene::network::node` with `dlt_p2p_node`
- Configuration: keep `p2p-endpoint` (port 2001/4243), `p2p-seed-node`; add `dlt-block-log-max-blocks`, `dlt-peer-max-disconnect-hours`, `dlt-mempool-max-tx`, `dlt-mempool-max-bytes`, `dlt-mempool-max-tx-size`, `dlt-mempool-max-expiration-hours`, `dlt-peer-exchange-max-per-reply`, `dlt-peer-exchange-max-per-subnet`, `dlt-peer-exchange-min-uptime-sec`
- Remove old config: `p2p-stats-enabled`, `p2p-stats-interval`, `p2p-stale-sync-detection`, `p2p-stale-sync-timeout-seconds` (replaced by built-in DLT mechanisms)
- Public API stays identical: `broadcast_block()`, `broadcast_block_post_validation()`, `broadcast_transaction()`, `broadcast_chain_status()`, `set_block_production()`, `resync_from_lib()`, `trigger_resync()`, `get_connections_count()`, `reconnect_seeds()`, `pause_block_processing()`, `resume_block_processing()`, `get_last_network_block_time()`
- **Transaction lifecycle**:
  - Transactions enter via `jsonrpc` plugin → added to P2P mempool (separate in-memory index, dedup by tx_id)
  - On receipt: check expiry (`expiration < now` → discard), check TaPoS fork alignment (`ref_block_num` not on our fork → discard)
  - Broadcast transaction to all "our fork" peers (excl. sender)
  - On new block (witness produces): broadcast block to all "our fork" peers
  - On block receipt: remove any transactions in this block from P2P mempool (using separate mempool index)
  - On fork switch: prune P2P mempool transactions whose `ref_block_num` is not on our fork
- Periodic: prune P2P mempool (expired + TaPoS-invalid), prune dlt_block_log (batch-prune 10000 at a time via `truncate_before()`), update peer status, reconnect to disconnected peers (backoff 30s→…→3600s), resolve forks, remove stale peers (8h non-response)

**Task 3.2**: Color-coded console logging:
```cpp
#define DLT_LOG_GREEN   "\033[32m"  // sync, production
#define DLT_LOG_WHITE   "\033[37m"  // normal block exchange
#define DLT_LOG_RED     "\033[91m"  // fork block
#define DLT_LOG_DGRAY   "\033[90m"  // transaction exchange
#define DLT_LOG_RESET   "\033[0m"

// Usage:
ilog(DLT_LOG_GREEN "Starting sync from peer ${p}" DLT_LOG_RESET, ("p", peer));
ilog(DLT_LOG_WHITE "Got block #${n} from our fork peer ${p}" DLT_LOG_RESET, ("n", num)("p", peer));
ilog(DLT_LOG_RED "FORK: Got block #${n} from ${p} — does NOT link to our head!" DLT_LOG_RESET, ("n", num)("p", peer));
ilog(DLT_LOG_DGRAY "Got transaction ${id} from ${p}" DLT_LOG_RESET, ("id", txid)("p", peer));
```

### Phase 4: Fork Resolution Implementation

**Task 4.1**: Fork resolution trigger (confirmed: `num_scheduled_witnesses * 2`):
```cpp
// Fork resolution fires after 42 blocks (2 full rounds) since first detection.
// Same threshold as emergency DLT minority fork detection (CHAIN_MAX_WITNESSES * 2).
static constexpr uint32_t FORK_RESOLUTION_BLOCK_THRESHOLD = CHAIN_MAX_WITNESSES * 2;

struct fork_branch_info {
    block_id_type tip;
    std::vector<block_id_type> blocks;  // blocks in this branch
    std::set<account_name_type> witnesses;
    share_type total_vote_weight;
    bool has_emergency_blocks;
    uint32_t block_count;
};

// Called each time a block is applied:
void dlt_p2p_node::track_fork_state(const signed_block& block) {
    auto competing = fork_db.fetch_block_by_number(block.block_num());
    if (competing.size() > 1) {
        if (!_fork_detected) {
            _fork_detected = true;
            _fork_detection_block_num = block.block_num();
        }
    }

    // Resolution trigger: 42 blocks (2 rounds) since fork was first detected
    if (_fork_detected && block.block_num() - _fork_detection_block_num >= FORK_RESOLUTION_BLOCK_THRESHOLD) {
        resolve_fork();
        _fork_detected = false;
    }
}
```

**Task 4.2**: Fork resolution logic (uses confirmed `fork_db.get_all_active_branch_tips()` + existing `compare_fork_branches()` + hysteresis confirmation):
```cpp
struct fork_resolution_state {
    block_id_type current_winner_tip;
    uint32_t consecutive_blocks_as_winner = 0;
    static constexpr uint32_t CONFIRMATION_BLOCKS = 6;  // Must maintain lead for 6 blocks

    bool is_confirmed() const {
        return consecutive_blocks_as_winner >= CONFIRMATION_BLOCKS;
    }
};

void dlt_p2p_node::resolve_fork() {
    // Get all distinct branch tips from fork_db
    auto branch_tips = fork_db.get_all_active_branch_tips();
    if (branch_tips.size() < 2) return;  // no fork to resolve

    fork_branch_info winner;
    share_type max_weight = 0;

    for (auto& tip : branch_tips) {
        auto info = compute_branch_info(tip, fork_db);
        // +10% bonus to longer chain (reuses existing compare_fork_branches logic)
        if (info.block_count > winner.block_count) {
            info.total_vote_weight += info.total_vote_weight / 10;
        }
        if (info.total_vote_weight > max_weight) {
            max_weight = info.total_vote_weight;
            winner = info;
        }
    }

    // Hysteresis: winner must maintain lead for CONFIRMATION_BLOCKS before we switch
    if (winner.tip == _fork_state.current_winner_tip) {
        _fork_state.consecutive_blocks_as_winner++;
    } else {
        _fork_state.current_winner_tip = winner.tip;
        _fork_state.consecutive_blocks_as_winner = 1;
    }

    if (!_fork_state.is_confirmed()) {
        ilog(DLT_LOG_ORANGE "Fork resolution: candidate ${t} has ${n}/${c} confirmations",
             ("t", winner.tip)("n", _fork_state.consecutive_blocks_as_winner)("c", fork_resolution_state::CONFIRMATION_BLOCKS));
        return;  // Not confirmed yet, wait
    }

    if (our_head_is_on_branch(winner)) {
        // We're on the majority fork — continue
        _fork_status = FORK_STATUS_NORMAL;
        ilog(DLT_LOG_GREEN "Fork resolved: we are on majority fork (weight=${w})" DLT_LOG_RESET, ("w", max_weight));
    } else {
        // We're on minority fork — switch or stop producing
        _fork_status = FORK_STATUS_MINORITY;
        wlog(DLT_LOG_RED "We are on MINORITY fork! Stopping production, switching to majority." DLT_LOG_RESET);
        switch_to_branch(winner.tip);
        notify_witness_plugin_minority_fork();
    }

    // Reset hysteresis state after resolution
    _fork_state = fork_resolution_state{};
}
```

### Phase 5: In-Place Replacement (No Additional Port)

The new DLT P2P system **replaces** the old `node.cpp`-based P2P — it does NOT run alongside it on a separate port. Reasons:
- Old P2P and new DLT P2P use different wire protocols (old: Graphene hello/synopsis/inventory; new: DLT hello/range/exchange types 5100-5113). They cannot communicate with each other — running both on different ports creates two isolated sub-networks.
- All witnesses can switch simultaneously (system is in emergency mode), so no gradual migration period is needed.
- The `witness` and `witness_guard` plugins have hard dependencies on `p2p_plugin` via `APPBASE_PLUGIN_REQUIRES`. Replacing the internals avoids any changes to those plugins.

**Task 5.1**: Replace `p2p_plugin` implementation in-place:
- Keep the same plugin name `"p2p"` and class name `p2p_plugin`
- Keep the same public API: `broadcast_block()`, `broadcast_block_post_validation()`, `broadcast_transaction()`, `broadcast_chain_status()`, `set_block_production()`, `resync_from_lib()`, `trigger_resync()`, `get_connections_count()`, `reconnect_seeds()`, `pause_block_processing()`, `resume_block_processing()`, `get_last_network_block_time()`
- Replace internal `p2p_plugin_impl` (which wraps `graphene::network::node`) with a new impl that wraps `dlt_p2p_node`
- Keep the same `p2p-endpoint` config option (default port 2001 mainnet, 4243 testnet)
- Keep `p2p-seed-node` config option (seed peers)
- Add new config options: `dlt-block-log-max-blocks`, `dlt-peer-max-disconnect-hours`, `dlt-mempool-max-tx`, `dlt-mempool-max-bytes`, `dlt-mempool-max-tx-size`, `dlt-mempool-max-expiration-hours`, `dlt-peer-exchange-max-per-reply`, `dlt-peer-exchange-max-per-subnet`, `dlt-peer-exchange-min-uptime-sec`
- Remove old config options that are no longer relevant: `p2p-stats-enabled`, `p2p-stats-interval`, `p2p-stale-sync-detection`, `p2p-stale-sync-timeout-seconds` (replaced by built-in DLT mechanisms)
- `witness`, `witness_guard`, and `snapshot` plugins require **zero code changes** — they consume the same `p2p_plugin` interface

**Task 5.2**: Remove old `node.cpp`-based code paths:
- `p2p_plugin_impl` no longer inherits from `graphene::network::node_delegate`
- The `graphene::network::node` class and `node.cpp` are no longer used by the plugin (can be kept in the library for reference but not linked into the build)
- Old message types (synopsis, inventory gossip, etc.) are no longer processed

**Task 5.3**: Testing:
- Unit tests for message serialization
- Integration test: 3-node network with fork injection
- Emergency mode activation test
- Anti-spam threshold test
- Verify witness plugin works with replaced p2p_plugin (no API changes)

---

## 5. Summary of Improvements Needed in User's Proposal

| Issue | Proposed Fix |
|-------|-------------|
| Transaction propagation definition | CONFIRMED: jsonrpc → P2P mempool → all "our fork" peers. Dedup when block confirms transaction (separate mempool index). Expired transactions pruned from P2P mempool. TaPoS fork-alignment check: if `ref_block_num` is not on our fork → prune from P2P mempool. |
| Single-block fetch is slow for bulk sync | CONFIRMED: Bulk `get_range` with info only about last block in the bulk. |
| "End of schedule round" ambiguous during forks | CONFIRMED: Use `num_scheduled_witnesses * 2` (42 blocks = 2 rounds), same threshold as existing emergency DLT minority fork detection. |
| "Our fork" determination needs 3 states | CONFIRMED: Node has 2 statuses (sync/forward). In sync: just get blocks after our head. In forward: mark peer as "our" or "not our" based on chain link. Don't care if peer is ahead. LIB below dlt_start = "unknown" during sync, re-evaluate in forward mode. |
| Peer disconnect handling | CONFIRMED: Flag disconnected, reconnect with incrementing backoff (30s→60s→…→3600s max). Remove after 8 hours of non-response. |
| dlt_block_log pruning is expensive | CONFIRMED: Modify `truncate_before()` to batch-prune 10000 blocks at once instead of 1000. |
| DLT gap filling / initial sync | CONFIRMED: DLT stores only top head range. Gaps → reset state + import snapshot from trusted peers. |
| Dual-mode migration with additional port | CONFIRMED: No additional port needed. In-place replacement — keep same `p2p` plugin name, same `p2p-endpoint` port (2001/4243), same public API. Replace internals only (`node.cpp` → `dlt_p2p_node`). `witness`/`witness_guard`/`snapshot` plugins require zero changes. |
| Mempool DoS protection | P0: Hard size limits (max_transactions=10000, max_memory=100MB, max_tx_size=64KB), max expiration 24h, eviction by oldest-expiry |
| Peer exchange poisoning | P1: Anti-sybil: max 2 peers per /24 subnet, max 10 peers per exchange reply, min 600s uptime before sharing |
| Fork resolution race condition | P1: Add hysteresis — winner must maintain lead for 6 consecutive blocks after 42-block window before switch executes |
| Block validation ordering | P1: block.previous must be known, per-peer expected_next_block tracking, pending_block_batch 30s timeout |
| Sync-to-forward transition race | P2: Sync stagnation detection — 30s no-block timeout, 3 retries, then transition with warning |
| Reconnection backoff amplification | P2: Add jitter (±25%), reset backoff on stable connection > 5min |
| Transaction fork-awareness during sync | P2: Tag sync-mode transactions as provisional, revalidate on SYNC→FORWARD transition |
| Protocol version negotiation | P3: `protocol_version` field in hello, major version mismatch disables exchange |
| Peer lifecycle state machine | P3: connecting(5s)→handshaking(10s)→syncing→active→disconnected→banned with timeouts |

---

## 6. File Structure

```
libraries/network/
├── include/graphene/network/
│   ├── dlt_p2p_messages.hpp       # New message types
│   ├── dlt_p2p_node.hpp           # New node class
│   └── dlt_p2p_peer_state.hpp     # Per-peer state struct
├── dlt_p2p_messages.cpp           # Message serialization
├── dlt_p2p_node.cpp               # Core network logic
└── CMakeLists.txt                 # Updated

plugins/p2p/                          # In-place replacement — same plugin name, same API
├── include/graphene/plugins/p2p/
│   └── p2p_plugin.hpp              # Same header, same public API
├── p2p_plugin.cpp                  # Replaced impl: wraps dlt_p2p_node instead of node.cpp
└── CMakeLists.txt
```
