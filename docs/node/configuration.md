# Node Configuration

VIZ Ledger nodes are configured via an INI file. The repository ships several templates in `share/vizd/config/`:

| Template | Use case |
|----------|---------|
| `config.ini` | Full mainnet node with public RPC |
| `config_witness.ini` | Validator node (localhost RPC, block production) |
| `config_testnet.ini` | Testnet / development |
| `config_stock_exchange.ini` | Market data consumer (minimal plugins) |
| `config_debug.ini` | Debug mode |

---

## Network & P2P

```ini
# Listen address for P2P connections (port 2001 standard)
p2p-endpoint = 0.0.0.0:2001

# Maximum peer connections (unlimited if unset)
p2p-max-connections = 200

# Seed nodes to bootstrap connectivity (repeatable)
p2p-seed-node = seed1.viz.world:2001
p2p-seed-node = seed2.viz.world:2001

# Checkpoints: trusted (block_num, block_id) pairs (repeatable)
# checkpoint = [12345,"0003039..." ]
```

---

## Webserver & RPC

```ini
# HTTP JSON-RPC endpoint
webserver-http-endpoint = 0.0.0.0:8090

# WebSocket JSON-RPC endpoint
webserver-ws-endpoint = 0.0.0.0:8091

# RPC thread pool size
webserver-thread-pool-size = 2
```

> **Security note:** For validator nodes, bind to `127.0.0.1` to prevent external access:
> ```ini
> webserver-http-endpoint = 127.0.0.1:8090
> webserver-ws-endpoint   = 127.0.0.1:8091
> ```

---

## Locking & Concurrency

```ini
# Microseconds to wait for a read lock before retrying
read-wait-micro = 500000

# Maximum read lock retry attempts
max-read-wait-retries = 2

# Microseconds to wait for a write lock before retrying
write-wait-micro = 500000

# Maximum write lock retry attempts
max-write-wait-retries = 3

# Serialize all write operations on a single thread (recommended)
single-write-thread = true

# Run plugin notifications on push_transaction (adds latency; default false)
enable-plugins-on-push-transaction = false
```

---

## Shared Memory (Database)

The blockchain state is stored in a memory-mapped file (`shared_memory.bin`).

```ini
# Initial size of the shared memory file
shared-file-size = 4G

# Minimum free space before triggering a resize
min-free-shared-file-size = 500M

# Amount to grow the file by on resize
inc-shared-file-size = 2G

# Check free space every N blocks
block-num-check-free-size = 1000
```

Tune `shared-file-size` based on chain size. For mainnet, start at `4G` and monitor growth.

---

## Plugin Activation

```ini
# Each 'plugin' line adds a plugin (repeatable)
# Minimum set for a full API node:
plugin = chain p2p webserver json_rpc database_api network_broadcast_api

# Additional indexing plugins (comment out on low-memory nodes):
plugin = social_network tags follow account_history account_by_key
plugin = committee_api invite_api paid_subscription_api custom_protocol_api

# For validator nodes only:
plugin = validator witness_api
```

### Plugin sets by node type

| Node type | Plugins |
|-----------|---------|
| Full node | All above |
| Validator | `chain p2p webserver json_rpc database_api network_broadcast_api validator witness_api` |
| Low-memory seed | `chain p2p` |
| Stock exchange | `chain p2p webserver json_rpc database_api network_broadcast_api account_history` |

---

## History & Tracking

```ini
# Discard vote objects before this block (saves memory, default 0 = keep all)
clear-votes-before-block = 0

# Skip indexing virtual operations (saves memory on validators)
skip-virtual-ops = false

# Track account history only for accounts in range (optional)
# track-account-range = ["alice","alice.zzz"]

# Whitelist/blacklist specific operation types for history
# history-whitelist-ops = transfer_operation
# history-blacklist-ops = custom_operation

# Start indexing history from this block number
# history-start-block = 1000000

# Maximum feed entries per account (follow plugin)
follow-max-feed-size = 500

# Private message tracking range (optional)
# pm-account-range = ["alice","alice.zzz"]
```

---

## Validator (Block Production)

Leave these unset on non-validator nodes.

```ini
# Allow production even if chain is stale (development/testnet only)
enable-stale-production = false

# Minimum participation % required to produce blocks (0–99)
required-participation = 33

# Validator account name (repeatable for multiple validators on one node)
validator = alice

# WIF signing key for the validator
private-key = 5JxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxWIF

# Emergency consensus private key (optional)
# emergency-private-key = 5Jxxx...
```

See [Validator Node](./validator-node.md) for full validator configuration.

---

## Logging

```ini
# Console logger (outputs to stderr)
log.console_appender.stderr.stream = std_error

# File logger for P2P messages
log.file_appender.p2p.filename = logs/p2p/p2p.log

# Log level: all, debug, info, warn, error, off
logger.default.level = warn
logger.default.appenders = stderr

logger.p2p.level = warn
logger.p2p.appenders = p2p
```

---

## Complete Reference

All options listed by source file:

| Source | Options covered |
|--------|----------------|
| `plugins/chain/plugin.hpp` | `shared-file-size`, `min-free-shared-file-size`, `inc-shared-file-size`, `block-num-check-free-size`, `single-write-thread`, `enable-plugins-on-push-transaction`, `read-wait-micro`, `max-read-wait-retries`, `write-wait-micro`, `max-write-wait-retries`, `skip-virtual-ops`, `clear-votes-before-block`, `track-account-range`, `history-whitelist-ops`, `history-blacklist-ops`, `history-start-block` |
| `plugins/p2p/p2p_plugin.hpp` | `p2p-endpoint`, `p2p-max-connections`, `p2p-seed-node`, `checkpoint` |
| `plugins/webserver/webserver_plugin.hpp` | `webserver-http-endpoint`, `webserver-ws-endpoint`, `webserver-thread-pool-size` |
| `plugins/validator/validator.hpp` | `enable-stale-production`, `required-participation`, `validator`, `private-key`, `emergency-private-key`, `fork-collision-timeout-blocks`, `ntp-server`, `ntp-request-interval`, `debug-block-production` |
| `plugins/follow/` | `follow-max-feed-size` |
| `plugins/private_message/` | `pm-account-range` |
