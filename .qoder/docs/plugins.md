# VIZ Blockchain — Plugins Reference

Complete specification of all VIZ node plugins: what they do, dependencies, status (active/deprecated), and JSON-RPC API methods.

---

## Plugin Architecture Overview

VIZ uses a modular plugin architecture based on Appbase. Plugins can:
- Provide JSON-RPC API methods
- Store additional data in the chainbase database
- React to blockchain events (applied blocks, operations, etc.)
- Depend on other plugins

### Plugin Categories

| Category | Description |
|---|---|
| **Core** | Essential for node operation |
| **API** | Expose JSON-RPC endpoints |
| **Index** | Index blockchain data for queries |
| **Infrastructure** | Networking, web server |
| **External** | Integration with external systems |
| **Debug/Test** | Development and testing only |

---

## Core Plugins

### `chain`
**Status:** Active (Required)
**Category:** Core
**Dependencies:** `json_rpc`

The fundamental plugin that manages the blockchain database, block validation, and transaction processing.

**Purpose:**
- Maintains the blockchain state database (chainbase)
- Validates and applies blocks and transactions
- Provides database access to other plugins
- Emits signals on block application

**JSON-RPC:** None (internal only)

**CLI options:**
| Option | Type | Description |
|--------|------|-------------|
| `--replay-blockchain` | `bool` | Clear chain database and replay all blocks |
| `--replay-if-corrupted` | `bool` (default: `true`) | Replay all blocks if shared memory is corrupted |
| `--force-replay-blockchain` | `bool` | Force clear chain database and replay all blocks |
| `--replay-from-snapshot` | `bool` | Crash recovery: import snapshot and replay dlt_block_log |
| `--auto-recover-from-snapshot` | `bool` (default: `true`) | Automatic runtime recovery from shared memory corruption via snapshot |
| `--resync-blockchain` | `bool` | Clear chain database and block log |

**Config options:**
```ini
shared-file-size = 2G
shared-file-dir = /path/to/blockchain
flush-state-interval = 0
```

| Option | Default | Description |
|--------|---------|-------------|
| `shared-file-size` | `2G` | Start size of the shared memory file |
| `shared-file-dir` | `state` | Location of the shared memory files |
| `inc-shared-file-size` | `2G` | Size increment when shared memory runs low |
| `min-free-shared-file-size` | `500M` | Minimum free space before auto-grow |
| `block-num-check-free-size` | `1000` | Check free space every N blocks |
| `flush-state-interval` | `10000` | Flush shared memory to disk every N blocks |
| `single-write-thread` | `false` | Push blocks/transactions from one thread |
| `skip-virtual-ops` | `false` | Skip virtual operations (saves memory) |
| `enable-plugins-on-push-transaction` | `false` | Notify plugins on push_transaction |
| `dlt-block-log-max-blocks` | `100000` | Blocks to keep in the DLT rolling block_log |

---

### `json_rpc`
**Status:** Active (Required)
**Category:** Core
**Dependencies:** None

Provides the JSON-RPC 2.0 framework for API method registration and dispatching.

**Purpose:**
- Registers API methods from all plugins
- Parses JSON-RPC requests
- Dispatches to appropriate handlers
- Returns formatted responses

**JSON-RPC:** None (framework only)

---

### `webserver`
**Status:** Active (Required for API access)
**Category:** Infrastructure
**Dependencies:** `json_rpc`

HTTP/WebSocket server that accepts JSON-RPC requests with built-in response caching.

**Purpose:**
- Serves HTTP and WebSocket connections
- Routes requests to `json_rpc` plugin
- Handles CORS, timeouts, connection limits
- Caches read-only JSON-RPC responses with id-independent cache keys
- Patches response IDs to match request IDs per JSON-RPC 2.0 spec
- Clears cache on each new block to maintain consistency
- Filters out mutating APIs (`network_broadcast_api.*`, `debug_node.*`) from cache

**JSON-RPC:** None (transport only)

**Config options:**
```ini
webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint = 0.0.0.0:8091
webserver-thread-pool-size = 32
webserver-cache-enabled = true
webserver-cache-size = 10000
```

**Cache behavior:**
- Cache keys are derived from `method` + `params` only (excluding `id`), preventing bypass via ID rotation spam
- Uses `fc::json::from_string` for robust JSON parsing — invalid JSON bypasses cache
- Mutating APIs are detected in both direct (`"method":"network_broadcast_api.xxx"`) and call-style (`"method":"call","params":["network_broadcast_api",...]`) formats
- Cached responses have their `id` field patched before sending to match the client's request ID

---

### `p2p`
**Status:** Active (Required for network sync)
**Category:** Infrastructure
**Dependencies:** `chain`

Peer-to-peer networking for block and transaction propagation.

**Purpose:**
- Discovers and connects to peers
- Syncs blockchain from the network
- Broadcasts blocks and transactions
- Maintains peer database
- Minority fork auto-recovery (`resync_from_lib()`)

**JSON-RPC:** None (internal only)

**Config options:**
```ini
p2p-endpoint = 0.0.0.0:2001
p2p-seed-node = seed.viz.world:2001
p2p-max-connections = 200
p2p-stats-enabled = true
p2p-stats-interval = 300
p2p-stale-sync-detection = false
p2p-stale-sync-timeout-seconds = 120
```

**P2P stats task:** When `p2p-stats-enabled = true`, every `p2p-stats-interval` seconds the plugin logs:
- Per-peer stats (IP, port, latency, bytes received, blocked status)
- Failed/rejected peers from the peer database
- **Block storage diagnostics:** head, LIB, earliest available block, DLT block log range, regular block log end, fork_db linked/unlinked counts and ranges, DLT mode flag, and total `resize()` count
- In DLT mode, also runs `dlt_block_log::verify_mapping()` to detect and self-heal stale memory-mapped file state

**Minority fork auto-recovery:** The P2P plugin exposes `resync_from_lib()` which is called by the witness plugin when a minority fork is detected (last 21 blocks all from our own witnesses). It pops all reversible blocks back to LIB, resets fork_db, re-initiates P2P sync, and reconnects seed nodes. This replicates the effect of a manual node restart. See [fork-collision-hardfork-proposal.md](fork-collision-hardfork-proposal.md) for details.

---

### `witness`
**Status:** Active
**Category:** Producer
**Dependencies:** `chain`, `p2p`

Block production and witness scheduling.

**Purpose:**
- Produces blocks when scheduled
- Manages witness signing keys
- Detects fork collisions and defers production
- Detects minority fork (last 21 blocks all from own witnesses) and triggers auto-recovery
- Supports emergency consensus block production

**JSON-RPC:** None (internal only)

**Config options:**
```ini
witness = "mywitness"
private-key = 5K...
enable-stale-production = false
required-participation = 3300
fork-collision-timeout-blocks = 21
```

| Option | Default | Description |
|---|---|---|
| `witness` | (none) | Witness account name(s) to produce blocks for |
| `private-key` | (none) | WIF private key(s) for block signing |
| `emergency-private-key` | (none) | WIF key for emergency consensus production |
| `enable-stale-production` | `false` | Allow production even if chain is stale or on a minority fork |
| `required-participation` | `3300` (33%) | Minimum witness participation rate (basis points) |
| `fork-collision-timeout-blocks` | `21` | Deferrals before forcing production past a fork collision |

**Minority fork detection:** Before producing a block, the witness plugin walks the last `CHAIN_MAX_WITNESSES` (21) blocks in fork_db. If ALL were produced by the node's own configured witnesses, the node is stuck on a minority fork. With `enable-stale-production=false` (default), the plugin calls `p2p.resync_from_lib()` to pop back to LIB and resync. With `enable-stale-production=true`, production continues (for bootstrap/testnet scenarios). Detection is skipped during emergency consensus mode.

**Emergency consensus:** When `emergency-private-key` is configured, the committee account is added to the witness set. During emergency consensus mode (`dgp.emergency_consensus_active`), the node produces blocks using the committee account's schedule.

See [block-processing.md](block-processing.md) for production timing details and [fork-collision-hardfork-proposal.md](fork-collision-hardfork-proposal.md) for fork handling.

---

### `snapshot`
**Status:** Active
**Category:** Infrastructure
**Dependencies:** `chain`

Snapshot creation, loading, and P2P sync for fast node bootstrap and crash recovery in DLT mode.

**Purpose:**
- Create JSON snapshots of blockchain state
- Load state from snapshots (near-instant startup)
- Serve snapshots to other nodes over TCP
- Download snapshots from trusted peers
- Crash recovery via snapshot + dlt_block_log replay

**JSON-RPC:** None

**Non-blocking snapshot creation:** Snapshot creation runs asynchronously on a dedicated background thread. Only the database read phase (serialization) holds a read lock (~1 second); compression and file I/O run without any lock. This eliminates read-lock timeouts and `unlinkable_block_exception` errors that occurred when snapshots ran synchronously inside the write-lock scope.

**CLI options:**
| Option | Type | Description |
|--------|------|-------------|
| `--snapshot <path>` | `string` | Load state from a snapshot file (DLT mode) |
| `--snapshot-auto-latest` | `bool` | Auto-discover latest snapshot in `snapshot-dir` |
| `--replay-from-snapshot` | `bool` | Crash recovery: import snapshot + replay dlt_block_log |
| `--auto-recover-from-snapshot` | `bool` (default: `true`) | Automatic runtime recovery from shared memory corruption |
| `--create-snapshot <path>` | `string` | Create a snapshot and exit |
| `--sync-snapshot-from-trusted-peer` | `bool` | Download snapshot from trusted peers on empty state |

**Config options:**
```ini
snapshot-dir = /data/snapshots
snapshot-every-n-blocks = 28800
snapshot-max-age-days = 90
allow-snapshot-serving = false
trusted-snapshot-peer = seed1.viz.world:8092
dlt-block-log-max-blocks = 100000
```

See [snapshot-plugin.md](snapshot-plugin.md) for full documentation.

---

## API Plugins

### `database_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Primary read API for blockchain state queries.

**Purpose:**
- Query blocks, transactions, accounts
- Query chain properties, hardfork status
- Validate transactions and signatures
- Query escrows, delegations, proposals

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `database_api.get_block_header` | Get block header by height |
| `database_api.get_block` | Get full signed block |
| `database_api.get_irreversible_block_header` | Get irreversible block header |
| `database_api.get_irreversible_block` | Get irreversible block |
| `database_api.set_block_applied_callback` | Subscribe to new blocks |
| `database_api.get_config` | Get compile-time chain constants |
| `database_api.get_dynamic_global_properties` | Get current chain state |
| `database_api.get_chain_properties` | Get median witness properties |
| `database_api.get_hardfork_version` | Get current hardfork version |
| `database_api.get_next_scheduled_hardfork` | Get next hardfork info |
| `database_api.get_accounts` | Get accounts by names |
| `database_api.lookup_account_names` | Lookup accounts (nullable) |
| `database_api.lookup_accounts` | List accounts by prefix |
| `database_api.get_account_count` | Get total account count |
| `database_api.get_master_history` | Get account master key history |
| `database_api.get_recovery_request` | Get pending recovery request |
| `database_api.get_escrow` | Get escrow by ID |
| `database_api.get_withdraw_routes` | Get vesting withdraw routes |
| `database_api.get_vesting_delegations` | Get active delegations |
| `database_api.get_expiring_vesting_delegations` | Get expiring delegations |
| `database_api.get_transaction_hex` | Get transaction as hex |
| `database_api.get_required_signatures` | Get required signatures |
| `database_api.get_potential_signatures` | Get all potential signers |
| `database_api.verify_authority` | Verify transaction authority |
| `database_api.verify_account_authority` | Verify account authority |
| `database_api.get_database_info` | Get database statistics |
| `database_api.get_proposed_transactions` | Get proposals for account |
| `database_api.get_accounts_on_sale` | List accounts for sale |
| `database_api.get_accounts_on_auction` | List accounts on auction |
| `database_api.get_subaccounts_on_sale` | List subaccounts for sale |

---

### `network_broadcast_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`, `p2p`

Broadcasts transactions and blocks to the network.

**Purpose:**
- Broadcast signed transactions
- Broadcast signed blocks (for witnesses)
- Synchronous transaction confirmation

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `network_broadcast_api.broadcast_transaction` | Broadcast transaction (async) |
| `network_broadcast_api.broadcast_transaction_synchronous` | Broadcast and wait for inclusion |
| `network_broadcast_api.broadcast_transaction_with_callback` | Broadcast with callback |
| `network_broadcast_api.broadcast_block` | Broadcast a signed block |

---

### `witness_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Query witness information.

**Purpose:**
- List active/scheduled witnesses
- Query witness by account or vote rank
- Get witness schedule

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `witness_api.get_active_witnesses` | Get current active witness set |
| `witness_api.get_witness_schedule` | Get witness schedule object |
| `witness_api.get_witnesses` | Get witnesses by IDs |
| `witness_api.get_witness_by_account` | Get witness by account name |
| `witness_api.get_witnesses_by_vote` | Get witnesses ranked by votes |
| `witness_api.get_witnesses_by_counted_vote` | Get witnesses by counted votes |
| `witness_api.get_witness_count` | Get total witness count |
| `witness_api.lookup_witness_accounts` | List witness accounts by prefix |

---

### `account_by_key`
**Status:** Active
**Category:** Index/API
**Dependencies:** `json_rpc`, `chain`

Indexes accounts by their public keys for reverse lookup.

**Purpose:**
- Find accounts that use a given public key
- Useful for wallet applications

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `account_by_key.get_key_references` | Get accounts using given public keys |

---

### `account_history`
**Status:** Active
**Category:** Index/API
**Dependencies:** `json_rpc`, `chain`, `operation_history`

Indexes operation history per account.

**Purpose:**
- Query operation history for a specific account
- Paginated access to account activity

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `account_history.get_account_history` | Get operations for an account |

#### `get_account_history`

**Parameters:**

| # | Name | Type | Description |
|---|---|---|---|
| 1 | `account` | string | Account name to query |
| 2 | `from` | uint32 | Starting sequence number, or `-1` for newest |
| 3 | `limit` | uint32 | Max entries to return (1-1000) |

**Behavior:**
- `from = -1` (or `4294967295`): Start from the most recent operation
- Returns entries in descending order (newest first)
- If `limit` exceeds available entries, returns all available without error
- Example: Account has 5 entries, request `from=-1, limit=10` → returns all 5 entries

**Request:**
```json
{
  "jsonrpc": "2.0",
  "method": "account_history.get_account_history",
  "params": ["on1x", -1, 10],
  "id": 1
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "4": {"trx_id": "abc...", "block": 1234, "op": [...]},
    "3": {"trx_id": "def...", "block": 1233, "op": [...]},
    "2": {"trx_id": "ghi...", "block": 1232, "op": [...]}
  },
  "id": 1
}
```

**Config options:**
```ini
track-account-range = ["", "zzzzzzzzzzzzzzzz"]
history-count-blocks = 4294967295
```

**Memory Management:**
- Old history entries are automatically purged based on `history-count-blocks`
- Coordinates with `operation_history` plugin to avoid dangling references
- Uses the more aggressive purge threshold between both plugins
- Signal handlers are properly disconnected on shutdown to prevent memory leaks

---

### `operation_history`
**Status:** Active
**Category:** Index/API
**Dependencies:** `json_rpc`, `chain`

Indexes all operations in blocks.

**Purpose:**
- Query operations within a block
- Lookup transactions by ID
- Provides base operation storage for `account_history` plugin

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `operation_history.get_ops_in_block` | Get operations in a block |
| `operation_history.get_transaction` | Get transaction by ID |

**Config options:**
```ini
history-whitelist-ops = []       # Only store these operations (exclusive with blacklist)
history-blacklist-ops = []       # Don't store these operations
history-start-block = 0          # Start recording from this block
history-count-blocks = 4294967295 # How many blocks of history to keep
```

**Memory Management:**
- Old operations are automatically purged based on `history-count-blocks`
- `account_history` plugin coordinates purging with this plugin
- Signal handlers are properly disconnected on shutdown to prevent memory leaks
- Exposes `get_min_keep_block()` for dependent plugins to coordinate purging

---

### `committee_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Query committee worker requests.

**Purpose:**
- Get committee request details
- List all committee requests
- Get votes on requests

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `committee_api.get_committee_request` | Get request by ID |
| `committee_api.get_committee_request_votes` | Get votes on a request |
| `committee_api.get_committee_requests_list` | List all request IDs |

---

### `invite_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Query invite codes.

**Purpose:**
- List active invites
- Lookup invite by ID or public key

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `invite_api.get_invites_list` | List all invite IDs |
| `invite_api.get_invite_by_id` | Get invite by database ID |
| `invite_api.get_invite_by_key` | Get invite by public key |

---

### `paid_subscription_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Query paid subscriptions.

**Purpose:**
- List subscription offerings
- Check subscription status between accounts

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `paid_subscription_api.get_paid_subscriptions` | List all subscription offerings |
| `paid_subscription_api.get_paid_subscription_options` | Get subscription config for account |
| `paid_subscription_api.get_paid_subscription_status` | Get subscription status subscriber→account |
| `paid_subscription_api.get_active_paid_subscriptions` | List active subscriptions for subscriber |
| `paid_subscription_api.get_inactive_paid_subscriptions` | List expired subscriptions |

---

### `follow`
**Status:** Deprecated
**Category:** Index/API
**Dependencies:** `json_rpc`, `chain`

Indexes follow relationships and content feeds.

**Purpose:**
- Track followers/following
- Build personalized feeds
- Track reblogs

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `follow.get_followers` | Get followers of an account |
| `follow.get_following` | Get accounts followed by an account |
| `follow.get_follow_count` | Get follower/following counts |
| `follow.get_feed_entries` | Get feed entries (references only) |
| `follow.get_feed` | Get feed with full content |
| `follow.get_blog_entries` | Get blog entries (references) |
| `follow.get_blog` | Get blog with full content |
| `follow.get_reblogged_by` | Get accounts that reblogged content |
| `follow.get_blog_authors` | Get authors reblogged on a blog |

---

### `tags`
**Status:** Deprecated
**Category:** Index/API
**Dependencies:** `json_rpc`, `chain`, `follow`

Indexes content by tags and provides content discovery.

**Purpose:**
- Query trending/hot/new content by tag
- Track tag statistics
- Content discovery APIs

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `tags.get_trending_tags` | Get tags sorted by activity |
| `tags.get_tags_used_by_author` | Get tags used by an author |
| `tags.get_discussions_by_trending` | Get trending discussions |
| `tags.get_discussions_by_created` | Get newest discussions |
| `tags.get_discussions_by_active` | Get recently active discussions |
| `tags.get_discussions_by_cashout` | Get discussions by cashout time |
| `tags.get_discussions_by_payout` | Get discussions by payout |
| `tags.get_discussions_by_votes` | Get discussions by vote count |
| `tags.get_discussions_by_children` | Get discussions by reply count |
| `tags.get_discussions_by_hot` | Get hot discussions |
| `tags.get_discussions_by_feed` | Get discussions from feed |
| `tags.get_discussions_by_blog` | Get discussions from blog |
| `tags.get_discussions_by_contents` | Get discussions by content |
| `tags.get_discussions_by_author_before_date` | Get author's posts before date |
| `tags.get_languages` | Get available content languages |

---

### `social_network`
**Status:** Deprecated
**Category:** API
**Dependencies:** `json_rpc`, `chain`

High-level content and social queries (combines multiple data sources).

**Purpose:**
- Query content discussions
- Get votes on content
- Committee and invite queries (convenience)

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `social_network.get_content` | Get discussion by author/permlink |
| `social_network.get_content_replies` | Get direct replies |
| `social_network.get_all_content_replies` | Get all nested replies |
| `social_network.get_account_votes` | Get votes cast by account |
| `social_network.get_active_votes` | Get votes on content |
| `social_network.get_replies_by_last_update` | Get replies sorted by update |
| `social_network.get_committee_request` | Get committee request |
| `social_network.get_committee_request_votes` | Get committee request votes |
| `social_network.get_committee_requests_list` | List committee requests |
| `social_network.get_invites_list` | List invites |
| `social_network.get_invite_by_id` | Get invite by ID |
| `social_network.get_invite_by_key` | Get invite by key |

---

### `private_message`
**Status:** Deprecated
**Category:** Index/API
**Dependencies:** `json_rpc`, `chain`

Indexes encrypted private messages sent via `custom_operation`.

**Purpose:**
- Track inbox/outbox messages
- Encrypted message protocol support

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `private_message.get_inbox` | Get received messages |
| `private_message.get_outbox` | Get sent messages |

**Config options:**
```ini
pm-account-range = ["", "zzzzzzzzzzzzzzzz"]
```

---

### `custom_protocol_api`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Tracks custom protocol sequences from `custom_operation`.

**Purpose:**
- Get account info with custom protocol metadata
- Useful for apps using custom_operation

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `custom_protocol_api.get_account` | Get account with custom protocol reference |

---

### `auth_util`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Authority verification utilities.

**Purpose:**
- Check signatures against account authority

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `auth_util.check_authority_signature` | Verify signature satisfies authority |

---

### `block_info`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Detailed block information queries.

**Purpose:**
- Get extended block information
- Block statistics

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `block_info.get_block_info` | Get block info for range |
| `block_info.get_blocks_with_info` | Get blocks with extended info |

---

### `raw_block`
**Status:** Active
**Category:** API
**Dependencies:** `json_rpc`, `chain`

Get raw serialized blocks.

**Purpose:**
- Export blocks in raw binary format
- Useful for block archival/replication

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `raw_block.get_raw_block` | Get raw block by height |

---

## Witness/Producer Plugins

### `witness`
**Status:** Active
**Category:** Core (for block producers)
**Dependencies:** `chain`, `p2p`

Block production plugin for witnesses.

**Purpose:**
- Sign and produce blocks on schedule
- Manage witness private keys

**JSON-RPC:** None

**Config options:**
```ini
witness = "your-witness-account"
private-key = 5K...
enable-stale-production = true          # Produce blocks even on a stale chain (default: false)
required-participation = 3300            # Min witness participation in basis points to produce (default: 33% = 3300)
```

**Bug Fix: `enable-stale-production` and `required-participation` option parsing**

Two bugs were fixed in the witness plugin option definitions ([witness.cpp](../../plugins/witness/witness.cpp)):

| Bug | Before | After |
|---|---|---|
| `enable-stale-production` used `implicit_value(false)` | `--enable-stale-production` without a value set production to `false` (same as not using the flag at all) | `implicit_value(true)` — using the flag alone now correctly enables stale production |
| `required-participation` used `implicit_value(33)` then multiplied by `CHAIN_1_PERCENT` | Config file value `required-participation = 50` was interpreted as 50×100=5000 basis points (500%) | Now uses `default_value(33 * CHAIN_1_PERCENT)` and reads the raw value directly — config value is in basis points |

The `required-participation` value is now always in **basis points** (0–10000 = 0%–100%):
- Default: `3300` = 33%
- Config: `required-participation = 5000` = 50%
- CLI: `--required-participation 5000` = 50%

**Optimization: Block Production Timing**

The witness plugin's production loop uses a timer + look-ahead mechanism to determine when to produce a block. The timer ticks at regular intervals and the look-ahead shifts `now` forward so the slot boundary is detected earlier.

Source: [witness.cpp](../../plugins/witness/witness.cpp) — `schedule_production_loop()`, `maybe_produce_block()`

| Parameter | Value | Meaning |
|---|---|---|
| Timer tick interval | 250ms | How often the production loop wakes up |
| Look-ahead | +250ms | `now = ntp_time + 250ms` — shifts current time forward |
| Lag threshold | 500ms | If `|scheduled_time - now| > 500ms`, block is NOT produced (LAG condition) |

The look-ahead compensates for OS timer jitter. With 250ms ticks + 250ms look-ahead, the tick at `T_slot - 250ms` aligns `now` exactly to the slot boundary, achieving near-zero-lag production:

```
Slot at T=6.000:
  Tick at T=5.750 → now=6.000 → slot matched → lag=0ms → PRODUCE
```

If the tick fires late (OS jitter), the next tick 250ms later still has comfortable margin:
```
  Tick at T=6.000 → now=6.250 → lag=250ms → PRODUCE (within 500ms threshold)
  Tick at T=6.250 → now=6.500 → lag=500ms → borderline LAG
```

**Previous behavior** (before optimization): 1000ms tick + 500ms look-ahead → best-case lag was 500ms (exactly at the threshold), and even 50ms of OS jitter caused a LAG condition.

---

## Debug/Test Plugins

### `debug_node`
**Status:** Active (Development only)
**Category:** Debug
**Dependencies:** `chain`

Development and testing utilities. **NOT for production use.**

**Purpose:**
- Generate test blocks
- Push blocks from files
- Set hardforks manually

**JSON-RPC Methods:**

| Method | Description |
|---|---|
| `debug_node.debug_generate_blocks` | Generate N test blocks |
| `debug_node.debug_generate_blocks_until` | Generate blocks until time |
| `debug_node.debug_push_blocks` | Push blocks from database |
| `debug_node.debug_push_json_blocks` | Push blocks from JSON file |
| `debug_node.debug_pop_block` | Pop and return last block |
| `debug_node.debug_get_witness_schedule` | Get witness schedule |
| `debug_node.debug_set_hardfork` | Force set hardfork |
| `debug_node.debug_has_hardfork` | Check if hardfork applied |

---

### `test_api`
**Status:** Active (Testing only)
**Category:** Test
**Dependencies:** `json_rpc`

Test API plugin for connectivity testing.

**JSON-RPC Methods:** None documented (internal testing)

---

## External Integration Plugins

### `mongo_db`
**Status:** Active
**Category:** External
**Dependencies:** `chain`

Exports blockchain data to MongoDB.

**Purpose:**
- Real-time export of blocks, transactions, operations
- Enable MongoDB-based queries and analytics

**JSON-RPC:** None

**Config options:**
```ini
mongodb-uri = mongodb://localhost:27017
mongodb-db-name = viz
```

---

## Plugin Status Summary

| Plugin | Status | Has API | Category |
|---|---|---|---|
| `chain` | Active | No | Core |
| `json_rpc` | Active | No | Core |
| `webserver` | Active | No | Infrastructure |
| `p2p` | Active | No | Infrastructure |
| `snapshot` | Active | No | Infrastructure |
| `database_api` | Active | Yes | API |
| `network_broadcast_api` | Active | Yes | API |
| `witness_api` | Active | Yes | API |
| `account_by_key` | Active | Yes | Index/API |
| `account_history` | Active | Yes | Index/API |
| `operation_history` | Active | Yes | Index/API |
| `committee_api` | Active | Yes | API |
| `invite_api` | Active | Yes | API |
| `paid_subscription_api` | Active | Yes | API |
| `follow` | Deprecated | Yes | Index/API |
| `tags` | Deprecated | Yes | Index/API |
| `social_network` | Deprecated | Yes | API |
| `private_message` | Deprecated | Yes | Index/API |
| `custom_protocol_api` | Active | Yes | API |
| `auth_util` | Active | Yes | API |
| `block_info` | Active | Yes | API |
| `raw_block` | Active | Yes | API |
| `witness` | Active | No | Producer |
| `debug_node` | Dev only | Yes | Debug |
| `test_api` | Test only | Yes | Test |
| `mongo_db` | Active | No | External |

---

## JSON-RPC Quick Reference

All methods use JSON-RPC 2.0 format:

```json
{
  "jsonrpc": "2.0",
  "method": "api_name.method_name",
  "params": {},
  "id": 1
}
```

### Complete API Method Index

| API | Method | Description |
|---|---|---|
| `database_api` | `get_block_header` | Block header by height |
| `database_api` | `get_block` | Full block by height |
| `database_api` | `get_irreversible_block_header` | Irreversible block header |
| `database_api` | `get_irreversible_block` | Irreversible block |
| `database_api` | `set_block_applied_callback` | Subscribe to blocks |
| `database_api` | `get_config` | Chain constants |
| `database_api` | `get_dynamic_global_properties` | Current chain state |
| `database_api` | `get_chain_properties` | Median witness props |
| `database_api` | `get_hardfork_version` | Current HF version |
| `database_api` | `get_next_scheduled_hardfork` | Next HF info |
| `database_api` | `get_accounts` | Accounts by names |
| `database_api` | `lookup_account_names` | Lookup accounts |
| `database_api` | `lookup_accounts` | List accounts |
| `database_api` | `get_account_count` | Total accounts |
| `database_api` | `get_master_history` | Key history |
| `database_api` | `get_recovery_request` | Recovery request |
| `database_api` | `get_escrow` | Escrow by ID |
| `database_api` | `get_withdraw_routes` | Withdraw routes |
| `database_api` | `get_vesting_delegations` | Delegations |
| `database_api` | `get_expiring_vesting_delegations` | Expiring delegations |
| `database_api` | `get_transaction_hex` | TX as hex |
| `database_api` | `get_required_signatures` | Required sigs |
| `database_api` | `get_potential_signatures` | Potential signers |
| `database_api` | `verify_authority` | Verify TX auth |
| `database_api` | `verify_account_authority` | Verify account auth |
| `database_api` | `get_database_info` | DB stats |
| `database_api` | `get_proposed_transactions` | Proposals |
| `database_api` | `get_accounts_on_sale` | Accounts for sale |
| `database_api` | `get_accounts_on_auction` | Accounts on auction |
| `database_api` | `get_subaccounts_on_sale` | Subaccounts for sale |
| `network_broadcast_api` | `broadcast_transaction` | Broadcast TX |
| `network_broadcast_api` | `broadcast_transaction_synchronous` | Broadcast TX (sync) |
| `network_broadcast_api` | `broadcast_transaction_with_callback` | Broadcast TX (callback) |
| `network_broadcast_api` | `broadcast_block` | Broadcast block |
| `witness_api` | `get_active_witnesses` | Active witnesses |
| `witness_api` | `get_witness_schedule` | Witness schedule |
| `witness_api` | `get_witnesses` | Witnesses by ID |
| `witness_api` | `get_witness_by_account` | Witness by account |
| `witness_api` | `get_witnesses_by_vote` | Witnesses by votes |
| `witness_api` | `get_witnesses_by_counted_vote` | Witnesses by counted votes |
| `witness_api` | `get_witness_count` | Witness count |
| `witness_api` | `lookup_witness_accounts` | List witnesses |
| `account_by_key` | `get_key_references` | Accounts by key |
| `account_history` | `get_account_history` | Account operations |
| `operation_history` | `get_ops_in_block` | Block operations |
| `operation_history` | `get_transaction` | TX by ID |
| `committee_api` | `get_committee_request` | Request by ID |
| `committee_api` | `get_committee_request_votes` | Request votes |
| `committee_api` | `get_committee_requests_list` | All requests |
| `invite_api` | `get_invites_list` | All invites |
| `invite_api` | `get_invite_by_id` | Invite by ID |
| `invite_api` | `get_invite_by_key` | Invite by key |
| `paid_subscription_api` | `get_paid_subscriptions` | All subscriptions |
| `paid_subscription_api` | `get_paid_subscription_options` | Subscription config |
| `paid_subscription_api` | `get_paid_subscription_status` | Subscription status |
| `paid_subscription_api` | `get_active_paid_subscriptions` | Active subscriptions |
| `paid_subscription_api` | `get_inactive_paid_subscriptions` | Inactive subscriptions |
| `follow` | `get_followers` | Followers |
| `follow` | `get_following` | Following |
| `follow` | `get_follow_count` | Follow counts |
| `follow` | `get_feed_entries` | Feed entries |
| `follow` | `get_feed` | Feed content |
| `follow` | `get_blog_entries` | Blog entries |
| `follow` | `get_blog` | Blog content |
| `follow` | `get_reblogged_by` | Rebloggers |
| `follow` | `get_blog_authors` | Blog authors |
| `tags` | `get_trending_tags` | Trending tags |
| `tags` | `get_tags_used_by_author` | Author's tags |
| `tags` | `get_discussions_by_trending` | Trending posts |
| `tags` | `get_discussions_by_created` | New posts |
| `tags` | `get_discussions_by_active` | Active posts |
| `tags` | `get_discussions_by_cashout` | Posts by cashout |
| `tags` | `get_discussions_by_payout` | Posts by payout |
| `tags` | `get_discussions_by_votes` | Posts by votes |
| `tags` | `get_discussions_by_children` | Posts by replies |
| `tags` | `get_discussions_by_hot` | Hot posts |
| `tags` | `get_discussions_by_feed` | Feed posts |
| `tags` | `get_discussions_by_blog` | Blog posts |
| `tags` | `get_discussions_by_contents` | Content posts |
| `tags` | `get_discussions_by_author_before_date` | Author posts |
| `tags` | `get_languages` | Languages |
| `social_network` | `get_content` | Discussion |
| `social_network` | `get_content_replies` | Replies |
| `social_network` | `get_all_content_replies` | All replies |
| `social_network` | `get_account_votes` | Account's votes |
| `social_network` | `get_active_votes` | Votes on content |
| `social_network` | `get_replies_by_last_update` | Replies by update |
| `private_message` | `get_inbox` | Inbox |
| `private_message` | `get_outbox` | Outbox |
| `custom_protocol_api` | `get_account` | Account + custom |
| `auth_util` | `check_authority_signature` | Check sig |
| `block_info` | `get_block_info` | Block info |
| `block_info` | `get_blocks_with_info` | Blocks + info |
| `raw_block` | `get_raw_block` | Raw block |
| `debug_node` | `debug_generate_blocks` | Generate blocks |
| `debug_node` | `debug_generate_blocks_until` | Generate until |
| `debug_node` | `debug_push_blocks` | Push blocks |
| `debug_node` | `debug_push_json_blocks` | Push JSON blocks |
| `debug_node` | `debug_pop_block` | Pop block |
| `debug_node` | `debug_get_witness_schedule` | Witness schedule |
| `debug_node` | `debug_set_hardfork` | Set hardfork |
| `debug_node` | `debug_has_hardfork` | Check hardfork |

---

## Recommended Plugin Sets

### Minimal API Node
```ini
plugin = chain
plugin = json_rpc
plugin = webserver
plugin = p2p
plugin = database_api
plugin = network_broadcast_api
```

### Full API Node
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
plugin = follow
plugin = tags
plugin = social_network
plugin = private_message
```

### Witness Node
```ini
plugin = chain
plugin = p2p
plugin = witness
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
