# Snapshot Plugin (DLT Mode)

The snapshot plugin enables near-instant node startup by serializing and restoring the full blockchain state as a JSON snapshot file. Instead of replaying millions of blocks from the block log, a node can load a pre-built snapshot and begin syncing from the snapshot's block height via P2P.

## Plugin Name

`snapshot`

## Dependencies

- `chain` plugin (required, auto-loaded)

## Config & CLI Options

### CLI-only options (command line arguments)

| Option | Type | Description |
|--------|------|-------------|
| `--snapshot <path>` | `string` | Load state from a snapshot file instead of replaying blockchain. The node opens in DLT mode (no block log). |
| `--create-snapshot <path>` | `string` | Create a snapshot file at the given path using the current database state, then exit. |

### Config file options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `snapshot-at-block` | `uint32_t` | `0` | Create a snapshot when the specified block number is reached (while the node is running). |
| `snapshot-every-n-blocks` | `uint32_t` | `0` | Automatically create a snapshot every N blocks (0 = disabled). |
| `snapshot-dir` | `string` | `""` | Directory for auto-generated snapshot files. Used by `snapshot-at-block` and `snapshot-every-n-blocks`. |

## Enabling the Plugin

The snapshot plugin is registered by default in `vizd`. To enable it, add it to your `config.ini`:

```ini
plugin = snapshot
```

Or pass it on the command line:

```bash
vizd --plugin snapshot
```

## Creating a Snapshot

### Method 1: One-shot snapshot (stop node, create, exit)

Stop the node, then restart it with the `--create-snapshot` flag. The node will open the existing database, serialize the full state at the current head block, write the snapshot file, and exit:

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

The snapshot is taken at the **current head block** using a strong read lock to ensure consistency. All 32 tracked object types are exported as JSON arrays with a SHA-256 checksum for integrity verification.

### Method 2: Snapshot at a specific block (no downtime)

The node creates a snapshot automatically when the specified block number is applied. Add to `config.ini`:

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

Start the node normally:

```bash
vizd
```

When block 5,000,000 is applied, the snapshot is created at `/data/snapshots/snapshot-block-5000000.json` without stopping the node. The snapshot uses `with_strong_read_lock` so regular node operation is briefly paused during serialization but resumes automatically.

### Method 3: Periodic automatic snapshots (no downtime, recommended)

The node creates snapshots automatically every N blocks. This is the recommended approach for production nodes that need regular backups.

Add to `config.ini`:

```ini
plugin = snapshot

# Create a snapshot every 100,000 blocks (~3.5 days at 3s/block)
snapshot-every-n-blocks = 100000

# Directory where snapshot files are saved
snapshot-dir = /data/snapshots
```

Start the node normally:

```bash
vizd
```

Snapshot files are named automatically: `snapshot-block-<BLOCK_NUM>.json`

Example output in the snapshots directory:

```
/data/snapshots/
  snapshot-block-100000.json
  snapshot-block-200000.json
  snapshot-block-300000.json
  ...
```

**Recommended intervals:**

| Interval | Blocks | Approximate time (at 3s/block) |
|----------|--------|-------------------------------|
| Frequent | 10,000 | ~8.3 hours |
| Daily | 28,800 | ~24 hours |
| Weekly | 100,000 | ~3.5 days |
| Rare | 1,000,000 | ~34.7 days |

**Notes on periodic snapshots:**
- The snapshot runs inside the `applied_block` signal callback, which is already under the database write lock. This means **block processing is paused** during snapshot serialization and file writing. No additional locks are needed and no deadlocks can occur.
- On a blockchain with tens of thousands of accounts/objects, serialization may take several seconds. During this time, the node will not process new blocks (they will queue up and be processed after the snapshot completes).
- For large chains, consider using a longer interval (e.g., `100000` blocks) to minimize the impact on block processing.
- Old snapshot files are **not** automatically deleted. Use an external cron job or script to manage disk space.
- If snapshot creation fails (e.g., disk full), the error is logged but the node continues running normally.

### Method 4: Combining at-block with periodic

You can use both `snapshot-at-block` and `snapshot-every-n-blocks` together:

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-every-n-blocks = 100000
snapshot-dir = /data/snapshots
```

This creates a snapshot at block 5,000,000 AND every 100,000 blocks.

### Managing snapshot disk space

Since periodic snapshots accumulate, add a cron job to clean up old files:

```bash
# Keep only the 5 most recent snapshots
ls -t /data/snapshots/snapshot-block-*.json | tail -n +6 | xargs rm -f
```

Or as a cron entry (runs daily at midnight):

```cron
0 0 * * * ls -t /data/snapshots/snapshot-block-*.json | tail -n +6 | xargs rm -f
```

## Loading from a Snapshot (DLT Mode)

To start a node from a snapshot file:

```bash
vizd --snapshot /path/to/snapshot.json --plugin snapshot
```

### What happens during snapshot loading

1. The **chain plugin** detects the `--snapshot` option and opens the database using `open_from_snapshot()` — this initializes chainbase, indexes, and evaluators but does **not** open the block log.
2. The **snapshot plugin** reads the JSON file, validates the header (format version, chain ID, SHA-256 checksum), and imports all objects into the database under a strong write lock.
3. After import, `initialize_hardforks()` is called to populate the hardfork schedule.
4. The fork database is seeded with the head block from the snapshot.
5. The `on_sync` signal is emitted so other plugins (P2P, APIs, etc.) know the node is ready.
6. The node begins syncing from **LIB + 1** via P2P network.

### Important notes

- **No block log**: In DLT mode, the block log is not opened. Historical block queries will not work until the node has synced new blocks.
- **Chain ID validation**: The snapshot's chain ID must match the node's compiled chain ID. Mismatches are rejected.
- **Checksum verification**: The payload checksum is verified before any objects are imported.
- **One-time operation**: The `--snapshot` flag is a CLI-only option. After the initial load, restart the node normally (without `--snapshot`) for subsequent runs.

## Snapshot File Format

The snapshot is a single JSON file with this structure:

```json
{
  "header": {
    "version": 1,
    "chain_id": "...",
    "snapshot_block_num": 12345678,
    "snapshot_block_id": "...",
    "snapshot_block_time": "2025-01-01T00:00:00",
    "last_irreversible_block_num": 12345660,
    "last_irreversible_block_id": "...",
    "snapshot_creation_time": "2025-01-01T00:05:00",
    "payload_checksum": "sha256...",
    "object_counts": {
      "account": 50000,
      "witness": 100,
      "content": 200000,
      ...
    }
  },
  "state": {
    "dynamic_global_property": [ ... ],
    "witness_schedule": [ ... ],
    "hardfork_property": [ ... ],
    "account": [ ... ],
    "account_authority": [ ... ],
    "witness": [ ... ],
    ...
    "fork_db_head_block": { ... }
  }
}
```

### Included Object Types (32 total)

**Critical (11)** — consensus-essential, always required:

- `dynamic_global_property` — global chain state (singleton)
- `witness_schedule` — current witness schedule (singleton)
- `hardfork_property` — hardfork tracking state (singleton)
- `account` — all accounts
- `account_authority` — master/active/regular authorities
- `witness` — witness registrations
- `witness_vote` — witness votes
- `block_summary` — block ID summaries (65536 entries)
- `content` — content/posts
- `content_vote` — content votes
- `block_post_validation` — block post-validation records

**Important (15)** — needed for full operation:

- `transaction` — pending transactions
- `vesting_delegation` — vesting delegations
- `vesting_delegation_expiration` — expiring delegations
- `fix_vesting_delegation` — delegation fix records
- `withdraw_vesting_route` — vesting withdrawal routes
- `escrow` — escrow transfers
- `proposal` — governance proposals
- `required_approval` — proposal approval requirements
- `committee_request` — committee funding requests
- `committee_vote` — committee votes
- `invite` — account invites
- `award_shares_expire` — expiring award shares
- `paid_subscription` — paid subscription offers
- `paid_subscribe` — active subscriptions
- `witness_penalty_expire` — witness penalty expirations

**Optional (5)** — metadata and recovery:

- `content_type` — content title/body/metadata
- `account_metadata` — account JSON metadata
- `master_authority_history` — authority change history
- `account_recovery_request` — pending recovery requests
- `change_recovery_account_request` — recovery account change requests

## Example: Full DLT Node Setup

### Step 1: Create a snapshot on a synced node

```bash
# Option A: One-shot (creates and exits)
vizd --create-snapshot /data/snapshots/viz-snapshot-20250101.json --plugin snapshot

# Option B: Already running with periodic snapshots in config.ini — just grab the latest file
ls -t /data/snapshots/snapshot-block-*.json | head -1
```

### Step 2: Transfer the snapshot file to the new node

```bash
scp /data/snapshots/viz-snapshot-20250101.json newnode:/data/snapshots/
```

### Step 3: Start the new node from the snapshot

```bash
vizd \
  --snapshot /data/snapshots/viz-snapshot-20250101.json \
  --plugin snapshot \
  --plugin p2p \
  --p2p-seed-node seed1.viz.world:2001 \
  --shared-file-size 4G
```

The node will load the snapshot state in seconds and begin syncing new blocks from the network.

### Step 4: Subsequent restarts (normal mode)

```bash
# After initial snapshot load, restart without --snapshot
vizd --plugin p2p --p2p-seed-node seed1.viz.world:2001
```

## Recommended Production Config

A full `config.ini` snippet for a production node with automatic periodic snapshots:

```ini
# Enable the snapshot plugin
plugin = snapshot

# Create a snapshot every ~24 hours (28800 blocks * 3 seconds = 86400 sec)
snapshot-every-n-blocks = 28800

# Store snapshots in a dedicated directory
snapshot-dir = /data/viz-snapshots

# Standard chain settings
shared-file-size = 4G
plugin = p2p
p2p-seed-node = seed1.viz.world:2001
```

This configuration ensures your node always has a recent snapshot available for quick recovery or for bootstrapping new nodes.

## Docker Usage

The standard Docker launch command:

```bash
docker run \
  -p 8083:2001 \
  -p 9991:8090 \
  -v ~/vizconfig:/etc/vizd \
  -v ~/vizhome:/var/lib/vizd \
  --name vizd -d vizblockchain/vizd:latest
```

Volumes:
- `~/vizconfig` → `/etc/vizd` — config files (`config.ini`, `seednodes`)
- `~/vizhome` → `/var/lib/vizd` — blockchain data, shared memory, snapshots

The entry point script (`vizd.sh`) supports `VIZD_EXTRA_OPTS` environment variable for passing additional CLI arguments.

### Creating a snapshot (periodic, no downtime)

Add snapshot options to your config file `~/vizconfig/config.ini`:

```ini
plugin = snapshot

# Create a snapshot every ~24 hours (28800 blocks * 3s = ~86400s)
snapshot-every-n-blocks = 28800

# Store snapshots inside the data volume
snapshot-dir = /var/lib/vizd/snapshots
```

Start (or restart) the container:

```bash
docker stop vizd && docker rm vizd

docker run \
  -p 8083:2001 \
  -p 9991:8090 \
  -v ~/vizconfig:/etc/vizd \
  -v ~/vizhome:/var/lib/vizd \
  --name vizd -d vizblockchain/vizd:latest
```

Snapshots will appear on the host at `~/vizhome/snapshots/snapshot-block-*.json`.

### Creating a one-shot snapshot (stop & export)

Stop the running container, then run a temporary one with `--create-snapshot`:

```bash
# Stop the running node
docker stop vizd

# Create a snapshot using VIZD_EXTRA_OPTS
docker run --rm \
  -v ~/vizconfig:/etc/vizd \
  -v ~/vizhome:/var/lib/vizd \
  -e VIZD_EXTRA_OPTS="--create-snapshot /var/lib/vizd/snapshots/viz-snapshot.json --plugin snapshot" \
  vizblockchain/vizd:latest

# The snapshot is now at ~/vizhome/snapshots/viz-snapshot.json on the host

# Restart the node normally
docker start vizd
```

### Creating a snapshot at a specific block (no downtime)

Add to `~/vizconfig/config.ini`:

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /var/lib/vizd/snapshots
```

Restart the container. When block 5,000,000 is applied, the snapshot file will be created at `/var/lib/vizd/snapshots/snapshot-block-5000000.json` (accessible on host at `~/vizhome/snapshots/`).

### Loading from a snapshot (new node bootstrap)

Place the snapshot file on the host:

```bash
# Copy snapshot to the new server
scp viz-snapshot.json newserver:~/vizhome/snapshots/
```

Start the container with `--snapshot` via `VIZD_EXTRA_OPTS`:

```bash
docker run \
  -p 8083:2001 \
  -p 9991:8090 \
  -v ~/vizconfig:/etc/vizd \
  -v ~/vizhome:/var/lib/vizd \
  -e VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/viz-snapshot.json --plugin snapshot" \
  --name vizd -d vizblockchain/vizd:latest
```

The node loads the snapshot state in seconds and begins syncing new blocks from P2P.

**Important:** After the first start with `--snapshot`, restart the container **without** `VIZD_EXTRA_OPTS`:

```bash
docker stop vizd && docker rm vizd

docker run \
  -p 8083:2001 \
  -p 9991:8090 \
  -v ~/vizconfig:/etc/vizd \
  -v ~/vizhome:/var/lib/vizd \
  --name vizd -d vizblockchain/vizd:latest
```

### Managing snapshot disk space (Docker)

Add a cron job on the **host** machine to keep only the 5 most recent snapshots:

```bash
# Add to crontab (runs daily at midnight)
0 0 * * * ls -t ~/vizhome/snapshots/snapshot-block-*.json | tail -n +6 | xargs rm -f
```

### Quick reference

| Task | Command |
|------|---------|
| Start with periodic snapshots | Add `snapshot-every-n-blocks` to `~/vizconfig/config.ini`, restart container |
| One-shot snapshot | `docker run --rm -e VIZD_EXTRA_OPTS="--create-snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...` |
| Load from snapshot | `docker run -e VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...` |
| Find snapshots on host | `ls -lt ~/vizhome/snapshots/` |
| Check snapshot creation logs | `docker logs vizd \| grep -i snapshot` |

## Modified Components

The snapshot plugin required changes to several core components:

| Component | Change |
|-----------|--------|
| `chainbase::generic_index` | Added `set_next_id()` / `next_id()` for ID preservation during import |
| `database.hpp/cpp` | Added `open_from_snapshot()`, `initialize_hardforks()`, `get_fork_db()` |
| `chain plugin` | Snapshot-aware startup flow — detects `--snapshot` and uses `open_from_snapshot()` |
| `content_object.hpp` | Added missing `FC_REFLECT` for `content_object`, `content_type_object`, `content_vote_object` |
| `witness_objects.hpp` | Added missing `FC_REFLECT` for `witness_vote_object` |
| `vizd/main.cpp` | Registered `snapshot_plugin`, linked `graphene::snapshot` |
