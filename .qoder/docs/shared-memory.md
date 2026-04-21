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
| `shared-file-dir` | `blockchain` | Directory for `shared_memory.bin` (relative to data dir or absolute) |
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

### Critical Race Condition

**The resize operation is inherently dangerous in a multi-threaded environment.** When `_segment.reset()` destroys the old mapping:

- All pointers/references into the old segment become **dangling**
- `boost::shared_mutex` does **not** protect against this — a read lock only prevents concurrent writes, but the resize **is** the write
- If any thread holds a read lock and has cached a reference/pointer to an object in the old mapping, that reference becomes invalid after `_segment.reset()`

The `resize()` function checks `_undo_session_count` but does **not** check for active read locks. The write lock held during `push_block()` prevents other threads from acquiring **new** read locks during the resize, but threads that already hold read locks can still be accessing the old mapping.

**This is the root cause of shared memory corruption** when resize occurs while API threads are reading.

### Corruption Symptoms

Typical corruption indicators:

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

After one resize (500M → 1000M), free space is still below 1000M, triggering **another resize on the next check**, causing cascading resizes that dramatically increase the chance of corruption.

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
    // Read from chainbase indices
    return accounts;
});
```

If the read lock cannot be acquired within `read-wait-micro × max-read-wait-retries`, the API returns error: `"Unable to acquire READ lock"`.

### Write Path (Block Application)

All state modifications go through `with_strong_write_lock()`:

```cpp
// push_block
with_strong_write_lock([&]() {
    _push_block(new_block, skip);
    check_free_memory(false, new_block.block_num());
});
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

1. **`min-free-shared-file-size` must be less than `inc-shared-file-size`** — otherwise cascading resizes occur, dramatically increasing corruption risk
2. **Pre-allocate generously** — set `shared-file-size` large enough that resize is rare. Resizing is the most dangerous operation.
3. **Use `single-write-thread = true`** in production — prevents write lock contention and reduces the window for resize race conditions
4. **Avoid resize during witness production** — a witness node should have enough pre-allocated memory that resize never triggers during block generation
5. **After corruption, always replay** — there is no safe way to repair a corrupted `shared_memory.bin`. Use `--replay-blockchain` or `--snapshot` to rebuild state from block_log/dlt_block_log.
6. **Backup before config changes** — changing `shared-file-size` to a larger value triggers `grow()` on next startup, which is safe. Reducing it has no effect (file doesn't shrink).
