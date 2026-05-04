# Snapshot Plugin

The snapshot plugin provides DLT (Distributed Ledger Technology) state snapshots for VIZ blockchain nodes. It supports creating, loading, and syncing snapshots between nodes via a dedicated TCP protocol.

## Basic Usage

### Creating a Snapshot

Create a snapshot from a fully-synced node:

```bash
vizd --create-snapshot /path/to/snapshot-block-500000.vizjson
```

The node will load the blockchain, create a zlib-compressed snapshot file, and exit.

### Loading from a Snapshot

Start a node from a snapshot instead of replaying the entire blockchain:

```bash
vizd --snapshot /path/to/snapshot-block-500000.vizjson
```

### Periodic Snapshots

Automatically create snapshots at regular intervals:

```ini
# In config.ini
plugin = snapshot
snapshot-every-n-blocks = 100000
snapshot-dir = /var/lib/vizd/snapshots
```

**Witness-Aware Deferral:** When the node is also a block-producing witness, periodic snapshot creation is automatically deferred if the witness is scheduled to produce within the next 4 slots (~12 seconds). This covers the typical snapshot creation time (~10 seconds) plus a safety margin, preventing snapshot serialization from causing the witness to miss its production slot. The snapshot is created once the witness is no longer scheduled in the near-term slots.

**Non-Blocking Snapshot Creation:** Snapshot creation runs asynchronously on a dedicated background thread. Only the database read phase (serialization of state) holds a read lock (~1 second); compression and file I/O run without any lock. This means block processing is only briefly paused during the read phase, and API/P2P reads are never blocked — eliminating the read-lock timeouts and `unlinkable_block_exception` errors that occurred when snapshot creation ran synchronously inside the write-lock scope.

### Snapshot at Specific Block

```ini
snapshot-at-block = 500000
snapshot-dir = /var/lib/vizd/snapshots
```

## P2P Snapshot Sync

Nodes can serve and download snapshots from each other via dedicated TCP connections (separate from the Graphene P2P layer).

### Serving Snapshots

Enable snapshot serving on a node that creates periodic snapshots:

```ini
plugin = snapshot

# Enable serving (anyone can download)
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-dir = /var/lib/vizd/snapshots
snapshot-every-n-blocks = 100000

# Optional: restrict to trusted peers only
# allow-snapshot-serving-only-trusted = true
# trusted-snapshot-peer = 1.2.3.4:8092
# trusted-snapshot-peer = 5.6.7.8:8092
```

### Downloading Snapshots (New Node Bootstrap)

Start a fresh node that automatically downloads a snapshot from trusted peers:

```bash
vizd --sync-snapshot-from-trusted-peer true
```

With the following in `config.ini`:

```ini
plugin = snapshot
trusted-snapshot-peer = seed1.example.com:8092
trusted-snapshot-peer = seed2.example.com:8092
snapshot-dir = /var/lib/vizd/snapshots
sync-snapshot-from-trusted-peer = true
```

**Note:** `sync-snapshot-from-trusted-peer` can be set in both `config.ini` and as a CLI option.

The node will:
1. Query all trusted peers for their latest snapshot (30-second timeout per peer)
2. Select the peer with the highest block number
3. Wait briefly for the server to release the query session, then connect for download (up to 3 retries with 2-second delays if the server's anti-spam check hasn't cleared the previous session yet)
4. Download the snapshot in 1 MB chunks with progress logging (5-minute timeout per chunk)
5. Verify the checksum
6. Load the snapshot and start syncing from that block

If no trusted peers respond, the node will **retry automatically** every `stalled-sync-timeout-minutes` minutes (default: 5) until a snapshot becomes available. The node will not fall back to genesis sync.

**Note on Timeouts:** Peer queries use a 30-second timeout. Chunk downloads use a 5-minute timeout to support slow connections (minimum ~3.4 KB/s). If a peer doesn't respond within the timeout (e.g., accepts TCP connection but never sends data), the node will skip that peer and try the next one.

### Trust Model

- **Public serving** (`allow-snapshot-serving = true`, `allow-snapshot-serving-only-trusted = false`): Any IP can connect and download snapshots. Good for public seed nodes.
- **Trusted-only serving** (`allow-snapshot-serving-only-trusted = true`): Only IPs from `trusted-snapshot-peer` list can download. Good for private networks.
- **Client side**: Only connects to endpoints in `trusted-snapshot-peer` (inherent trust).

### Anti-Spam Protection

The snapshot TCP server has built-in anti-spam measures:

- **Max 5 concurrent connections**: The server accepts up to 5 simultaneous connections, each handled in a separate fiber (via `fc::async`). Additional connections are rejected.
- **1 active session per IP**: If an IP already has an active download in progress, additional connections from the same IP are rejected. Sessions are eagerly released via RAII when the connection handler exits, minimizing the window where a legitimate reconnection (e.g., query → download) could be falsely rejected.
- **Rate limiting (3 connections/hour per IP)**: Each IP is limited to 3 connections per hour. Exceeding this triggers a rejection with a warning log. This prevents abuse where a client repeatedly connects to waste server bandwidth.
- **Enforced connection timeout (60s)**: Each connection has a hard deadline. The timeout is checked before each I/O operation (initial request read, each chunk transfer). Slow or stalled clients are disconnected when the deadline expires, freeing the session slot.
- **Trusted peers bypass nothing**: Anti-spam rules apply equally to all connections (trusted and untrusted). Trust enforcement (`allow-snapshot-serving-only-trusted`) is checked first; anti-spam is checked after.

### Protocol

The snapshot sync uses a binary request-response protocol over TCP:

```
Wire format: [4 bytes: payload_size][4 bytes: msg_type][payload_bytes]
```

Message types:
| Type | Name | Description |
|------|------|-------------|
| 1 | `SNAPSHOT_INFO_REQUEST` | Empty payload. "What's your latest snapshot?" |
| 2 | `SNAPSHOT_INFO_REPLY` | `{block_num, block_id, checksum, compressed_size}` |
| 3 | `SNAPSHOT_DATA_REQUEST` | `{block_num, offset, chunk_size}` |
| 4 | `SNAPSHOT_DATA_REPLY` | `{offset, data_bytes, is_last}` |
| 5 | `SNAPSHOT_NOT_AVAILABLE` | No snapshot available |

## DLT Rolling Block Log

In DLT mode (snapshot-based nodes), the main `block_log` is empty because it requires contiguous blocks from genesis. To allow DLT nodes to serve recent blocks to P2P peers, a **separate** `dlt_block_log` file with an offset-aware index is used.

The `dlt_block_log` stores blocks in the same binary format as the regular block_log, but its index file has an 8-byte header storing the first block number. This allows starting from any block number (e.g., 10,000,000) without needing entries for blocks 1 through 9,999,999.

```ini
# Keep last 100,000 blocks in dlt_block_log (default)
dlt-block-log-max-blocks = 100000
```

**Behavior:**
- When `> 0` and in DLT mode: irreversible blocks are written to `dlt_block_log`, and old blocks are truncated when the window exceeds 2x the limit (amortized cost).
- When `= 0`: no DLT block_log writes (original behavior).
- Ignored for normal (non-DLT) nodes that always write the full `block_log`.
- The P2P layer automatically falls back to `dlt_block_log` when serving blocks not found in the main `block_log`.

Files stored in the blockchain data directory:
- `dlt_block_log` -- block data (same format as `block_log`)
- `dlt_block_log.index` -- offset-aware index

## P2P Sync After Snapshot Import (DLT Mode)

When a node starts from a snapshot, it loads state at the snapshot's block height but has no block history. P2P sync must then fetch all subsequent blocks from the network. Several fixes ensure this works reliably:

### LIB Promotion

After snapshot import, LIB (Last Irreversible Block) is promoted to equal the head block number. This ensures that P2P's blockchain synopsis starts from the snapshot's head, causing peers to only offer blocks after the snapshot point (which can link correctly in the fork database).

### Fork Database Seeding

- **Fresh snapshot import**: The fork database (`fork_db`) is seeded with the head block so incoming blocks can link to it.
- **DLT mode restart**: On restart, `fork_db` is in-memory and empty. The node tries to seed it from the `dlt_block_log` if the head block is covered. If not (e.g., first restart after snapshot import), the early rejection logic (below) handles the empty `fork_db` case.

### Early Block Rejection in `_push_block`

When a node is far behind the network, it receives two types of blocks simultaneously:
- **Sync blocks** (sequential, starting from head+1) — must be accepted
- **Broadcast blocks** (real-time, potentially thousands ahead) — must be rejected silently

Early-rejection checks in `_push_block` prevent sync disruption:

1. **Duplicate blocks**: If a block is at or before head and its ID matches our chain, it's already applied — skip silently (prevents unnecessary `fork_db` push attempts that would throw `unlinkable_block_exception`).

2. **Blocks at/before head on a different fork with unknown parent**: If a block is at or before our head but on a different fork (different ID), AND its parent isn't in the fork_db (the fork diverged before the fork_db's window), throw `unlinkable_block_exception`. The P2P layer catches this and soft-bans the peer (stale fork) or restarts sync (block ahead of head). Without this check, `fork_db.push_block` throws the same exception anyway, but the P2P layer can't distinguish the cause.

3. **Far-ahead blocks with unknown parent**: If a block is above our head, its `previous` is neither our `head_block_id()` nor in `fork_db`, and it's not the genesis block, return `false` silently instead of pushing to `fork_db`. Without this guard, `fork_db.push_block` throws `unlinkable_block_exception` which triggers P2P sync restart, clearing the sync queue and preventing forward progress. This is critical after snapshot import: the node is at block N, the network is at N+1000, and every broadcast block would otherwise disrupt the sequential sync.

4. **Immediate successor always allowed**: Blocks whose `previous == head_block_id()` always pass — this is the critical sync case where the next sequential block must be accepted, even when `fork_db` is empty after a restart.

5. **Fork DB head-seeding**: Before pushing to `fork_db`, if `new_block.previous == head_block_id()` and the head block is NOT in `fork_db`, the head block is fetched from the block log and seeded into `fork_db` via `start_block()`. This ensures the incoming block can link to the chain even after snapshot import, stale sync recovery, or `fork_db` trimming where the head was absent. Without this, `fork_db::_push_block()` would throw `unlinkable_block_exception` ("block does not link to known chain"), silently rejecting valid next-blocks. This also fixes witness nodes generating their own blocks (where `generate_block()` sets `pending_block.previous = head_block_id()`).

6. **Direct-extension bypass**: After pushing to `fork_db`, if `new_block.previous == head_block_id()`, the fork switch logic is bypassed entirely and the block falls through to `apply_block()`. This handles the case where `fork_db._head` points to a stale higher block accumulated from previous failed sync cycles (stale sync recovery does not reset `fork_db`), preventing valid next-blocks from being silently rejected by the fork switch comparison.

### Deferred Resize and Sync Recovery

When shared memory is exhausted during block processing, the node schedules a deferred resize and throws `deferred_resize_exception`. The P2P layer handles this as a transient local condition:

- **Sync path**: Does NOT soft-ban the peer (the peer did nothing wrong). Instead, restarts sync with all active peers so the missed block is re-fetched after the resize completes.
- **Broadcast path**: Does NOT soft-ban or disconnect. The block will be re-received naturally after the resize.

Without this fix, `deferred_resize_exception` in the sync path would fall through to the generic error handler, causing a 1-hour soft-ban of the peer AND losing the missed block — the next sync block (N+2) would fail to link because N+1 was never applied, permanently stalling sync.

### P2P Soft-Ban Trigger Reference

All soft-ban triggers set `fork_rejected_until = now + duration` and `inhibit_fetching_sync_blocks = true`. The default duration is 1 hour (3600s), but **trusted peers** (IPs matching `trusted-snapshot-peer` config) get a reduced 5-minute (300s) ban, allowing faster recovery from transient errors. Broadcast blocks from soft-banned peers are silently discarded. The `inhibit_fetching_sync_blocks` flag is automatically reset when the ban expires.

| # | Code Path | Exception / Condition | Block Position | Action | Reason |
|---|-----------|----------------------|----------------|--------|--------|
| 1 | Sync: `send_sync_block_to_node_delegate` | `block_older_than_undo_history` | any | Soft-ban 1h (5 min trusted) | Peer on a fork too old for undo history |
| 2 | Sync: `send_sync_block_to_node_delegate` | `unlinkable_block_exception` + block ≤ head | ≤ head | Soft-ban 1h (5 min trusted) | Dead fork, block at/below head |
| 3 | Sync: `send_sync_block_to_node_delegate` | `unlinkable_block_exception` + block > head | > head | Restart sync | Behind; need to fetch missing parents |
| 4 | Sync: `send_sync_block_to_node_delegate` | `deferred_resize_exception` | any | Restart sync, no soft-ban | Local condition; peer not at fault |
| 5 | Sync: `send_sync_block_to_node_delegate` | Generic `fc::exception` | any | Soft-ban 1h (5 min trusted) | Unspecified rejection; prevent reconnect loops |
| 6 | Broadcast: `process_block_during_normal_operation` | `unlinkable_block_exception` + block ≤ head | ≤ head | Soft-ban 1h (5 min trusted) | Stale fork |
| 7 | Broadcast: `process_block_during_normal_operation` | `unlinkable_block_exception` + block > head | > head | Restart sync | Behind; resync to catch up |
| 8 | Broadcast: `process_block_during_normal_operation` | `block_older_than_undo_history` | any | Soft-ban 1h (5 min trusted) | Dead fork, stale blocks |
| 9 | Broadcast: `process_block_during_normal_operation` | `deferred_resize_exception` | any | No action | Local transient; block re-received after resize |
| 10 | Broadcast: `process_block_during_normal_operation` | `fc::exception` + block ≤ head | ≤ head | Soft-ban 1h (5 min trusted) | Fork rejection; prevent cascading disconnects |
| 11 | Broadcast: `process_block_during_normal_operation` | `fc::exception` + block > head | > head | Disconnect | Genuinely invalid block |
| 12 | Chain: `_push_block` | Block ≤ head, different fork, parent not in fork_db | ≤ head | Throw `unlinkable_block_exception` | Fork diverged before fork_db window |
| 13 | Chain: `_push_block` | Block > head, `previous != head_block_id`, parent not in fork_db | > head | Return `false` silently | Prevent sync restart storms from broadcast blocks |
| 14 | Chain: `push_block` | `bad_alloc` → `deferred_resize_exception` | any | Throw `deferred_resize_exception` | Shared memory exhausted; P2P must not penalize peer |

### Soft-Ban Notification Protocol

When a node soft-bans a peer (e.g., spam strike threshold exceeded), it sends a `dlt_soft_ban_message` (type 5114) **before** closing the connection. This allows the banned peer to:

1. **Stop sending data immediately** — instead of continuing to spam until the connection times out
2. **Enter BANNED state** with the correct duration — the ban message includes `ban_duration_sec` and a `reason` string
3. **Log a yellow/orange notice** — the receiving node logs: `Peer X soft-banned us for Ns (reason: Y)`

The receiving node closes the connection and waits for the ban duration before attempting to reconnect. This prevents wasted bandwidth from both sides when a peer is already rejected.

| Field | Type | Description |
|-------|------|-------------|
| `ban_duration_sec` | uint32_t | Ban duration in seconds (typically 3600 for 1 hour) |
| `reason` | string | Human-readable ban reason (e.g., "spam strike threshold exceeded") |

### `is_known_block()` in DLT Mode

In DLT mode, the `block_summary` table (TAPOS buffer, 65536 entries) survives snapshot import, but block data may not be available on disk. Simply returning `true` from `block_summary` would mislead P2P peers into requesting blocks the node can't serve.

The DLT mode fix checks `block_summary` as a hint, then verifies the block is on the **preferred chain** via `find_block_id_for_num()`. This allows P2P's `has_item()` to correctly identify blocks we already have (enabling proper sync negotiation) while preventing false positives for blocks we can't serve.

### Fork Database Bug Fixes

Several bugs in `fork_database` were fixed that previously prevented out-of-order block caching from working:
- **Dead code fix**: `_unlinked_index.insert()` was after `throw` (unreachable) — moved before the throw
- **Missing `_push_next()` call**: After inserting a new block, previously-unlinkable blocks that can now link were never resolved — added the call
- **Duplicate block check**: Added early return if a block already exists in the index
- **`reset()` cleanup**: Added `_unlinked_index.clear()` to properly clear the unlinked cache on reset

### Async Snapshot Creation Fix

Snapshot creation previously ran **synchronously** inside `on_applied_block`, which is called within the database write-lock scope of `push_block()`. This caused the write lock to be held for the entire 3+ second snapshot duration, blocking all other threads from reading the database.

**Symptoms:**
- "Read lock timeout" / "No more retries for read lock" in API and P2P threads
- `unlinkable_block_exception` for P2P blocks that arrived during the stall (triggering sync restarts with peers)
- Witness block production continuing (on the same thread) while P2P blocks were blocked

**Fix:**
- Snapshot creation now runs **asynchronously** on a dedicated `fc::thread` — the `on_applied_block` callback returns immediately, releasing the write lock
- `create_snapshot` is split into two phases:
  - **Phase 1 (read lock, ~1 second)**: Serializes all database state into local variables. Block processing waits during this brief phase, but API/P2P reads proceed concurrently
  - **Phase 2 (no lock, ~2 seconds)**: Compression, checksum computation, and file I/O. Block processing and API reads run normally
- `snapshot_in_progress` atomic flag prevents overlapping snapshots
- Plugin shutdown waits for any in-progress snapshot to complete before quitting

## Trusted Seeds Diagnostic Test

The `test-trusted-seeds` option probes all configured `trusted-snapshot-peer` endpoints at startup and reports connectivity metrics, then exits. It is intended as a diagnostic tool to verify that snapshot seeds are reachable and performant before deploying a node.

### What Is Measured

For each trusted peer:
- **Connection time** — TCP handshake duration in milliseconds
- **Latency** — Round-trip time for an info-request / info-reply in milliseconds
- **Snapshot info** — Block number, compressed size, checksum (if snapshot is available)
- **Download speed** — Throughput measured from a single 1 MB chunk download in KB/s
- **Status** — `REACHABLE`, `NO_SNAPSHOT`, `TIMEOUT`, or `ERROR`

### Configuration

```ini
plugin = snapshot

trusted-snapshot-peer = 80.87.202.57:8092
trusted-snapshot-peer = seed2.example.com:8092

# Enable the diagnostic test (node exits after testing)
test-trusted-seeds = true
```

Or as a CLI flag:

```bash
vizd --plugin snapshot --trusted-snapshot-peer 80.87.202.57:8092 --test-trusted-seeds true
```

### Example Output

```
[test-trusted-seeds] Testing 2 trusted peer(s)...

[test-trusted-seeds] Peer 1/2: 80.87.202.57:8092
  Connection: 45 ms
  Latency (info request): 120 ms
  Snapshot: block 18500000, size 156 MB, checksum abc123456789...
  Download speed (1 MB probe): 2450 KB/s

[test-trusted-seeds] Peer 2/2: 192.168.1.10:8092
  Connection: timeout (30s)

=== Trusted Seeds Test Summary === (2 peer(s))
  80.87.202.57:8092    REACHABLE   connect=45ms  latency=120ms  speed=2450KB/s  block=18500000  size=156MB
  192.168.1.10:8092    TIMEOUT

Test complete. Exiting.
```

### Notes

- The node exits after the test is complete. This option is **not** for normal operation.
- If no `trusted-snapshot-peer` entries are configured, the node exits immediately with a warning.
- The speed probe downloads one 1 MB chunk. Actual full-download speed may differ slightly.
- The test runs before the snapshot TCP server starts, so it does not affect other connected clients.

## Stale Snapshot Detection (DLT Mode)

In DLT mode, the `dlt_block_log` is a rolling window — old blocks are pruned as new ones arrive. If the node's latest snapshot is older than the DLT block log's start block, downloading nodes would face an **unsyncable gap**: the snapshot restores state at block N, but the DLT block log starts at block M > N, leaving blocks N+1..M-1 unavailable.

### How It Works

At startup, the snapshot plugin checks:
1. Is the node in DLT mode?
2. Is snapshot serving or periodic creation enabled?
3. Is the latest snapshot's block number **less than** `dlt_block_log.start_block_num()`?

If all conditions are true, the plugin logs a **STALE SNAPSHOT DETECTED** warning and sets an internal flag. On the first fully-synced block (not during P2P catch-up), the plugin creates an **urgent fresh snapshot** immediately — either asynchronously or deferred if the witness is about to produce.

### Example Scenario

```
Latest snapshot:     snapshot-block-900.vizjson   (block 900)
DLT block log:       blocks 1000..2000
Gap:                 blocks 901..999 are missing
```

A downloading node would restore state at block 900 but the serving node can only provide blocks from 1000 onward — P2P sync fails. The stale detection creates a fresh snapshot at the current head (e.g., block 2000), eliminating the gap.

### Log Output

```
STALE SNAPSHOT DETECTED: latest snapshot at block 900 is older than DLT block log start at block 1000.
Downloading nodes would have a sync gap (blocks 900..1000 missing).
A fresh snapshot will be created on the first synced block.
```

When the fresh snapshot is created:
```
Creating urgent fresh snapshot (stale snapshot detected at startup): /data/snapshots/snapshot-block-2000.vizjson
```

### Notes

- The check only runs when `allow-snapshot-serving = true` or `snapshot-every-n-blocks > 0`.
- The urgent snapshot follows the same witness-aware deferral and async creation as periodic snapshots.
- After the fresh snapshot is created, normal periodic snapshot scheduling resumes.
- If no snapshot exists at all (`snap_block = 0`), it is also considered stale (0 < any DLT start block).

## Stalled Sync Detection (DLT Mode)

For DLT mode nodes that may fall behind the network, automatic stalled sync detection can re-download a newer snapshot when P2P sync is no longer possible (peers have pruned old blocks).

### How It Works

1. Node tracks the time of last received block
2. Background thread checks every 30 seconds
3. If no blocks received for the configured timeout, node queries trusted peers
4. If a newer snapshot is available, node clears state and reloads from it
5. If no newer snapshot, timer resets and P2P sync continues

### Configuration

```ini
plugin = snapshot

# Required: trusted peers for snapshot download
trusted-snapshot-peer = seed1.example.com:8092
trusted-snapshot-peer = seed2.example.com:8092

# Enable stalled sync detection
enable-stalled-sync-detection = true

# Timeout before triggering re-download (default: 5 minutes)
stalled-sync-timeout-minutes = 5
```

### Use Cases

- **Node was offline for days/weeks**: Instead of failing to sync because peers no longer have old blocks, the node automatically downloads a fresh snapshot.
- **Network partition**: If the node cannot reach the chain head via P2P, it will attempt to bootstrap from a snapshot.
- **DLT mode recovery**: Essential for nodes running in DLT mode without full block history.

## P2P Stale Sync Detection

The P2P plugin can automatically detect and recover from network stalls — when no blocks are received from any peer for an extended period. This is a lightweight recovery mechanism that does **not** require downloading a snapshot.

### How It Works

When enabled, the P2P plugin tracks the last time a block was received via the network. A background task checks every 30 seconds whether the elapsed time exceeds the configured timeout. If a stall is detected, the node performs three recovery actions in sequence:

1. **Reset sync from LIB** — The P2P layer's sync start point is reset to the last irreversible block (LIB). This ensures the node resumes from a safe, fork-proof position instead of potentially chasing a dead fork.
2. **Resync with connected peers** — The node explicitly restarts synchronization with all currently connected peers by sending fresh `fetch_blockchain_item_ids_message` requests.
3. **Reconnect seed peers** — All seed nodes from `p2p-seed-node` config are re-added to the connection queue and reconnection is attempted for any that were disconnected.

This is complementary to the snapshot plugin's stalled sync detection (which downloads a new snapshot). The P2P stale recovery is faster and less disruptive — it only adjusts sync state and reconnects peers, without requiring any state reload.

### Config Options

```ini
# Enable P2P stale sync detection (default: false)
p2p-stale-sync-detection = true

# Timeout in seconds before recovery triggers (default: 120 = 2 minutes)
p2p-stale-sync-timeout-seconds = 120
```

### Comparison with Snapshot Stalled Sync Detection

| Feature | P2P Stale Sync | Snapshot Stalled Sync |
|---------|---------------|----------------------|
| Plugin | P2P | Snapshot |
| Trigger | No blocks received for timeout | No blocks received for timeout |
| Recovery action | Reset sync + reconnect peers | Download newer snapshot + reload state |
| Timeout default | 120 seconds | 5 minutes |
| Use case | Temporary network partition, peer disconnections | Node far behind, peers lack old blocks |
| DLT mode | Works for all nodes | Designed for DLT mode |

Both can be enabled independently. For DLT nodes, the snapshot detection provides deeper recovery (fresh state), while P2P detection handles transient connectivity issues without state reload.

## Config Reference

### Config file options (`config.ini`)

| Option | Default | Description |
|--------|---------|-------------|
| `snapshot-at-block` | 0 | Create snapshot at specific block number |
| `snapshot-every-n-blocks` | 0 | Create periodic snapshots (0 = disabled) |
| `snapshot-dir` | "" | Directory for auto-generated snapshots |
| `allow-snapshot-serving` | false | Enable TCP snapshot serving |
| `allow-snapshot-serving-only-trusted` | false | Restrict serving to trusted IPs |
| `snapshot-serve-endpoint` | 0.0.0.0:8092 | TCP listen endpoint for serving |
| `trusted-snapshot-peer` | (none) | Trusted peer IP:port (repeatable) |
| `sync-snapshot-from-trusted-peer` | false | Download snapshot on empty state (config.ini or CLI) |
| `enable-stalled-sync-detection` | false | Auto-detect stalled sync and re-download snapshot |
| `stalled-sync-timeout-minutes` | 5 | Timeout for stalled sync detection and startup retry interval |
| `test-trusted-seeds` | false | Probe all trusted peers at startup (connect time, latency, speed) and exit |
| `dlt-block-log-max-blocks` | 100000 | Rolling DLT block_log window size (0 = disabled) |
| `dlt-stats-interval-sec` | 300 | Interval in seconds between P2P peer stats log output (min 30) |
| `dlt-peer-max-disconnect-hours` | 8 | Remove peer from known list after this many hours of non-response |
| `dlt-mempool-max-tx` | 10000 | Maximum number of transactions in P2P mempool |
| `dlt-mempool-max-bytes` | 104857600 | Maximum total bytes of transactions in P2P mempool (default 100MB) |
| `dlt-mempool-max-tx-size` | 65536 | Maximum single transaction size in bytes (default 64KB) |
| `dlt-mempool-max-expiration-hours` | 24 | Reject transactions with expiration too far in the future (hours) |
| `dlt-peer-exchange-max-per-reply` | 10 | Max peers to include in a peer exchange reply |
| `dlt-peer-exchange-max-per-subnet` | 2 | Max peers per /24 subnet in peer exchange replies |
| `dlt-peer-exchange-min-uptime-sec` | 600 | Min connection uptime (seconds) before sharing a peer in exchange replies |

### CLI options

| Option | Description |
|--------|-------------|
| `--snapshot <path>` | Load state from snapshot file |
| `--create-snapshot <path>` | Create snapshot and exit |
| `--sync-snapshot-from-trusted-peer true` | Download snapshot from trusted peers on empty state (also available in config.ini) |
