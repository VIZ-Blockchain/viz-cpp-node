# P2P Message Reference

DLT P2P uses a binary protocol over raw TCP. Each message is framed with a 4-byte little-endian header containing the message type ID, followed by a length-prefixed data payload serialized with FC reflection.

**Header file:** [libraries/network/include/graphene/network/dlt_p2p_messages.hpp](../../libraries/network/include/graphene/network/dlt_p2p_messages.hpp)

---

## Message Type Summary

| Type ID | Name | Direction | Purpose |
|---------|------|-----------|---------|
| 5100 | `dlt_hello_message` | initiator → acceptor | Initial handshake — chain state and capabilities |
| 5101 | `dlt_hello_reply_message` | acceptor → initiator | Handshake reply — fork alignment, exchange status |
| 5102 | `dlt_range_request_message` | any | Ask peer if it has a specific block |
| 5103 | `dlt_range_reply_message` | any | Response: block availability range |
| 5104 | `dlt_get_block_range_message` | syncing → peer | Request a range of blocks (bulk sync) |
| 5105 | `dlt_block_range_reply_message` | peer → syncing | Bulk block delivery |
| 5106 | `dlt_get_block_message` | any | Request a single block |
| 5107 | `dlt_block_reply_message` | any | Single block delivery |
| 5108 | `dlt_not_available_message` | any | Requested block not available |
| 5109 | `dlt_fork_status_message` | any | Live chain state update (head, LIB, fork, DLT range) |
| 5110 | `dlt_peer_exchange_request` | any | Request peer address list |
| 5111 | `dlt_peer_exchange_reply` | any | Peer address list response |
| 5112 | `dlt_peer_exchange_rate_limited` | any | Rate-limit response to peer exchange request |
| 5113 | `dlt_transaction_message` | any | Transaction broadcast |
| 5114 | `dlt_soft_ban_message` | any → banned | Soft-ban notification before disconnect |
| 5115 | `dlt_gap_fill_request` | any | Request specific blocks to fill a gap |
| 5116 | `dlt_gap_fill_reply` | any | Gap fill block delivery |

---

## Enumerations

### `dlt_node_status`

| Value | Meaning |
|-------|---------|
| `DLT_NODE_STATUS_SYNC` (0) | Node is behind; actively pulling blocks from peers |
| `DLT_NODE_STATUS_FORWARD` (1) | Node is caught up; exchanges blocks via broadcast |

### `dlt_fork_status`

| Value | Meaning |
|-------|---------|
| `DLT_FORK_STATUS_NORMAL` (0) | On the majority fork |
| `DLT_FORK_STATUS_LOOKING_RESOLUTION` (1) | Fork detected; running resolution algorithm |
| `DLT_FORK_STATUS_MINORITY` (2) | Confirmed on a minority fork |

### `dlt_peer_lifecycle_state`

| Value | Meaning |
|-------|---------|
| `DLT_PEER_LIFECYCLE_CONNECTING` (0) | TCP connect in progress (5 s timeout) |
| `DLT_PEER_LIFECYCLE_HANDSHAKING` (1) | Hello exchange in progress (10 s timeout) |
| `DLT_PEER_LIFECYCLE_SYNCING` (2) | Exchanging sync blocks (`we_need_sync_items` or peer needs blocks from us) |
| `DLT_PEER_LIFECYCLE_ACTIVE` (3) | Fully synchronized; normal block/transaction exchange |
| `DLT_PEER_LIFECYCLE_DISCONNECTED` (4) | Not connected; eligible for reconnect after backoff |
| `DLT_PEER_LIFECYCLE_BANNED` (5) | Soft-banned; no reconnect until ban expires |

---

## Detailed Message Reference

### 5100 — `dlt_hello_message`

Sent immediately after TCP connection is established. Carries the initiating node's complete chain state and capabilities.

```cpp
struct dlt_hello_message {
    uint16_t      protocol_version;    // currently 1
    block_id_type head_block_id;
    uint32_t      head_block_num;
    block_id_type lib_block_id;
    uint32_t      lib_block_num;
    uint32_t      dlt_earliest_block;  // oldest block in our rolling DLT log (0 if none)
    uint32_t      dlt_latest_block;    // most recent block in our DLT log
    bool          emergency_active;    // emergency consensus is currently active
    bool          has_emergency_key;   // we hold the emergency committee private key
    uint8_t       fork_status;         // dlt_fork_status enum
    uint8_t       node_status;         // dlt_node_status enum (SYNC or FORWARD)
};
```

**Notes:**
- `dlt_earliest_block` is critical for DLT-range-aware fork alignment. The acceptor uses it to avoid requesting blocks that are no longer in the initiator's rolling window.
- `has_emergency_key` identifies the emergency master node. Other nodes may prioritize syncing from this peer during emergency consensus mode.

---

### 5101 — `dlt_hello_reply_message`

Sent by the acceptor in response to 5100. Completes the handshake.

```cpp
struct dlt_hello_reply_message {
    bool          exchange_enabled;    // true if we consider the initiator caught up
    bool          fork_alignment;      // true if the initiator is on the same fork
    block_id_type initiator_head_seen; // echo: initiator's head_block_id as we see it
    block_id_type initiator_lib_seen;  // echo: initiator's lib_block_id as we see it
    uint32_t      our_dlt_earliest;    // our earliest DLT block
    uint32_t      our_dlt_latest;      // our latest DLT block
    uint8_t       our_fork_status;     // dlt_fork_status enum
    uint8_t       our_node_status;     // dlt_node_status enum
};
```

**Fork alignment check is multi-tiered** to handle DLT-pruned block ranges:

| Case | Check performed |
|------|----------------|
| Peer has no blocks (`head_num == 0`) | → aligned |
| Peer head is in our DLT range | `is_block_known(peer.head_block_id)` |
| Peer head + 1 == our earliest | Read `our_earliest_block.previous == peer.head_block_id` |
| Fallback | `is_block_known(peer.lib_block_id)` |

**`exchange_enabled`** is `true` when the acceptor's fork_db contains the initiator's head block (i.e., the initiator is within the exchange window and on the same fork). Only exchange-enabled peers receive block and transaction broadcasts.

---

### 5102 — `dlt_range_request_message`

Asks a peer whether it has a specific block by number and/or ID.

```cpp
struct dlt_range_request_message {
    uint32_t      block_num;
    block_id_type block_id;   // hash of the block being asked about
};
```

---

### 5103 — `dlt_range_reply_message`

Response to 5102. Returns the serving range available from the peer.

```cpp
struct dlt_range_reply_message {
    uint32_t  range_start;   // earliest block the peer can serve
    uint32_t  range_end;     // latest block the peer can serve
    bool      has_blocks;    // false if the peer has no blocks at all
};
```

---

### 5104 — `dlt_get_block_range_message`

Requests a contiguous range of blocks during SYNC mode. Maximum 200 blocks per request.

```cpp
struct dlt_get_block_range_message {
    uint32_t      start_block_num;
    uint32_t      end_block_num;
    block_id_type prev_block_id;  // hash of block (start_block_num - 1); used to verify chain continuity
};
```

**Notes:**
- The serving peer validates that `blocks[0].previous == prev_block_id` before sending.
- The gap between `start_block_num` and the serving peer's `dlt_earliest_block` may require a bridging peer (see [P2P Overview — Gap Detection](./overview.md#sync-mode)).

---

### 5105 — `dlt_block_range_reply_message`

Response to 5104. Contains up to 200 blocks.

```cpp
struct dlt_block_range_reply_message {
    std::vector<signed_block> blocks;
    uint32_t                  last_block_next_available;  // next available block after this batch
    bool                      is_last;  // true if no more blocks are available on this peer
};
```

**`is_last = true`** triggers `transition_to_forward()` on the receiving side if the node has caught up.

---

### 5106 — `dlt_get_block_message`

Requests a single block by number.

```cpp
struct dlt_get_block_message {
    uint32_t      block_num;
    block_id_type prev_block_id;  // hash of (block_num - 1) for chain link verification
};
```

---

### 5107 — `dlt_block_reply_message`

Response to 5106.

```cpp
struct dlt_block_reply_message {
    signed_block  block;
    uint32_t      next_available;  // next block number the peer can serve (0 if at head)
    bool          is_last;         // true if this is the peer's head block
};
```

---

### 5108 — `dlt_not_available_message`

Sent when the peer cannot serve a requested block (block outside DLT log range, or block unknown).

```cpp
struct dlt_not_available_message {
    uint32_t  block_num;
};
```

The requesting node should look for another peer with the block in range or trigger gap fill / SYNC mode.

---

### 5109 — `dlt_fork_status_message`

Live chain state update. Sent when a node's head, LIB, DLT window, or fork status changes, and on SYNC → FORWARD transition.

```cpp
struct dlt_fork_status_message {
    uint8_t       fork_status;         // dlt_fork_status enum
    block_id_type head_block_id;
    uint32_t      head_block_num;
    block_id_type lib_block_id;
    uint32_t      lib_block_num;
    uint32_t      dlt_earliest_block;
    uint32_t      dlt_latest_block;
    uint8_t       node_status;         // dlt_node_status enum
};
```

**Key use case:** When a node transitions SYNC → FORWARD, it broadcasts this message to all connected peers so they can re-evaluate `exchange_enabled` for this node immediately rather than waiting for the next hello cycle.

The receiving node updates its local `dlt_peer_state` for the sender and re-checks fork alignment + exchange eligibility.

---

### 5110 — `dlt_peer_exchange_request`

Empty message requesting a list of known peers.

```cpp
struct dlt_peer_exchange_request {
    // empty
};
```

Rate limit: **3 requests per 5-minute sliding window** per peer. Violators receive `dlt_peer_exchange_rate_limited` (5112).

---

### 5111 — `dlt_peer_exchange_reply`

Response to 5110. Contains endpoint information for known peers.

```cpp
struct dlt_peer_endpoint_info {
    fc::ip::endpoint  endpoint;
    node_id_t         node_id;
};

struct dlt_peer_exchange_reply {
    std::vector<dlt_peer_endpoint_info> peers;
};
```

**Filters applied before inclusion in the reply:**
- Peer uptime ≥ `dlt-peer-exchange-min-uptime-sec` (default 600 s)
- Maximum `dlt-peer-exchange-max-per-subnet` (default 2) peers per /24 subnet
- `is_incoming` peers excluded (ephemeral source ports)
- Maximum `dlt-peer-exchange-max-per-reply` (default 10) peers total

---

### 5112 — `dlt_peer_exchange_rate_limited`

Sent instead of 5111 when the request rate limit is exceeded.

```cpp
struct dlt_peer_exchange_rate_limited {
    uint32_t  wait_seconds;  // how long the requester should wait before retrying
};
```

---

### 5113 — `dlt_transaction_message`

Carries a signed transaction for mempool propagation.

```cpp
struct dlt_transaction_message {
    signed_transaction trx;
};
```

Received transactions are added to the P2P mempool (filtered by expiry, TaPoS, size) before being pushed to the chain's `_pending_tx`. Successfully accepted transactions are relayed to all exchange-enabled peers.

---

### 5114 — `dlt_soft_ban_message`

Sent to a peer just before the connection is closed due to spam or protocol violation. The receiving peer enters BANNED state for `ban_duration_sec` and will not attempt to reconnect until the ban expires.

```cpp
struct dlt_soft_ban_message {
    uint32_t    ban_duration_sec;  // ban duration in seconds
    std::string reason;            // human-readable reason
};
```

Common reasons logged (color-coded orange/yellow on both sides):
- `"spam_strikes_exceeded"` — 10 invalid-packet strikes
- `"dead_fork_blocks"` — peer repeatedly sent blocks from a dead fork
- `"protocol_violation"` — unexpected message type or malformed data

---

### 5115 — `dlt_gap_fill_request`

Requests specific blocks to fill a gap detected in the node's block stream. Works in both SYNC and FORWARD modes.

```cpp
struct dlt_gap_fill_request {
    std::vector<uint32_t> block_nums;  // specific block numbers requested
};
```

**Constraints:**
- Maximum **100 blocks** per request (`GAP_FILL_MAX_BLOCKS`).
- **5-second cooldown** between requests from the same node (`GAP_FILL_COOLDOWN_SEC`).
- Larger gaps are requested in 100-block chunks; subsequent chunks are triggered on the next periodic task cycle.
- The serving peer reads blocks from its `dlt_block_log`; block numbers outside the serving range result in 5108.
- Serving accepts requests from any active peer (not only exchange-enabled ones).

Triggered from three locations:
1. `on_dlt_block_reply()` — out-of-order block detected
2. `periodic_task()` — proactive every-5s gap check
3. `resume_block_processing()` — after snapshot pause completes

---

### 5116 — `dlt_gap_fill_reply`

Response to 5115. Contains the requested blocks (may be a subset if some were unavailable).

```cpp
struct dlt_gap_fill_reply {
    std::vector<signed_block> blocks;  // requested blocks; may be partial
};
```

---

## Handshake Flow

```
Initiator                              Acceptor
    │                                     │
    │──TCP connect──────────────────────►│
    │                                     │
    │  5100 dlt_hello_message             │
    │──────────────────────────────────►│
    │  (head/lib, DLT range,              │
    │   emergency_active, node_status)    │
    │                                     │ check fork alignment
    │                                     │ set exchange_enabled
    │  5101 dlt_hello_reply_message       │
    │◄──────────────────────────────────│
    │  (exchange_enabled, fork_alignment, │
    │   our DLT range, our node_status)   │
    │                                     │
    ├── exchange_enabled = true ──────────┼── FORWARD or sync exchange begins
    └── exchange_enabled = false ─────────┴── initiator enters SYNC mode
```

---

## Sync Mode Flow

```
Syncing Node                           Serving Peer
    │                                     │
    │  5104 dlt_get_block_range           │
    │  (start=our_head+1, end=+200,       │
    │   prev_block_id=our_head_id)        │
    │──────────────────────────────────►│
    │                                     │ reads dlt_block_log
    │  5105 dlt_block_range_reply         │
    │◄──────────────────────────────────│
    │  (blocks=[N+1..N+200], is_last)     │
    │                                     │
    │  apply each block                   │
    │  if is_last → transition_to_forward │
    │  else → request next batch          │
```

If a gap exists between `our_head + 1` and the serving peer's `dlt_earliest_block`, the node searches for a bridging peer before requesting.

---

## Forward Mode Broadcast

```
block producer            Peer A                 Peer B (exchange-enabled)
    │                        │                        │
    │ produce block           │                        │
    │ ─5109 fork_status──────►│                        │
    │ (via transition notify)  │                        │
    │                          │                        │
    │ ─block broadcast ───────►│ (exchange-enabled)     │
    │                          │ ─block broadcast ─────►│
    │                          │                        │ push_block()
    │                          │                        │ ─5109 fork_status─► peers
```

Only exchange-enabled peers in `DLT_FORK_STATUS_NORMAL` or `DLT_FORK_STATUS_LOOKING_RESOLUTION` receive block broadcasts.

---

## Gap Fill Flow

```
Node (FORWARD, gap detected)          Peer (has gap blocks)
    │                                     │
    │  5115 dlt_gap_fill_request          │
    │  (block_nums=[N+1, N+2, N+3])       │
    │──────────────────────────────────►│
    │                                     │ reads dlt_block_log
    │  5116 dlt_gap_fill_reply            │
    │◄──────────────────────────────────│
    │  (blocks=[N+1, N+2, N+3])          │
    │                                     │
    │  apply blocks → head advances       │
```

---

## Wire Format

Each message is written as:

```
[ 4 bytes: type ID (uint32 LE) ][ 4 bytes: payload length (uint32 LE) ][ N bytes: FC-serialized payload ]
```

Reading uses `fc::tcp_socket::readsome()` / `writesome()` (non-blocking, fiber-yielding). There is no encryption layer — all peers on the same chain share a common network identity and messages are not signed per-message.

---

See also: [P2P Overview](./overview.md), [Sync Scenarios](./sync-scenarios.md), [Stats Reference](./stats-reference.md).
