# CLI Wallet

The `cli_wallet` executable provides an interactive command-line interface for managing accounts, signing and broadcasting transactions, and querying the blockchain.

---

## Connection

```bash
cli_wallet --server-rpc-endpoint="ws://127.0.0.1:8091"
```

On first run, set a password:
```
new >>> set_password "yourpassword"
```

Then unlock:
```
locked >>> unlock "yourpassword"
```

---

## Wallet Management

| Command | Description |
|---------|-------------|
| `is_new` | Returns `true` if no password set yet |
| `is_locked` | Returns `true` if wallet is locked |
| `lock` | Lock the wallet |
| `unlock "password"` | Unlock the wallet |
| `set_password "password"` | Set or change the password |
| `load_wallet_file "file.json"` | Load a wallet file (`""` = reload current) |
| `save_wallet_file "file.json"` | Save wallet to file |
| `set_transaction_expiration 60` | Set transaction TTL in seconds |
| `quit` | Exit the wallet |
| `help` | List all commands |
| `gethelp "command"` | Detailed help for one command |

---

## Key Management

| Command | Description |
|---------|-------------|
| `import_key "5K..."` | Import a WIF private key |
| `suggest_brain_key` | Generate a suggested brain key with public/private pair |
| `list_keys` | List all private keys (WIF) in the wallet |
| `get_private_key "VIZpubkey..."` | Get WIF for a known public key |
| `get_private_key_from_password "account" "role" "password"` | Derive key from credentials |
| `normalize_brain_key "words..."` | Normalize a brain key string |

---

## Querying

| Command | Description |
|---------|-------------|
| `info` | Current blockchain state |
| `database_info` | Database object statistics |
| `get_block 1000000` | Block data |
| `get_ops_in_block 1000000 false` | Operations in block (`true` = virtual only) |
| `get_active_validators` | Active validator set |
| `get_account "alice"` | Account object |
| `list_accounts "" 100` | Paginated account list |
| `list_my_accounts` | Accounts with keys in wallet |
| `get_account_history "alice" -1 100` | Last 100 operations for alice |
| `get_transaction "txid..."` | Transaction by ID |
| `get_master_history "alice"` | Master key change history |
| `get_withdraw_routes "alice" "all"` | Withdrawal routes (`"incoming"`, `"outgoing"`, `"all"`) |
| `get_proposed_transactions "alice" 0 100` | Proposals requiring alice's approval |

---

## Account Operations

| Command | Description |
|---------|-------------|
| `create_account "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" true` | Create account with auto-generated keys |
| `create_account_with_keys "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" "VIZmaster..." "VIZactive..." "VIZregular..." "VIZmemo..." true` | Create account with specified keys |
| `update_account "account" "{}" "VIZm..." "VIZa..." "VIZr..." "VIZmemo..." true` | Update all keys and metadata |
| `update_account_auth_key "account" "active" "VIZnewkey..." 1 true` | Add key to authority (weight 0 = remove) |
| `update_account_auth_account "account" "active" "guardian" 1 true` | Add account to authority |
| `update_account_auth_threshold "account" "active" 2 true` | Set authority weight threshold |
| `update_account_meta "account" "{\"key\":\"value\"}" true` | Update JSON metadata (regular auth) |
| `update_account_memo_key "account" "VIZnewmemo..." true` | Update memo key |
| `delegate_vesting_shares "alice" "bob" "100.000000 SHARES" true` | Delegate SHARES (0 = remove) |

---

## Transfer and Vesting

| Command | Description |
|---------|-------------|
| `transfer "alice" "bob" "10.000 VIZ" "memo" true` | Transfer VIZ (prefix memo with `#` for encrypted) |
| `transfer_to_vesting "alice" "alice" "100.000 VIZ" true` | Stake VIZ as SHARES |
| `withdraw_vesting "alice" "100.000000 SHARES" true` | Start power-down (0 = cancel) |
| `set_withdraw_vesting_route "alice" "bob" 5000 false true` | Route 50% of withdrawals to bob as VIZ |

---

## Validator Operations

| Command | Description |
|---------|-------------|
| `list_validators "" 100` | List validators |
| `get_validator "validatorname"` | Validator object |
| `update_validator "myvalidator" "https://url" "VIZsigningkey..." true` | Register/update validator |
| `update_chain_properties "myvalidator" {...} true` | Vote on chain properties (init format) |
| `versioned_update_chain_properties "myvalidator" {...} true` | Vote on versioned chain properties (hf9 format) |
| `vote_for_validator "alice" "myvalidator" true true` | Vote for validator (`false` = remove vote) |
| `set_voting_proxy "alice" "proxy" true` | Set validator vote proxy (`""` = remove) |

---

## Escrow Operations

| Command | Description |
|---------|-------------|
| `escrow_transfer "alice" "bob" "agent" 1 "100.000 VIZ" "1.000 VIZ" "2024-06-01T00:00:00" "2024-07-01T00:00:00" "{}" true` | Create escrow |
| `escrow_approve "alice" "bob" "agent" "bob" 1 true true` | Approve escrow (who = `"bob"` or `"agent"`) |
| `escrow_dispute "alice" "bob" "agent" "alice" 1 true` | Raise dispute (who = `"alice"` or `"bob"`) |
| `escrow_release "alice" "bob" "agent" "agent" "bob" 1 "100.000 VIZ" true` | Release funds |

---

## Recovery Operations

| Command | Description |
|---------|-------------|
| `request_account_recovery "recovery" "victim" {"weight_threshold":1,...} true` | Request recovery as recovery account |
| `recover_account "victim" {"recent_master_auth"} {"new_master_auth"} true` | Confirm recovery |
| `change_recovery_account "account" "new_recovery" true` | Change recovery account (30-day delay) |

---

## Committee Operations

| Command | Description |
|---------|-------------|
| `committee_worker_create_request "creator" "https://url" "worker" "100.000 VIZ" "500.000 VIZ" 604800 true` | Create funding request |
| `committee_worker_cancel_request "creator" 123 true` | Cancel request |
| `committee_vote_request "voter" 123 10000 true` | Vote (+10000 = full support, -10000 = full oppose, 0 = remove) |

---

## Invite Operations

| Command | Description |
|---------|-------------|
| `create_invite "creator" "10.000 VIZ" "VIZinvitekey..." true` | Create invite |
| `claim_invite_balance "initiator" "receiver" "5Kinvitesecret..." true` | Claim invite balance |
| `invite_registration "initiator" "newaccount" "5Kinvitesecret..." "VIZnewaccountkey..." true` | Create account from invite |
| `use_invite_balance "initiator" "receiver" "5Kinvitesecret..." true` | Use invite (may vest to SHARES) |

---

## Award Operations

| Command | Description |
|---------|-------------|
| `award "alice" "bob" 1000 0 "Great work!" [] true` | Award with energy-based reward |
| `fixed_award "alice" "bob" "10.000000 SHARES" 5000 0 "Reward" [] true` | Award fixed SHARES amount |

Beneficiary format: `[{"account":"charlie","weight":2000}]`

---

## Subscription Operations

| Command | Description |
|---------|-------------|
| `set_paid_subscription "account" "https://url" 3 "10.000 VIZ" 30 true` | Create subscription (3 levels, 10 VIZ/period, 30-day period) |
| `paid_subscribe "subscriber" "account" 2 "20.000 VIZ" 1 true true` | Subscribe to level 2 |

---

## Account Market

| Command | Description |
|---------|-------------|
| `set_account_price "account" "account" "100.000 VIZ" true true` | List for sale |
| `set_subaccount_price "account" "account" "50.000 VIZ" true true` | List subaccount creation for sale |
| `buy_account "buyer" "account" "100.000 VIZ" "VIZnewkey..." "0.000 VIZ" true` | Buy account |
| `target_account_sale "account" "account" "targetbuyer" "100.000 VIZ" true true` | Targeted sale |

---

## Custom Operation

```bash
custom [] ["alice"] "my_app" "{\"action\":\"follow\",\"target\":\"bob\"}" true
```

Parameters: `required_active_auths` `required_regular_auths` `id` `json` `broadcast`

---

## Transaction Builder

Build and sign custom multi-operation transactions:

```bash
begin_builder_transaction           # Returns handle (e.g. 0)
add_operation_to_builder_transaction 0 [2,{"from":"alice","to":"bob","amount":"10.000 VIZ","memo":""}]
sign_builder_transaction 0 true     # Sign and broadcast
```

| Command | Description |
|---------|-------------|
| `begin_builder_transaction` | Start a new transaction (returns handle) |
| `add_operation_to_builder_transaction handle [type_id, op]` | Add operation |
| `replace_operation_in_builder_transaction handle idx [type_id, op]` | Replace operation |
| `preview_builder_transaction handle` | Preview transaction JSON |
| `sign_builder_transaction handle broadcast` | Sign (and optionally broadcast) |
| `propose_builder_transaction handle author title memo expiry review broadcast` | Wrap in proposal |
| `remove_builder_transaction handle` | Discard |
| `get_prototype_operation "transfer_operation"` | Get empty operation template |
| `serialize_transaction {trx}` | Get hex serialization |
| `sign_transaction {trx} broadcast` | Sign arbitrary transaction |

---

## NS DNS Helpers

Store DNS records in account metadata:

```bash
ns_set_records "myaccount" {"a_records":["188.120.231.153"],"ssl_hash":"4a4613...","ttl":28800} true
ns_get_summary "myaccount"
ns_extract_a_records "myaccount"
ns_remove_records "myaccount" true
```

Validation helpers: `ns_validate_ipv4`, `ns_validate_sha256_hash`, `ns_validate_ttl`, `ns_validate_ssl_txt_record`, `ns_validate_metadata`, `ns_create_metadata`.

---

## Private Messaging

```bash
get_encrypted_memo "alice" "bob" "#secret message"
decrypt_memo "#encrypteddata..."
get_inbox "myaccount" "2024-01-15T00:00:00" 100 0
get_outbox "myaccount" "2024-01-15T00:00:00" 100 0
```

---

See also: [JSON-RPC API](./json-rpc.md), [Operations Overview](../protocol/operations/overview.md), [Data Types](../protocol/data-types.md).
