# Chain Plugin

The chain plugin is the core component of every VIZ Ledger node. It manages the chainbase shared-memory database, accepts and validates blocks and transactions, maintains fork database and block log state, and coordinates startup with the snapshot and P2P plugins. All other plugins depend on it.

**Source:** [plugins/chain/plugin.cpp](../../plugins/chain/plugin.cpp)

---

## Dependencies

```
json_rpc::plugin
```

The chain plugin must be the first domain plugin initialized; `json_rpc` is its only formal dependency and is loaded first by the AppBase framework.

---

## Configuration

### CLI-only flags

These are one-time recovery or maintenance operations; they cause the node to perform a specific action on startup and cannot be set in `config.ini`.

| Flag | Description |
|------|-------------|
| `--replay-blockchain` | Wipe chainbase shared memory and replay the full block log from block 1. |
| `--force-replay-blockchain` | Same as `--replay-blockchain` but skips the corruption check. Use when the block log is intact but chainbase is unreadable. |
| `--replay-from-snapshot <path>` | Crash recovery for DLT nodes: wipe shared memory, import a snapshot, then replay the DLT rolling block log. See [Snapshot Plugin](./snapshot.md). |
| `--snapshot-auto-latest` | With `--replay-from-snapshot`: auto-discover the latest snapshot in `snapshot-dir` instead of specifying the path manually. |
| `--auto-recover-from-snapshot` | Default `true`. Automatically recover at runtime when shared memory corruption is detected during block processing or generation, without a restart. Disable with `--no-auto-recover-from-snapshot`. |
| `--resync-blockchain` | Wipe both shared memory and the block log; start from genesis or from a snapshot. Destructive — use only when recovering from complete data loss. |
| `--check-locks` | Validate lock ordering (development only). |
| `--validate-database-invariants` | Run database consistency checks on every block (very slow; development only). |

### Config file options

#### Shared memory

| Option | Default | Description |
|--------|---------|-------------|
| `shared-file-dir` | `state` | Directory for the shared memory file (absolute path, or relative to the data directory). |
| `shared-file-size` | `2G` | Initial shared memory size. Use `4G`–`16G` for production nodes depending on chain age and object counts. |
| `inc-shared-file-size` | `2G` | Growth increment when free space falls below the minimum threshold. |
| `min-free-shared-file-size` | `500M` | Auto-grow when free shared memory falls below this value. |
| `block-num-check-free-size` | `1000` | Check free space every N blocks. |
| `flush-state-interval` | `10000` | Force a full flush to disk every N blocks. Higher values improve throughput at the cost of more data to replay after an unclean shutdown. |

#### Block log and DLT

| Option | Default | Description |
|--------|---------|-------------|
| `dlt-block-log-max-blocks` | `100000` | Number of recent blocks to keep in the DLT rolling block log (`dlt_block_log.log`). Only active in DLT mode (after snapshot import). Set to `0` to disable. |
| `checkpoint` | — | Block-number/block-ID pairs that must match during replay; can be specified multiple times. |

#### Performance

| Option | Default | Description |
|--------|---------|-------------|
| `single-write-thread` | `false` | Route all write operations through a dedicated io_service thread. Improves consistency under high concurrency; slight throughput cost. |
| `skip-virtual-ops` | `false` | Skip virtual operation processing. Reduces memory use; breaks plugins that index virtual ops (`account_history`, `operation_history`). |
| `enable-plugins-on-push-transaction` | `false` | Notify observer plugins when transactions enter the pending pool (before block application). |
| `read-wait-micro` | *(db default)* | Read lock timeout in microseconds. |
| `max-read-wait-retries` | *(db default)* | Retry attempts before a read lock timeout is fatal. |
| `write-wait-micro` | *(db default)* | Write lock timeout in microseconds. |
| `max-write-wait-retries` | *(db default)* | Retry attempts before a write lock timeout is fatal. |

---

## Startup Sequence

```
plugin_initialize()    ← parse CLI and config options; validate snapshot path
plugin_startup()       ← open or create database
  ├─ --resync          → wipe shared memory + block log; init genesis
  ├─ --replay          → wipe shared memory; replay from block log
  ├─ --snapshot        → import snapshot; start DLT mode
  ├─ --replay-from-snapshot → import snapshot; replay dlt_block_log
  └─ normal restart    → open existing shared memory; replay if revision mismatch
emit on_sync()         ← P2P and validator plugins activate
```

All snapshot loading happens inside `plugin_startup()`, before P2P or validator ever see the database.

---

## Block Acceptance

`chain::plugin::accept_block()` is the entry point for all incoming blocks (from P2P and from the validator). It:

1. Validates the block timestamp is not too far in the future.
2. Under a write lock, calls `database::push_block()`.
3. Updates the fork database and block log.
4. Emits the `applied_block` signal to all subscriber plugins.
5. On `shared_memory_corruption_exception`, calls `attempt_auto_recovery()` if auto-recovery is enabled.

Transaction acceptance (`accept_transaction()`) follows the same path via `database::push_transaction()`.

---

## Shared Memory

The chainbase database lives in a single memory-mapped file (`shared_memory.bin`) in `shared-file-dir`. Key sizing guidance:

- Start with `shared-file-size = 4G` for a node loading from a recent snapshot.
- The database auto-grows by `inc-shared-file-size` when free space drops below `min-free-shared-file-size`.
- After a clean shutdown, the file shrinks back to actual used size.
- After a crash, run with `--replay-blockchain` or `--replay-from-snapshot` to rebuild consistent state.

---

## Troubleshooting

| Symptom | Action |
|---------|--------|
| `FC_ASSERT` or `database_revision_exception` on startup | Revision mismatch — run `--replay-blockchain` |
| Chainbase open fails with corruption error | Run `--replay-from-snapshot --snapshot-auto-latest` (DLT nodes) or `--replay-blockchain` (full nodes) |
| Node stuck at genesis after `--resync-blockchain` | Block log was also wiped; provide `--snapshot` to load state from a snapshot |
| Shared memory grows unbounded | Check `inc-shared-file-size` and `min-free-shared-file-size` settings; verify chain is applying blocks normally |
| `write lock timeout` errors | Another process holds the write lock; check for stale `vizd` processes |
| Auto-recovery fires repeatedly | Underlying storage may have hardware faults; check disk health; also verify `snapshot-every-n-blocks` is configured so fresh snapshots exist |

---

See also: [Snapshot Plugin](./snapshot.md), [Validator Plugin](./validator.md), [P2P Overview](../p2p/overview.md), [Block Processing](../consensus/block-processing.md).
