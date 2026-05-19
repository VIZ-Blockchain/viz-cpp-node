# Plugin Overview

VIZ Ledger uses the **AppBase** plugin framework. Each plugin has a lifecycle (`plugin_initialize` → `plugin_startup` → `plugin_shutdown`), may register JSON-RPC methods, may store data in the chainbase database, and may subscribe to chain signals (e.g., `applied_block`).

---

## Plugin Categories

| Category | Description |
|----------|-------------|
| **Core** | Required for any node operation |
| **Infrastructure** | Networking, web server, snapshots |
| **API** | Expose JSON-RPC endpoints for clients |
| **Index** | Index chain data into chainbase for fast queries |
| **Producer** | Block signing and production |
| **External** | Integration with external systems (MongoDB) |
| **Debug/Test** | Development only; not for production |

---

## Plugin Inventory

### Core

| Plugin | Status | Deps | JSON-RPC |
|--------|--------|------|---------|
| `chain` | Required | `json_rpc` | — |
| `json_rpc` | Required | — | — |

### Infrastructure

| Plugin | Status | Deps | JSON-RPC |
|--------|--------|------|---------|
| `webserver` | Required for API | `json_rpc` | — |
| `p2p` | Required for network | `chain` | — |
| `snapshot` | Recommended | `chain` | — |
| `witness_guard` | Recommended for validators | `chain`, `p2p` | — |

### API

| Plugin | Status | Deps | JSON-RPC |
|--------|--------|------|---------|
| `database_api` | Active | `json_rpc`, `chain` | Yes |
| `network_broadcast_api` | Active | `json_rpc`, `chain`, `p2p` | Yes |
| `witness_api` | Active | `json_rpc`, `chain` | Yes |
| `account_by_key` | Active | `json_rpc`, `chain` | Yes |
| `account_history` | Active | `json_rpc`, `chain`, `operation_history` | Yes |
| `operation_history` | Active | `json_rpc`, `chain` | Yes |
| `committee_api` | Active | `json_rpc`, `chain` | Yes |
| `invite_api` | Active | `json_rpc`, `chain` | Yes |
| `paid_subscription_api` | Active | `json_rpc`, `chain` | Yes |
| `custom_protocol_api` | Active | `json_rpc`, `chain` | Yes |
| `auth_util` | Active | `json_rpc`, `chain` | Yes |
| `block_info` | Active | `json_rpc`, `chain` | Yes |
| `raw_block` | Active | `json_rpc`, `chain` | Yes |
| `follow` | Deprecated | `json_rpc`, `chain` | Yes |
| `tags` | Deprecated | `json_rpc`, `chain`, `follow` | Yes |
| `social_network` | Deprecated | `json_rpc`, `chain` | Yes |
| `private_message` | Deprecated | `json_rpc`, `chain` | Yes |

### Producer

| Plugin | Status | Deps | JSON-RPC |
|--------|--------|------|---------|
| `validator` | Active | `chain`, `p2p` | — |

### External

| Plugin | Status | Deps | JSON-RPC |
|--------|--------|------|---------|
| `mongo_db` | Active | `chain` | — |

### Debug / Test

| Plugin | Status | Deps | JSON-RPC |
|--------|--------|------|---------|
| `debug_node` | Dev only | `chain` | Yes |
| `test_api` | Test only | `json_rpc` | — |

---

## Core Plugins

### `chain`

Manages the chainbase database, applies blocks and transactions, and emits signals to all other plugins.

**Key config options:**

| Option | Default | Description |
|--------|---------|-------------|
| `shared-file-size` | `2G` | Initial shared memory file size |
| `shared-file-dir` | `state` | Directory for shared memory files |
| `inc-shared-file-size` | `2G` | Growth increment when space is low |
| `min-free-shared-file-size` | `500M` | Trigger auto-grow below this threshold |
| `flush-state-interval` | `10000` | Force flush to disk every N blocks |
| `skip-virtual-ops` | `false` | Skip virtual operations (reduces memory) |
| `dlt-block-log-max-blocks` | `100000` | Rolling DLT block log capacity |

**Key CLI flags:**

| Flag | Description |
|------|-------------|
| `--replay-blockchain` | Wipe chainbase and replay from block log |
| `--force-replay-blockchain` | Same as above, ignores corruption check |
| `--replay-from-snapshot` | Import snapshot then replay DLT block log (crash recovery) |
| `--auto-recover-from-snapshot` | Enable automatic recovery from shared memory corruption |
| `--resync-blockchain` | Wipe chainbase and block log; start from genesis or snapshot |

---

### `json_rpc`

Framework plugin; no config. Registers and dispatches all JSON-RPC methods. Must be loaded first.

All JSON-RPC requests use the 2.0 format:
```json
{
  "jsonrpc": "2.0",
  "method": "api_name.method_name",
  "params": {},
  "id": 1
}
```

---

## Infrastructure Plugins

### `webserver`

HTTP and WebSocket server that forwards requests to `json_rpc`. Includes a read-only response cache.

**Key config options:**

| Option | Default | Description |
|--------|---------|-------------|
| `webserver-http-endpoint` | — | HTTP listen address (e.g., `0.0.0.0:8090`) |
| `webserver-ws-endpoint` | — | WebSocket listen address (e.g., `0.0.0.0:8091`) |
| `webserver-thread-pool-size` | `32` | Worker threads for HTTP/WS handling |
| `webserver-cache-enabled` | `true` | Enable response caching |
| `webserver-cache-size` | `10000` | Maximum cached entries |

Cache keys are derived from `method + params` (not `id`), preventing bypass by rotating the request `id`. Mutating methods (`network_broadcast_api.*`, `debug_node.*`) are never cached. The cache clears on each new applied block.

See [Webserver](./webserver.md) for full details.

---

### `p2p`

DLT P2P networking — block and transaction propagation, peer management, minority fork recovery.

**Key config options:**

| Option | Default | Description |
|--------|---------|-------------|
| `p2p-endpoint` | — | Listen address (e.g., `0.0.0.0:2001`) |
| `seed-node` | — | Static seed peer(s) |
| `p2p-max-connections` | — | Maximum simultaneous connections |
| `dlt-block-log-max-blocks` | `100000` | Rolling DLT log capacity |
| `dlt-stats-interval-sec` | `300` | Peer stats log interval |

See [P2P Overview](../p2p/overview.md) for the full P2P architecture.

---

### `snapshot`

Snapshot creation, loading, and P2P snapshot sync for fast bootstrap and crash recovery.

See [Snapshot](../node/snapshot.md) and [Plugin: Snapshot](./snapshot.md) for details.

---

## API Plugins

### `database_api`

Primary read API. Query blocks, transactions, accounts, chain state, hardfork version, delegations, proposals.

See [Database API](./database-api.md) for the full method reference.

---

### `network_broadcast_api`

Submit and broadcast signed transactions and blocks.

| Method | Description |
|--------|-------------|
| `broadcast_transaction` | Submit a transaction (async) |
| `broadcast_transaction_synchronous` | Submit and wait for inclusion in a block |
| `broadcast_transaction_with_callback` | Submit with callback on inclusion or expiry |
| `broadcast_block` | Submit a signed block (validators only) |

---

### `witness_api`

Query validator state: active set, schedule, individual validators, vote rankings.

| Method | Description |
|--------|-------------|
| `get_active_witnesses` | Current 21-validator active set |
| `get_witness_schedule` | Full schedule object |
| `get_witnesses` | Validators by database IDs |
| `get_witness_by_account` | Single validator by account name |
| `get_witnesses_by_vote` | Validators sorted by total vote weight |
| `get_witnesses_by_counted_vote` | Validators sorted by counted vote weight |
| `get_witness_count` | Total number of registered validators |
| `lookup_witness_accounts` | List validator account names by prefix |

---

### `account_by_key`

Reverse-lookup accounts by public key.

| Method | Description |
|--------|-------------|
| `get_key_references` | Get account names that use given public keys |

---

### `account_history`

Per-account operation history, paginated.

| Method | Description |
|--------|-------------|
| `get_account_history(account, from, limit)` | Get operations; `from=-1` returns newest; max 1000 per call |

**Config options:**
- `track-account-range` — account name range to index (default: all accounts)
- `history-count-blocks` — retain N blocks of history

---

### `operation_history`

All-operations index for block-level and transaction queries.

| Method | Description |
|--------|-------------|
| `get_ops_in_block(block_num, virtual_ops)` | Operations in a block; `virtual_ops=true` includes virtual ops |
| `get_transaction(tx_id)` | Transaction by ID |

**Config options:**
- `history-whitelist-ops` / `history-blacklist-ops` — filter which op types are stored
- `history-start-block` — start indexing from this block number
- `history-count-blocks` — retain N blocks of history

---

### `committee_api`

Query committee worker requests and votes.

| Method | Description |
|--------|-------------|
| `get_committee_request(id)` | Request by ID |
| `get_committee_request_votes(id)` | Votes on a request |
| `get_committee_requests_list(from, limit, status)` | Paginated request list |

---

### `invite_api`

Query active invite codes.

| Method | Description |
|--------|-------------|
| `get_invites_list` | All invite IDs |
| `get_invite_by_id(id)` | Invite by database ID |
| `get_invite_by_key(pub_key)` | Invite by public key |

---

### `paid_subscription_api`

Query subscription offerings and subscriber status.

| Method | Description |
|--------|-------------|
| `get_paid_subscriptions` | All active subscription offerings |
| `get_paid_subscription_options(account)` | Subscription config for an account |
| `get_paid_subscription_status(subscriber, account)` | Status of a specific subscription |
| `get_active_paid_subscriptions(subscriber)` | Active subscriptions for a subscriber |
| `get_inactive_paid_subscriptions(subscriber)` | Expired subscriptions |

---

### Deprecated API Plugins

| Plugin | Methods | Notes |
|--------|---------|-------|
| `follow` | Followers/following, feeds, blog, reblogs | Still functional; not recommended for new integrations |
| `tags` | Trending/hot/new content by tag | Still functional; not recommended for new integrations |
| `social_network` | Content, votes, replies | Wraps committee/invite queries; still functional |
| `private_message` | Inbox/outbox for encrypted messages | `custom_operation`-based; still functional |

---

## Producer Plugin

### `validator`

Block signing and production. Runs a 250 ms timer loop; produces when the next slot is assigned to a configured validator account.

**Key config options:**

| Option | Default | Description |
|--------|---------|-------------|
| `validator` | — | Validator account name(s) |
| `private-key` | — | WIF private key(s) for signing |
| `emergency-private-key` | — | Emergency consensus signing key |
| `enable-stale-production` | `false` | Produce even when chain is stale (testnet only) |
| `required-participation` | `3300` | Min participation in basis points (3300 = 33%) |
| `fork-collision-timeout-blocks` | `21` | Deferrals before forcing production past a collision |

`required-participation` is always in **basis points** (0–10000 = 0%–100%).

See [Validator Plugin](./validator.md) for full production timing and fork handling details.

---

## Recommended Plugin Sets

### Minimal API node

```ini
plugin = chain
plugin = json_rpc
plugin = webserver
plugin = p2p
plugin = database_api
plugin = network_broadcast_api
```

### Full API node

```ini
plugin = chain
plugin = json_rpc
plugin = webserver
plugin = p2p
plugin = database_api
plugin = network_broadcast_api
plugin = witness_api
plugin = account_by_key
plugin = account_history
plugin = operation_history
plugin = committee_api
plugin = invite_api
plugin = paid_subscription_api
```

### Validator node

```ini
plugin = chain
plugin = p2p
plugin = validator
plugin = json_rpc
plugin = webserver
plugin = database_api
plugin = network_broadcast_api
plugin = witness_api
plugin = snapshot

snapshot-every-n-blocks = 28800
snapshot-dir = /data/snapshots
dlt-block-log-max-blocks = 100000
```

---

See also: [Chain Plugin](./chain.md), [Validator Plugin](./validator.md), [Snapshot Plugin](./snapshot.md), [P2P Overview](../p2p/overview.md), [JSON-RPC API](../api/json-rpc.md).
