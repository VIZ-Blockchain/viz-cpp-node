# Shared Memory

All blockchain state in a VIZ node is stored in a single memory-mapped file (`shared_memory.bin`) managed by the **chainbase** library. The node cannot operate without this file.

---

## Architecture

```
vizd process
├── block_log / dlt_block_log  — raw block bytes (disk)
└── shared_memory.bin (mmap)   — all chain state (chainbase)
    ├── account_index
    ├── witness_index
    ├── transaction_index
    └── ... (all other object indices)
```

API threads (webserver thread pool) acquire **shared read locks**; block application holds an **exclusive write lock**. Multiple readers can coexist; a writer blocks all readers.

---

## Configuration

All options go in `config.ini`.

### Size Options

| Option | Default | Description |
|--------|---------|-------------|
| `shared-file-dir` | `state` | Directory for `shared_memory.bin` (relative to data dir or absolute) |
| `shared-file-size` | `2G` | Initial allocation. If file exists and this is larger, file grows. Does not trigger replay. |
| `inc-shared-file-size` | `2G` | Auto-growth step when free space falls below threshold |
| `min-free-shared-file-size` | `500M` | Free-space threshold that triggers auto-growth |

**Rule:** `min-free-shared-file-size` must be less than `inc-shared-file-size`, otherwise cascading resizes occur.

### Lock Timeout Options

| Option | Default | Description |
|--------|---------|-------------|
| `read-wait-micro` | `500000` (500 ms) | Timeout per read lock attempt |
| `max-read-wait-retries` | `3` | Max read attempts before error |
| `write-wait-micro` | `500000` (500 ms) | Timeout per write lock attempt |
| `max-write-wait-retries` | `3` | Max write attempts before error |

### Performance Options

| Option | Default | Description |
|--------|---------|-------------|
| `single-write-thread` | `false` | Serialize all block/transaction pushes. **Recommended for production.** |
| `block-num-check-free-size` | `1000` | Check free space every N blocks |
| `flush-state-interval` | — | Flush shared memory to disk every N blocks |
| `clear-votes-before-block` | `0` | Drop votes older than this block (0 = keep all). Reduces memory. |
| `skip-virtual-ops` | `false` | Skip virtual operation notifications. Saves CPU during replay. |

---

## Recommended Configurations

**Validator node (production):**
```ini
shared-file-size = 4G
inc-shared-file-size = 2G
min-free-shared-file-size = 500M
single-write-thread = true
```

**API node (high read throughput):**
```ini
shared-file-size = 8G
inc-shared-file-size = 2G
min-free-shared-file-size = 500M
single-write-thread = true
read-wait-micro = 1000000
max-read-wait-retries = 10
webserver-thread-pool-size = 256
```

**Replay / initial sync:**
```ini
shared-file-size = 8G
inc-shared-file-size = 4G
min-free-shared-file-size = 500M
block-num-check-free-size = 10
skip-virtual-ops = true
```

---

## Auto-Resize

The database auto-grows when free space drops below `min-free-shared-file-size`. Each resize:

1. Pauses all operations (including block production and API requests).
2. Destroys the current memory mapping.
3. Extends the file by `inc-shared-file-size`.
4. Re-maps the file and rebuilds all index pointers.

Pre-allocate `shared-file-size` generously to minimize resize frequency. Each resize causes a latency spike.

---

## Size Planning

Approximate usage for a VIZ mainnet full node:

| Component | Estimated Size |
|-----------|---------------|
| Account index (~14 K accounts) | ~50 MB |
| Validator index | ~5 MB |
| Operation history (operation_history plugin) | 200–500 MB |
| Account history (account_history plugin) | 100–300 MB |
| Other indexes | 100–200 MB |
| **Recommended starting size** | **4–8 GB** |

---

## Startup Sequence

```
1. Open shared_memory.bin (grow if shared-file-size is larger)
2. Acquire exclusive file lock
3. Initialize indices
4. If genesis missing → init_genesis()
5. Open block_log or dlt_block_log
6. undo_all() → rewind to last irreversible block
7. Verify head block matches block log
```

---

## Recovery

| Symptom | Action |
|---------|--------|
| `CRITICAL: validator X account object MISSING` | Corruption — use `--replay-from-snapshot --snapshot-auto-latest` |
| `Could not modify object, uniqueness constraint violated` | Corruption — use `--replay-from-snapshot --snapshot-auto-latest` |
| `Unable to acquire READ lock` | Lock contention — increase `read-wait-micro` / enable `single-write-thread` |
| Node crashes in a loop on startup | Corrupted file — `--replay-from-snapshot --snapshot-auto-latest` |

Recovery options:

- `--replay-from-snapshot --snapshot-auto-latest` — delete shared memory, import latest snapshot, replay dlt_block_log.
- `--resync-blockchain` — delete shared memory and block log, sync from peers.
- `--snapshot <path>` — load from specific snapshot, replay dlt_block_log on top.

---

See also: [Chain Plugin](../plugins/chain.md), [Snapshot Plugin](../plugins/snapshot.md), [Block Log](./block-log.md).
