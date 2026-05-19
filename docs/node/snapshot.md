# Snapshots

The snapshot plugin enables near-instant node startup by serializing the full blockchain state to a JSON file. Instead of replaying millions of blocks from the block log, a node loads a snapshot and syncs only the blocks produced since the snapshot was taken.

Nodes loaded from a snapshot run in **DLT mode**: the main block log stays empty and a compact **DLT rolling block log** (`dlt_block_log`) holds the most recent irreversible blocks.

---

## Quick Reference

| Goal | Command / Config |
|------|-----------------|
| Create snapshot once (stop node) | `vizd --create-snapshot /path/snap.json --plugin snapshot` |
| Create snapshot at block N | `snapshot-at-block = N` + `snapshot-dir = /path` |
| Create snapshot every N blocks | `snapshot-every-n-blocks = N` + `snapshot-dir = /path` |
| Bootstrap new node from file | `vizd --snapshot /path/snap.json --plugin snapshot` |
| Crash recovery | `vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot` |
| P2P auto-bootstrap | `sync-snapshot-from-trusted-peer = true` + `trusted-snapshot-peer = host:8092` |

---

## Enabling the Plugin

```ini
plugin = snapshot
```

---

## Configuration Reference

### CLI-only flags

| Flag | Description |
|------|-------------|
| `--snapshot <path>` | Bootstrap from a snapshot file (DLT mode). Skipped if `shared_memory.bin` already exists or the file was already imported (renamed to `.used`). |
| `--snapshot-auto-latest` | Auto-discover the latest snapshot in `snapshot-dir` by parsing block numbers from filenames. Use with `--replay-from-snapshot`. Ignored if `--snapshot` is set. |
| `--replay-from-snapshot` | Crash recovery: always wipes shared memory, imports snapshot, replays `dlt_block_log`. Does not rename the snapshot file. Requires `--snapshot` or `--snapshot-auto-latest`. |
| `--auto-recover-from-snapshot` | (default: `true`) Automatic runtime recovery when shared memory corruption is detected — no restart required. Disable with `--no-auto-recover-from-snapshot`. |
| `--create-snapshot <path>` | Create a snapshot at the given path using the current database, then exit. |
| `--sync-snapshot-from-trusted-peer` | (default: `false`) Download snapshot from trusted peers when state is empty. Must be explicitly enabled. |

### Config file options

| Option | Default | Description |
|--------|---------|-------------|
| `snapshot-at-block` | `0` | Create a snapshot when this block number is reached (0 = disabled). |
| `snapshot-every-n-blocks` | `0` | Create a snapshot every N blocks (0 = disabled). Only fires on live blocks — skipped during P2P catchup. |
| `snapshot-dir` | — | Directory for auto-generated snapshots. Created automatically if absent. |
| `snapshot-max-age-days` | `90` | Delete snapshots older than N days after a new one is created (0 = disabled). |
| `allow-snapshot-serving` | `false` | Serve snapshots to other nodes over TCP. |
| `allow-snapshot-serving-only-trusted` | `false` | Restrict serving to trusted peers only. |
| `snapshot-serve-endpoint` | `0.0.0.0:8092` | TCP endpoint for the snapshot server. |
| `trusted-snapshot-peer` | — | Trusted peer address for P2P snapshot sync (repeatable). |
| `dlt-block-log-max-blocks` | `100000` | Rolling block log size in DLT mode (chain plugin option). 0 = disabled. |

---

## Creating Snapshots

### Method 1: One-shot (stop node, create file, exit)

Stop the running node first, then:

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

The node opens the existing database, replays if needed, writes the snapshot, and exits before P2P or validator plugins activate.

### Method 2: Snapshot at a specific block (no downtime)

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

When block 5,000,000 is applied, the snapshot is written to `/data/snapshots/snapshot-block-5000000.json` without stopping the node.

### Method 3: Periodic snapshots (recommended for production)

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800   # ~24 hours at 3 s/block
snapshot-dir = /data/snapshots
snapshot-max-age-days = 90
```

Files are named `snapshot-block-<N>.json` automatically. Snapshot creation is asynchronous:

- **Phase 1** (read lock, ~1 s): serialize all database objects into memory.
- **Phase 2** (no lock, ~2 s): compress, checksum, write to disk.

Block processing is paused only during Phase 1; API and P2P reads are unaffected throughout.

**Recommended intervals:**

| Frequency | Blocks | Approx. time |
|-----------|--------|--------------|
| Frequent | 10,000 | ~8 hours |
| Daily | 28,800 | ~24 hours |
| Weekly | 100,000 | ~3.5 days |

### Method 4: Combining at-block and periodic

Both settings can be active simultaneously:

```ini
snapshot-at-block = 5000000
snapshot-every-n-blocks = 100000
snapshot-dir = /data/snapshots
```

---

## Bootstrap: Loading from a Snapshot (DLT Mode)

Transfer a snapshot file to the new node, then:

```bash
vizd \
  --snapshot /data/snapshots/viz-snapshot.json \
  --plugin snapshot \
  --plugin p2p \
  --p2p-seed-node seed1.viz.world:2001
```

The node loads state in seconds and begins P2P sync from the snapshot's block height.

### What happens during load

1. `chain::plugin_startup()` detects `--snapshot`.
2. Three safety checks (in order): shared memory already exists → skip; file not found (already `.used`) → skip; otherwise proceed.
3. Database opened via `open_from_snapshot()` (wipes and re-initializes chainbase).
4. Snapshot JSON validated (format version, chain ID, SHA-256 checksum), all 32 object types imported.
5. Snapshot file renamed to `.used` to prevent re-import on restart.
6. LIB promoted to `head_block_num` so P2P synopsis starts from the snapshot head.
7. Fork database seeded with the snapshot's head block.
8. P2P plugin starts and syncs blocks from `LIB + 1` onward.

### Restart safety

| Scenario | Result |
|----------|--------|
| First start (no `shared_memory.bin`, file present) | Imports snapshot, renames to `.used` |
| Restart (shared_memory exists) | Skips import — uses existing state |
| Restart (shared_memory wiped, file already `.used`) | Skips import — file not found |
| Force re-import | `--resync-blockchain` + a fresh snapshot file |

No need to remove `--snapshot` from the command line or Docker `VIZD_EXTRA_OPTS` after the initial import.

---

## Snapshot File Format

The snapshot is a single JSON file:

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
    "dynamic_global_property": [ ... ],
    "account": [ ... ],
    ...
  }
}
```

### 32 included object types

**Critical (11)** — consensus-essential:
`dynamic_global_property`, `witness_schedule`, `hardfork_property`, `account`, `account_authority`, `validator`, `witness_vote`, `block_summary`, `content`, `content_vote`, `block_post_validation`

**Important (15)** — needed for full operation:
`transaction`, `vesting_delegation`, `vesting_delegation_expiration`, `fix_vesting_delegation`, `withdraw_vesting_route`, `escrow`, `proposal`, `required_approval`, `committee_request`, `committee_vote`, `invite`, `award_shares_expire`, `paid_subscription`, `paid_subscribe`, `witness_penalty_expire`

**Optional (5)** — metadata and recovery:
`content_type`, `account_metadata`, `master_authority_history`, `account_recovery_request`, `change_recovery_account_request`

---

## DLT Rolling Block Log

In DLT mode the main `block_log` stays empty. The `dlt_block_log` (files `dlt_block_log.log` + `dlt_block_log.index`) stores recent irreversible blocks for:

- **P2P block serving** — peers can request recent blocks for fork resolution.
- **API access** — `get_block` works for blocks in the rolling window.

```ini
dlt-block-log-max-blocks = 100000   # keep last ~3.5 days of blocks
```

When the log exceeds this size, old blocks are pruned from the front. The implementation tracks logical file sizes independently of the memory-mapped file to prevent stale-size bugs after thousands of resize cycles.

---

## Crash Recovery: `--replay-from-snapshot`

Use this when `shared_memory.bin` is corrupted (unclean shutdown, disk full, hardware fault). Normal `--replay-blockchain` is not available in DLT mode because `block_log` is empty.

```bash
# Specify snapshot path explicitly
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.json --plugin snapshot

# Or auto-discover the latest snapshot
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

Recovery steps:
1. Always wipes `shared_memory.bin` (assumes corruption).
2. Imports the snapshot state.
3. Replays `dlt_block_log` from `snapshot_head + 1` onward.
4. Emits `on_sync` — P2P fills the remaining gap to the live chain head.

The snapshot file is **not** renamed to `.used` (it may be needed again).

### Example recovery scenario

A DLT node crashes at block 79,274,318 with `snapshot-every-n-blocks = 100000` and `dlt-block-log-max-blocks = 100000`:

```
/data/viz-snapshots/snapshot-block-79273800.json   ← latest snapshot
/blockchain/dlt_block_log.*                         ← contains blocks 79174319..79274318
/blockchain/shared_memory.bin                       ← CORRUPTED
```

```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

```
Loading state from snapshot: .../snapshot-block-79273800.json  (12.3 s)
Replaying dlt_block_log from block 79273801 to 79274318...
  100%  518 of 518  (block 79274318, elapsed 7.2 s)
Recovery complete. Started on blockchain with 79274318 blocks.
```

The node is now at block 79,274,318; P2P sync delivers the rest.

---

## Automatic Runtime Recovery: `--auto-recover-from-snapshot`

Enabled by default (`true`). When corruption is detected during block processing or generation at runtime, the node:

1. Finds the latest snapshot in `snapshot-dir`.
2. Closes the database.
3. Wipes and re-imports using the same code path as `--replay-from-snapshot`.
4. Resumes P2P sync — no restart required.

**Prerequisites:** `plugin = snapshot` enabled and snapshots present in `snapshot-dir`.

To disable (for debugging):

```bash
vizd --no-auto-recover-from-snapshot
```

---

## P2P Snapshot Sync

Nodes can download snapshots from trusted peers over a custom TCP protocol, eliminating manual file transfers.

### Snapshot server

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
```

### New node bootstrap

```ini
plugin = snapshot
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

When the node starts with 0 blocks and `sync-snapshot-from-trusted-peer = true`, it queries all trusted peers, selects the one with the highest snapshot, downloads it in 1 MB chunks, verifies the SHA-256 checksum, and imports — all before P2P or validator plugins activate.

### Security

- Downloads exceeding 2 GB are rejected.
- Checksum verified via streaming SHA-256 (never fully loaded into memory).
- Rate limiting, max 5 concurrent connections, 60-second per-connection deadline.
- `allow-snapshot-serving-only-trusted = true` restricts to the `trusted-snapshot-peer` list.

---

## Docker

Set `VIZD_EXTRA_OPTS` to pass snapshot flags:

```bash
# Bootstrap from snapshot
docker run -e VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...

# Crash recovery
docker run -e VIZD_EXTRA_OPTS="--replay-from-snapshot --snapshot-auto-latest --plugin snapshot" ...
```

Periodic snapshots via `config.ini` (no `VIZD_EXTRA_OPTS` needed):

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800
snapshot-dir = /var/lib/vizd/snapshots
```

Snapshot files are accessible on the host at the mounted volume path.

| Task | Method |
|------|--------|
| Periodic snapshots | `snapshot-every-n-blocks` in config |
| One-shot snapshot | `--create-snapshot` via `VIZD_EXTRA_OPTS` |
| Bootstrap new node | `--snapshot` via `VIZD_EXTRA_OPTS` |
| Crash recovery | `--replay-from-snapshot --snapshot-auto-latest` via `VIZD_EXTRA_OPTS` |
| Auto-recovery | Default — ensure `plugin = snapshot` and `snapshot-dir` set |
| P2P auto-bootstrap | `sync-snapshot-from-trusted-peer = true` + `trusted-snapshot-peer` in config |

---

## Recommended Production Config

```ini
plugin = snapshot

# Snapshot every ~24 hours
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
snapshot-max-age-days = 90

# DLT rolling block log: keep last ~3.5 days
dlt-block-log-max-blocks = 100000

shared-file-size = 4G
plugin = p2p
p2p-seed-node = seed1.viz.world:2001
```

---

## Stale Snapshot Detection

If a serving node's latest snapshot is older than the `dlt_block_log` start block, new nodes downloading the snapshot cannot sync the missing blocks. At startup the plugin detects this and automatically creates a fresh snapshot on the next live block — no manual intervention required.

---

## Troubleshooting

| Problem | Check |
|---------|-------|
| Node re-imports snapshot every restart | Snapshot file not renaming to `.used` — check write permissions on snapshot directory |
| `item_not_available` from peers | DLT block log may not cover the advertised blocks — verify `dlt-block-log-max-blocks` is large enough |
| P2P sync stalls after snapshot load | Check `[logger.sync]` in config; verify LIB was promoted to head after import |
| Snapshot creation fails | Check disk space in `snapshot-dir`; node continues running on failure |
| Auto-recovery fires unexpectedly | Check for disk errors; inspect logs for `shared_memory_corruption_exception` |
| P2P download rejected (>2 GB) | Snapshot too large — increase `dlt-block-log-max-blocks` on the serving node to reduce per-snapshot size |

---

See also: [Snapshot Plugin](../plugins/snapshot.md) for the complete implementation reference, and [P2P Overview](../p2p/overview.md) for DLT sync protocol details.
