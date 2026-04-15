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

**Witness-Aware Deferral:** When the node is also a block-producing witness, periodic snapshot creation is automatically deferred if the witness is scheduled to produce within the next 4 slots (~12 seconds). This covers the typical snapshot creation time (~10 seconds) plus a safety margin, preventing snapshot serialization (which holds the database write lock) from causing the witness to miss its production slot. The snapshot is created once the witness is no longer scheduled in the near-term slots.

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
3. Download the snapshot in 1 MB chunks with progress logging (30-second timeout per operation)
4. Verify the checksum
5. Load the snapshot and start syncing from that block

If no trusted peers respond, the node will **retry automatically** every `stalled-sync-timeout-minutes` minutes (default: 5) until a snapshot becomes available. The node will not fall back to genesis sync.

**Note on Timeouts:** All P2P snapshot operations have a 30-second timeout. If a peer doesn't respond within this time (e.g., accepts TCP connection but never sends data), the node will skip that peer and try the next one. This prevents indefinite hangs when some peers are unresponsive.

### Trust Model

- **Public serving** (`allow-snapshot-serving = true`, `allow-snapshot-serving-only-trusted = false`): Any IP can connect and download snapshots. Good for public seed nodes.
- **Trusted-only serving** (`allow-snapshot-serving-only-trusted = true`): Only IPs from `trusted-snapshot-peer` list can download. Good for private networks.
- **Client side**: Only connects to endpoints in `trusted-snapshot-peer` (inherent trust).

### Anti-Spam Protection

The snapshot TCP server has built-in anti-spam measures:

- **Max 5 concurrent connections**: The server accepts up to 5 simultaneous connections, each handled in a separate fiber (via `fc::async`). Additional connections are rejected.
- **1 active session per IP**: If an IP already has an active download in progress, additional connections from the same IP are rejected. Session tracking is protected by a mutex for thread safety across fibers.
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
| `dlt-block-log-max-blocks` | 100000 | Rolling DLT block_log window size (0 = disabled) |

### CLI options

| Option | Description |
|--------|-------------|
| `--snapshot <path>` | Load state from snapshot file |
| `--create-snapshot <path>` | Create snapshot and exit |
| `--sync-snapshot-from-trusted-peer true` | Download snapshot from trusted peers on empty state (also available in config.ini) |
