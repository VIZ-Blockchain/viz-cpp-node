# JSON-RPC API

All VIZ node APIs use JSON-RPC 2.0 over HTTP POST or WebSocket.

---

## Request Format

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "api_name.method_name",
  "params": [arg1, arg2]
}
```

- `id` may be any number or string; it is echoed in the response.
- `params` may be an array or object depending on the method.
- Both HTTP POST and WebSocket are supported. Subscriptions require WebSocket.

---

## Response Format

**Success:**
```json
{ "jsonrpc": "2.0", "id": 1, "result": { ... } }
```

**Error:**
```json
{ "jsonrpc": "2.0", "id": 1, "error": { "code": -32602, "message": "Invalid params" } }
```

### Error Codes

| Code | Meaning |
|------|---------|
| `-32700` | Parse error — invalid JSON |
| `-32600` | Invalid request |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32603` | Internal error |
| `-32099` to `-32000` | Server error (exception from handler) |

---

## Plugin Namespaces

| Namespace | Status | Description |
|-----------|--------|-------------|
| `database_api` | Active | Block, account, chain state queries |
| `network_broadcast_api` | Active | Transaction and block broadcast |
| `validator_api` | Active | Validator queries |
| `account_by_key` | Active | Reverse key lookup |
| `account_history` | Active | Per-account operation history |
| `operation_history` | Active | Block operation queries |
| `committee_api` | Active | Committee request queries |
| `invite_api` | Active | Invite queries |
| `paid_subscription_api` | Active | Subscription queries |
| `custom_protocol_api` | Active | Custom protocol metadata |
| `auth_util` | Active | Authority verification |
| `block_info` | Active | Extended block info |
| `raw_block` | Active | Raw block export |

---

## `database_api` Methods

| Method | Description |
|--------|-------------|
| `get_block_header(block_num)` | Block header for given height |
| `get_block(block_num)` | Full signed block |
| `get_irreversible_block_header(block_num)` | Block header if irreversible |
| `get_irreversible_block(block_num)` | Full block if irreversible |
| `set_block_applied_callback(callback)` | WebSocket: subscribe to new blocks |
| `get_config()` | Compile-time chain constants |
| `get_dynamic_global_properties()` | Current chain state |
| `get_chain_properties()` | Median validator chain properties |
| `get_hardfork_version()` | Current hardfork version string |
| `get_next_scheduled_hardfork()` | Next pending hardfork info |
| `get_accounts(names[])` | Full account objects |
| `lookup_account_names(names[])` | Same as get_accounts but nullable |
| `lookup_accounts(lower_bound, limit)` | Paginated account name list |
| `get_account_count()` | Total registered accounts |
| `get_master_history(account)` | Master key change history |
| `get_recovery_request(account)` | Pending account recovery request |
| `get_escrow(from, escrow_id)` | Escrow object |
| `get_withdraw_routes(account, type)` | Vesting withdrawal routes (`"incoming"` / `"outgoing"` / `"all"`) |
| `get_vesting_delegations(account, from, limit, type)` | Delegations (`"delegated"` / `"received"`) |
| `get_expiring_vesting_delegations(account, from, limit)` | Delegations in return window |
| `get_transaction_hex(trx)` | Hex-encoded serialized transaction |
| `get_required_signatures(trx, available_keys[])` | Minimal key set to sign |
| `get_potential_signatures(trx)` | All keys that could sign |
| `verify_authority(trx)` | `true` if fully signed |
| `verify_account_authority(name, keys[])` | `true` if keys satisfy authority |
| `get_database_info()` | Chainbase memory usage stats |
| `get_proposed_transactions(account, from, limit)` | Proposals requiring account approval |
| `get_accounts_on_sale(from, limit)` | Accounts listed for direct sale |
| `get_accounts_on_auction(from, limit)` | Accounts listed for auction |
| `get_subaccounts_on_sale(from, limit)` | Subaccount creation rights for sale |

---

## `network_broadcast_api` Methods

| Method | Description |
|--------|-------------|
| `broadcast_transaction(trx)` | Broadcast (async) |
| `broadcast_transaction_synchronous(trx)` | Broadcast and wait for inclusion in a block |
| `broadcast_transaction_with_callback(callback, trx)` | Broadcast with WebSocket callback |
| `broadcast_block(block)` | Broadcast a signed block (validators) |

---

## `validator_api` Methods

| Method | Description |
|--------|-------------|
| `get_active_validators()` | Current active validator set (21 accounts) |
| `get_validator_schedule()` | Full validator schedule object |
| `get_validators(ids[])` | Validators by internal IDs |
| `get_validator_by_account(account)` | Validator object for an account |
| `get_validators_by_vote(lower_bound, limit)` | Validators ranked by vote weight |
| `get_validators_by_counted_vote(lower_bound, limit)` | Validators by counted votes |
| `get_validator_count()` | Total registered validators |
| `lookup_validator_accounts(lower_bound, limit)` | List validator account names |

---

## `account_history` Methods

### `get_account_history(account, from, limit)`

Returns operations involving `account`. `from = -1` starts from the most recent.

```json
{
  "method": "account_history.get_account_history",
  "params": ["alice", -1, 100]
}
```

Returns a map of `{ sequence: { trx_id, block, op: [type_id, data] } }`.

---

## `operation_history` Methods

| Method | Description |
|--------|-------------|
| `get_ops_in_block(block_num, only_virtual)` | Operations in a block |
| `get_transaction(trx_id)` | Transaction by ID |

---

## `committee_api` Methods

| Method | Description |
|--------|-------------|
| `get_committee_request(request_id)` | Request details and status |
| `get_committee_request_votes(request_id)` | Votes on a request |
| `get_committee_requests_list(from, limit)` | List of request IDs |

---

## `invite_api` Methods

| Method | Description |
|--------|-------------|
| `get_invites_list(from, limit)` | All active invite IDs |
| `get_invite_by_id(id)` | Invite by internal ID |
| `get_invite_by_key(public_key)` | Invite by public key |

---

## `paid_subscription_api` Methods

| Method | Description |
|--------|-------------|
| `get_paid_subscriptions(from, limit)` | All subscription offerings |
| `get_paid_subscription_options(account)` | Subscription config for account |
| `get_paid_subscription_status(subscriber, account)` | Subscription status |
| `get_active_paid_subscriptions(subscriber, from, limit)` | Active subscriptions |
| `get_inactive_paid_subscriptions(subscriber, from, limit)` | Expired subscriptions |

---

## WebSocket Subscriptions

Only available over a persistent WebSocket connection.

| Method | Callback data |
|--------|---------------|
| `database_api.set_block_applied_callback` | Block header on every applied block |
| `database_api.set_pending_transaction_callback` | Transaction when entering pending pool |
| `database_api.cancel_all_subscriptions` | Unsubscribe all |

---

## Recommended Plugin Sets

**Minimal API node:**
```ini
plugin = chain json_rpc webserver p2p
plugin = database_api network_broadcast_api
```

**Full API node (add):**
```ini
plugin = validator_api account_by_key account_history operation_history
plugin = committee_api invite_api paid_subscription_api
```

**Validator node (add):**
```ini
plugin = validator snapshot
```

---

See also: [Database API](../plugins/database-api.md), [Webserver Plugin](../plugins/webserver.md), [Operations Overview](../protocol/operations/overview.md).
