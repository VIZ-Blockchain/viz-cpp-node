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
| `libraries/network/include/graphene/network/dlt_p2p_messages.hpp` | 319 | All DLT message types (5100-5116), enums, structs, FC_REFLECT macros |
| `libraries/network/dlt_p2p_messages.cpp` | 21 | Static `type` constants for each message struct |
| `libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp` | 123 | `dlt_peer_state`, `dlt_known_peer`, `dlt_mempool_entry`, `dlt_fork_resolution_state`, `dlt_fork_branch_info` |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | 337 | `dlt_p2p_delegate` interface + `dlt_p2p_node` class declaration |
| `libraries/network/dlt_p2p_node.cpp` | 2387 | Full `dlt_p2p_node` implementation |

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

`dlt_p2p_messages.hpp` implements all 17 message types (5100-5116) exactly as specified:

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
| `dlt_gap_fill_request_type` | 5115 | `dlt_gap_fill_request` — block_nums (exchange-only, FORWARD mode gap fill) |
| `dlt_gap_fill_reply_type` | 5116 | `dlt_gap_fill_reply` — blocks (exchange-only, FORWARD mode gap fill) |

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
- `check_sync_catchup()` — compares our head against all peers' heads, transitions to FORWARD if caught up (P26 fix)
- `transition_to_forward()` — revalidates provisional mempool entries, re-evaluates `exchange_enabled` for all peers (P25 fix)

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

`p2p_plugin.cpp` rewritten from 1951 lines (node.cpp-based) to 487 lines (dlt_p2p_node wrapper):

**`dlt_delegate` class** (implements `dlt_p2p_delegate`):
- Bridges chain state queries using `chain.db()` with appropriate read locks
- `read_block_by_num()` — checks dlt_block_log first, then fork_db
- `accept_block()` — calls `push_block()`, catches `unlinkable_block_exception` → stores in fork_db; 60s startup grace period for near-head blocks (P22 fix)
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
|---|----------|--------|
| P17 | ~~CRITICAL~~ **Fixed** | DLT block log corruption on crash → auto-detected and reset |
| P18 | ~~CRITICAL~~ **Fixed** | Master stops producing blocks for minutes (`slot=0` loop) → stall detector + NTP force-sync |
| P19 | ~~HIGH~~ **Fixed** | Slave stuck in SYNC → gap detection + multi-peer fallback + snapshot warning |
| P20 | ~~CRITICAL~~ **Fixed** | Dead fork blocks → DEAD_FORK result, soft-ban, no crash |
| P21 | ~~CRITICAL~~ **Fixed** | Dead fork crash loop → blocks rejected, fork_db protected |
| P22 | ~~HIGH~~ **Fixed** | fork_db rejection cascade on restart → seed 100 blocks + 60s grace period |
| P23 | ~~HIGH~~ **Fixed** | `fetch_branch_from` assertion failure → graceful empty-branch return |
| P24 | ~~CRITICAL~~ **Fixed** | Snapshot lock isolation → periodic tasks skip DB, stall check aware |
| P25 | ~~HIGH~~ **Fixed** | Slave-produced block ignored → exchange_enabled re-evaluated on block accept + FORWARD transition |
| P26 | ~~MED~~ **Fixed** | Sync state confusion → `check_sync_catchup()` on block accept + periodic task |
| P27 | ~~CRITICAL~~ **Fixed** (diag) | Write lock diagnostic — overall + per-plugin timing, lock-holder ID |
| P36 | ~~HIGH~~ **Fixed** | `block_too_old_exception` during SYNC range processing + FORWARD mode gap fill |

Full analysis in [DLT 4-Node Sync Scenarios](./dlt-4-node-sync-scenarios.md#new-problems-discovered-post-implementation).

---

## Subsequent Enhancements & Fixes (2026-05-07)

### P23: fetch_branch_from Assertion Safety

**Files:** `libraries/chain/fork_database.cpp`

**Root cause:** `fetch_branch_from()` had 6 `FC_ASSERT` calls that crashed the node when block IDs weren't in the fork_db index. In production, peers on different forks triggered these assertions, crashing the node.

**Fix:** Replaced all `FC_ASSERT` calls with graceful early returns (empty branches + `wlog`). Callers already handle empty branches (e.g., `is_head_on_branch()` catches exceptions, `compare_fork_branches()` has try-catch with fork_db reset).

### P26: SYNC→FORWARD Transition Fix

**Files:** `libraries/network/dlt_p2p_node.cpp`, `libraries/network/include/graphene/network/dlt_p2p_node.hpp`

**Root cause:** `transition_to_forward()` was only called in `on_dlt_block_range_reply()` and `sync_stagnation_check()`. If a slave caught up via individual block replies, the transition never triggered.

**Fix:** Added `check_sync_catchup()` that compares `our_head` against all active peers' `peer_head_num`. Called from `on_dlt_block_reply()` after accepting a block, and from `periodic_task()`.

### P25: exchange_enabled Re-evaluation

**Files:** `libraries/network/dlt_p2p_node.cpp`

**Root cause:** `exchange_enabled` was set once during hello handshake and never updated. Slaves that caught up still had `exchange_enabled=false` on the master side, so their block broadcasts were ignored.

**Fix:** Three re-evaluation triggers: (1) in `transition_to_forward()` re-check `is_block_known(peer_head_id)`, (2) in `on_dlt_block_range_reply()` enable exchange when a non-exchange-enabled peer's block is ACCEPTED, (3) same in `on_dlt_block_reply()`.

### P22: fork_db Restart Recovery

**Files:** `libraries/chain/database.cpp`, `plugins/p2p/p2p_plugin.cpp`

**Root cause:** After restart, fork_db was seeded with only the head block. Peers sending sync blocks near the head were rejected as "dead fork" because their parent chain wasn't in fork_db.

**Fix:** (1) Seed the last 100 blocks from block_log/dlt_block_log into fork_db on startup. (2) Dead-fork grace period: for the first 60s after startup, blocks within 10 of the head are treated as `FORK_DB_ONLY` instead of `DEAD_FORK`.

### P18: slot=0 Production Stall Detector

**Files:** `plugins/witness/witness.cpp`

**Root cause:** `get_slot_at_time()` returns 0 when NTP time is behind `head_block_time()`. After crash/restart with NTP desync, the master could loop on `not_time_yet` for minutes.

**Fix:** Added `_slot_zero_streak` counter: at streak=10 (~3s) logs warning + forces NTP resync; at streak=120 (~30s) logs CRITICAL error. Counter resets on any non-stall result.

### P19: Sync Gap Detection + Multi-Peer Fallback

**Files:** `libraries/network/dlt_p2p_node.cpp`

**Root cause:** When `our_head + 1 < peer_dlt_earliest` (gap between our head and what the peer can serve), blocks were clamped but didn't link to our head. No attempt was made to find a peer with the missing blocks.

**Fix:** When a gap is detected in `request_blocks_from_peer()`: (1) search other peers for one whose DLT range covers the missing blocks, (2) if found, defer current peer and sync from the bridging peer, (3) if no peer can bridge, log "Snapshot may be required" warning and still attempt the clamped request.

---

## Subsequent Enhancements & Fixes (2026-05-08)

### P36: block_too_old_exception During SYNC Range Processing + Gap Fill Exchange Packet

**Files:** `plugins/p2p/p2p_plugin.cpp`, `libraries/network/include/graphene/network/dlt_p2p_messages.hpp`, `libraries/network/dlt_p2p_messages.cpp`, `libraries/network/include/graphene/network/dlt_p2p_node.hpp`, `libraries/network/dlt_p2p_node.cpp`

**Problem B — Root cause:** When processing a block range reply in SYNC mode, the first block can trigger `fork_db._push_next()` which cascades and links previously-deferred blocks from a competing fork. This advances `fork_db._head` far beyond the database head. Subsequent blocks in the range (from a different fork or the same chain) are then rejected with `block_too_old_exception` because they fall outside fork_db's `_max_size=2` sliding window. The exception fell through to the generic `catch (const fc::exception&)` which returned `REJECTED`, causing the P2P layer to skip updating `expected_next_block`. This made every subsequent block in the range appear "out of order", creating an unresolvable gap.

**Problem B — Fix:** Added `catch (const graphene::chain::block_too_old_exception& e)` in `dlt_delegate::accept_block()` before the generic `fc::exception` catch. Returns `ALREADY_KNOWN` instead of `REJECTED`. This is correct because fork_db already has a better chain at that height (that's why it considers the block "too old"). The `ALREADY_KNOWN` result allows `expected_next_block` to be updated properly, preventing the cascading gap.

**Gap fill — Root cause:** When transitioning SYNC→FORWARD with a 1-2 block gap, the only recovery mechanism was the FORWARD→SYNC fallbehind detection (threshold=2 blocks), causing oscillation between modes. Broadcast blocks arrive out-of-order and get deferred to fork_db._unlinked_index, but there's no way to proactively request the specific missing blocks.

**Gap fill — Fix:** Added two new exchange-only message types:

| Type | ID | Purpose |
|------|----|---------|
| `dlt_gap_fill_request` | 5115 | Request specific block numbers from exchange-enabled peers |
| `dlt_gap_fill_reply` | 5116 | Return requested blocks from dlt_block_log or fork_db |

Protocol:
- Only exchanged between exchange-enabled peers (`on_dlt_gap_fill_request` rejects non-exchange peers)
- Maximum 100 blocks per request (`GAP_FILL_MAX_BLOCKS`)
- 5-second cooldown between requests (`GAP_FILL_COOLDOWN_SEC`)
- Only used in FORWARD mode for gaps ≤100 blocks (larger gaps use SYNC mode)
- Requesting peer selects the exchange-enabled peer with the highest head block
- Requested blocks must be within the serving peer's DLT log range

Gap fill is triggered in three places:
1. `on_dlt_block_reply()` — when an out-of-order block is detected in FORWARD mode
2. `periodic_task()` — proactive gap detection every 5s cycle
3. `resume_block_processing()` — after snapshot pause, tries gap fill before falling back to SYNC
