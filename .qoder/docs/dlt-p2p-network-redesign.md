# DLT P2P Network Redesign — Implementation Status

Implementation of the [design plan](../plans/dlt-p2p-network-redesign_91a7ca29.md).

## Architecture

In-place replacement of the old Graphene synopsis-based P2P (`node.cpp`, 6978 lines) with a new DLT-specific protocol. Same plugin name, same port, same public API — only the internal implementation changes.

```
Before:  p2p_plugin → graphene::network::node (node.cpp, STCP, synopsis, inventory gossip)
After:   p2p_plugin → dlt_p2p_node (dlt_p2p_node.cpp, raw TCP, DLT hello/range/exchange)
```

### Delegate Pattern

The network library only links `fc` and `graphene_protocol` — NOT `graphene_chain`. So `dlt_p2p_node` cannot directly access the database, `dlt_block_log`, or `fork_db`. The `dlt_p2p_delegate` abstract interface bridges this gap:

```
dlt_p2p_node (network lib)  ←→  dlt_p2p_delegate (abstract interface)  ←→  dlt_delegate (p2p_plugin)
```

This matches the old `node_delegate` pattern.

### Fiber Architecture

All I/O runs on the p2p thread using fc's cooperative fiber model:

- **Accept loop fiber**: `_thread->async(accept_loop)` — blocks on `tcp_server::accept()`, yields while waiting
- **Read loop fibers**: one per peer via `_thread->async(read_loop)` — blocks on `tcp_socket::readsome()`, yields while waiting
- **Periodic task fiber**: `_thread->async(periodic_loop)` — sleeps 5s between iterations
- All fibers run cooperatively on the same `fc::thread` — no mutexes needed for shared state
- `close()` cancels all fibers via `fc::future::cancel_and_wait()`

### Wire Format

Raw TCP (no STCP encryption). Each message on the wire:

```
[4 bytes: data size (uint32_t)] [4 bytes: msg_type (uint32_t)] [size bytes: fc::raw::pack(T)]
```

This matches the old `message_oriented_connection` format without 16-byte padding (no encryption layer). The `send_message()` method writes the header and data separately to avoid `fc::raw::pack(message)` which adds a varint length prefix to the data vector.

## File Map

### New Files (Created)

| File | Lines | Purpose |
|------|-------|---------|
| `libraries/network/include/graphene/network/dlt_p2p_messages.hpp` | 290 | All DLT message types (5100-5114), enums, structs, FC_REFLECT macros |
| `libraries/network/dlt_p2p_messages.cpp` | 25 | Static `type` constants for each message struct |
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp` | 150 | `dlt_peer_state`, `dlt_known_peer`, `dlt_mempool_entry`, `dlt_fork_resolution_state`, `dlt_fork_branch_info` |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | 284 | `dlt_p2p_delegate` interface + `dlt_p2p_node` class declaration |
| `libraries/network/dlt_p2p_node.cpp` | 1740 | Full `dlt_p2p_node` implementation |

### Modified Files

| File | Change |
|------|--------|
| `libraries/network/CMakeLists.txt` | Removed 6 old source files and 6 old headers; added new DLT files |
| `plugins/p2p/p2p_plugin.cpp` | Replaced `node.cpp`-based impl with `dlt_p2p_node` wrapper + `dlt_delegate` |
| `plugins/p2p/CMakeLists.txt` | Removed `graphene::snapshot` dependency |
| `plugins/p2p/include/.../p2p_plugin.hpp` | **Unchanged** — same public API preserved |

### Deleted Files (12 total)

| Type | Files |
|------|-------|
| Source | `node.cpp`, `peer_connection.cpp`, `peer_database.cpp`, `stcp_socket.cpp`, `message_oriented_connection.cpp`, `core_messages.cpp` |
| Headers | `node.hpp`, `peer_connection.hpp`, `peer_database.hpp`, `stcp_socket.hpp`, `message_oriented_connection.hpp`, `core_messages.hpp` |

### Kept Files (still in network lib)

| File | Reason |
|------|--------|
| `config.hpp` | Defines `MAX_MESSAGE_SIZE`, `GRAPHENE_NET_MAX_BLOCKS_PER_PEER_DURING_SYNCING` used by DLT code |
| `exceptions.hpp` | Defines `unlinkable_block_exception` etc. used by chain and plugins |
| `message.hpp` | Core `message` / `message_header` structs — wire format foundation |

## Plan Phase → Implementation Mapping

### Phase 1: New Message Types ✅

`dlt_p2p_messages.hpp` implements all 15 message types (5100-5114) exactly as specified:

| Message Type | ID | Struct |
|---|---|---|
| `dlt_hello_message_type` | 5100 | `dlt_hello_message` — protocol_version, head/LIB, DLT range, emergency, fork/node status |
| `dlt_hello_reply_message_type` | 5101 | `dlt_hello_reply_message` — exchange_enabled, fork_alignment, recognized blocks |
| `dlt_range_request_message_type` | 5102 | `dlt_range_request_message` |
| `dlt_range_reply_message_type` | 5103 | `dlt_range_reply_message` |
| `dlt_get_block_range_message_type` | 5104 | `dlt_get_block_range_message` — start/end + prev_block_id |
| `dlt_block_range_reply_message_type` | 5105 | `dlt_block_range_reply_message` — blocks vector + is_last |
| `dlt_get_block_message_type` | 5106 | `dlt_get_block_message` |
| `dlt_block_reply_message_type` | 5107 | `dlt_block_reply_message` — block + next_available + is_last |
| `dlt_not_available_message_type` | 5108 | `dlt_not_available_message` |
| `dlt_fork_status_message_type` | 5109 | `dlt_fork_status_message` |
| `dlt_peer_exchange_request_type` | 5110 | `dlt_peer_exchange_request` (empty body) |
| `dlt_peer_exchange_reply_type` | 5111 | `dlt_peer_exchange_reply` — peers vector |
| `dlt_peer_exchange_rate_limited_type` | 5112 | `dlt_peer_exchange_rate_limited` — wait_seconds |
| `dlt_transaction_message_type` | 5113 | `dlt_transaction_message` — signed_transaction |
| `dlt_soft_ban_message_type` | 5114 | `dlt_soft_ban_message` — ban_duration_sec, reason (sent before disconnecting a banned peer) |

Enums: `dlt_node_status` (SYNC/FORWARD), `dlt_fork_status` (NORMAL/LOOKING_RESOLUTION/MINORITY), `dlt_peer_lifecycle_state` (6 states).

All FC_REFLECT macros defined for serialization.

### Phase 2: DLT P2P Node ✅

`dlt_p2p_node.hpp` + `dlt_p2p_node.cpp` implement the full node:

**Connection management**:
- `connect_to_peer()` — synchronous connect on p2p thread, sends hello, starts read loop
- `accept_loop()` — fiber that accepts incoming connections, creates peer state, sends hello, starts read loop
- `start_read_loop()` — per-peer fiber that reads message_header + data, dispatches to `on_message()`
- `handle_disconnect()` — cancels read fiber, closes socket, calculates backoff with jitter
- Periodic reconnect/backoff/expire logic

**Hello handshake**:
- `build_hello_message()` — queries delegate for all chain state
- `build_hello_reply()` — checks fork alignment via `delegate->is_block_known()`
- `on_dlt_hello()` — stores peer chain state, sends reply, transitions lifecycle, starts sync if SYNC mode
- `on_dlt_hello_reply()` — processes exchange_enabled/fork_alignment, starts block fetch

**Block sync (SYNC mode)**:
- `request_blocks_from_peer()` — requests up to 200 blocks after our head
- `on_dlt_get_block_range()` — reads blocks from dlt_block_log via delegate, sends reply
- `on_dlt_block_range_reply()` — validates prev_hash, applies blocks, transitions to FORWARD when `is_last`
- `on_dlt_get_block()` / `on_dlt_block_reply()` — single-block fetch variant
- `sync_stagnation_check()` — 30s no-block timeout, 3 retries, then FORWARD with warning
- `transition_to_forward()` — revalidates provisional mempool entries

**Mempool** (separate from chain's `_pending_tx`):
- `add_to_mempool()` — dedup by tx_id, check expiry/size/TaPoS, enforce limits with oldest-expiry eviction, retranslate to our-fork peers
- `remove_transactions_in_block()` — prune on block receipt
- `prune_mempool_on_fork_switch()` — remove TaPoS-invalid entries on fork
- `periodic_mempool_cleanup()` — prune expired + TaPoS-invalid entries
- Provisional entries tagged during SYNC, revalidated on transition to FORWARD

**Fork resolution**:
- `track_fork_state()` — 42-block threshold (2 full rounds), triggers `resolve_fork()`
- `resolve_fork()` — finds heaviest branch, hysteresis with 6-block confirmation
- `dlt_fork_resolution_state` — tracks `current_winner_tip` and `consecutive_blocks_as_winner`

**Anti-spam**:
- `record_packet_result()` — single `spam_strikes` counter per peer, reset on good packet, soft-ban at threshold=10
- `soft_ban_peer()` — sets BANNED state for 3600s, sends `dlt_soft_ban_message` notification, then closes connection

**Peer exchange** (rate-limited):
- `on_dlt_peer_exchange_request()` — 10-min cooldown per peer, subnet diversity filter, min uptime 600s
- `on_dlt_peer_exchange_reply()` — adds to known peers, connects if under max_connections
- Subnet diversity via `/24` prefix comparison

**Peer lifecycle** (connecting→handshaking→syncing→active→disconnected→banned):
- Timeouts: connecting=5s, handshaking=10s
- Reconnection: backoff 30s→60s→…→3600s with ±25% jitter, reset on stable >5min
- Permanent removal after 8h non-response

**Color-coded logging**: GREEN=sync/production, WHITE=normal block exchange, RED=fork, DARK_GRAY=transactions, ORANGE=warnings, CYAN=peer stats

### Phase 3: P2P Plugin Replacement ✅

`p2p_plugin.cpp` rewritten from 1951 lines (node.cpp-based) to 478 lines (dlt_p2p_node wrapper):

**`dlt_delegate` class** (implements `dlt_p2p_delegate`):
- Bridges chain state queries using `chain.db()` with appropriate read locks
- `read_block_by_num()` — checks dlt_block_log first, then fork_db
- `accept_block()` — calls `push_block()`, catches `unlinkable_block_exception` → stores in fork_db
- `get_fork_branch_tips()` — fetches from fork_db at head_num through head_num+5
- `is_tapos_block_known()` — delegates to `chain.db().is_known_block()`

**Config options** (new DLT-specific):
| Option | Default | Purpose |
|--------|---------|---------|
| `dlt-block-log-max-blocks` | 100000 | Max blocks in DLT block log |
| `dlt-peer-max-disconnect-hours` | 8 | Remove peer after this many hours non-response |
| `dlt-mempool-max-tx` | 10000 | Hard cap on mempool entries |
| `dlt-mempool-max-bytes` | 104857600 (100MB) | Hard cap on total mempool memory |
| `dlt-mempool-max-tx-size` | 65536 (64KB) | Reject oversized transactions |
| `dlt-mempool-max-expiration-hours` | 24 | Reject far-future expiration |
| `dlt-peer-exchange-max-per-reply` | 10 | Cap peers per exchange reply |
| `dlt-peer-exchange-max-per-subnet` | 2 | Anti-sybil: max 2 per /24 |
| `dlt-peer-exchange-min-uptime-sec` | 600 | Min uptime before sharing |
| `dlt-stats-interval-sec` | 300 (5 min) | Interval between P2P peer stats log output (min 30) |

**Removed old config**: `p2p-stats-enabled`, `p2p-stats-interval`, `p2p-stale-sync-detection`, `p2p-stale-sync-timeout-seconds` (replaced by `dlt-stats-interval-sec` and P2P-level stale sync detection)

**Plugin startup** (deadlock fix):
- Old: `p2p_thread.async([...infinite loop...]).wait()` — blocks forever
- New: `p2p_thread.async([create node, set_thread, configure, start]).wait()` — returns after setup
- `dlt_p2p_node::start()` internally spawns accept loop + periodic task as fibers
- Thread reference passed via `node->set_thread(fc::thread::current())`

**Soft-ban notification**:
- `soft_ban_peer()` sends `dlt_soft_ban_message` (type 5114) before closing the connection
- Receiving peer enters BANNED state with the specified duration and logs an orange/yellow notice
- Prevents wasted bandwidth — both sides stop sending data immediately

**Peer stats**:
- `log_peer_stats()` outputs cyan-colored peer statistics at configurable interval
- Shows: node status, fork state, head/LIB, per-peer details (flags, ranges, spam strikes, ban time)
- Interval configured via `dlt-stats-interval-sec` (default 300s = 5 min)

**Out-of-order/duplicate block handling**:
- Duplicate blocks (already applied) from peers are silently skipped, not counted as spam
- Out-of-order blocks in range replies fall through to fork_db/push_block instead of soft-banning
- Deserialization errors no longer increment spam strikes
- Oversized messages from old-protocol peers disconnect with `skip_backoff_increase=true`

### Phase 4: Fork Resolution ✅

Implemented in `dlt_p2p_node.cpp`:
- `FORK_RESOLUTION_BLOCK_THRESHOLD = 42` (matches `CHAIN_MAX_WITNESSES * 2`)
- `dlt_fork_resolution_state::CONFIRMATION_BLOCKS = 6` (hysteresis)
- `track_fork_state()` called after each block application
- `resolve_fork()` with vote-weight winner + consecutive-block confirmation
- `_fork_status` exposed via `is_on_majority_fork()` for witness plugin

### Phase 5: In-Place Replacement ✅

- Same plugin name `"p2p"`, same `p2p-endpoint` port (2001/4243)
- Same public API — zero changes to witness, witness_guard, snapshot plugins
- Old `node.cpp` and all related files removed from build and deleted from disk
- `p2p_plugin.hpp` completely unchanged

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Delegate pattern instead of direct chain access | Network lib only links `fc` + `graphene_protocol`, not `graphene_chain`. Delegate avoids circular dependency. |
| Raw TCP instead of STCP encryption | DLT emergency mode means all witnesses switch simultaneously — no need for backward-compatible encryption. Simpler wire protocol. |
| fc::thread fibers instead of per-peer threads | All I/O uses fc's cooperative fiber model. `readsome()`/`writesome()` yield the fiber, allowing multiple peers on one thread without mutexes. |
| Manual header+data writes instead of `fc::raw::pack(msg)` | `fc::raw::pack(message)` adds varint length prefix to the data vector, creating mismatched wire format. Writing header and data separately matches the read side. |
| Single `spam_strikes` counter | Simpler and more effective than old multi-counter system (`unlinkable_block_strikes`, `sync_spam_strikes`, etc.). Reset-on-good naturally recovers from transient issues. |
| Separate P2P mempool | Chain's `_pending_tx` only applies after acceptance. P2P mempool provides earlier filtering (expiry, TaPoS, size limits) before pushing to chain. |
| In-place replacement, no dual-mode | Old and new protocols are incompatible. Dual-mode creates isolated sub-networks. Emergency mode means all witnesses can switch simultaneously. |

## Known Limitations / Future Work

- `dlt_delegate::has_emergency_private_key()` returns `false` — needs integration with witness plugin
- `dlt_delegate::switch_to_fork()` is a stub — needs full implementation with block replay
- `dlt_delegate::resync_from_lib()` is empty — handled at plugin level instead
- `compute_branch_info()` returns simplified info — detailed vote-weight computation needs fork_db traversal
- `dlt_block_log` batch pruning (10000 at a time) not yet connected — `periodic_dlt_prune_check()` is a no-op
- No unit tests yet for the new message types or node logic
- Build not verified (no BOOST_ROOT/OPENSSL_ROOT_DIR configured in the environment)
