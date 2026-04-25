# Shared Memory Architecture

The VIZ node stores all blockchain state in a memory-mapped file (`shared_memory.bin`) managed by the **chainbase** library, which wraps Boost.Interprocess `managed_mapped_file`. This is the sole database for chain state — the node **cannot operate without shared memory**.

---

## Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      vizd process                           │
│                                                             │
│  ┌──────────────┐     ┌──────────────────────────────────┐  │
│  │  block_log    │     │  shared_memory.bin (mmap)        │  │
│  │  dlt_block_log│     │                                  │  │
│  │  (raw blocks) │     │  ┌────────────────────────────┐  │  │
│  │              │     │  │  chainbase::database        │  │  │
│  └──────┬───────┘     │  │  ┌──────────────────────┐  │  │  │
│         │             │  │  │  account_index       │  │  │  │
│         │ replay/     │  │  │  witness_index       │  │  │  │
│         │ sync        │  │  │  transaction_index   │  │  │  │
│         ▼             │  │  │  ... (all objects)   │  │  │  │
│  ┌──────────────┐     │  │  └──────────────────────┘  │  │  │
│  │  database     │────▶  │  boost::shared_mutex _mutex│  │  │
│  │  (chainbase)  │     │  └────────────────────────────┘  │  │
│  └──────────────┘     └──────────────────────────────────┘  │
│                                                             │
│  ┌──────────────┐     ┌──────────────────────────────────┐  │
│  │  API threads  │────▶│  read_lock (shared, multiple)    │  │
│  │  (webserver   │     │  write_lock (exclusive)          │  │
│  │   pool=256)   │     └──────────────────────────────────┘  │
│  └──────────────┘                                           │
└─────────────────────────────────────────────────────────────┘
```

### Key Source Files

| File | Role |
|------|------|
| `thirdparty/chainbase/include/chainbase/chainbase.hpp` | Core chainbase database class, lock wrappers, index types |
| `thirdparty/chainbase/src/chainbase.cpp` | `open()`, `close()`, `resize()`, `flush()`, `wipe()` implementations |
| `libraries/chain/database.cpp` | Chain-level `open()`, `_resize()`, `check_free_memory()`, `push_block()`, `_generate_block()` |
| `libraries/chain/include/graphene/chain/database.hpp` | Chain database class declaration, resize/memory parameters |
| `plugins/chain/plugin.cpp` | Config option definitions, initialization, snapshot loading |
| `plugins/witness/witness.cpp` | Lockless reads in `maybe_produce_block()` and `is_witness_scheduled_soon()`, guarded by `operation_guard` |
| `plugins/p2p/p2p_plugin.cpp` | Lockless reads in block post-validation (`get_witness_key()`), guarded by `operation_guard` |

---

## Memory-Mapped File Internals

### File Format

The `shared_memory.bin` file is a Boost.Interprocess `managed_mapped_file`:

- **On create**: file is allocated at `shared-file-size` bytes, OS maps it into process address space
- **On open (write)**: if file exists and `shared-file-size` > current size, `managed_mapped_file::grow()` extends it
- **On open (read-only)**: mapped read-only, no growth possible
- **File lock**: when opened for writing, a `boost::interprocess::file_lock` ensures **only one process** can write

### Internal Structure

All chainbase objects (accounts, witnesses, transactions, etc.) are stored as C++ objects allocated inside the mapped segment using Boost.Interprocess allocators:

```cpp
template<typename T>
using allocator = boost::interprocess::allocator<T, managed_mapped_file::segment_manager>;

using shared_string = boost::interprocess::basic_string<char, std::char_traits<char>, allocator<char>>;
```

Objects are organized into **indices** (Boost.MultiIndex containers) that live inside the mapped segment. Pointers within these structures are **offset-based** (managed by the segment manager), which is why the file can be re-mapped at a different virtual address and still function — provided the remapping is done correctly.

---

## Locking Model

### Lock Types

Chainbase uses a single `boost::shared_mutex _mutex` per database instance:

| Lock Type | Wrapper Method | Concurrency |
|-----------|---------------|-------------|
| **Read lock** (`boost::shared_lock`) | `with_read_lock()`, `with_weak_read_lock()`, `with_strong_read_lock()` | Multiple readers can hold simultaneously |
| **Write lock** (`boost::unique_lock`) | `with_write_lock()`, `with_weak_write_lock()`, `with_strong_write_lock()` | Exclusive — blocks all readers AND other writers |

### Lock Variants

| Variant | Wait Time | Retries | Use Case |
|---------|-----------|---------|----------|
| **weak** | `_read_wait_micro` / `_write_wait_micro` (default 500ms) | `_max_read_wait_retries` / `_max_write_wait_retries` (default 3) | Normal API calls |
| **strong** | 1,000,000 μs (1 sec) | 100,000 | Critical operations (block apply, genesis init, replay) |

### Read Lock Guarantees

A read lock guarantees that **no write lock is held at the moment of acquisition** and that **no write lock can be acquired while any read lock is held**. However:

- A pending write lock request will **block** new read lock acquisitions (writer priority prevents starvation)
- The read lock does **not** prevent the segment from being destroyed by `resize()` if the resize happens from the same thread or while no read locks are held

### Write Lock Behavior

The write lock is **exclusive**: while held, no other thread can acquire either a read or write lock. All block processing, state modifications, and memory resizing occur under write lock.

---

## Configuration Parameters

All parameters are defined in `plugins/chain/plugin.cpp` and read from `config.ini`.

### Size Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `shared-file-dir` | `state` | Directory for `shared_memory.bin` (relative to data dir or absolute) |
| `shared-file-size` | `2G` | Initial size of the shared memory file. If file exists and this value is larger, the file grows. If smaller, no change. Does **not** require replay. |
| `inc-shared-file-size` | `2G` | Step size for auto-growth. When free space drops below `min-free-shared-file-size`, the file grows by this amount. |
| `min-free-shared-file-size` | `500M` | Free space threshold that triggers auto-growth. |

### Lock Timeout Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `read-wait-micro` | `500000` (500ms) | Timeout per read lock acquisition attempt |
| `max-read-wait-retries` | `3` | Maximum read lock retry attempts before throwing `"Unable to acquire READ lock"` |
| `write-wait-micro` | `500000` (500ms) | Timeout per write lock acquisition attempt |
| `max-write-wait-retries` | `3` | Maximum write lock retry attempts before throwing `"Unable to acquire WRITE lock"` |

### Operational Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `single-write-thread` | `false` | Serialize all block/transaction pushes through one thread. Reduces write lock contention but limits throughput. |
| `block-num-check-free-size` | `1000` | Check free space in shared memory every N blocks. Lower values = more frequent checks = earlier resize detection but more overhead. |
| `flush-state-interval` | (unset) | Flush shared memory changes to disk every N blocks |
| `clear-votes-before-block` | `0` | Remove votes older than this block number (0 = keep all). Reduces memory usage. |
| `skip-virtual-ops` | `false` | Skip virtual operation plugin notifications. Saves memory and processing. |
| `enable-plugins-on-push-transaction` | `false` | Enable plugin notifications for pushed transactions (not applied blocks). Safe to disable for performance. |

### Recommended Configurations

**Witness node (production):**
```ini
shared-file-size = 4G
inc-shared-file-size = 2G
min-free-shared-file-size = 500M
block-num-check-free-size = 1000
single-write-thread = true
```

**API node (high read throughput):**
```ini
shared-file-size = 8G
inc-shared-file-size = 2G
min-free-shared-file-size = 500M
block-num-check-free-size = 1000
single-write-thread = true
read-wait-micro = 1000000
max-read-wait-retries = 10
webserver-thread-pool-size = 256
```

**Replay/sync:**
```ini
shared-file-size = 8G
inc-shared-file-size = 4G
min-free-shared-file-size = 500M
block-num-check-free-size = 10
clear-votes-before-block = 0
skip-virtual-ops = true
```

---

## Auto-Resize Workflow

### Trigger Conditions

Resize is triggered in two places:

1. **Periodic check** — `check_free_memory()` called after each `_push_block()` in `push_block()`:
   ```
   if (current_block_num % block_num_check_free_size == 0
       && free_memory < min_free_shared_file_size)
       → _resize()
   ```

2. **bad_alloc fallback** — if `_push_block()` throws `boost::interprocess::bad_alloc`:
   ```
   catch bad_alloc → set_reserved_memory(free_memory()) → _resize() → retry _push_block()
   ```

### Resize Sequence

The resize is implemented in `chainbase::database::resize()` (`thirdparty/chainbase/src/chainbase.cpp`):

```
1. Assert no undo sessions active (_undo_session_count == 0)
2. _segment.reset()              ← DESTROY the memory mapping
3. open(_data_dir, read_write, new_size)  ← Re-open with larger size
   ├── managed_mapped_file::grow()    ← Extend file on disk
   └── new managed_mapped_file(...)   ← Re-map into address space
4. _index_list.clear()           ← Clear cached index pointers
5. _index_map.clear()
6. for each index_type:
       index_type->add_index(*this) ← Rebuild index pointers from new mapping
```

### Resize Barrier

Because `_segment.reset()` invalidates **all** pointers/references into shared memory, the resize must ensure that **no thread** — whether holding a lock or reading locklessly — has any live reference into the mapped segment. A simple write lock is insufficient because several code paths read chainbase indices without holding any lock ("lockless reads").

The **resize barrier** (`chainbase::database`) solves this with an atomic operation counter, a flag, and a condition variable:

```
┌──────────────────────────────────────────────────────────────────┐
│                    Resize Barrier Protocol                       │
│                                                                  │
│  Normal operation:                                               │
│    enter_operation()  ── wait while _resize_in_progress          │
│                       ── increment _active_operations            │
│    ... access shared memory ...                                  │
│    exit_operation()   ── decrement _active_operations            │
│                       ── notify resize thread if last op         │
│                                                                  │
│  Resize:                                                         │
│    begin_resize_barrier()  ── set _resize_in_progress = true     │
│                            ── wait until _active_operations == 0 │
│    ... _segment.reset() + open() + rebuild indices ...           │
│    end_resize_barrier()    ── set _resize_in_progress = false    │
│                            ── notify all waiting threads         │
└──────────────────────────────────────────────────────────────────┘
```

**Participation points:**

| Code Path | How It Participates |
|-----------|--------------------|
| `with_read_lock()` / `with_write_lock()` | `operation_guard` acquired automatically inside the lock wrapper before the `boost::shared_mutex` lock |
| `_generate_block()` lockless reads (pre-write-lock) | Explicit scoped `operation_guard` around `get_slot_at_time()`, `get_scheduled_witness()`, `get_witness()`, `find_account()` |
| `_generate_block()` lockless reads (post-write-lock) | Second `operation_guard` (`op_guard2`) around `get_dynamic_global_properties()`, `head_block_id()`, `get_witness()`, `get_hardfork_property_object()`; released via `release()` before `push_block()` |
| Witness plugin `maybe_produce_block()` | Explicit `operation_guard` around `get_slot_at_time()`, `get_scheduled_witness()`, index lookups; released via `release()` before `generate_block()` |
| Witness plugin `is_witness_scheduled_soon()` | Explicit `operation_guard` around `get_slot_at_time()`, `get_scheduled_witness()`, index lookups; released via `release()` before return |
| P2P plugin block post-validation | Explicit `operation_guard` around `get_witness_key()` calls; released via `release()` before `apply_block_post_validation()` |

**Key classes:**

- `chainbase::database::operation_guard` — RAII guard that calls `enter_operation()` on construction and `exit_operation()` on destruction. Supports early release via `release()`. Move-only (non-copyable).
- `chainbase::database::make_operation_guard()` — Factory method returning an `operation_guard`.
- `chainbase::database::begin_resize_barrier()` / `end_resize_barrier()` — Called by `apply_pending_resize()` to establish exclusive access for the resize.

**Immediate resize (reindex) does NOT use the barrier.** During reindex, the caller already holds an exclusive write lock and no API threads are running. Using the barrier would deadlock because the write lock itself holds an operation guard.

### Historical Context: Pre-Barrier Race Condition

Before the resize barrier was added, the resize used `with_strong_write_lock()` which only blocked threads holding or waiting for a `boost::shared_mutex` lock. This left lockless reads unprotected:

- `boost::shared_mutex` does **not** protect against segment destruction — a read lock only prevents concurrent writes, but the resize **is** the write
- Threads performing lockless reads (witness plugin, `_generate_block()`) could hold stale pointers into the old mapping after `_segment.reset()`

This was the root cause of shared memory corruption symptoms like `CRITICAL: Witness X account object MISSING from database!`.

### Corruption Symptoms

Typical corruption indicators (should not occur with the resize barrier in place, but listed for historical reference and diagnostics):

- `CRITICAL: Witness X account object MISSING from database!` — account index entry not found despite `account_index_size` showing entries exist
- `Could not modify object, most likely a uniqueness constraint was violated` — internal index pointers corrupted, uniqueness check fails
- Node crashes and restarts in an infinite loop — each restart opens the corrupted file, fails to produce/apply blocks, crashes again

### Why Misconfigured Thresholds Make It Worse

If `min-free-shared-file-size > inc-shared-file-size`:
```
shared-file-size = 500M
inc-shared-file-size = 500M
min-free-shared-file-size = 1000M    ← THRESHOLD > INCREMENT!
```

After one resize (500M → 1000M), free space is still below 1000M, triggering **another resize on the next check**, causing cascading resizes. While the resize barrier prevents corruption, frequent resizes still cause latency spikes as all operations are paused during each resize.

---

## Concurrency Architecture

### Thread Model

```
th_0    ─── Main thread: block production loop, block application
th_180+ ─── Webserver thread pool (default 256 threads): JSON-RPC API calls
            Each API call acquires read_lock (weak) or write_lock (for broadcast)
th_?    ─── P2P thread: block/transaction reception
```

### Read Path (API Calls)

Most `database_api` methods use `with_weak_read_lock()`:

```cpp
// Example: get_accounts
auto result = with_weak_read_lock([&]() {
    // Read from chainbase indices — operation_guard acquired automatically
    return accounts;
});
```

If the read lock cannot be acquired within `read-wait-micro × max-read-wait-retries`, the API returns error: `"Unable to acquire READ lock"`. If a resize barrier is active, the `operation_guard` inside `with_weak_read_lock()` will block until the resize completes before attempting the lock.

### Write Path (Block Application)

All state modifications go through `with_strong_write_lock()`:

```cpp
// push_block
apply_pending_resize();  // uses resize barrier (begin/end)
with_strong_write_lock([&]() {  // operation_guard acquired inside
    _push_block(new_block, skip);
    check_free_memory(false, new_block.block_num());
});
```

### Lockless Reads (Witness Plugin, Block Generation, P2P)

Some code paths read from chainbase indices **without** holding a `boost::shared_mutex` lock. These must explicitly use an `operation_guard` to participate in the resize barrier:

```cpp
// witness.cpp — maybe_produce_block()
auto op_guard = db.make_operation_guard();
uint32_t slot = db.get_slot_at_time(now);           // lockless read
string scheduled = db.get_scheduled_witness(slot);  // lockless read
// ... more lockless reads ...
op_guard.release();  // release before generate_block() which has its own guard
```

```cpp
// witness.cpp — is_witness_scheduled_soon()
auto op_guard = db.make_operation_guard();
uint32_t slot = db.get_slot_at_time(now);
string scheduled = db.get_scheduled_witness(s);
// ...
op_guard.release();  // release before returning true
```

```cpp
// database.cpp — _generate_block() (pre-write-lock reads)
{
    auto op_guard = make_operation_guard();
    uint32_t slot_num = get_slot_at_time(when);
    string scheduled_witness = get_scheduled_witness(slot_num);
    const auto& witness_obj = get_witness(witness_owner);
    const auto* witness_acct = find_account(witness_owner);
} // op_guard released before with_strong_write_lock
```

```cpp
// database.cpp — _generate_block() (post-write-lock reads)
auto op_guard2 = make_operation_guard();
auto maximum_block_size = get_dynamic_global_properties().maximum_block_size;
// ... with_strong_write_lock, then post-lock reads ...
pending_block.previous = head_block_id();
const auto& witness = get_witness(witness_owner);
const auto& hfp = get_hardfork_property_object();
const auto& dgp_block = get_dynamic_global_properties();
op_guard2.release();  // release before push_block()
```

```cpp
// p2p_plugin.cpp — block post-validation
auto op_guard = chain.db().make_operation_guard();
fc::ecc::public_key w_signing_key = chain.db().get_witness_key(account);
op_guard.release();  // release before apply_block_post_validation()
```

### single-write-thread Mode

When `single-write-thread = true`, all `push_block()` and `push_transaction()` calls are serialized through a single `fc::async()` queue. This:

- Prevents concurrent write lock contention
- Reduces the chance of read lock timeouts for API threads
- Is **recommended for all production nodes**

When `single-write-thread = false`, blocks and transactions can be pushed from multiple threads simultaneously, causing frequent write lock contention and `"Unable to acquire READ lock"` errors for API clients.

---

## Startup and Recovery

### Normal Startup

```
1. chainbase::database::open(shared_mem_dir, read_write, shared_file_size)
   ├── If file exists and shared_file_size > file_size → grow()
   ├── Map file into process address space
   └── Acquire file lock (exclusive write access)
2. Initialize indices
3. If no dynamic_global_property_object → init_genesis()
4. Open block_log / dlt_block_log
5. undo_all() → rewind to last irreversible block
6. Verify head_block matches block_log
```

### Snapshot Import Startup

```
1. chainbase::database::wipe(shared_mem_dir) ← Delete old shared memory
2. chainbase::database::open(shared_mem_dir, read_write, shared_file_size) ← Create fresh
3. init_genesis() ← Write genesis state
4. Load snapshot data via callback
5. Replay dlt_block_log blocks on top (if recovery mode)
```

### Replay

```
1. Open existing shared memory
2. with_strong_write_lock():
   ├── set_reserved_memory(1GB) ← Protect from fragmentation
   ├── For each block in block_log:
   │   ├── apply_block()
   │   └── check_free_memory() ← May trigger _resize()
   └── set_reserved_memory(0)
```

### Crash Recovery

If the node crashes due to shared memory corruption:

1. **Automatic restart** (Docker/systemd) will try to open the corrupted file
2. If the file is corrupted, the node will likely crash again (infinite loop)
3. **Manual recovery options:**
   - `--replay-blockchain` — Delete shared memory, replay from block_log
   - `--resync-blockchain` — Delete shared memory AND block_log, sync from network
   - `--snapshot <path>` — Load from snapshot file, then replay dlt_block_log

---

## File Layout

```
<shared-file-dir>/
├── shared_memory.bin     ← Main memory-mapped file (all chain state)
```

The `shared_memory.bin` file size grows in steps of `inc-shared-file-size`. The OS may not actually allocate physical memory until pages are touched (sparse file / lazy allocation), but the virtual address space reservation equals the file size.

### Size Planning

Approximate memory usage for a VIZ mainnet node at ~79M blocks:

| Component | Estimated Size |
|-----------|---------------|
| Account index (~14K accounts) | ~50 MB |
| Witness index | ~5 MB |
| Transaction history (operation_history plugin) | ~200–500 MB |
| Account history (account_history plugin) | ~100–300 MB |
| Follow/social indexes | ~50–100 MB |
| Other indexes | ~100–200 MB |
| **Total recommended starting size** | **4–8 GB** |

---

## Diagnostic Commands

### Check shared memory file size
```bash
ls -lh /var/lib/vizd/blockchain/shared_memory.bin
```

### Monitor free memory (from logs)
```
Free memory is now XmM
Memory is almost full on block N, increasing to XmM
```

### Detect corruption
```
CRITICAL: Witness X account object MISSING from database!
Could not modify object, most likely a uniqueness constraint was violated
```

### Lock timeout monitoring
```
Read lock timeout
No more retries for read lock
Write lock timeout
FATAL write lock timeout!!!
```

---

## Safety Rules

1. **`min-free-shared-file-size` must be less than `inc-shared-file-size`** — otherwise cascading resizes occur, causing frequent operation pauses
2. **Pre-allocate generously** — set `shared-file-size` large enough that resize is rare. Each resize pauses all operations while the segment is remapped.
3. **Use `single-write-thread = true`** in production — prevents write lock contention
4. **Avoid resize during witness production** — a witness node should have enough pre-allocated memory that resize never triggers during block generation. The resize barrier guarantees safety but introduces latency.
5. **After corruption, always replay** — there is no safe way to repair a corrupted `shared_memory.bin`. Use `--replay-blockchain` or `--snapshot` to rebuild state from block_log/dlt_block_log.
6. **Backup before config changes** — changing `shared-file-size` to a larger value triggers `grow()` on next startup, which is safe. Reducing it has no effect (file doesn't shrink).
7. **Any new lockless read path must use `operation_guard`** — if you add code that reads from chainbase indices without `with_read_lock()`/`with_write_lock()`, wrap it with `make_operation_guard()` to participate in the resize barrier. Failing to do so can cause stale pointer access during resize.
