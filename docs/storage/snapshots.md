# Snapshot Plugin

The snapshot plugin enables near-instant node startup by serializing and restoring the full blockchain state as a JSON snapshot file. Instead of replaying millions of blocks from the block log, a node can load a pre-built snapshot and begin syncing from that point via P2P.

---

## Enabling

```ini
plugin = snapshot
```

Or on the command line:

```bash
vizd --plugin snapshot
```

---

## Configuration Reference

### CLI-only Options

| Option | Type | Description |
|--------|------|-------------|
| `--snapshot <path>` | string | Load state from a snapshot file (DLT mode). Skips import if `shared_memory.bin` already exists; renames file to `.used` after successful import. |
| `--snapshot-auto-latest` | bool | Auto-discover the latest snapshot in `snapshot-dir` by block number in filename. Ignored if `--snapshot` is set. |
| `--replay-from-snapshot` | bool | Crash recovery: import snapshot then replay `dlt_block_log` to reach latest available state. Always wipes shared memory; does not rename the snapshot. |
| `--auto-recover-from-snapshot` | bool (default: `true`) | Automatic runtime recovery when shared memory corruption is detected. Closes database, finds latest snapshot, wipes shared memory, imports and replays — without a restart. |
| `--create-snapshot <path>` | string | Create a snapshot from current database state, then exit. |
| `--sync-snapshot-from-trusted-peer` | bool (default: `false`) | Download and load snapshot from trusted peers when state is empty. Must be explicitly enabled. |

### Config File Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `snapshot-at-block` | uint32 | `0` | Create snapshot when this block number is reached |
| `snapshot-every-n-blocks` | uint32 | `0` | Create snapshot every N blocks (0 = disabled) |
| `snapshot-dir` | string | `""` | Directory for auto-generated snapshot files |
| `snapshot-max-age-days` | uint32 | `90` | Delete snapshots older than N days (0 = disabled) |
| `allow-snapshot-serving` | bool | `false` | Enable serving snapshots over TCP to other nodes |
| `allow-snapshot-serving-only-trusted` | bool | `false` | Restrict serving to trusted peers only |
| `snapshot-serve-endpoint` | string | `0.0.0.0:8092` | TCP endpoint for snapshot serving |
| `trusted-snapshot-peer` | string (multi) | — | Trusted peer for snapshot sync (`IP:port`). Repeatable. |
| `dlt-block-log-max-blocks` | uint32 | `100000` | Recent blocks to keep in DLT rolling block log (chain plugin) |

---

## Creating Snapshots

### Method 1: One-shot (node stops and exits)

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

The node opens the existing database, replays if needed, creates the snapshot, and exits before P2P activates.

### Method 2: At a specific block (no downtime)

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

When block 5,000,000 is applied, the snapshot is written to `/data/snapshots/snapshot-block-5000000.json` on a background thread — block processing is only briefly paused.

### Method 3: Periodic automatic snapshots (recommended)

```ini
plugin = snapshot
snapshot-every-n-blocks = 100000
snapshot-dir = /data/snapshots
```

Snapshots are created every 100,000 blocks (~3.5 days). Snapshots are skipped while syncing — they only trigger on live blocks.

**Recommended intervals:**

| Interval | Blocks | Approximate time |
|----------|--------|-----------------|
| Frequent | 10,000 | ~8 hours |
| Daily | 28,800 | ~24 hours |
| Weekly | 100,000 | ~3.5 days |
| Rare | 1,000,000 | ~35 days |

### Snapshot Rotation

Old snapshots are automatically deleted after each new snapshot if older than `snapshot-max-age-days` (default 90). To disable:

```ini
snapshot-max-age-days = 0
```

---

## Loading from a Snapshot (DLT Mode)

```bash
vizd --snapshot /path/to/snapshot.json --plugin snapshot
```

What happens during load:

1. Snapshot plugin registers a load callback on the chain plugin.
2. Chain plugin checks: if `shared_memory.bin` already exists → skips import (restart safety). If snapshot file not found → skips import.
3. Database is opened via `open_from_snapshot()` — shared memory is wiped, chainbase initialized.
4. Snapshot is validated (format version, chain ID, SHA-256 checksum) and all 32 object types imported.
5. Snapshot file is renamed to `.used`.
6. LIB is promoted to `head_block_num` so P2P synopsis starts from the snapshot head.
7. Fork database is seeded with the snapshot head block.
8. P2P starts syncing from LIB + 1.

### Restart Safety

| Restart scenario | What happens |
|-----------------|--------------|
| 1st start (no shared_memory, file exists) | Imports snapshot, renames to `.used` |
| Restart (shared_memory exists) | Skips import |
| Restart (shared_memory wiped, file is `.used`) | Skips import |
| Force re-import | `--resync-blockchain` + fresh snapshot file |

The `--snapshot` flag is safe to leave on the command line permanently.

---

## DLT Rolling Block Log

When running in DLT mode (loaded from snapshot), the main `block_log` is empty. A separate **DLT rolling block log** (`dlt_block_log`) stores the most recent irreversible blocks.

- Enables P2P block serving (peers can request recent blocks for fork resolution).
- Enables API calls like `get_block` for recent blocks.
- Stored in `dlt_block_log.log` and `dlt_block_log.index` in the blockchain data directory.
- Rolling window: when the log exceeds `dlt-block-log-max-blocks`, old blocks are truncated from the front.

```ini
dlt-block-log-max-blocks = 100000
```

---

## Crash Recovery: `--replay-from-snapshot`

When `shared_memory.bin` is corrupted after an unclean shutdown, use this mode:

```bash
# Specify snapshot path explicitly
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.vizjson --plugin snapshot

# Auto-discover the latest snapshot
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

Recovery steps:
1. Wipes `shared_memory.bin` (always — assumes corruption).
2. Imports snapshot state.
3. Replays `dlt_block_log` blocks beyond the snapshot head.
4. Resumes P2P sync from the replayed head.

| Aspect | `--snapshot` | `--replay-from-snapshot` |
|--------|-------------|--------------------------|
| Shared memory check | Skips if exists | Always wipes |
| Snapshot rename | Renames to `.used` | Does not rename |
| DLT block log replay | No | Yes |
| Use case | Bootstrap | Crash recovery |

---

## Automatic Runtime Recovery: `--auto-recover-from-snapshot`

Enabled by default (`true`). When shared memory corruption is detected during block processing or block generation, the node:

1. Closes the database.
2. Finds the latest snapshot in `snapshot-dir`.
3. Wipes shared memory, imports snapshot, replays `dlt_block_log`.
4. Resumes P2P sync — **no restart required**.

Prerequisites:
- `plugin = snapshot` must be enabled.
- Snapshots must exist in `snapshot-dir`.

To disable (e.g., for debugging):

```bash
vizd --no-auto-recover-from-snapshot
```

---

## P2P Snapshot Sync

Nodes can download snapshots directly from trusted peers over TCP.

### Server configuration

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
```

### Client configuration

```ini
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

The client connects to each trusted peer, selects the one with the highest block number, downloads the snapshot in 1 MB chunks, verifies the SHA-256 checksum, and imports it.

Security features: max 2 GB snapshot size, trusted peer list, rate limiting, 60-second connection deadline, streaming checksum verification.

---

## Snapshot File Format

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

### Included Object Types (32 total)

**Critical (11):** `dynamic_global_property`, `witness_schedule`, `hardfork_property`, `account`, `account_authority`, `validator`, `witness_vote`, `block_summary`, `content`, `content_vote`, `block_post_validation`

**Important (15):** `transaction`, `vesting_delegation`, `vesting_delegation_expiration`, `fix_vesting_delegation`, `withdraw_vesting_route`, `escrow`, `proposal`, `required_approval`, `committee_request`, `committee_vote`, `invite`, `award_shares_expire`, `paid_subscription`, `paid_subscribe`, `witness_penalty_expire`

**Optional (5):** `content_type`, `account_metadata`, `master_authority_history`, `account_recovery_request`, `change_recovery_account_request`

---

## Recommended Production Config

```ini
plugin = snapshot

# Create a snapshot every ~24 hours
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots

# Auto-delete snapshots older than 90 days
snapshot-max-age-days = 90

# DLT rolling block log: keep last 100k blocks
dlt-block-log-max-blocks = 100000

shared-file-size = 4G
plugin = p2p
p2p-seed-node = seed1.viz.world:2001
```

---

## Docker Quick Reference

| Task | Command |
|------|---------|
| Start with periodic snapshots | Add `snapshot-every-n-blocks` to config, restart container |
| One-shot snapshot | `VIZD_EXTRA_OPTS="--create-snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot"` |
| Load from snapshot | `VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot"` |
| Crash recovery | `VIZD_EXTRA_OPTS="--replay-from-snapshot --snapshot-auto-latest --plugin snapshot"` |
| Auto-recovery (default) | Enabled by default; ensure `plugin = snapshot` and `snapshot-every-n-blocks` are set |

---

See also: [Chain Plugin](../plugins/chain.md), [Shared Memory](./shared-memory.md), [Block Log](./block-log.md), [P2P Sync Scenarios](../p2p/sync-scenarios.md).
