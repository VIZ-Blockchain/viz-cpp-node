# Monitoring

This page covers health checks, log patterns, P2P peer statistics, and integration with external monitoring stacks for VIZ Ledger nodes.

---

## Health Check: Node Sync

Query the node's dynamic global properties to verify it is running and syncing:

```bash
curl -s -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}' \
  | python3 -m json.tool
```

Check `head_block_number` — it must increase every 3 seconds while synced. Check `time` — it must be within a few seconds of wall clock.

A simple liveness probe script:

```bash
#!/bin/bash
RESPONSE=$(curl -sf -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}')
if [ $? -ne 0 ]; then echo "CRIT: RPC unreachable"; exit 2; fi

HEAD=$(echo "$RESPONSE" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['head_block_number'])")
echo "OK: head_block_number=$HEAD"
```

---

## Log Patterns

### Block production (validator nodes)

```
# Good: slot produced
produced block #123456 @2025-01-01T00:00:03 validator=alice sz=2048

# Missed slot
MISSED-SLOT-OUR-validator: alice missed slot at 2025-01-01T00:00:06

# Minority fork detected
MINORITY FORK DETECTED: rolling back to LIB #123400

# Watchdog fired
WATCHDOG: no production for 180s, clearing flags
```

### P2P connectivity

```
# New peer connected
New peer is connected (203.0.113.10:2001), now 8 active peers

# Peer soft-banned
soft-banning peer 203.0.113.10:2001 for 300s: reason=only_fork_db_blocks_no_progress

# Sync complete
Sync: peer 203.0.113.10 says we're up-to-date
```

### Snapshot and recovery

```
# Snapshot created
Snapshot created at block 5000000 in 14.2s: /data/snapshots/snapshot-block-5000000.json

# Auto-recovery triggered
shared_memory_corruption_exception detected — starting auto-recovery
Recovery complete. Resumed from block 4999500
```

### Sync logger (DLT mode)

Enable the `sync` logger to see sync negotiation details:

```ini
[logger.sync]
level = info
appenders = stderr
```

Key messages:
- `Starting sync with peer ...` — sync initiated
- `on_blockchain_item_ids_inventory: ...` — block ID batch received
- `Sync: peer X says we're up-to-date` — sync complete for this peer
- `DEFERRED_RESIZE: sync block #N deferred` — shared memory resize delayed a block
- `auto-clearing stuck peer_needs_sync_items_from_us` — 30s safety net cleared a stuck flag

---

## Log Configuration

Logs are configured in `config.ini`:

```ini
# Console output
log.console_appender.stderr.stream = std_error

# P2P log file
log.file_appender.p2p.filename = logs/p2p/p2p.log

# Log levels: all, debug, info, warn, error, off
logger.default.level = warn
logger.default.appenders = stderr

logger.p2p.level = warn
logger.p2p.appenders = p2p
```

> **Note:** `node.cpp` routes all its `ilog`/`wlog` calls to the `p2p` logger. To see P2P messages, configure the `p2p` logger level to `info`.

Log rotation via `logrotate` (example `/etc/logrotate.d/vizd`):

```
/data/vizd/logs/p2p/p2p.log {
    daily
    rotate 14
    compress
    missingok
    copytruncate
}
```

---

## P2P Peer Statistics

The P2P plugin logs peer health metrics every 5 minutes (configurable). Enable in `config.ini`:

```ini
p2p-stats-enabled = true
p2p-stats-interval = 300   # seconds
```

Sample log output:

```
P2P peer | ip: 203.0.113.10  | port: 2001 | latency: 45ms  | bytes_in: 12345 | blocked: false | reason:
P2P peer | ip: 198.51.100.5  | port: 2001 | latency: 120ms | bytes_in: 8765  | blocked: true  | reason: soft_ban
Block storage | dlt_log: [79174319..79274318] | dlt_resizes: 412 | fork_db: linked=18 unlinked=0
```

Fields:
- `latency` — round-trip delay in ms
- `bytes_in` — delta bytes received since last measurement
- `blocked` / `reason` — soft-ban or inhibition status and cause
- `Block storage` — DLT block log range, resize count, fork_db state

A high `dlt_resizes` count combined with a shrinking `dlt_log` range may indicate the mapped-file self-heal ran. A `reason: soft_ban` peer may be on a fork or sending only stale data.

---

## Prometheus & Grafana

The node does not expose a Prometheus endpoint natively. Use [Node Exporter](https://github.com/prometheus/node_exporter) for OS-level metrics and scrape the JSON-RPC endpoint with a custom exporter:

```python
# minimal example: scrape head_block_number
import requests, time
from prometheus_client import Gauge, start_http_server

g = Gauge('viz_head_block_number', 'Current head block')

def collect():
    r = requests.post('http://localhost:8090', json={
        "jsonrpc": "2.0", "method": "call",
        "params": ["database_api", "get_dynamic_global_properties", []],
        "id": 1
    }, timeout=5)
    g.set(r.json()['result']['head_block_number'])

start_http_server(9100)
while True:
    collect()
    time.sleep(3)
```

**Recommended dashboard panels:**

| Panel | Metric / Source |
|-------|----------------|
| Head block | `viz_head_block_number` (increases every 3 s when synced) |
| Block lag | `time() - viz_head_block_time` (seconds behind wall clock) |
| Peer count | Parsed from P2P stats log |
| Peer latency | P2P stats log, by peer IP |
| Shared memory free | `viz_shared_memory_free_mb` from custom exporter |
| CPU / RAM | Node Exporter standard metrics |
| Disk I/O | Node Exporter `node_disk_*` |

---

## ELK / Centralized Logging

Forward node logs to a central collector. Example with Filebeat:

```yaml
# filebeat.yml
filebeat.inputs:
  - type: log
    paths:
      - /data/vizd/logs/p2p/p2p.log
    fields:
      service: vizd
      node: validator-1

output.logstash:
  hosts: ["logstash:5044"]
```

Parse key fields (Logstash grok or Elasticsearch ingest pipeline):

```
MISSED-SLOT-OUR-validator: %{WORD:validator} missed slot at %{TIMESTAMP_ISO8601:slot_time}
produced block #%{NUMBER:block_num} @%{TIMESTAMP_ISO8601:block_time} validator=%{WORD:producer}
```

---

## Validator-Specific Monitoring

### Key metrics to alert on

| Condition | Severity | Action |
|-----------|----------|--------|
| `MISSED-SLOT-OUR-validator` in logs | Warning | Check NTP, network latency, CPU load |
| `MINORITY FORK DETECTED` | Critical | Verify P2P connectivity to seed nodes |
| `WATCHDOG: no production for 180s` | Critical | Check validator key and node health |
| `no_private_key` result code | Critical | Signing key mismatch — check config |
| `low_participation` result code | Warning | Network health degraded |
| Head block stopped advancing | Critical | Node may be stalled |
| Peer count drops to 0 | Critical | Network partition or firewall issue |

### NTP check

```bash
chronyc tracking | grep "System time"
# or
timedatectl | grep "NTP synchronized"
```

The validator plugin uses its own NTP client (configurable via `ntp-server` in config), but OS clock sync is also important. A drift >200ms can cause missed slots.

---

## Database Maintenance

### Shared memory sizing

Monitor free space warnings in logs:

```
chainbase: shared memory low — resizing from 4G to 6G
```

Proactively configure growth parameters in `config.ini`:

```ini
shared-file-size = 4G
min-free-shared-file-size = 500M
inc-shared-file-size = 2G
block-num-check-free-size = 1000
```

### Snapshot backup verification

After creating a snapshot, verify it loads cleanly on a test node:

```bash
vizd --create-snapshot /tmp/verify-snap.json --plugin snapshot
# Expected: exits cleanly with "Snapshot created at block N"
```

Periodically test crash recovery:

```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
# Expected: imports snapshot, replays dlt_block_log, emits "Recovery complete"
```

---

## Incident Response Checklist

**Node not syncing:**
1. Check peer count (`p2p-stats-enabled` logs or RPC `get_info`).
2. Verify firewall allows TCP port 2001 inbound.
3. Check `p2p-seed-node` settings — try alternate seeds.
4. Look for `soft_ban` entries in P2P stats — the node may be on a fork.

**Validator not producing:**
1. Check `validator` and `private-key` in `config.ini` match on-chain signing key.
2. Verify `low_participation` is not the cause (network health).
3. Check NTP synchronization.
4. Look for `MINORITY FORK DETECTED` — node may need to resync.

**Node crashed / shared memory corrupted:**
1. If `--auto-recover-from-snapshot` is enabled (default) and snapshots exist, the node recovers automatically — check logs.
2. Manual recovery: `vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot`.
3. If no snapshots exist: `vizd --replay-blockchain` (requires full block log; unavailable in DLT mode).

**RPC unreachable:**
1. Check `webserver-http-endpoint` binding — validators default to `127.0.0.1:8090`.
2. Check firewall or reverse proxy configuration.
3. Verify plugin list includes `webserver json_rpc database_api`.

---

See also: [Validator Node](./validator-node.md), [Validator Guard](./validator-guard.md), [Snapshots](./snapshot.md), [Configuration](./configuration.md).
