# Snapshot Plugin

The snapshot plugin enables near-instant node startup by serializing and restoring the full blockchain state as a JSON file. Instead of replaying millions of blocks from the block log, a node loads a pre-built snapshot and begins syncing from the snapshot's block height via P2P.

**Source:** [plugins/snapshot/snapshot.cpp](../../plugins/snapshot/snapshot.cpp)

---

## Dependencies

```
chain::plugin
```

---

## Configuration

### CLI-only options

| Option | Default | Description |
|--------|---------|-------------|
| `--snapshot <path>` | — | Load state from a snapshot file (DLT mode). Skips import if `shared_memory.bin` already exists; renames the file to `.used` after successful import. |
| `--snapshot-auto-latest` | `false` | Auto-discover the latest snapshot in `snapshot-dir` by block number from filename. Ignored if `--snapshot` is also specified. |
| `--replay-from-snapshot` | `false` | Crash recovery: import a snapshot then replay blocks from the DLT rolling block log. Always wipes shared memory; does NOT rename the snapshot file to `.used`. Requires `--snapshot` or `--snapshot-auto-latest`. |
| `--auto-recover-from-snapshot` | `true` | Automatic runtime recovery from shared memory corruption — no restart required. Requires `plugin = snapshot` and snapshots in `snapshot-dir`. Disable with `--no-auto-recover-from-snapshot`. |
| `--create-snapshot <path>` | — | Create a snapshot at the given path using the current database state, then exit. Runs before P2P or validator activate. |
| `--sync-snapshot-from-trusted-peer` | `false` | Download and load a snapshot from trusted peers when state is empty. Requires `trusted-snapshot-peer`. Opt-in to prevent accidental state wipe. |

### Config file options

| Option | Default | Description |
|--------|---------|-------------|
| `snapshot-at-block` | `0` | Create a snapshot when this block number is reached (0 = disabled). |
| `snapshot-every-n-blocks` | `0` | Create a snapshot every N blocks (0 = disabled). Only fires on live blocks — skipped during initial P2P sync. |
| `snapshot-dir` | — | Directory for auto-generated snapshot files. Auto-created if absent. |
| `snapshot-max-age-days` | `90` | Delete snapshots older than N days after creating a new one (0 = disabled). |
| `allow-snapshot-serving` | `false` | Enable serving snapshots over TCP to other nodes. |
| `allow-snapshot-serving-only-trusted` | `false` | Restrict snapshot serving to configured trusted peers only. |
| `snapshot-serve-endpoint` | `0.0.0.0:8092` | TCP endpoint for the snapshot serving listener. |
| `trusted-snapshot-peer` | — | Trusted peer endpoint for P2P snapshot sync (`IP:port`); may be repeated. |

The `dlt-block-log-max-blocks` option (in the `chain` plugin config section) controls the rolling DLT block log size and is closely related to snapshot operation — see [DLT Rolling Block Log](#dlt-rolling-block-log) below.

---

## Creating Snapshots

### Method 1: One-shot (node stopped)

Stop the node, then restart it with `--create-snapshot`. The node opens the existing database (replaying from the block log if needed), writes the snapshot, and exits before P2P or validator activate:

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

### Method 2: At a specific block (no downtime)

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

When block 5,000,000 is applied, the node writes `/data/snapshots/snapshot-block-5000000.json` without interrupting operation.

### Method 3: Periodic (recommended)

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800
snapshot-dir = /data/snapshots
snapshot-max-age-days = 90
```

Files are named `snapshot-block-<N>.json`. Recommended intervals:

| Interval | Blocks | Time (at 3 s/block) |
|----------|--------|---------------------|
| Frequent | 10,000 | ~8 h |
| Daily | 28,800 | ~24 h |
| Weekly | 100,000 | ~3.5 days |

### Method 4: Combining at-block and periodic

Both options can be set together; `snapshot-at-block` fires once and `snapshot-every-n-blocks` fires repeatedly.

### How snapshot creation works

Snapshot creation is asynchronous and split into two phases to minimize impact:

- **Phase 1 (read lock, ~1 s):** All 32 tracked object types are serialized into memory. Block processing waits during this phase; API and P2P reads proceed concurrently.
- **Phase 2 (no lock, ~2 s):** Compression, SHA-256 checksum computation, and file I/O. Normal node operation resumes.

If creation fails (e.g., disk full), the error is logged and the node continues running.

---

## Loading from a Snapshot (DLT Mode)

### First-time startup

```bash
vizd --snapshot /path/to/snapshot.json --plugin snapshot
```

On first load:
1. The snapshot plugin validates the file header (format version, chain ID, SHA-256 checksum).
2. Shared memory is wiped and re-initialized from snapshot state.
3. All 32 object types are imported.
4. LIB is promoted to `head_block_num` so P2P sync starts from the snapshot point.
5. The snapshot file is renamed to `.used` to prevent re-import on restart.
6. P2P plugin starts and syncs forward from the snapshot head.

### Restart safety

The node is safe to restart with `--snapshot` still on the command line (e.g., via `VIZD_EXTRA_OPTS`):

| Scenario | Behavior |
|----------|----------|
| First start (no shared memory, file exists) | Imports snapshot; renames to `.used` |
| Restart (shared memory exists) | Skips import — uses existing state |
| Restart (shared memory wiped, file already `.used`) | Skips import — "snapshot file not found" |
| Force re-import | Use `--resync-blockchain` + provide a fresh snapshot file |

### DLT mode

After snapshot import the node runs in **DLT mode**: the main `block_log` is empty. A separate [DLT rolling block log](#dlt-rolling-block-log) stores recent blocks for P2P serving and local block queries. The mode is detected automatically on subsequent restarts (empty `block_log` + existing chainbase state).

---

## Snapshot File Format

Each snapshot is a single JSON file:

```json
{
  "header": {
    "version": 1,
    "chain_id": "...",
    "snapshot_block_num": 12345678,
    "snapshot_block_id": "...",
    "snapshot_block_time": "2025-01-01T00:00:00",
    "last_irreversible_block_num": 12345660,
    "payload_checksum": "sha256...",
    "object_counts": { "account": 50000, ... }
  },
  "state": {
    "dynamic_global_property": [...],
    "account": [...],
    ...
  }
}
```

### Included object types (32 total)

**Critical (11)** — consensus-essential:
`dynamic_global_property`, `witness_schedule`, `hardfork_property`, `account`, `account_authority`, `validator`, `witness_vote`, `block_summary`, `content`, `content_vote`, `block_post_validation`

**Important (15)** — required for full operation:
`transaction`, `vesting_delegation`, `vesting_delegation_expiration`, `fix_vesting_delegation`, `withdraw_vesting_route`, `escrow`, `proposal`, `required_approval`, `committee_request`, `committee_vote`, `invite`, `award_shares_expire`, `paid_subscription`, `paid_subscribe`, `witness_penalty_expire`

**Optional (5)** — metadata and recovery:
`content_type`, `account_metadata`, `master_authority_history`, `account_recovery_request`, `change_recovery_account_request`

---

## DLT Rolling Block Log

In DLT mode the main `block_log` is empty. The **DLT rolling block log** (`dlt_block_log.log` + `dlt_block_log.index`) stores the most recent irreversible blocks.

This enables:
- **P2P block serving** — peers requesting recent blocks for fork resolution and sync catch-up.
- **Local block queries** — API calls like `get_block` work for the stored range.

### Configuration

```ini
# Keep the last 100,000 blocks (default)
dlt-block-log-max-blocks = 100000

# Disable the DLT block log entirely
# dlt-block-log-max-blocks = 0
```

The log uses a rolling window: when it exceeds `dlt-block-log-max-blocks`, old blocks are pruned from the front. On restart, the log is preserved and new blocks are appended from where they left off.

### Stale snapshot detection

If the latest snapshot's block number is less than the DLT log's start block, downloading nodes cannot bridge the gap (snapshot at block N, DLT log starts at M > N, blocks N+1..M-1 missing). The plugin detects this at startup and creates an urgent fresh snapshot on the first live block after sync completes.

---

## Crash Recovery: `--replay-from-snapshot`

When `shared_memory.bin` becomes corrupted and the node cannot start normally, `--replay-from-snapshot` combines snapshot import with DLT block log replay:

```bash
# Specify the snapshot explicitly
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.vizjson --plugin snapshot

# Or auto-discover the latest snapshot
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

**Recovery steps:**
1. Wipes shared memory (always — assumes corruption).
2. Imports the snapshot.
3. Replays blocks from `dlt_block_log` beyond the snapshot head, restoring up to the latest available state.
4. Resumes P2P sync from the replayed head block.

### Comparison: `--snapshot` vs `--replay-from-snapshot`

| Aspect | `--snapshot` | `--replay-from-snapshot` |
|--------|-------------|--------------------------|
| Purpose | Bootstrap a new node | Recover from corruption |
| Shared memory check | Skips if already exists | Always wipes and re-imports |
| Snapshot file rename | Renames to `.used` | Does NOT rename |
| DLT block log replay | No | Yes |
| Typical use | First-time setup | Crash recovery |

### Example recovery

A DLT node crashes at block 79,274,318. State:
```
snapshots/snapshot-block-79273800.vizjson   ← 518 blocks behind crash point
blockchain/dlt_block_log.log                ← contains blocks 79174319..79274318
blockchain/shared_memory.bin                ← CORRUPTED
```

Recovery command: `vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot`

```
Loading state from snapshot: snapshot-block-79273800.vizjson
Snapshot loaded at block 79273800, elapsed time 12.3 sec
Replaying dlt_block_log from block 79273801 to 79274318 (518 blocks)...
Done replaying, head_block=79274318, elapsed time: 7.3 sec
```

P2P sync fills the gap to the live chain head.

---

## Automatic Runtime Recovery: `--auto-recover-from-snapshot`

Enabled by default. When shared memory corruption is detected during block processing or block generation, the node:

1. Closes the database.
2. Finds the latest snapshot in `snapshot-dir`.
3. Wipes shared memory, imports the snapshot, replays `dlt_block_log`.
4. Resumes P2P sync — no restart required.

**Prerequisites:**
- `plugin = snapshot` must be enabled.
- Snapshots must exist in `snapshot-dir`. Use `snapshot-every-n-blocks` to create them automatically.
- `dlt_block_log` should cover blocks beyond the snapshot for minimal data loss.

If recovery fails (no snapshot found, import error), the node logs an error and shuts down cleanly.

To disable: `--no-auto-recover-from-snapshot`

---

## P2P Snapshot Sync

Nodes can serve and download snapshots over TCP, enabling fully automated bootstrap without manual file transfers.

### Server configuration

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/snapshots

# Optional: restrict to trusted peers only
# allow-snapshot-serving-only-trusted = true
# trusted-snapshot-peer = 1.2.3.4:8092
```

### Client configuration

```ini
plugin = snapshot
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

`sync-snapshot-from-trusted-peer` defaults to `false` — must be explicitly enabled to prevent accidental state wipe.

### How it works

1. **Query:** Connects to each trusted peer, sends a `snapshot_info_request`, collects metadata (block number, checksum, size).
2. **Select:** Picks the peer with the highest block number.
3. **Download:** Downloads in 1 MB chunks; progress is logged to console every 5%.
4. **Verify:** SHA-256 checksum verified via streaming (no full-file load into memory).
5. **Import:** Wipes state, loads the verified snapshot, initializes hardforks.

All operations happen inside `chain::plugin_startup()`, before P2P and validator activate. The node is fully blocked until import completes.

### Security

- Maximum download size: 2 GB.
- Max 5 concurrent connections per server, each with a 60-second connection deadline.
- Rate limiting per IP.
- Control messages limited to 64 KB; only data replies allow up to 64 MB.
- The TCP server runs on a dedicated thread, independent of the main I/O loop.

---

## Recommended Production Config

```ini
plugin = snapshot

# Daily snapshots (~24 h at 3 s/block)
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots

# Keep 90 days of snapshots (default)
snapshot-max-age-days = 90

# DLT rolling block log: last 100k blocks (default)
dlt-block-log-max-blocks = 100000
```

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Node re-imports snapshot on every restart | `shared_memory.bin` is being deleted between restarts; or snapshot file was never renamed to `.used` |
| `Snapshot file not found` on start | File was already renamed to `.used` from a previous successful import; or wrong path |
| `Chain ID mismatch` on snapshot load | Snapshot was created from a different chain; cannot import |
| `Checksum mismatch` | Snapshot file is corrupt or partially transferred |
| Snapshot creation never fires | `snapshot-dir` not set, or node is still in P2P sync (periodic snapshots skip sync mode) |
| Stale snapshot warning at startup | Latest snapshot is older than `dlt_block_log` start; node will create a fresh snapshot after the next live block |
| Auto-recovery fires but fails | No snapshots in `snapshot-dir`; check that `snapshot-every-n-blocks` is configured |
| P2P snapshot download fails | Check `trusted-snapshot-peer` is reachable on port 8092; check `allow-snapshot-serving = true` on server |

---

See also: [Snapshot](../node/snapshot.md), [Validator Plugin](./validator.md), [Chain Plugin](./chain.md), [P2P Overview](../p2p/overview.md).
