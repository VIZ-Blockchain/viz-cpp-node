# VIZ P2P Message Protocol Reference

Complete reference for all P2P network message types, their structures, flow diagrams, and the transaction exchange pipeline through JSON-RPC, chain, and network broadcast plugins.

---

## Overview

The VIZ P2P network uses a binary protocol over TCP with ECDH key exchange. Messages are serialized using FC reflection. Each message carries a type tag (`core_message_type_enum`) used for dispatch.

Messages fall into three categories:

| Category | Range | Purpose |
|----------|-------|---------|
| Item messages | 1000-1099 | Block and transaction data payloads |
| Core protocol | 5000-5099 | Sync, handshake, peer discovery, diagnostics |
| Extension | 6000+ | Block post-validation |

**Key files:**
- [core_messages.hpp](file:///d:/Work/viz-cpp-node/libraries/network/include/graphene/network/core_messages.hpp) — message type enum, all message structs, FC_REFLECT serialization
- [core_messages.cpp](file:///d:/Work/viz-cpp-node/libraries/network/core_messages.cpp) — static type constant definitions
- [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) — message dispatch, sync state machine, peer management
- [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) — bridge between P2P network and blockchain

---

## Message Type Enum

```cpp
// core_messages.hpp:72-95
enum core_message_type_enum {
    trx_message_type                       = 1000,
    block_message_type                     = 1001,

    core_message_type_first                = 5000,
    item_ids_inventory_message_type        = 5001,
    blockchain_item_ids_inventory_message_type = 5002,
    fetch_blockchain_item_ids_message_type = 5003,
    fetch_items_message_type               = 5004,
    item_not_available_message_type        = 5005,
    hello_message_type                     = 5006,
    connection_accepted_message_type       = 5007,
    connection_rejected_message_type       = 5008,
    address_request_message_type           = 5009,
    address_message_type                   = 5010,
    closing_connection_message_type        = 5011,
    current_time_request_message_type      = 5012,
    current_time_reply_message_type        = 5013,
    check_firewall_message_type            = 5014,
    check_firewall_reply_message_type      = 5015,
    get_current_connections_request_message_type = 5016,
    get_current_connections_reply_message_type   = 5017,
    chain_status_announcement_message_type = 5018,
    core_message_type_last                 = 5018,

    block_post_validation_message_type     = 6009,
};
```

---

## Detailed Message Reference

### 1000 — trx_message

**Purpose:** Carries a signed transaction between peers.

**Structure:**
```cpp
struct trx_message {
    static const core_message_type_enum type;   // = trx_message_type (1000)
    signed_transaction trx;
};
```

**FC_REFLECT:** `(trx)`

**Dispatch path:**
1. [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `process_ordinary_message()` (line 5099) — checks `items_requested_from_peer` set
2. If `msg_type == trx_message_type`: calls `_delegate->handle_transaction(trx_msg)`
3. [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `handle_transaction()` (line 287) — calls `chain.accept_transaction(trx_msg.trx)`
4. On success: the message is broadcast to all other connected peers via `broadcast()` (node.cpp line 5146)

**Usage sites:**
| Caller | File | Line |
|--------|------|------|
| JSON-RPC `broadcast_transaction` | [network_broadcast_api.cpp](file:///d:/Work/viz-cpp-node/plugins/network_broadcast_api/network_broadcast_api.cpp) | 50 |
| Witness block production (contained txs) | [witness.cpp](file:///d:/Work/viz-cpp-node/plugins/witness/witness.cpp) | — |
| P2P relay (incoming tx → other peers) | [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) | 5146 |

---

### 1001 — block_message

**Purpose:** Carries a full signed block between peers.

**Structure:**
```cpp
struct block_message {
    static const core_message_type_enum type;   // = block_message_type (1001)
    signed_block block;
    block_id_type   block_id;   // cached block.id() for quick ID lookup
};
```

**FC_REFLECT:** `(block)(block_id)`

**Dispatch path:**
1. [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `process_block_message()` (line 4773) — routes to sync or normal handler
2. Sync path: `process_block_during_sync()` → `send_sync_block_to_node_delegate()` → `_delegate->handle_block(blk_msg, sync_mode=true, ...)`
3. Normal path: `process_block_during_normal_operation()` → `_delegate->handle_block(blk_msg, sync_mode=false, ...)`
4. [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `handle_block()` (line 145) — calls `chain.accept_block(blk_msg.block, ...)`
5. On success: broadcast to other peers

**DLT emergency near-caught-up logic** (p2p_plugin.cpp:219): When `sync_mode && gap <= 2 && dlt_mode && block_age < 30s`, treats the sync block as a normal block to avoid triggering "Syncing Blockchain started" and disrupting witness production.

**Usage sites:**
| Caller | File | Line |
|--------|------|------|
| JSON-RPC `broadcast_block` | [network_broadcast_api.cpp](file:///d:/Work/viz-cpp-node/plugins/network_broadcast_api/network_broadcast_api.cpp) | 88 |
| Witness block production | [witness.cpp](file:///d:/Work/viz-cpp-node/plugins/witness/witness.cpp) | — |
| P2P relay | [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) | 5146 |

---

### 5001 — item_ids_inventory_message

**Purpose:** Gossip advertisement. Tells a peer "I have these items available" so the peer can request them.

This is the **push-based broadcast** mechanism — when a node receives a new block or transaction, it advertises it to all other connected peers (except the one it came from).

**Structure:**
```cpp
struct item_ids_inventory_message {
    static const core_message_type_enum type;   // = 5001
    uint32_t              item_type;            // trx_message_type or block_message_type
    std::vector<item_hash_t> item_hashes_available;
};
```

**Handler:** [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `on_item_ids_inventory_message()` (line 3339)

**Gate conditions** (message is skipped if):
- `we_need_sync_items_from_peer == true` (we're syncing from this peer)
- Any peer has `we_need_sync_items_from_peer == true` (global sync in progress)
- Head block time is >30 seconds behind real time

**Inventory Gate Deadlock Breaker** (node.cpp:3480-3513): If head is >30s behind AND no sync is in progress with ANY peer, triggers `start_synchronizing_with_peer()` to break the stalemate.

---

### 5002 — blockchain_item_ids_inventory_message

**Purpose:** Response to a `fetch_blockchain_item_ids_message`. Contains a sequential list of block IDs after the common ancestor point identified by the synopsis. Used in **pull-based sync mode**.

**Structure:**
```cpp
struct blockchain_item_ids_inventory_message {
    static const core_message_type_enum type;   // = 5002
    uint32_t total_remaining_item_count;         // how many more block IDs the peer has beyond this batch
    uint32_t item_type;                          // always block_message_type
    std::vector<item_hash_t> item_hashes_available;  // sequential block IDs
};
```

**Handler:** [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `on_blockchain_item_ids_inventory_message()` (line 2787)

**Validation:** Checks that IDs are sequential block numbers, links to the synopsis, and the first ID is in our fork history or synopsis. On failure → disconnects with "invalid response".

**Done condition:** When `total_remaining_item_count == 0` and all items are already known, sets `we_need_sync_items_from_peer = false` (transitions to broadcast mode).

**Server-side generation:** [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `get_block_ids()` (line 337)

---

### 5003 — fetch_blockchain_item_ids_message

**Purpose:** Request from a syncing node to a peer. Sends a "blockchain synopsis" (logarithmically-spaced block IDs from our chain) so the peer can find the most recent common block and respond with `blockchain_item_ids_inventory_message`.

**Structure:**
```cpp
struct fetch_blockchain_item_ids_message {
    static const core_message_type_enum type;   // = 5003
    uint32_t item_type;                          // always block_message_type
    std::vector<item_hash_t> blockchain_synopsis;
};
```

**Handler:** [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `on_fetch_blockchain_item_ids_message()` (line 2748)

**Synopsis generation:** [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `get_blockchain_synopsis()` (line 568)

**Synopsis format (logarithmic fall-off):**
- First entry: highest non-undoable block (LIB)
- Second: ~1/2 way through undoable segment
- Third: ~3/4 way through
- Fourth: ~7/8 way through
- Last: head block (guaranteed by post-loop guard)

---

### 5004 — fetch_items_message

**Purpose:** Request actual block or transaction data from a peer. Contains a list of item hashes to fetch. The peer responds with `block_message` or `trx_message` (one per item) or `item_not_available_message`.

**Structure:**
```cpp
struct fetch_items_message {
    static const core_message_type_enum type;   // = 5004
    uint32_t              item_type;
    std::vector<item_hash_t> items_to_fetch;
};
```

**Handler:** [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `on_fetch_items_message()`

**Server-side serving:** [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `get_item()` (line 528) — fetches block or transaction from chain database.

---

### 5005 — item_not_available_message

**Purpose:** Response when a peer requests an item we don't have. The requesting peer may soft-ban or apply a strike counter.

**Structure:**
```cpp
struct item_not_available_message {
    static const core_message_type_enum type;   // = 5005
    item_id requested_item;                      // { item_type, item_hash }
};
```

---

### 5006 — hello_message

**Purpose:** Initial handshake message sent immediately after TCP connection. Exchanges node identity, protocol version, chain state, and capabilities.

**Structure:**
```cpp
struct hello_message {
    static const core_message_type_enum type;   // = 5006
    std::string              user_agent;
    uint32_t                 core_protocol_version;
    fc::ip::address          inbound_address;
    uint16_t                 inbound_port;
    uint16_t                 outbound_port;
    node_id_t                node_public_key;
    fc::ecc::compact_signature signed_shared_secret;
    fc::variant_object       user_data;          // extensible key-value metadata
};
```

**FC_REFLECT:** `(user_agent)(core_protocol_version)(inbound_address)(inbound_port)(outbound_port)(node_public_key)(signed_shared_secret)(user_data)`

**Handler:** [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `on_hello_message()` (line 2299)

**user_data fields** (generated by `generate_hello_user_data()`, node.cpp:2231):

| Key | Type | Description |
|-----|------|-------------|
| `fc_git_revision_sha` | string | FC library git revision |
| `fc_git_revision_unix_timestamp` | uint32 | FC library build timestamp |
| `platform` | string | `"osx"`, `"linux"`, `"win32"`, or `"other"` |
| `bitness` | uint32 | `32` or `64` |
| `node_id` | node_id_t | Node public key as hex |
| `last_known_block_hash` | block_id_type | Head block hash |
| `last_known_block_number` | uint32 | Head block number |
| `last_known_block_time` | time_point_sec | Head block timestamp |
| `last_known_fork_block_number` | uint32 | Latest hardfork block known to this node |
| `chain_id` | chain_id_type | Blockchain chain ID |
| `dlt_mode` | bool | Node is in DLT (rolling block log) mode |
| `dlt_earliest_block` | uint32 | Earliest available block in DLT window (only present if dlt_mode=true) |
| `emergency_consensus_active` | bool | Emergency consensus is active on this node |
| `has_emergency_key` | bool | Node holds the emergency committee private key (block_producer heuristic) |

**Validation checks performed on hello:**
1. ECDH signature validation (line 2338)
2. Hardfork compatibility check (line 2353)
3. Chain ID match (line 2386)
4. Duplicate connection check (line 2407)

**Rejection reasons** (`rejection_reason_code` enum):
- `unspecified` — generic
- `different_chain` — chain ID mismatch
- `already_connected` — duplicate node_id
- `connected_to_self` — connected to own node_id
- `not_accepting_connections` — node is full
- `blocked` — in allowed_peers list but blocked
- `invalid_hello_message` — signature validation failed
- `client_too_old` — hardfork version too old

---

### 5007 — connection_accepted_message

**Purpose:** Empty message sent by the receiving node to confirm it has accepted the hello handshake and the connection is fully established.

**Structure:**
```cpp
struct connection_accepted_message {
    static const core_message_type_enum type;   // = 5007
    // empty
};
```

**Handler:** [node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/node.cpp) `on_connection_accepted_message()` (line 2502)

Takes the peer from `peer_connection::their_connection_state::connection_accepted` to `connection_established`. Calls `new_peer_just_added()` which starts sync.

---

### 5008 — connection_rejected_message

**Purpose:** Sent when a node rejects the hello handshake. The connection is closed after this message.

**Structure:**
```cpp
struct connection_rejected_message {
    static const core_message_type_enum type;   // = 5008
    std::string              user_agent;
    uint32_t                 core_protocol_version;
    fc::ip::endpoint         remote_endpoint;
    std::string              reason_string;
    fc::enum_type<uint8_t, rejection_reason_code> reason_code;
};
```

---

### 5009/5010 — address_request_message / address_message

**Purpose:** Peer discovery. A node requests known peer addresses from a connected peer, and receives a list of `address_info` records.

**address_info structure:**
```cpp
struct address_info {
    fc::ip::endpoint    remote_endpoint;
    fc::time_point_sec  last_seen_time;
    fc::microseconds    latency;
    node_id_t           node_id;
    fc::enum_type<uint8_t, peer_connection_direction> direction;   // unknown, inbound, outbound
    fc::enum_type<uint8_t, firewalled_state> firewalled;           // unknown, firewalled, not_firewalled
};
```

---

### 5011 — closing_connection_message

**Purpose:** Graceful disconnect. Sent before a peer closes the connection, with an optional reason.

**Structure:**
```cpp
struct closing_connection_message {
    static const core_message_type_enum type;   // = 5011
    std::string   reason_for_closing;
    bool          closing_due_to_error;
    fc::oexception error;
};
```

---

### 5012/5013 — current_time_request_message / current_time_reply_message

**Purpose:** NTP-style clock synchronization. A node requests the peer's current time for clock offset calculation.

**current_time_reply_message structure:**
```cpp
struct current_time_reply_message {
    static const core_message_type_enum type;   // = 5013
    fc::time_point request_sent_time;            // our timestamp when we sent the request
    fc::time_point request_received_time;        // peer's timestamp when they received our request
    fc::time_point reply_transmitted_time;       // peer's timestamp when they sent the reply
};
```

Clock offset is computed as: `((T2 - T1) + (T3 - T4)) / 2` (standard NTP formula).

Sent on every new connection in `new_peer_just_added()` (node.cpp:5186).

---

### 5014/5015 — check_firewall_message / check_firewall_reply_message

**Purpose:** NAT/firewall detection. A node asks a peer to try connecting to a specified endpoint to test if the requesting node is reachable from the internet.

---

### 5016/5017 — get_current_connections_request_message / get_current_connections_reply_message

**Purpose:** Network diagnostics. Request a peer's list of current connections, upload/download rates.

---

### 6009 — block_post_validation_message

**Purpose:** Block signing witness verification (Hardfork 11+). Sent after a block is applied to confirm the signing witness identity. The receiving node validates the signature against the witness's signing key on-chain and triggers `apply_block_post_validation()`.

**Structure:**
```cpp
struct block_post_validation_message {
    static const core_message_type_enum type;   // = 6009
    block_id_type  block_id;
    std::string    witness_account;
    signature_type witness_signature;
};
```

**Handler:** [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `handle_message()` (line 304)

**Validation:** Recovers the public key from the signature, compares against the witness's on-chain `signing_key`. If matched, calls `apply_block_post_validation()`.

**Broadcast:** Sent by witnesses after producing a block. Also emitted via [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `broadcast_block_post_validation()` (line 1470).

---

### 5018 — chain_status_announcement_message

**Purpose:** Announces a node's chain state — head block, irreversible block, DLT mode window, and emergency consensus status — to all connected peers. Sent automatically when a new peer joins (via `connection_count_changed`) and can also be broadcast manually via `p2p_plugin::broadcast_chain_status()`.

This message complements the hello handshake (5006): the hello provides a **snapshot** of chain state at connection time, while chain_status_announcement provides **live updates** when DLT window shifts or emergency consensus activates/deactivates during an ongoing connection.

**Structure:**
```cpp
struct chain_status_announcement_message {
    static const core_message_type_enum type;   // = 5018

    block_id_type  head_block_id;                // hash of our head block
    uint32_t       head_block_num;               // height of our head block
    uint32_t       last_irreversible_block_num;  // LIB height
    bool           dlt_mode;                     // true if rolling block log mode
    uint32_t       dlt_earliest_block;           // lowest block number we can serve
    bool           emergency_consensus_active;   // true if emergency consensus is on
    bool           has_emergency_key;            // true if we hold committee private key
};
```

**Handler:** [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `handle_message()` (line 326) — logs the received chain state at debug level. The peer's prior hello `user_data` already stores this info; this message serves as a refresh.

**Broadcast trigger:** [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) `connection_count_changed()` (line 793) — when the connection count increases (new peer joined), a `chain_status_announcement_message` is built from the chain database and broadcast to all peers.

**Manual call site:** `p2p_plugin::broadcast_chain_status()` — can be called from any plugin (e.g., witness or snapshot) when chain state materially changes.

---

## Message Flow Diagrams

### Handshake Flow

```
Client (Initiator)                          Server (Acceptor)
      |                                          |
      |  TCP connect                             |
      |----------------------------------------->|
      |                                          |
      |  hello_message (5006)                    |
      |  [user_agent, protocol_version,           |
      |   node_public_key, signed_shared_secret,  |
      |   user_data{...chain state...}]           |
      |----------------------------------------->|
      |                                          | validate signature
      |                                          | check chain ID
      |                                          | check hardfork version
      |                                          | check duplicate connection
      |                                          |
      |  connection_accepted_message (5007)      |
      |<-----------------------------------------|
      |                                          |
      |  hello_message (5006)                    |
      |  [server's chain state]                  |
      |<-----------------------------------------|
      | validate                                  |
      |                                          |
      |  connection_accepted_message (5007)      |
      |----------------------------------------->|
      |                                          |
      |  current_time_request_message (5012)     |
      |<-----------------------------------------|
      |  current_time_reply_message (5013)       |
      |----------------------------------------->|

Both sides now call new_peer_just_added() → start_synchronizing_with_peer()
```

---

### Sync Mode Flow (Pull-Based)

```
Our Node (needs blocks)                   Peer (has blocks)
      |                                        |
      |  fetch_blockchain_item_ids_msg (5003)  |
      |  [blockchain_synopsis: LIB,...head]    |
      |--------------------------------------->|
      |                                        | Server finds common ancestor
      |                                        | via get_block_ids()
      |                                        |
      |  blockchain_item_ids_inventory (5002)  |
      |  [remaining=N, item_hashes=[...]]      |
      |<---------------------------------------|
      |                                        |
      |  fetch_items_message (5004)            |
      |  [items_to_fetch=[id1, id2, ...]]      |
      |--------------------------------------->|
      |                                        | Server calls get_item()
      |  block_message (1001)                  |
      |  [signed_block data]                   |
      |<---------------------------------------|
      | push_block()                            |
      |   ... repeat for each block ...         |
      |                                        |
      |  fetch_blockchain_item_ids_msg (5003)  |
      |  (next batch if remaining > 0)         |
      |--------------------------------------->|
      |   ... repeat until remaining == 0 ...  |

When remaining == 0 and all items known:
  we_need_sync_items_from_peer = false  (transition to broadcast mode)
```

---

### Broadcast/Inventory Mode Flow (Push-Based)

```
Witness Node          Peer A                Peer B (us)           Peer C
     |                   |                      |                    |
     | 1. generate_block |
     | 2. broadcast_block|                      |                    |
     |  [block_message]  |                      |                    |
     |------------------>|                      |                    |
     |                   |                      |                    |
     |                   | 3. item_ids_inventory|                    |
     |                   |    [block #N hash]   |                    |
     |                   |--------------------->|                    |
     |                   |                      |                    |
     |                   | 4. fetch_items_msg   |                    |
     |                   |    [request block]   |                    |
     |                   |<---------------------|                    |
     |                   |                      |                    |
     |                   | 5. block_message     |                    |
     |                   |    [block data]      |                    |
     |                   |--------------------->|                    |
     |                   |                      |                    |
     |                   |                      | 6. push_block()    |
     |                   |                      | 7. item_ids_inv    |
     |                   |                      |    [block #N hash] |
     |                   |                      |------------------->|
     |                   |                      |                    |
     |                   |                      | 8. fetch + block   |
     |                   |                      |<------------------>|
```

**Broadcast suppression during sync:** When `we_need_sync_items_from_peer == true` for ANY peer, incoming inventory from ALL peers is skipped (node.cpp:3358). This prevents inventory-request timeouts from killing sync connections.

---

### Block Post-Validation Flow

```
Witness Node                              Other Peers
     |                                        |
     | 1. produce_block() + broadcast         |
     | 2. broadcast_block_post_validation()   |
     |    [block_id, witness_account, sig]    |
     |--------------------------------------->|
     |                                        | 3. handle_message() in p2p_plugin
     |                                        |    - recover public key from signature
     |                                        |    - compare with witness on-chain signing_key
     |                                        |    - if match: apply_block_post_validation()
```

---

## Transaction Exchange Flow

### JSON-RPC → Chain → P2P Broadcast

This is how a transaction submitted via the JSON-RPC API reaches the P2P network:

```
External Client
     |
     | HTTP/WS POST {"jsonrpc":"2.0", "method":"call",
     |               "params":["network_broadcast_api","broadcast_transaction",
     |                         [signed_transaction]]}
     v
[webserver plugin]  →  [json_rpc plugin]  →  dispatch to registered API
                                                    |
[network_broadcast_api_plugin::broadcast_transaction]
     |
     | 1. pimpl->_chain.accept_transaction(trx)
     |    → chain_plugin::accept_transaction()
     |    → database::push_transaction()
     |    → validates, pushes to _pending_tx
     |
     | 2. pimpl->_p2p.broadcast_transaction(trx)
     |    → p2p_plugin::broadcast_transaction()
     |    → node->broadcast(trx_message(trx))
     |    → sends trx_message (1000) to all connected peers
     |
     | 3. Peers receive → process_ordinary_message()
     |    → _delegate->handle_transaction(trx_msg)
     |    → chain.accept_transaction()
     |    → broadcast() to their OTHER peers (relay)
     |
     v
  Transaction propagates through the network via gossip
  (each peer that accepts it broadcasts to its other peers)

Eventually included in a block by a witness.
```

### Callback-Based Variant

`broadcast_transaction_synchronous` (network_broadcast_api.cpp:55):
- Registers a callback keyed by `transaction_id`
- Accepts and broadcasts the transaction
- When a block containing the transaction is applied (`on_applied_block`, line 159), fires the callback with `(txid, block_num, trx_num, expired=false)`
- When the transaction expires before inclusion, fires callback with `expired=true`

### P2P Receive → Chain → Relay

```
Remote Peer
     |
     | trx_message (1000) arrives
     v
[node.cpp] process_ordinary_message()
     |
     | Check: items_requested_from_peer contains this item?
     |   No → "received a message I didn't ask for" → disconnect
     |   Yes → erase from items_requested_from_peer
     |
     | msg_type == trx_message_type?
     |   Yes → _delegate->handle_transaction(trx_msg)
     |    No → _delegate->handle_message(msg)  [for block_post_validation, etc.]
     |
     v
[p2p_plugin.cpp] handle_transaction()
     |
     | chain.accept_transaction(trx_msg.trx)
     |   → database::push_transaction()
     |   → validates, pushes to _pending_tx
     |
     | On success: node.cpp broadcast() to all OTHER peers
     v
  Transaction relayed to rest of network
```

---

## Dispatch Decision Tree (node.cpp)

```
message received
     |
     +-- connection_accepted (5007) → on_connection_accepted_message()
     |
     +-- connection_rejected (5008) → disconnect
     |
     +-- address_request (5009)     → send address_message
     +-- address_message (5010)     → add to potential_peer_db
     +-- closing_connection (5011)  → disconnect
     |
     +-- current_time_request (5012)→ send current_time_reply
     +-- current_time_reply (5013)  → update clock_offset
     |
     +-- check_firewall (5014)      → try connecting, send result
     +-- check_firewall_reply (5015)→ update firewalled state
     |
     +-- get_current_connections_req (5016) → send reply
     +-- get_current_connections_reply (5017) → diagnostics
     |
     +-- chain_status_announcement (5018) → handle_message()
     |     → log DLT/emergency state from peer
     |
     +-- hello_message (5006) → on_hello_message()
     |     → validate, accept or reject
     |
     +-- fetch_blockchain_item_ids (5003)
     |     → on_fetch_blockchain_item_ids()
     |     → build synopsis, find common ancestor
     |     → send blockchain_item_ids_inventory
     |
     +-- blockchain_item_ids_inventory (5002)
     |     → on_blockchain_item_ids_inventory()
     |     → validate sequential IDs
     |     → add unknown IDs to ids_of_items_to_get
     |
     +-- fetch_items (5004) → on_fetch_items()
     |     → get_item() for each requested hash
     |     → send block_message or trx_message
     |
     +-- item_not_available (5005) → soft-ban / strike
     |
     +-- item_ids_inventory (5001)
     |     → on_item_ids_inventory()
     |     → if not syncing: request unknown blocks
     |
     +-- block_message (1001) OR trx_message (1000)
           → process_ordinary_message()
           → was this item requested? (items_requested_from_peer)
           → delegate to handle_block() or handle_transaction()
           → on success: broadcast to other peers
```

---

## DLT Mode Considerations for P2P Messages

### Synopsis Handling

When a DLT node (rolling window of blocks) receives a `fetch_blockchain_item_ids_message`:

1. **Synopsis below DLT range**: If ALL synopsis entries are below `earliest_available_block_num()`, the peer is NOT on a fork — it simply has older blocks than we store. Use the highest synopsis entry as anchor and serve from our earliest block. (Implemented in [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp#L412-L442))

2. **Synopsis above head**: If ALL synopsis entries are above our head, the peer is ahead of us. Return empty — nothing to serve.

3. **Synopsis matched below range**: When the matched anchor is below `earliest_available_block_num()`, include it in the response as a known anchor, then continue from earliest. Prevents "invalid response" disconnections.

### Block Serving

`get_item()` (p2p_plugin.cpp:528) in DLT mode: when a block is not found, logs the full context (available range, DLT range) and throws `key_not_found_exception` instead of the generic "Couldn't find block" error. This gives visibility into whether the block is genuinely missing or just outside the DLT window.

### Near-Caught-Up Sync Blocks

[p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp#L202-L226): When a sync block arrives with `gap <= 2 && dlt_mode && block_age < 30s`, it's treated as a normal (non-sync) block. This prevents "Syncing Blockchain started" from firing when the node is only 1-2 blocks behind, which would set `currently_syncing=true` and disrupt witness block production.

---

## How `chain_status_announcement_message` (5018) Makes DLT Mode Safer

### Problem: The Hello Handshake Alone Is Not Enough

The hello message (5006) sends chain state **once** at connection time. In DLT mode with emergency consensus, the chain state can change dramatically *during* an active connection:

- **DLT window slides forward** every block (the rolling window of ~350 blocks advances by 1 each time).
- **Emergency consensus can activate or deactivate** at any time (1-hour timeout triggers; exit after 21 normal blocks).
- **The emergency master can change** (blank key after 5 missed rounds).

Without live updates, peers make decisions based on **stale connection-time data**, leading to:

1. **False `peer_is_on_an_unreachable_fork`**: A peer sends a synopsis with entries from block numbers we *used* to have but which have now aged out of our DLT window. Without knowing our DLT `earliest_available_block_num()`, the peer can't adjust its synopsis to use entries inside our window. Result: we throw the fork exception, disconnect the peer, and create a sync oscillation.

2. **Sync ping-pong in emergency mode**: During emergency consensus, competing forks at the same height cause both nodes to restart sync. Without knowing the peer is also in emergency mode, a node treats the peer's blocks as "normal" fork blocks and triggers full sync restarts instead of recognizing the emergency situation and treating it as a committee-led recovery.

3. **Inventory flooding during DLT window mismatch**: A peer in broadcast mode may advertise block IDs that are below our DLT window. We can't serve them, but we don't know the peer has stale information. This generates spurious `fetch_items` → `item_not_available` cycles that increment the peer's strike counter and lead to soft-bans.

### Solution: The Chain Status Announcement

The `chain_status_announcement_message` (5018) solves these problems by providing:

#### 1. Live DLT Window Information (`dlt_mode`, `dlt_earliest_block`)

When a peer receives a `chain_status_announcement` showing the sender is in DLT mode with `dlt_earliest_block = 79632101`, it knows:

- **Don't use block numbers below 79632101 in synopses** sent to this peer — those blocks are no longer in their rolling window.
- **Don't request blocks below 79632101** — they'll get `item_not_available` responses and risk soft-ban strikes.
- **Expected result**: Eliminates the entire class of `peer_is_on_an_unreachable_fork` errors caused by below-DLT-range synopses.

#### 2. Live Emergency Consensus Status (`emergency_consensus_active`, `has_emergency_key`)

When a peer receives a `chain_status_announcement` with `emergency_consensus_active = true`:

- **Don't treat competing blocks at the same height as forks** — they may be emergency committee blocks from a legitimate committee recovery.
- **If `has_emergency_key = true`**: This peer is the emergency master — prioritize syncing from it.
- **If `has_emergency_key = false`**: This peer is an emergency follower — don't soft-ban it for producing blocks we disagree with (the committee decides).
- **Expected result**: Eliminates sync ping-pong loops and false fork detections during emergency consensus recovery.

#### 3. Continuous Refresh on New Connections

The message is automatically broadcast via `connection_count_changed()` whenever a new peer joins. This means:

- **Every new peer immediately learns** our current DLT window and emergency status, even if we connected hours ago.
- **No polling needed** — push-based, not pull-based.
- **Zero protocol breakage** — old peers simply ignore message type 5018 (it goes to `handle_message()` which, pre-5018, would throw `Invalid Message Type`, but since old peers never send this message to begin with, the throw never fires).

### Error Classes Eliminated

| Error Class | Root Cause | Fixed By |
|---|---|---|
| `peer_is_on_an_unreachable_fork` (below-range) | Peer synopsis entries below our DLT `earliest_available_block_num()` | Peer sees `dlt_earliest_block` and adjusts synopsis |
| Sync restart oscillation | Both nodes think the other is on a fork in emergency mode | Both see `emergency_consensus_active=true` and relax fork detection |
| `item_not_available` soft-bans | Peer requests blocks we can't serve (below window) | Peer skips below-DLT block requests |
| `unlinkable_block_exception` spamming | Dead-fork sync blocks from before our head | Peer sees `dlt_mode` and avoids sending ancient blocks |
| Emergency follower disconnection | Master treats follower blocks as invalid fork blocks | `has_emergency_key` flag identifies master vs follower roles |

### Integration with Existing Protections

The `chain_status_announcement` works **alongside** (not instead of) the existing server-side protections:

| Protection | Side | When It Helps |
|---|---|---|
| `chain_status_announcement` info | Client (peer) | **Before** sending — avoids problematic requests entirely |
| `get_block_ids()` below-range check | Server | **During** synopsis processing — catches what the client missed |
| `get_block_ids()` above-head check | Server | **During** synopsis processing — handles ahead-of-us peers |
| DLT near-caught-up logic | Server | **During** block receiving — prevents sync mode disruption |
| Soft-ban strike counters | Server | **After** repeated errors — last-resort penalty |

**Key insight**: The server-side protections handle errors reactively (strikes, disconnection), while `chain_status_announcement` prevents errors proactively (peers know what not to do before they do it). Both layers together create defense-in-depth.

### Backward Compatibility

| Peer Combination | Behavior |
|---|---|
| Old ↔ Old | No change. Works as before. |
| New ↔ New | Both exchange DLT/emergency info via hello user_data AND chain_status_announcement (5018). Full benefits. |
| New → Old | New peer sends hello with DLT fields (old peer ignores unknown keys). New peer may send 5018 (old peer's handle_message throws `Invalid Message Type`). This is a one-time benign throw since old peers don't understand the type. The new peer's code handles this gracefully. |
| Old → New | Old peer sends hello without DLT fields. New peer defaults `peer_dlt_mode=false`, `peer_emergency_active=false` — assumes the old peer operates in normal (non-DLT, non-emergency) mode. The new peer does NOT send 5018 to the old peer because `connection_count_changed` broadcasts to ALL peers, which includes the old one. This is a known limitation — old peers will see one `Invalid Message Type` throw per new-peer connection. To mitigate, future work could add per-peer capability tracking based on hello user_data fields. |

---

## Key Type Definitions

| Type | Underlying | Size | Defined In |
|------|-----------|------|------------|
| `node_id_t` | `fc::ecc::public_key_data` | 33 bytes | core_messages.hpp:51 |
| `item_hash_t` | `fc::ripemd160` | 20 bytes | core_messages.hpp:52 |
| `item_id` | `{ uint32_t item_type; item_hash_t item_hash; }` | 24 bytes | core_messages.hpp:54 |
| `block_id_type` | `fc::ripemd160` | 20 bytes | protocol/types.hpp |
| `transaction_id_type` | `fc::ripemd160` | 20 bytes | protocol/types.hpp |

---

## Key Configuration Constants

| Constant | File | Default | Description |
|----------|------|---------|-------------|
| `GRAPHENE_NET_PROTOCOL_VERSION` | config.hpp | — | Version sent in hello_message |
| `GRAPHENE_NET_DEFAULT_PEER_CONNECTION_RETRY_TIME` | config.hpp | 30s | Base retry interval |
| `GRAPHENE_NET_MAX_FAILED_CONNECTION_ATTEMPTS` | config.hpp | 5 | Cap on failure counter (max backoff = 180s) |
| `GRAPHENE_NET_DEFAULT_DESIRED_CONNECTIONS` | config.hpp | 20 | Target active connections |
| `GRAPHENE_NET_MIN_BLOCK_IDS_TO_PREFETCH` | config.hpp | 10000 | Min IDs to collect before block fetching during concurrent sync |
| `DISCONNECT_RECONNECT_COOLDOWN_SEC` | node.cpp | 30s | Per-IP cooldown after disconnect |
