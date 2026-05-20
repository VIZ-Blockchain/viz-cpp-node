# Database API

The `database_api` plugin exposes read-only JSON-RPC methods for querying blockchain state: blocks, accounts, chain properties, delegation, authority verification, and governance.

**Source:** [plugins/database_api/](../../plugins/database_api/)

---

## Dependencies

```
json_rpc::plugin, chain::plugin
```

---

## Request Format

All methods use JSON-RPC 2.0 over HTTP POST or WebSocket:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "database_api.<method_name>",
  "params": [<arg1>, <arg2>, ...]
}
```

---

## Blocks and Transactions

### `get_block_header(block_num)`

Returns the block header for the given block number, or `null` if not found.

```json
{ "method": "database_api.get_block_header", "params": [12345678] }
```

**Returns:** `block_header` — `previous`, `timestamp`, `witness`, `transaction_merkle_root`, `extensions`.

---

### `get_block(block_num)`

Returns the full signed block including all transactions.

```json
{ "method": "database_api.get_block", "params": [12345678] }
```

**Returns:** `signed_block` — all header fields plus `transactions[]` with full operation data, and the response additionally includes `block_id`, `signing_key`, `transaction_ids[]`.

---

### `get_irreversible_block_header(block_num)`

Same as `get_block_header` but only returns the block if it has reached LIB (is irreversible).

---

### `get_irreversible_block(block_num)`

Same as `get_block` but only returns the block if it has reached LIB.

---

### `set_block_applied_callback(callback)`

Register a WebSocket callback to be notified on every applied block. The callback receives the block header as a JSON variant.

**WebSocket only.** Unsubscribe with `cancel_all_subscriptions`.

---

## Chain Globals

### `get_config()`

Returns compile-time constants: chain ID, token symbols, block interval, max block size, voting periods, and all `CHAIN_*` constants.

```json
{ "method": "database_api.get_config", "params": [] }
```

---

### `get_dynamic_global_properties()`

Returns live chain state: current head block number and ID, time, head validator, total vesting shares, participation rate, DPO fund balances, and more.

```json
{ "method": "database_api.get_dynamic_global_properties", "params": [] }
```

Key fields: `head_block_number`, `head_block_id`, `time`, `current_witness`, `total_vesting_shares`, `total_vesting_fund_viz`, `committee_fund`, `last_irreversible_block_num`, `participation_count`.

---

### `get_chain_properties()`

Returns the current on-chain governance parameters (set via `chain_properties_update_operation`): minimum account creation fee, maximum block size, create account with viz fee, bandwidth reserve percent, and reward parameters.

```json
{ "method": "database_api.get_chain_properties", "params": [] }
```

---

### `get_hardfork_version()`

Returns the currently active hardfork version string (e.g., `"1.0.0"`).

```json
{ "method": "database_api.get_hardfork_version", "params": [] }
```

---

### `get_next_scheduled_hardfork()`

Returns the version and scheduled live time of the next pending hardfork.

```json
{ "method": "database_api.get_next_scheduled_hardfork", "params": [] }
```

**Returns:** `{ "hf_version": "1.0.0", "live_time": "2025-01-01T00:00:00" }`

---

## Accounts

### `get_accounts(account_names)`

Returns full account objects for the given list of account names.

```json
{ "method": "database_api.get_accounts", "params": [["alice", "bob"]] }
```

**Returns:** Array of `account_api_object` — name, balance, vesting shares, received vesting, delegated vesting, keys, recovery account, created, post count, voting power, and more.

---

### `lookup_account_names(account_names)`

Same as `get_accounts` but returns `null` for accounts that do not exist.

```json
{ "method": "database_api.lookup_account_names", "params": [["alice", "nonexistent"]] }
```

**Returns:** Array of `optional<account_api_object>` — `null` for missing accounts.

---

### `lookup_accounts(lower_bound_name, limit)`

Returns a set of account names starting from `lower_bound_name`, up to `limit` results (max 1000). Useful for paginated account enumeration.

```json
{ "method": "database_api.lookup_accounts", "params": ["alice", 100] }
```

**Returns:** Set of account name strings.

---

### `get_account_count()`

Returns the total number of registered accounts.

```json
{ "method": "database_api.get_account_count", "params": [] }
```

---

## Account State

### `get_master_history(account)`

Returns the history of master authority changes for the given account.

```json
{ "method": "database_api.get_master_history", "params": ["alice"] }
```

**Returns:** Array of `master_authority_history_api_object` — `account`, `previous_master_authority`, `last_valid_time`.

---

### `get_recovery_request(account)`

Returns the pending account recovery request for the given account, if any.

```json
{ "method": "database_api.get_recovery_request", "params": ["alice"] }
```

**Returns:** `optional<account_recovery_request_api_object>` — `account_to_recover`, `new_master_authority`, `expires`.

---

### `get_escrow(from, escrow_id)`

Returns the escrow transfer object for the given sender and escrow ID.

```json
{ "method": "database_api.get_escrow", "params": ["alice", 1] }
```

**Returns:** `optional<escrow_api_object>` — all escrow fields including `from`, `to`, `agent`, `ratification_deadline`, `escrow_expiration`, amounts, and approval status.

---

### `get_withdraw_routes(account, type)`

Returns the vesting power withdrawal routes for an account. `type` is one of `"incoming"`, `"outgoing"`, or `"all"`.

```json
{ "method": "database_api.get_withdraw_routes", "params": ["alice", "outgoing"] }
```

**Returns:** Array of `{ "from_account", "to_account", "percent", "auto_vest" }`.

---

### `get_vesting_delegations(account, from, limit, type)`

Returns vesting delegations for an account. `type` is `"delegated"` (delegations made by this account) or `"received"` (delegations received).

```json
{ "method": "database_api.get_vesting_delegations", "params": ["alice", "", 100, "delegated"] }
```

**Returns:** Array of `vesting_delegation_api_object` — `delegator`, `delegatee`, `vesting_shares`, `min_delegation_time`.

---

### `get_expiring_vesting_delegations(account, from, limit)`

Returns vesting delegation expiration entries for an account — delegations that have been retracted and are waiting for the return window.

```json
{ "method": "database_api.get_expiring_vesting_delegations", "params": ["alice", "1970-01-01T00:00:00", 100] }
```

**Returns:** Array of `vesting_delegation_expiration_api_object` — `delegator`, `vesting_shares`, `expiration`.

---

## Authority and Transaction Validation

### `get_transaction_hex(trx)`

Returns the hex-encoded serialized binary form of a transaction. Useful for signing and broadcasting.

```json
{ "method": "database_api.get_transaction_hex", "params": [{ ...transaction object... }] }
```

**Returns:** Hex string.

---

### `get_required_signatures(trx, available_keys)`

Given a partially-signed transaction and the set of public keys available to the signer, returns the minimal subset of keys that must sign to authorize the transaction.

```json
{
  "method": "database_api.get_required_signatures",
  "params": [{ ...trx... }, ["VIZ5...", "VIZ6..."]]
}
```

**Returns:** Set of public key strings.

---

### `get_potential_signatures(trx)`

Returns all public keys that could potentially sign the transaction (across all involved accounts and authority levels). Use this to pre-filter a wallet's key set before calling `get_required_signatures`.

```json
{ "method": "database_api.get_potential_signatures", "params": [{ ...trx... }] }
```

**Returns:** Set of public key strings.

---

### `verify_authority(trx)`

Returns `true` if the transaction has all required signatures; throws an error with a description of what is missing otherwise.

```json
{ "method": "database_api.verify_authority", "params": [{ ...signed_trx... }] }
```

---

### `verify_account_authority(name, signers)`

Returns `true` if the given set of public keys has sufficient authority to act on behalf of `name`.

```json
{ "method": "database_api.verify_account_authority", "params": ["alice", ["VIZ5..."]] }
```

---

## Database Info

### `get_database_info()`

Returns chainbase shared memory usage statistics.

```json
{ "method": "database_api.get_database_info", "params": [] }
```

**Returns:**
```json
{
  "total_size": 4294967296,
  "free_size": 1073741824,
  "reserved_size": 0,
  "used_size": 3221225472,
  "index_list": [
    { "name": "account_object", "record_count": 52341 },
    ...
  ]
}
```

---

## Governance

### `get_proposed_transactions(account, from, limit)`

Returns governance proposals that require approval from `account`.

```json
{ "method": "database_api.get_proposed_transactions", "params": ["alice", 0, 100] }
```

**Returns:** Array of `proposal_api_object` — full proposal details including required approvals, expiration, and operation list.

---

## Account Market

### `get_accounts_on_sale(from, limit)`

Returns accounts currently listed for sale (direct sale, not auction).

```json
{ "method": "database_api.get_accounts_on_sale", "params": [0, 100] }
```

**Returns:** Array of `account_on_sale_api_object` — `account`, `account_seller`, `account_offer_price`, `account_on_sale_start_time`, `target_buyer`.

---

### `get_accounts_on_auction(from, limit)`

Returns accounts listed for auction.

```json
{ "method": "database_api.get_accounts_on_auction", "params": [0, 100] }
```

**Returns:** Array of `account_on_sale_api_object` — same as above plus `current_bid`, `current_bidder`, `current_bidder_key`, `last_bid`.

---

### `get_subaccounts_on_sale(from, limit)`

Returns account namespace registrations available for sale (subaccount creation rights).

```json
{ "method": "database_api.get_subaccounts_on_sale", "params": [0, 100] }
```

**Returns:** Array of `subaccount_on_sale_api_object` — `account`, `subaccount_seller`, `subaccount_offer_price`.

---

## Error Codes

| Code | Meaning |
|------|---------|
| `-32700` | Parse error — invalid JSON |
| `-32600` | Invalid request — missing required fields |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32603` | Internal error |
| `-32099` to `-32000` | Server error (exception from method handler) |

---

See also: [Plugin Overview](./overview.md), [validator_api methods](./overview.md#validator_api), [JSON-RPC API](../api/json-rpc.md).
