# Running a Validator Node

Validators (block producers) are accounts scheduled by the Fair-DPOS algorithm to produce blocks every 3 seconds. Running a validator node requires a registered validator account, a signing key, and a properly configured node.

---

## Prerequisites

1. A VIZ Ledger account registered as a validator via `validator_update_operation`.
2. The WIF private key corresponding to the signing key registered on-chain.
3. A synced full node (validator plugin requires chain + p2p + snapshot plugins).

---

## Configuration

Use `share/vizd/config/config_witness.ini` as the base template.

Key settings:

```ini
# P2P — allow public inbound connections for block propagation
p2p-endpoint = 0.0.0.0:2001
p2p-seed-node = seed1.viz.world:2001

# RPC — bind to localhost for security (validators don't need public API)
webserver-http-endpoint = 127.0.0.1:8090
webserver-ws-endpoint   = 127.0.0.1:8091

# Required plugins for a validator
plugin = chain p2p webserver json_rpc database_api network_broadcast_api validator witness_api

# Skip virtual-op indexing to save memory (validators don't need it)
skip-virtual-ops = true

# Shared memory — 2G is enough for low-memory validator builds
shared-file-size = 2G

# ─── Validator identity ───────────────────────────────────────
# Your validator account name
validator = myvalidator

# Your signing private key (WIF format)
private-key = 5JxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxWIF

# Participation threshold — stop producing if network drops below this
required-participation = 33

# Do NOT enable this on mainnet (only for testnet/bootstrap)
# enable-stale-production = false
```

---

## NTP Synchronization

Accurate time is critical for block production. The validator plugin maintains its own NTP client:

```ini
# NTP servers (defaults are pool.ntp.org, time.google.com, time.cloudflare.com)
ntp-server = pool.ntp.org
ntp-server = time.cloudflare.com

# Check NTP every 15 minutes
ntp-request-interval = 900

# Discard NTP replies with round-trip > 150ms
ntp-round-trip-threshold = 150
```

Ensure the server OS clock is also synchronized (via `chrony` or `systemd-timesyncd`).

---

## Starting the Node

```bash
./vizd --config-file /data/vizd/config.ini --data-dir /data/vizd
```

### Docker

```bash
docker run -d \
  --name vizd-validator \
  --restart unless-stopped \
  -p 2001:2001 \
  -v /data/vizd:/var/lib/vizd \
  -e VIZD_WITNESS=myvalidator \
  -e VIZD_PRIVATE_KEY=5Jxxx... \
  vizblockchain/vizd:lowmem
```

Use the `lowmem` image for validators — it excludes unnecessary indexing plugins.

---

## Registering / Updating Your Validator

Use `cli_wallet` or any compatible wallet to broadcast a `validator_update_operation`:

```json
{
  "type": "validator_update_operation",
  "value": {
    "owner": "myvalidator",
    "url": "https://mysite.example/validator",
    "block_signing_key": "VIZ5hqSa...",
    "props": [3, {
      "account_creation_fee": "1.000 VIZ",
      "maximum_block_size": 65536
    }]
  }
}
```

The `block_signing_key` must match the `private-key` configured in the node.

To disable your validator (remove from schedule), set `block_signing_key` to the null key:

```json
"block_signing_key": "VIZ1111111111111111111111111111111114T1Anm"
```

---

## Production Loop: What the Node Does

The validator plugin runs a 250ms production timer on a **dedicated thread** (isolated from P2P I/O). On each tick it calls `maybe_produce_block()` which checks (in order):

1. **Sync gate** (DLT mode): Not producing if catching up from peers.
2. **Snapshot gate**: Not producing if snapshot creation is in progress.
3. **Participation check**: Network must have ≥33% validator participation.
4. **Slot assignment**: Is this node's validator scheduled for the current slot?
5. **Key check**: Does the node have the correct private key?
6. **Minority fork detection**: If the last 21 blocks are all from this node's validators — rollback and resync.
7. **Fork collision resolution**: If another block exists at the same height, apply vote-weight comparison.
8. **Lag check**: If the node is >500ms past the slot boundary — skip.
9. **Generate and broadcast** the block.

See [Validator Plugin](../plugins/validator.md) for the complete execution flow.

---

## Production Results (Log Messages)

| Result | Meaning |
|--------|---------|
| `produced` | Block produced and broadcast successfully |
| `not_synced` | Node still catching up or snapshot in progress |
| `not_time_yet` | No slot available or NTP drift |
| `not_my_turn` | Another validator is scheduled for this slot |
| `no_private_key` | Configured validator scheduled but private key missing |
| `low_participation` | Network participation below threshold |
| `lag` | Woke up >500ms past slot — slot missed |
| `fork_collision` | Competing block at next height — waiting |
| `minority_fork` | Node is on isolated fork — rolling back |

---

## Safety Mechanisms

### Network Partition Guard
If fewer than 33% of validators are participating, production stops to prevent split-brain scenarios. Override with `enable-stale-production = true` only for bootstrap/testnet.

### Minority Fork Detection
If the node's fork database shows 21+ consecutive blocks all from this node's own validators, it automatically rolls back to LIB and resyncs. This catches network isolation.

### Production Watchdog
If no block has been produced for 180 seconds (60s for emergency master) while `should_be_producing` is true, the watchdog automatically clears stuck flags (`minority_fork_recovering`, P2P catchup, chain syncing) and attempts to resume.

### Snapshot Safety
Block production is paused while a snapshot is being created to avoid write-lock conflicts.

---

## Monitoring

Watch for these log patterns:

```
# Good: block produced
produced block #123456 ... validator=myvalidator

# Warning: missed slot
MISSED-SLOT-OUR-validator: ...

# Warning: minority fork
MINORITY FORK DETECTED: rolling back to LIB

# Warning: watchdog fires
WATCHDOG: no production for 180s, clearing flags
```

Also see [Monitoring](./monitoring.md) and [Validator Guard](./validator-guard.md) for automated alerting.

---

## Multiple Validators on One Node

The `validator` and `private-key` options are repeatable:

```ini
validator = alice
validator = alice.backup
private-key = 5Jxxx...   # Alice's key
private-key = 5Jyyy...   # Alice.backup's key
```

The node will produce blocks for any of the configured validators when scheduled.

---

## Emergency Consensus Key

For nodes involved in emergency consensus recovery:

```ini
emergency-private-key = 5Jzzz...   # Committee emergency key
```

When set, the node automatically adds `CHAIN_EMERGENCY_WITNESS_ACCOUNT` to its validator set and participates in emergency block production. See [Emergency Consensus](../consensus/emergency-consensus.md).

---

## Troubleshooting

| Problem | Check |
|---------|-------|
| Not producing | Verify `validator` and `private-key` in config; check signing key registered on-chain matches config |
| `no_private_key` in logs | On-chain signing key doesn't match any `private-key` in config |
| `low_participation` | Network health issue — check peer count and other validators |
| `minority_fork` | Network isolation — verify connectivity to seed nodes |
| NTP stall warnings | Check OS NTP sync: `chronyc tracking` or `timedatectl` |
| Slot hijacks | Signing key may have been blanked by emergency master; restore via `validator_update_operation` |
