# VIZ Blockchain — Common Data Types

This document describes all shared data types used across VIZ protocol operations and virtual operations. These types appear as field types throughout operation structures.

---

## Primitive Types

| C++ type | JSON representation | Description |
|---|---|---|
| `string` | `string` | UTF-8 string |
| `bool` | `boolean` | true / false |
| `uint8_t` | `integer` | Unsigned 8-bit integer |
| `uint16_t` | `integer` | Unsigned 16-bit integer (0–65535) |
| `int16_t` | `integer` | Signed 16-bit integer (-32768–32767) |
| `uint32_t` | `integer` | Unsigned 32-bit integer |
| `int32_t` | `integer` | Signed 32-bit integer |
| `uint64_t` | `string` or `integer` | Unsigned 64-bit integer (use string in JS to avoid overflow) |
| `int64_t` | `string` or `integer` | Signed 64-bit integer |
| `share_type` | `integer` | Alias for `safe<int64_t>` — token satoshi amount |
| `time_point_sec` | `string` | ISO 8601 UTC datetime: `"2024-01-15T12:00:00"` |

---

## `account_name_type`

Fixed-length string (max 32 bytes). Must comply with domain-name rules:
- Dot-separated labels, each label 3+ characters
- Begins with a letter, ends with letter or digit
- Only lowercase letters, digits, hyphens
- Min length: `CHAIN_MIN_ACCOUNT_NAME_LENGTH` (2)
- Max length: `CHAIN_MAX_ACCOUNT_NAME_LENGTH` (16)

**JSON:** plain string, e.g. `"alice"`, `"alice.bob"`

---

## `public_key_type`

A secp256k1 compressed public key encoded in base58check with `VIZ` prefix.

**JSON:** string, e.g. `"VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"`

### Checklist
- [ ] Prefix must be `VIZ` (not `STM`, `GLS`, etc.)
- [ ] 33-byte compressed public key + 4-byte checksum = 37 bytes base58-encoded
- [ ] Validate checksum on deserialization

---

## `asset`

Represents a token amount with its symbol.

```
{
  "amount": integer,   // satoshi value (int64)
  "symbol": integer    // asset_symbol_type (uint64)
}
```

**However**, in JSON API the asset is typically serialized as a string:
```
"10.000 VIZ"
"5.000000 SHARES"
```

### Asset Symbols

| Symbol name | String | Decimals | Description |
|---|---|---|---|
| `TOKEN_SYMBOL` | `VIZ` | 3 | Main liquid token |
| `SHARES_SYMBOL` | `SHARES` | 6 | Vesting shares (staked VIZ) |

### Checklist
- [ ] Parse/format amount with correct decimal places (VIZ=3, SHARES=6)
- [ ] When constructing `asset` for operations, use the string format: `"10.000 VIZ"`
- [ ] Validate symbol matches expected token type per operation field

---

## `authority`

Multi-signature authority structure controlling an account's permission level.

```json
{
  "weight_threshold": 1,
  "account_auths": [
    ["alice", 1]
  ],
  "key_auths": [
    ["VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9", 1]
  ]
}
```

### Fields

| Field | Type | Description |
|---|---|---|
| `weight_threshold` | `uint32_t` | Minimum total weight required to satisfy authority |
| `account_auths` | `[[account_name, weight], ...]` | Account-based signers |
| `key_auths` | `[[public_key, weight], ...]` | Key-based signers |

### Authority Levels

| Level | Used for |
|---|---|
| `master` | Highest security — changing keys, account recovery |
| `active` | Token operations — transfer, vesting, witness voting |
| `regular` | Social operations — content, awards, committee voting |

### Checklist
- [ ] Sum of weights for satisfied keys/accounts must be >= `weight_threshold`
- [ ] `account_auths` entries are `[string, uint16]` pairs
- [ ] `key_auths` entries are `[string, uint16]` pairs (public key as VIZ-prefixed base58)
- [ ] Empty authority = `{ "weight_threshold": 0, "account_auths": [], "key_auths": [] }`

---

## `beneficiary_route_type`

Specifies a beneficiary account and their share weight for content rewards.

```json
{
  "account": "alice",
  "weight": 2500
}
```

| Field | Type | Description |
|---|---|---|
| `account` | `account_name_type` | Beneficiary account name |
| `weight` | `uint16_t` | Weight in basis points (100% = 10000) |

### Checklist
- [ ] Sum of all beneficiary weights must not exceed 10000 (100%)
- [ ] Beneficiaries array must be sorted by account name (ascending)
- [ ] Each beneficiary account must exist on-chain

---

## `extensions_type`

Currently unused — always an empty array `[]`.

```json
"extensions": []
```

---

## `versioned_chain_properties`

A static variant holding one of the chain property versions. The variant is serialized as a 2-element array `[type_index, object]`.

| Index | Type |
|---|---|
| 0 | `chain_properties_init` |
| 1 | `chain_properties_hf4` |
| 2 | `chain_properties_hf6` |
| 3 | `chain_properties_hf9` |

Example (hf9 = index 3):
```json
[3, {
  "account_creation_fee": "1.000 VIZ",
  "maximum_block_size": 65536,
  "create_account_delegation_ratio": 10,
  "create_account_delegation_time": 2592000,
  "min_delegation": "1.000 VIZ",
  "min_curation_percent": 0,
  "max_curation_percent": 10000,
  "bandwidth_reserve_percent": 1000,
  "bandwidth_reserve_below": "1.000000 SHARES",
  "flag_energy_additional_cost": 1000,
  "vote_accounting_min_rshares": 0,
  "committee_request_approve_min_percent": 1000,
  "inflation_witness_percent": 2000,
  "inflation_ratio_committee_vs_reward_fund": 1000,
  "inflation_recalc_period": 28800,
  "data_operations_cost_additional_bandwidth": 0,
  "witness_miss_penalty_percent": 100,
  "witness_miss_penalty_duration": 86400,
  "create_invite_min_balance": "1.000 VIZ",
  "committee_create_request_fee": "1.000 VIZ",
  "create_paid_subscription_fee": "1.000 VIZ",
  "account_on_sale_fee": "10.000 VIZ",
  "subaccount_on_sale_fee": "1.000 VIZ",
  "witness_declaration_fee": "1.000 VIZ",
  "withdraw_intervals": 28
}]
```

---

## `chain_properties_init` Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `account_creation_fee` | `asset` (VIZ) | `1.000 VIZ` | Fee to create a new account |
| `maximum_block_size` | `uint32_t` | 131072 | Max block size in bytes |
| `create_account_delegation_ratio` | `uint32_t` | 10 | Ratio of delegated SHARES on account creation |
| `create_account_delegation_time` | `uint32_t` | 2592000 | Minimum delegation time (seconds) |
| `min_delegation` | `asset` (VIZ) | `1.000 VIZ` | Minimum delegation amount |
| `min_curation_percent` | `int16_t` | 0 | Min curation reward percent (basis points) |
| `max_curation_percent` | `int16_t` | 10000 | Max curation reward percent (basis points) |
| `bandwidth_reserve_percent` | `int16_t` | 1000 | % of bandwidth reserved for low-stake accounts |
| `bandwidth_reserve_below` | `asset` (SHARES) | `1.000000 SHARES` | Threshold for bandwidth reserve |
| `flag_energy_additional_cost` | `int16_t` | 1000 | Extra energy cost for flag/downvote |
| `vote_accounting_min_rshares` | `uint32_t` | 0 | Min rshares for payout accounting |
| `committee_request_approve_min_percent` | `int16_t` | 1000 | Min approval % for committee requests |

## Additional fields in `chain_properties_hf4`

| Field | Type | Description |
|---|---|---|
| `inflation_witness_percent` | `int16_t` | Witness reward % from block inflation |
| `inflation_ratio_committee_vs_reward_fund` | `int16_t` | Ratio committee/reward fund |
| `inflation_recalc_period` | `uint32_t` | Blocks per inflation recalc |

## Additional fields in `chain_properties_hf6`

| Field | Type | Description |
|---|---|---|
| `data_operations_cost_additional_bandwidth` | `uint32_t` | Extra bandwidth % for data operations |
| `witness_miss_penalty_percent` | `int16_t` | Vote penalty % for missed block |
| `witness_miss_penalty_duration` | `uint32_t` | Duration of miss penalty (seconds) |

## Additional fields in `chain_properties_hf9`

| Field | Type | Description |
|---|---|---|
| `create_invite_min_balance` | `asset` (VIZ) | Min balance to create invite |
| `committee_create_request_fee` | `asset` (VIZ) | Fee to create committee request |
| `create_paid_subscription_fee` | `asset` (VIZ) | Fee to create paid subscription |
| `account_on_sale_fee` | `asset` (VIZ) | Fee to list account for sale |
| `subaccount_on_sale_fee` | `asset` (VIZ) | Fee to list subaccounts for sale |
| `witness_declaration_fee` | `asset` (VIZ) | Fee to declare as witness |
| `withdraw_intervals` | `uint16_t` | Number of withdraw intervals |

---

## Operation Type Indices

The `operation` is a `static_variant`. When serialized, it is a 2-element array: `[type_id, operation_object]`.

### Regular Operations

| ID | Operation Name |
|---|---|
| 0 | `vote_operation` *(deprecated)* |
| 1 | `content_operation` *(deprecated)* |
| 2 | `transfer_operation` |
| 3 | `transfer_to_vesting_operation` |
| 4 | `withdraw_vesting_operation` |
| 5 | `account_update_operation` |
| 6 | `witness_update_operation` |
| 7 | `account_witness_vote_operation` |
| 8 | `account_witness_proxy_operation` |
| 9 | `delete_content_operation` *(deprecated)* |
| 10 | `custom_operation` |
| 11 | `set_withdraw_vesting_route_operation` |
| 12 | `request_account_recovery_operation` |
| 13 | `recover_account_operation` |
| 14 | `change_recovery_account_operation` |
| 15 | `escrow_transfer_operation` |
| 16 | `escrow_dispute_operation` |
| 17 | `escrow_release_operation` |
| 18 | `escrow_approve_operation` |
| 19 | `delegate_vesting_shares_operation` |
| 20 | `account_create_operation` |
| 21 | `account_metadata_operation` |
| 22 | `proposal_create_operation` |
| 23 | `proposal_update_operation` |
| 24 | `proposal_delete_operation` |
| 25 | `chain_properties_update_operation` |

### Virtual Operations

| ID | Operation Name |
|---|---|
| 26 | `author_reward_operation` |
| 27 | `curation_reward_operation` |
| 28 | `content_reward_operation` |
| 29 | `fill_vesting_withdraw_operation` |
| 30 | `shutdown_witness_operation` |
| 31 | `hardfork_operation` |
| 32 | `content_payout_update_operation` |
| 33 | `content_benefactor_reward_operation` |
| 34 | `return_vesting_delegation_operation` |
| 35 | `committee_worker_create_request_operation` |
| 36 | `committee_worker_cancel_request_operation` |
| 37 | `committee_vote_request_operation` |
| 38 | `committee_cancel_request_operation` *(virtual)* |
| 39 | `committee_approve_request_operation` *(virtual)* |
| 40 | `committee_payout_request_operation` *(virtual)* |
| 41 | `committee_pay_request_operation` *(virtual)* |
| 42 | `witness_reward_operation` *(virtual)* |
| 43 | `create_invite_operation` |
| 44 | `claim_invite_balance_operation` |
| 45 | `invite_registration_operation` |
| 46 | `versioned_chain_properties_update_operation` |
| 47 | `award_operation` |
| 48 | `receive_award_operation` *(virtual)* |
| 49 | `benefactor_award_operation` *(virtual)* |
| 50 | `set_paid_subscription_operation` |
| 51 | `paid_subscribe_operation` |
| 52 | `paid_subscription_action_operation` *(virtual)* |
| 53 | `cancel_paid_subscription_operation` *(virtual)* |
| 54 | `set_account_price_operation` |
| 55 | `set_subaccount_price_operation` |
| 56 | `buy_account_operation` |
| 57 | `account_sale_operation` *(virtual)* |
| 58 | `use_invite_balance_operation` |
| 59 | `expire_escrow_ratification_operation` *(virtual)* |
| 60 | `fixed_award_operation` |
| 61 | `target_account_sale_operation` |
| 62 | `bid_operation` *(virtual)* |
| 63 | `outbid_operation` *(virtual)* |
