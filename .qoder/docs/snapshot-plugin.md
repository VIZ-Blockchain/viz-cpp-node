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
| `--snapshot <path>` | `string` | Load state from a snapshot file instead of replaying blockchain. The node opens in DLT mode (no block log). Safe for restarts — skips import if shared_memory already exists, and renames the file to `.used` after successful import. |
| `--snapshot-auto-latest` | `bool` (default: `false`) | Auto-discover the latest snapshot file in `snapshot-dir` by parsing block numbers from filenames (`snapshot-block-NNNNN.vizjson` or `.json`). Typically used with `--replay-from-snapshot` to avoid specifying the file path manually. Ignored if `--snapshot` is already specified. |
| `--replay-from-snapshot` | `bool` (default: `false`) | Crash recovery mode: import a snapshot and then replay blocks from `dlt_block_log` to bring the node up to the latest available state. Unlike `--snapshot`, this always wipes shared memory (assumes corruption), does NOT rename the snapshot to `.used`, and replays subsequent blocks from the DLT rolling block log. Requires `--snapshot <path>` or `--snapshot-auto-latest`. |
| `--create-snapshot <path>` | `string` | Create a snapshot file at the given path using the current database state, then exit. |
| `--sync-snapshot-from-trusted-peer` | `bool` (default: `false`) | Download and load snapshot from trusted peers when state is empty (`head_block_num == 0`). Requires `trusted-snapshot-peer` to be configured. Defaults to `false` (opt-in) — must be explicitly enabled to prevent accidental state wipe via `chainbase::wipe()`. |

### Config file options (snapshot plugin)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `snapshot-at-block` | `uint32_t` | `0` | Create snapshot when the specified block number is reached (while the node is running). |
| `snapshot-every-n-blocks` | `uint32_t` | `0` | Automatically create a snapshot every N blocks (0 = disabled). |
| `snapshot-dir` | `string` | `""` | Directory for auto-generated snapshot files. Used by `snapshot-at-block` and `snapshot-every-n-blocks`. |
| `snapshot-max-age-days` | `uint32_t` | `90` | Delete snapshots older than N days after creating a new one (0 = disabled). Built-in rotation replaces external cron jobs. |
| `allow-snapshot-serving` | `bool` | `false` | Enable serving snapshots over TCP to other nodes. |
| `allow-snapshot-serving-only-trusted` | `bool` | `false` | Restrict snapshot serving to trusted peers only (from `trusted-snapshot-peer` list). |
| `snapshot-serve-endpoint` | `string` | `0.0.0.0:8092` | TCP endpoint for the snapshot serving listener. |
| `trusted-snapshot-peer` | `string` (multi) | — | Trusted peer endpoint for snapshot sync (`IP:port`). Can be specified multiple times. |

### Config file options (chain plugin)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `dlt-block-log-max-blocks` | `uint32_t` | `100000` | Number of recent blocks to keep in the DLT rolling block log (0 = disabled). Only active in DLT mode (after snapshot import). |

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

Stop the node, then restart it with the `--create-snapshot` flag. The node will open the existing database (block log + shared memory), replay if needed to bring the state up to date, create the snapshot, and exit — **before** P2P or witness plugins activate:

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

### What happens during `--create-snapshot`

1. All plugins call `plugin_initialize()`. The **snapshot plugin** registers a `snapshot_create_callback` on the **chain plugin**.
2. The **chain plugin** `plugin_startup()` opens the database normally — block log, shared memory, and replays from block log if the chainbase revision doesn't match the head block.
3. After the database is fully loaded, the chain plugin calls `snapshot_create_callback()` — the **snapshot plugin** serializes all 32 tracked object types as JSON arrays with a SHA-256 checksum, writes the file, and calls `app().quit()`.
4. The chain plugin **never calls `on_sync()`** — P2P and witness plugins never activate.

All snapshot creation happens **inside** `chain::plugin_startup()`. The database is fully consistent (post-replay) and no new blocks arrive during serialization.

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

When block 5,000,000 is applied, the snapshot is created at `/data/snapshots/snapshot-block-5000000.json` without stopping the node. Snapshot creation runs asynchronously on a dedicated background thread — only the database read phase (serialization) holds a read lock, so block processing is only briefly paused and API/P2P reads are never blocked.

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
- **Only triggers on live blocks**: Periodic snapshots are skipped while the node is syncing from P2P (block time >60s behind wall clock). This prevents wasteful snapshot creation during initial sync from genesis or from an older snapshot. Snapshots begin only after the node catches up to the live chain head.
- **Auto-creates directory**: The snapshot directory (`snapshot-dir`) is automatically created if it doesn't exist. No need to `mkdir` before starting the node.
- **Non-blocking snapshot creation**: Snapshot creation runs asynchronously on a dedicated `fc::thread`. The `on_applied_block` callback returns immediately after scheduling the snapshot — the write lock is released and block processing resumes. The snapshot itself is split into two phases:
  - **Phase 1 (read lock, ~1 second)**: Serializes all database state into local variables. During this phase, block processing waits (read lock conflicts with write lock), but API/P2P reads proceed concurrently.
  - **Phase 2 (no lock, ~2 seconds)**: Compression, checksum computation, and file I/O. Block processing and API reads run normally during this phase.
- This prevents the read-lock timeouts and `unlinkable_block_exception` errors that previously occurred when snapshot creation ran synchronously inside the write-lock scope (which blocked all database access for 3+ seconds).
- For large chains, consider using a longer interval (e.g., `100000` blocks) to minimize the impact on block processing.
- **Built-in rotation**: Old snapshot files are automatically deleted if older than `snapshot-max-age-days` (default 90 days, 0 = disabled). Rotation runs after each new snapshot is created.
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

Snapshot rotation is built-in. By default, snapshots older than 90 days are automatically deleted after each new snapshot is created.

To configure or disable rotation, set `snapshot-max-age-days` in `config.ini`:

```ini
# Delete snapshots older than 30 days (default: 90)
snapshot-max-age-days = 30

# Or disable rotation entirely
# snapshot-max-age-days = 0
```

Alternatively, you can use an external cron job for finer control:

```bash
# Keep only the 5 most recent snapshots
ls -t /data/snapshots/snapshot-block-*.json | tail -n +6 | xargs rm -f
```

## Loading from a Snapshot (DLT Mode)

To start a node from a snapshot file:

```bash
vizd --snapshot /path/to/snapshot.json --plugin snapshot
```

### What happens during snapshot loading

1. All plugins call `plugin_initialize()`. The **snapshot plugin** registers a `snapshot_load_callback` on the **chain plugin**.
2. The **chain plugin** `plugin_startup()` detects the `--snapshot` option and checks three conditions in order:
   - **shared_memory.bin already exists** → skips import, falls through to normal startup (prevents re-importing on container restart).
   - **Snapshot file not found** (e.g., already renamed to `.used`) → skips import, falls through to normal startup.
   - **Both checks pass** → proceeds with snapshot import.
3. The chain plugin opens the database using `open_from_snapshot()` — this wipes shared memory, initializes chainbase, indexes, and evaluators.
4. The chain plugin calls `snapshot_load_callback()` — the **snapshot plugin** reads the JSON file, validates the header (format version, chain ID, SHA-256 checksum), imports all 32 tracked object types into the database under a strong write lock, and calls `initialize_hardforks()` to populate the hardfork schedule.
5. **The snapshot file is renamed to `.used`** (e.g., `snapshot.json` → `snapshot.json.used`) to prevent re-import on restart.
6. LIB (Last Irreversible Block) is promoted to `head_block_num` so P2P's blockchain synopsis starts from the snapshot's head — peers will only offer blocks after the snapshot point.
7. The fork database is seeded with the head block from the snapshot.
8. The chain plugin emits `on_sync` so other plugins (webserver, APIs, etc.) know the node is ready.
9. **P2P plugin** starts — sees the snapshot's head block and begins syncing from **LIB + 1** via the P2P network.

All snapshot loading happens **inside** `chain::plugin_startup()`, before any other plugin starts. P2P and witness never see incomplete/genesis state.

### Restart safety

The node is safe to restart with `--snapshot` still on the command line (e.g., via `VIZD_EXTRA_OPTS` in Docker). Three layers of protection prevent accidental re-import:

| Restart scenario | What happens |
|---|---|
| **1st start** (no shared_memory, file exists) | Imports snapshot, renames file to `.used` |
| **Restart** (shared_memory exists) | Skips import: "Shared memory already exists" |
| **Restart** (shared_memory wiped, file already `.used`) | Skips import: "Snapshot file not found" |
| **Force re-import** | `--resync-blockchain` wipes shared_memory + provide a fresh snapshot file |

### Important notes

- **No block log (DLT mode)**: When loaded from a snapshot, the node runs in DLT mode — the main `block_log` remains empty. A separate **DLT rolling block log** stores recent blocks (see below).
- **Automatic DLT mode detection on restart**: After the initial snapshot load, the node detects DLT mode automatically (block_log empty, chainbase has state), skips block_log validation, and continues syncing from P2P.
- **Chain ID validation**: The snapshot's chain ID must match the node's compiled chain ID. Mismatches are rejected.
- **Checksum verification**: The payload checksum is verified before any objects are imported.
- **Restart with `--snapshot` is safe**: See "Restart safety" above — no need to remove the flag after initial import.

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

### Step 4: Subsequent restarts

The node is safe to restart even with `--snapshot` still on the command line. It detects existing shared_memory and skips re-import automatically:

```bash
# Just restart — no need to remove --snapshot flag
vizd --plugin p2p --p2p-seed-node seed1.viz.world:2001
```

## DLT Rolling Block Log

When a node runs in DLT mode (loaded from snapshot), the main `block_log` is empty. However, a separate **DLT rolling block log** (`dlt_block_log`) stores the most recent irreversible blocks. This enables:

- **P2P block serving**: Peers can request recent blocks from this node (for fork resolution and initial sync catch-up).
- **Local block queries**: API calls like `get_block` work for recent blocks.

### Configuration

```ini
# Keep the last 100,000 blocks in the DLT block log (default)
dlt-block-log-max-blocks = 100000

# Or disable the DLT block log entirely
# dlt-block-log-max-blocks = 0
```

### How it works

- The DLT block log is stored in two files: `dlt_block_log.log` and `dlt_block_log.index` in the blockchain data directory.
- The index uses an **offset-aware format**: an 8-byte header stores the start block number, followed by 8-byte position entries for each block.
- When the log exceeds `dlt-block-log-max-blocks`, old blocks are truncated from the front (rolling window).
- On restart, the DLT block log is preserved — blocks are only re-written from where they left off.
- When the DLT block log is empty (fresh snapshot import), the node skips ahead to the last irreversible block number, since snapshot state is already trusted.
- If a block is not found in the fork database during DLT block log writes (normal after restart), the gap is logged via `wlog` and fills naturally as LIB advances past the post-restart head.

### P2P block serving path

When a peer requests a block:
1. `p2p_plugin::get_item()` → `database::fetch_block_by_id()`
2. First checks main `_block_log` (empty in DLT mode)
3. Falls back to `_dlt_block_log` for recent blocks
4. If not found in either → block unavailable

**Note on `is_known_block()`:** In DLT mode, the `block_summary` table (TAPOS buffer, 65536 entries) survives snapshot import but may reference blocks not actually available on disk. The DLT mode implementation checks `block_summary` as a hint, then verifies the block is on the **preferred chain** via `find_block_id_for_num()`. This two-step check:
- Returns `true` for blocks on our chain (enabling P2P's `has_item()` to work correctly during sync negotiation)
- Returns `false` for blocks not on our chain or blocks where `block_summary` has stale fork entries (preventing the node from lying to P2P peers about being able to serve the block data)
- Falls through to `fetch_block_by_id()` for blocks outside the `block_summary` range

### P2P sync reliability (DLT mode)

After snapshot import, the node must sync all subsequent blocks from P2P. Several fixes ensure this works:

**LIB promotion:** After snapshot import, LIB is set to `head_block_num` so P2P's blockchain synopsis starts from the snapshot's head. Peers will only offer blocks after the snapshot point, which can link correctly in the fork database.

**Fork database seeding:**
- Fresh snapshot import: `fork_db` is seeded with the head block via `start_block()`. The head block is also appended to `dlt_block_log` so that restart can reconstruct `fork_db` from it.
- DLT mode restart: `fork_db` is in-memory and lost on restart. The node seeds it from `dlt_block_log` if it covers the head block (guaranteed after the fix above). If `dlt_block_log` somehow does not cover the head, the early rejection logic in `_push_block` handles the empty `fork_db` case by always allowing blocks whose `previous == head_block_id()`

**Block ID advertisement clamping (`get_block_ids`):** After snapshot import, the `block_summary` (TAPOS buffer, 65536 entries) contains block IDs for blocks the node *knows about* but cannot serve (the actual block data only exists in `dlt_block_log` which starts at the snapshot head). Without clamping, `get_block_ids()` would advertise these un-serveable blocks to peers, causing them to request the blocks, receive `item_not_available`, and disconnect with "You are missing a sync item you claim to have, your database is probably corrupted."

Fix: `database::earliest_available_block_num()` returns the lowest block the node can actually serve (from `dlt_block_log`, `block_log`, or `fork_db`). In DLT mode after snapshot import, this is typically the snapshot head block. `get_block_ids()` in `p2p_plugin.cpp` clamps its start to `earliest_available_block_num()`, ensuring the node only advertises blocks it can deliver.

**Graceful `item_not_available` handling:** When a peer sends `item_not_available` for a sync block, the node no longer disconnects with a "corrupted database" message. Instead, it sets `inhibit_fetching_sync_blocks = true` on that peer and tries other peers. This allows DLT nodes with limited block history to participate in the network without being aggressively disconnected.

**Broadcast inventory suppression during sync:** During initial sync (catching up from snapshot), peers send both sync data (block IDs for catch-up) and broadcast items (recent transactions/blocks at chain tip). If the node tries to fetch these broadcast items, the 1-second `active_ignored_request_timeout` in `terminate_inactive_connections()` fires before the items arrive, disconnecting peers and killing sync connections.

The node suppresses broadcast inventory (`on_item_ids_inventory_message`) with a 3-layer defense:
1. **Per-peer sync check:** Skip if the originating peer has `we_need_sync_items_from_peer = true`
2. **Global sync check:** Skip if *any* active peer has `we_need_sync_items_from_peer = true` — prevents inventory from non-syncing peers from polluting `items_requested_from_peer`
3. **Head block time check:** Skip if the node's head block is >30 seconds behind wall clock — catches the brief window after all peers respond "up to date" but the node is still behind (e.g., when the peer was at the same block and set `we_need_sync_items_from_peer = false`)

Broadcast items are useless during sync — the node will receive them naturally once caught up.

**Early block rejection in `_push_block`:** When a node is far behind, it receives sync blocks (sequential, must accept) and broadcast blocks (real-time, potentially thousands ahead, must reject silently). Checks prevent sync disruption:
1. Duplicate blocks at/before head with matching ID → skipped silently
2. Blocks at/before head on a different fork with parent not in fork_db → silently rejected (prevents infinite P2P sync restart loop where each failure triggers another sync attempt with the same peer)
3. Far-ahead blocks whose parent is neither `head_block_id()` nor in `fork_db` → returned false silently (no `unlinkable_block_exception` thrown, preventing P2P sync restart)
4. Blocks with `previous == head_block_id()` → always allowed (critical for the first sync block to be accepted)

**Fork database bug fixes:** Several bugs in `fork_database` were fixed:
- `_unlinked_index.insert()` was dead code (after `throw`) — moved before the throw
- `_push_next()` was never called after inserting a new block — added the call to resolve previously-unlinkable blocks
- Duplicate block check added in `_push_block`
- `_unlinked_index.clear()` added to `reset()`

## Crash Recovery: `--replay-from-snapshot`

When `shared_memory.bin` becomes corrupted (e.g., after an unclean shutdown, disk full, or hardware fault), the node cannot start normally. The standard `--replay-blockchain` replays from `block_log`, which is empty in DLT mode. The `--replay-from-snapshot` option provides a recovery path that combines snapshot import with DLT block log replay.

### The problem

| Scenario | Why it fails |
|----------|-------------|
| Normal startup | `shared_memory.bin` has corrupted indices → FC_ASSERT crash |
| `--replay-blockchain` | Reads from `block_log`, which is empty in DLT mode |
| `--snapshot <path>` alone | Imports snapshot state but does not replay subsequent blocks from `dlt_block_log` |

### The solution

`--replay-from-snapshot` performs a three-step recovery:

1. **Wipe & import snapshot** — Always wipes `shared_memory.bin` (assumes corruption), opens the database from genesis via `open_from_snapshot()`, and imports the snapshot state.
2. **Replay dlt_block_log** — If the `dlt_block_log` contains blocks beyond the snapshot's head, they are replayed via `database::reindex_from_dlt()` to bring the node as close to the chain tip as possible.
3. **Resume P2P sync** — The node emits `on_sync` and begins normal P2P sync from the replayed head block onward.

Unlike `--snapshot`, the recovery mode:
- Always wipes shared memory (no "already exists" check)
- Does **not** rename the snapshot file to `.used` (the snapshot may be needed again)
- Initializes hardforks after snapshot import (before replay)
- Replays blocks from `dlt_block_log` with standard reindex skip flags

### Usage

```bash
# Simple: specify the snapshot path explicitly
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.vizjson --plugin snapshot

# Convenient: auto-discover the latest snapshot
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

With `--snapshot-auto-latest`, the node scans `snapshot-dir` for files matching `snapshot-block-*.vizjson` or `snapshot-block-*.json`, parses the block number from each filename, and selects the one with the highest block number. This avoids having to manually find and specify the snapshot path.

### What happens during recovery

1. All plugins call `plugin_initialize()`. The **snapshot plugin** registers a `snapshot_load_callback` on the **chain plugin**.
2. The **chain plugin** `plugin_startup()` detects `--replay-from-snapshot` and validates that a snapshot path is available (via `--snapshot` or `--snapshot-auto-latest`).
3. The chain plugin opens the database using `open_from_snapshot()` — this wipes shared memory, initializes chainbase, indexes, and evaluators in DLT mode.
4. The chain plugin calls `snapshot_load_callback()` — the **snapshot plugin** reads the JSON file, validates the header, imports all tracked object types, and calls `initialize_hardforks()`.
5. The chain plugin calls `initialize_hardforks()` to populate the hardfork schedule arrays.
6. The chain plugin checks if `dlt_block_log` has blocks beyond the snapshot head. If yes, it calls `database::reindex_from_dlt(snapshot_head + 1)`.
7. `reindex_from_dlt()` replays each block from the DLT rolling block log with reindex skip flags (no signature checks, no merkle checks, etc.), reporting progress to stderr every 10%.
8. After replay, the fork database is seeded with the last block from `dlt_block_log`.
9. The chain plugin emits `on_sync` — P2P starts syncing from the replayed head block.

### Example recovery scenario

A DLT-mode node with periodic snapshots every 100,000 blocks and `dlt-block-log-max-blocks = 100000` crashes at block 79,274,318 with corrupted shared memory:

```
/data/viz-snapshots/
  snapshot-block-79273800.vizjson    ← latest snapshot (518 blocks behind crash point)

/blockchain/
  dlt_block_log.log                  ← contains blocks 79174319..79274318
  dlt_block_log.index
  shared_memory.bin                  ← CORRUPTED
```

Recovery command:
```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

Recovery log output:
```
RECOVERY MODE: replaying from snapshot + dlt_block_log...
Opening database for snapshot import. Please wait...
Database opened for snapshot import (DLT mode), elapsed time 2.1 sec
Loading state from snapshot: /data/viz-snapshots/snapshot-block-79273800.vizjson
Snapshot loaded successfully at block 79273800, elapsed time 12.3 sec
Snapshot loaded at block 79273800. Initializing hardforks...
Replaying dlt_block_log from block 79273801 to 79274318...
   10%   52 of 518   (block 79273852, 3840M free, elapsed 0.8 sec)
   20%   104 of 518   (block 79273904, 3839M free, elapsed 1.5 sec)
   ...
   100%   518 of 518   (block 79274318, 3830M free, elapsed 7.2 sec)
Done replaying from dlt_block_log, head_block=79274318, elapsed time: 7.3 sec
Recovery complete. Started on blockchain with 79274318 blocks
```

The node is now at block 79,274,318 and P2P sync fills the gap to the live chain head.

### Key differences from `--snapshot`

| Aspect | `--snapshot` | `--replay-from-snapshot` |
|--------|-------------|--------------------------|
| Purpose | Bootstrap a new node | Recover from corruption |
| Shared memory check | Skips if already exists | Always wipes and re-imports |
| Snapshot file rename | Renames to `.used` | Does NOT rename |
| DLT block log replay | No | Yes, from snapshot_head+1 |
| Hardfork initialization | In callback | In callback + explicit call |
| Typical use case | First-time node setup | Crash recovery |

## P2P Snapshot Sync

Nodes can download snapshots directly from trusted peers over a custom TCP protocol, enabling fully automated bootstrap without manual file transfers.

### Server (snapshot provider)

Add to `config.ini`:

```ini
plugin = snapshot

# Enable TCP snapshot serving
allow-snapshot-serving = true

# TCP endpoint for the snapshot server
snapshot-serve-endpoint = 0.0.0.0:8092

# Optional: restrict to trusted peers only
# allow-snapshot-serving-only-trusted = true
# trusted-snapshot-peer = 1.2.3.4:8092

# Must have snapshots to serve
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
```

### Client (new node bootstrap)

Start a new node that automatically downloads and loads a snapshot from trusted peers:

```bash
vizd \
  --plugin snapshot \
  --plugin p2p
```

With `config.ini`:

```ini
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
```

Since `sync-snapshot-from-trusted-peer` defaults to `false`, you must explicitly enable it in `config.ini` along with `trusted-snapshot-peer`:

```ini
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

When the node starts with 0 blocks and sync is enabled, it automatically downloads the snapshot from the best available peer. To disable auto-sync (the default):

```ini
sync-snapshot-from-trusted-peer = false
```

If the node has 0 blocks and no `trusted-snapshot-peer` is configured, a console warning is shown advising the user to configure one.

### How P2P sync works

1. **Query phase**: The node connects to each trusted peer (5-second timeout per operation: connect, send, read), sends a `snapshot_info_request`, and collects metadata (block number, checksum, compressed size). Progress is logged to console.
2. **Selection**: Picks the peer with the highest block number.
3. **Download phase**: Downloads the snapshot in 1 MB chunks, writing to a temp file. Download progress is printed to console every 5% (size in MB and percentage).
4. **Verification**: Streams the downloaded file through SHA-256 to verify checksum (without loading into memory).
5. **Import**: Clears database state, loads the verified snapshot, initializes hardforks. Each stage (decompress, parse, validate, import) is logged to console with timing.

All operations happen during `chain::plugin_startup()`, **before** P2P and witness plugins activate. The node is fully blocked during download and import — no blocks are processed until the snapshot is loaded.

### Security features

- **Max snapshot size**: Downloads exceeding 2 GB are rejected.
- **Streaming checksum**: SHA-256 verification uses streaming (1 MB chunks) to avoid loading the entire file into memory.
- **Trusted peer list**: Connections are only accepted from/to configured trusted peers.
- **Anti-spam**: Rate limiting (max connections per hour per IP), max 5 concurrent connections (each in a separate fiber with mutex-protected session tracking), 60-second enforced connection deadline (checked before each I/O operation, not just post-hoc).
- **Payload limits**: Control messages limited to 64 KB, only data replies allow up to 64 MB.
- **Dedicated server thread**: The TCP server runs all fibers (accept loop, watchdog, connection handlers) on a dedicated `fc::thread`, ensuring they are not blocked by the main thread's `io_serv->run()` loop.

## Recommended Production Config

A full `config.ini` snippet for a production node with automatic periodic snapshots:

```ini
# Enable the snapshot plugin
plugin = snapshot

# Create a snapshot every ~24 hours (28800 blocks * 3 seconds = 86400 sec)
snapshot-every-n-blocks = 28800

# Store snapshots in a dedicated directory
snapshot-dir = /data/viz-snapshots

# Auto-delete snapshots older than 90 days (default)
snapshot-max-age-days = 90

# DLT rolling block log: keep last 100k blocks (default)
dlt-block-log-max-blocks = 100000

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

**Restart safety:** The node is safe to restart with `VIZD_EXTRA_OPTS` still set. On restart:
1. If shared_memory already exists → skips import, uses existing state.
2. If the snapshot file was renamed to `.used` → skips import.
3. No need to remove `VIZD_EXTRA_OPTS` after the first run.

### Managing snapshot disk space (Docker)

Snapshot rotation is built-in (default: delete files older than 90 days). Add `snapshot-max-age-days` to `~/vizconfig/config.ini` to customize:

```ini
# Delete snapshots older than 30 days
snapshot-max-age-days = 30
```

Alternatively, add a cron job on the **host** machine:

```bash
# Keep only the 5 most recent snapshots
0 0 * * * ls -t ~/vizhome/snapshots/snapshot-block-*.json | tail -n +6 | xargs rm -f
```

### Quick reference

| Task | Command |
|------|---------|
| Start with periodic snapshots | Add `snapshot-every-n-blocks` to `~/vizconfig/config.ini`, restart container |
| One-shot snapshot | `docker run --rm -e VIZD_EXTRA_OPTS="--create-snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...` |
| Load from snapshot | `docker run -e VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...` |
| Crash recovery | `docker run -e VIZD_EXTRA_OPTS="--replay-from-snapshot --snapshot-auto-latest --plugin snapshot" ...` |
| P2P auto-bootstrap | Add `trusted-snapshot-peer = <ip>:<port>` to config, start container with `--plugin snapshot` |
| Find snapshots on host | `ls -lt ~/vizhome/snapshots/` |
| Check snapshot creation logs | `docker logs vizd \| grep -i snapshot` |
| Force re-import | `docker run -e VIZD_EXTRA_OPTS="--resync-blockchain --snapshot /path/snap.json --plugin snapshot" ...` |

## Modified Components

The snapshot plugin required changes to several core components:

| Component | Change |
|-----------|--------|
| `chainbase::generic_index` | Added `set_next_id()` / `next_id()` for ID preservation during import |
| `fork_database.cpp` | Fixed `_unlinked_index.insert()` dead code (moved before `throw`); added `_push_next(item)` call at end of `_push_block` to resolve previously-unlinkable blocks; added duplicate block check in `_push_block`; added `_unlinked_index.clear()` to `reset()` |
| `database.hpp/cpp` | Added `open_from_snapshot()`, `initialize_hardforks()`, `reindex_from_dlt()`, `get_fork_db()`, `_dlt_mode` flag with `set_dlt_mode()` setter; DLT mode skips block_log writes and detects empty block_log on restart; `is_known_block()` in DLT mode checks block_summary then verifies preferred chain via `find_block_id_for_num()` (prevents both lying to peers and breaking sync negotiation); DLT restart seeds fork_db from dlt_block_log when available; early rejection in `_push_block`: duplicate blocks (at/before head, same ID), blocks at/before head on different fork with unknown parent (prevents P2P sync restart loop), far-ahead broadcast blocks (parent unknown and not head_block_id), with immediate successor (`previous == head_block_id()`) always allowed; DLT block log gap logging via `wlog`; DLT block log empty-skip logic for fresh snapshot imports |
| `dlt_block_log.hpp/cpp` | New class: offset-aware rolling block log with 8-byte header index, `truncate_before()` for rotation, read/write with mutex locking |
| `chain plugin` | Added `snapshot_load_callback`, `snapshot_create_callback`, `snapshot_p2p_sync_callback`; restart safety (shared_memory check + `.used` rename + file existence check); LIB promotion after snapshot import (LIB = head_block_num for correct P2P synopsis); `dlt-block-log-max-blocks` config option; diagnostic warning when node has 0 blocks and no snapshot sync configured; `--replay-from-snapshot` crash recovery mode (wipes shared_memory, imports snapshot, replays dlt_block_log via `reindex_from_dlt`, does not rename snapshot); `--snapshot-auto-latest` auto-discovery of latest snapshot in `snapshot-dir` |
| `snapshot plugin` | Full implementation: create/load/periodic snapshots; P2P TCP sync (serve + download with concurrent fiber handling on a dedicated `fc::thread`); 5-second peer operation timeout (connect, send, read); anti-spam (rate limiting, mutex-protected session tracking, enforced per-operation connection deadline); cached snapshot info; streaming SHA-256 checksum; built-in rotation (`snapshot-max-age-days`); elapsed time logging for all operations; console progress logging for download/import; `sync-snapshot-from-trusted-peer` defaults to `false` (opt-in); auto-creates snapshot directory; periodic snapshots only trigger on live blocks (skipped during P2P sync); memory optimization: compressed data freed immediately after decompression; `--snapshot-auto-latest` auto-discovery of latest snapshot file by block number; `find_latest_snapshot()` helper; **async snapshot creation**: `on_applied_block` schedules snapshot on a dedicated `fc::thread` (not the main thread's fc scheduler, which is blocked by `io_serv->run()`); `create_snapshot` splits into Phase 1 (read lock for DB serialization only) and Phase 2 (compression + file I/O without lock); `snapshot_in_progress` atomic flag prevents overlapping snapshots; `write_snapshot_to_file` accepts pre-captured header+state (no DB access needed); shutdown waits for in-progress snapshot before quitting thread; **DLT restart safety**: `load_snapshot()` persists the snapshot's head block into `dlt_block_log` so `database::open()` can seed `fork_db` on restart |
| `content_object.hpp` | Added missing `FC_REFLECT` for `content_object`, `content_type_object`, `content_vote_object` |
| `witness_objects.hpp` | Added missing `FC_REFLECT` for `witness_vote_object` |
| `proposal_object.hpp` | Added missing `FC_REFLECT` for `required_approval_object` |
| `vizd/main.cpp` | Registered `snapshot_plugin`, linked `graphene::snapshot` |
| `p2p_plugin` | Added `_dlt_block_log` fallback in `get_item()` for serving blocks to peers in DLT mode |
