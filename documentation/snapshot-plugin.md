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
```

The node will:
1. Query all trusted peers for their latest snapshot
2. Select the peer with the highest block number
3. Download the snapshot in 1 MB chunks with progress logging
4. Verify the checksum
5. Load the snapshot and start syncing from that block

### Trust Model

- **Public serving** (`allow-snapshot-serving = true`, `allow-snapshot-serving-only-trusted = false`): Any IP can connect and download snapshots. Good for public seed nodes.
- **Trusted-only serving** (`allow-snapshot-serving-only-trusted = true`): Only IPs from `trusted-snapshot-peer` list can download. Good for private networks.
- **Client side**: Only connects to endpoints in `trusted-snapshot-peer` (inherent trust).

### Anti-Spam Protection

The snapshot TCP server has built-in anti-spam measures:

- **1 active session per IP**: If an IP already has an active download in progress, additional connections from the same IP are rejected.
- **Rate limiting (3 connections/hour per IP)**: Each IP is limited to 3 connections per hour. Exceeding this triggers a rejection with a warning log. This prevents abuse where a client repeatedly connects to waste server bandwidth.
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
| `dlt-block-log-max-blocks` | 100000 | Rolling DLT block_log window size (0 = disabled) |

### CLI options

| Option | Description |
|--------|-------------|
| `--snapshot <path>` | Load state from snapshot file |
| `--create-snapshot <path>` | Create snapshot and exit |
| `--sync-snapshot-from-trusted-peer true` | Download snapshot from trusted peers on empty state |
