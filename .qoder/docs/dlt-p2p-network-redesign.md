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
| `libraries/network/include/graphene/network/dlt_p2p_messages.hpp` | 289 | All DLT message types (5100-5114), enums, structs, FC_REFLECT macros |
| `libraries/network/dlt_p2p_messages.cpp` | 25 | Static `type` constants for each message struct |
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp` | 150 | `dlt_peer_state`, `dlt_known_peer`, `dlt_mempool_entry`, `dlt_fork_resolution_state`, `dlt_fork_branch_info` |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | 316 | `dlt_p2p_delegate` interface + `dlt_p2p_node` class declaration |
| `libraries/network/dlt_p2p_node.cpp` | 1877 | Full `dlt_p2p_node` implementation |

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

`p2p_plugin.cpp` rewritten from 1951 lines (node.cpp-based) to 536 lines (dlt_p2p_node wrapper):

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

## Subsequent Enhancements & Fixes (2026-05-05)

### DLT-Range-Aware Fork Alignment (P1-P16 fixes)

The original `check_fork_alignment` only called `is_block_known()` on peer head/LIB IDs. In DLT mode, old blocks are pruned from the rolling block log, so peers on the same chain were falsely flagged as "different fork" and disconnected.

**Fix:** `check_fork_alignment` now accepts the full `dlt_hello_message` and performs multi-tier alignment:

| Check | Condition | Result |
|-------|-----------|--------|
| Empty peer | `head_block_num == 0` | Aligned (new node, no fork to be on) |
| Range overlap | `head_num >= our_earliest && head_num <= our_latest` | Uses `is_block_known(head_id)` |
| Boundary link | `head_num + 1 == our_earliest` | Reads `our_earliest_block`, checks `previous == head_id` |
| LIB fallback | Always | `is_block_known(lib_id)` as before |

**Peer lifecycle fix:** `on_dlt_hello()` now transitions SYNC peers to ACTIVE regardless of `exchange_enabled`:
```cpp
// OLD:  if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC)
// NEW:
if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC
    || hello.node_status == DLT_NODE_STATUS_SYNC)
```

This eliminates the HANDSHAKING timeout → disconnect → reconnect loop for same-chain peers whose blocks were pruned.

Full details in [DLT 4-Node Sync Scenarios](./dlt-4-node-sync-scenarios.md).

### Soft-Ban Notification (type 5114)

`dlt_soft_ban_message` is sent before disconnecting a banned peer:
- Contains `ban_duration_sec` and human-readable `reason`
- Receiving peer enters BANNED state with the specified duration
- Logged as orange/yellow notice on both sides
- Prevents wasted bandwidth — both sides stop sending immediately

### Peer Stats Logging

`log_peer_stats()` outputs cyan-colored peer statistics at configurable interval (`dlt-stats-interval-sec`, default 300s). Shows node status, fork state, head/LIB, per-peer details (flags, ranges, spam strikes, ban time).

### Out-of-Order / Duplicate Block Tolerance

- Duplicate blocks (already applied) from peers are silently skipped, not counted as spam
- Out-of-order blocks in range replies fall through to fork_db instead of soft-banning
- Deserialization errors no longer increment spam strikes
- Oversized messages from old-protocol peers disconnect with `skip_backoff_increase=true`

### Block Processing Pause/Resume

`pause_block_processing()` / `resume_block_processing()` with `_block_processing_paused` flag allows the snapshot or other plugins to temporarily halt P2P block intake during critical operations.

### Additional Public API

| Method | Purpose |
|--------|---------|
| `broadcast_block_post_validation()` | Broadcast block by ID+witness+signature after validation |
| `broadcast_chain_status()` | Send hello to all connected peers |
| `trigger_resync()` | Force re-enter SYNC mode and re-request blocks |
| `reconnect_seeds()` | Re-connect to all seed nodes |
| `pause_block_processing()` / `resume_block_processing()` | Temporarily halt P2P block intake |
| `set_stats_log_interval()` | Configure periodic peer stats output interval |

### C++14 constexpr ODR-use Fix

`static constexpr` members that are ODR-used (e.g., `MAX_RECONNECT_BACKOFF_SEC`) now have out-of-line definitions in `dlt_p2p_node.cpp`:
```cpp
constexpr uint32_t dlt_peer_state::PEER_EXCHANGE_COOLDOWN_SEC;
constexpr uint32_t dlt_peer_state::PENDING_BATCH_TIMEOUT_SEC;
constexpr uint32_t dlt_peer_state::INITIAL_RECONNECT_BACKOFF_SEC;
constexpr uint32_t dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC;
```

### `dlt_block_accept_result` Enum

New enum replaces the old `bool` return from `accept_block()`:
```cpp
enum class dlt_block_accept_result {
    ACCEPTED,      // pushed to chain (became head or fork_db head)
    FORK_DB_ONLY,  // stored in fork_db but not applied (unlinkable / competing fork)
    DEAD_FORK,     // block from a dead fork (parent not in fork_db, at/below head)
    REJECTED       // failed validation entirely
};
```

---

## Subsequent Enhancements & Fixes (2026-05-06)

### Dead Fork Block Crash Protection (P20/P21)

Added `DEAD_FORK` to `dlt_block_accept_result` enum. When `push_block()` throws `unlinkable_block_exception` and the block is at/below our head (`block.block_num() <= head_block_num()`), the delegate returns `DEAD_FORK` instead of pushing to `fork_db._unlinked_index`. The P2P layer soft-bans peers sending dead-fork blocks and breaks out of the block processing loop. `transition_to_forward()` is now guarded by `any_block_applied` — a range full of dead-fork rejects does NOT end sync mode.

**Files:** `dlt_p2p_node.hpp`, `p2p_plugin.cpp`, `dlt_p2p_node.cpp`

### DLT Block Log Corruption Recovery (P17)

New `dlt_block_log::is_consistent_with(db_head_block_num)` method detects corruption on startup: single-block log with thousands in DB, DLT head exceeding DB head, or far-behind with few blocks. In `database::open()`, corrupted DLT block logs are auto-reset before fork_db seeding. P2P sync rebuilds the log naturally after reset.

**Files:** `dlt_block_log.hpp`, `dlt_block_log.cpp`, `database.cpp`

### Snapshot Lock Isolation Prevention (P24)

When `_block_processing_paused` is true (snapshot in progress), `periodic_task()` skips operations that need database read locks: `sync_stagnation_check()`, `periodic_peer_exchange()`, `log_peer_stats()`. Non-DB housekeeping (reconnect, lifecycle, validation, mempool cleanup, banned-peer unban) still runs. `check_stalled_sync_loop()` skips stall detection when `snapshot_in_progress` is true and resets the timer. `serialize_state()` now logs progress every 5s during long serialization.

**Files:** `dlt_p2p_node.cpp`, `snapshot/plugin.cpp`

### Write Lock Diagnostic Logging (P27)

`notify_applied_block()` now times the overall signal notification and logs a warning if it exceeds 200ms (with block number, duration, and connected plugin count). Self-timing added to the 3 most likely slow handlers: `mongo_db::on_block()`, `operation_history::purge_old_history()`, `account_history::purge_old_history()` — each logs if >100ms. The chainbase `with_strong_write_lock` macro already captures `__FILE__`/`__LINE__`/`__func__` so lock timeout messages identify the call site.

**Files:** `database.cpp`, `mongo_db_plugin.cpp`, `operation_history/plugin.cpp`, `account_history/plugin.cpp`

---

## Known Limitations / Future Work

- `has_emergency_private_key()` **now queries witness plugin** (was hardcoded `false`)
- `switch_to_fork()` **now has full implementation** with fork_db fetch + `push_block()`
- `resync_from_lib()` is handled at plugin level (by design — delegate returns early)
- `compute_branch_info()` returns simplified info — detailed vote-weight computation needs fork_db traversal
- `dlt_block_log` batch pruning (10000 at a time) not yet connected — `periodic_dlt_prune_check()` is a no-op
- No unit tests yet for the new message types or node logic
- `dlt_delegate::is_tapos_block_known()` uses `find_block_id_for_num()` — may need chain index access

---

## Build Issues (GCC 13 / Docker)

The following compilation/linking issues block building with newer GCC (13+) in Docker:

| # | Issue | File | Fix |
|---|-------|------|-----|
| P28 | `multimap::erase` with `std::pair` — C++17 removed value-erase overload | `dlt_p2p_node.cpp` | Use iterator-erase: `_mempool_by_expiry.erase(it_by_expiry)` |
| P28b | `ip::address::data()` doesn't exist in fc | `dlt_p2p_node.cpp` | Use `fc::raw::pack()` |
| P29 | Missing `witness_plugin.hpp` — actual file is `witness.hpp` | `p2p_plugin.cpp` | Fix include path |
| P30 | 6+ API mismatches in `p2p_plugin.cpp` (see below) | `p2p_plugin.cpp` | Update delegate calls |
| P31 | Linker error: `static constexpr` ODR-use | `dlt_p2p_peer_state.hpp` | **Fixed** — out-of-line definitions added |

**P30 API mismatch details:**
| Error | Fix Needed |
|-------|------------|
| `with_read_lock([&]{...})` — expects 6 args, 1 provided | Add `lock_type, timeout_ms, file, line, func` params |
| `is_emergency_consensus` is not a member | Field renamed; find new name in `dynamic_global_property_object` |
| `blocks.front()->id()` — `id` is field, not method | Change to `blocks.front()->id` |
| `catch (const unlinkable_block_exception&)` — missing var name | Add variable: `catch (const unlinkable_block_exception& e)` |
| `accept_transaction` doesn't exist | Renamed to `apply_transaction` or similar |
| `push_block(*block)` with `fork_item` — not `signed_block` | Use `fork_item->data` |
| `is_known_block(ref_block_num)` with `uint32_t` — expects `block_id_type` | Fetch block ID by number first |

---

## Known Runtime Issues

Post-implementation issues observed in production (4-node DLT emergency consensus network):

| # | Severity | Problem |
|---|----------|---------|
| P17 | ~~CRITICAL~~ **Fixed** | DLT block log corruption on crash → auto-detected and reset |
| P18 | CRITICAL | Master stops producing blocks for minutes (`slot=0` loop) |
| P19 | HIGH | Slave stuck in SYNC — synopsis returns empty, never receives blocks |
| P20 | ~~CRITICAL~~ **Fixed** | Dead fork blocks → DEAD_FORK result, soft-ban, no crash |
| P21 | ~~CRITICAL~~ **Fixed** | Dead fork crash loop → blocks rejected, fork_db protected |
| P22 | HIGH | fork_db rejection cascade on restart |
| P23 | HIGH | `fetch_branch_from` assertion failure during synopsis |
| P24 | ~~CRITICAL~~ **Fixed** | Snapshot lock isolation → periodic tasks skip DB, stall check aware |
| P25 | HIGH | Slave-produced block ignored by master → fork switch |
| P26 | MED | Sync state confusion — slave never transitions SYNC→FORWARD |
| P27 | ~~CRITICAL~~ **Fixed** (diag) | Write lock diagnostic — overall + per-plugin timing, lock-holder ID |

Full analysis in [DLT 4-Node Sync Scenarios](./dlt-4-node-sync-scenarios.md#new-problems-discovered-post-implementation).
