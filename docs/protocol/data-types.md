# Common Data Types

All shared data types used across VIZ Ledger protocol operations and virtual operations.

---

## Primitive Types

| C++ type | JSON representation | Description |
|----------|---------------------|-------------|
| `string` | `string` | UTF-8 string |
| `bool` | `boolean` | `true` / `false` |
| `uint8_t` | `integer` | Unsigned 8-bit integer |
| `uint16_t` | `integer` | Unsigned 16-bit integer (0–65535) |
| `int16_t` | `integer` | Signed 16-bit integer (−32768–32767) |
| `uint32_t` | `integer` | Unsigned 32-bit integer |
| `int32_t` | `integer` | Signed 32-bit integer |
| `uint64_t` | `string` or `integer` | Unsigned 64-bit integer — use string in JavaScript to avoid overflow |
| `int64_t` | `string` or `integer` | Signed 64-bit integer |
| `share_type` | `integer` | Alias for `safe<int64_t>` — token amount in satoshi units |
| `time_point_sec` | `string` | ISO 8601 UTC datetime: `"2024-01-15T12:00:00"` (no timezone suffix) |

---

## `account_name_type`

A fixed-length string (max 16 bytes) identifying an account. Rules:

- Dot-separated labels; each label is at least 3 characters.
- Begins with a letter, ends with a letter or digit.
- Only lowercase letters (`a`–`z`), digits (`0`–`9`), hyphens (`-`).
- Minimum length: 2 characters (`CHAIN_MIN_ACCOUNT_NAME_LENGTH`).
- Maximum length: 16 characters (`CHAIN_MAX_ACCOUNT_NAME_LENGTH`).

**JSON:** plain string — `"alice"`, `"alice.bob"`

---

## `public_key_type`

A secp256k1 compressed public key encoded as base58check with a `VIZ` prefix.

**JSON:** string — `"VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"`

- Prefix must be `VIZ` (not `STM`, `GLS`, or any other).
- Encoded from a 33-byte compressed public key + 4-byte checksum = 37 bytes total, base58-encoded.

---

## `asset`

Represents a token amount with its symbol. In JSON API responses and operation parameters, serialized as a human-readable string:

```
"10.000 VIZ"
"5.000000 SHARES"
```

### Token symbols

| Symbol | String | Decimals | Description |
|--------|--------|----------|-------------|
| `TOKEN_SYMBOL` | `VIZ` | 3 | Main liquid token |
| `SHARES_SYMBOL` | `SHARES` | 6 | Vesting shares (staked VIZ) |

When constructing operations, always use the string format. Parse by splitting on the space character: amount part left, symbol right. VIZ uses 3 decimal places; SHARES uses 6.

---

## `authority`

Multi-signature authority structure controlling an account permission level.

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

| Field | Type | Description |
|-------|------|-------------|
| `weight_threshold` | `uint32_t` | Minimum total weight required to satisfy authority |
| `account_auths` | `[[account_name, weight], ...]` | Account-based signers |
| `key_auths` | `[[public_key, weight], ...]` | Key-based signers |

The sum of weights for the satisfied signers must be ≥ `weight_threshold`. An empty authority is `{ "weight_threshold": 0, "account_auths": [], "key_auths": [] }`.

### Authority levels

| Level | Used for |
|-------|---------|
| `master` | Highest security — changing keys, account recovery |
| `active` | Token operations — transfer, vesting, validator voting |
| `regular` | Social operations — content, awards, committee voting |

---

## `beneficiary_route_type`

Specifies a beneficiary and their reward share for content payouts.

```json
{ "account": "alice", "weight": 2500 }
```

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Beneficiary account |
| `weight` | `uint16_t` | Share in basis points (10000 = 100%) |

- The sum of all beneficiary weights must not exceed 10000.
- Beneficiaries must be sorted by account name (ascending) in the array.
- Each beneficiary account must exist on-chain.

---

## `extensions_type`

Currently unused — always serialized as an empty array.

```json
"extensions": []
```

---

## `versioned_chain_properties`

A static variant holding one of the chain properties versions. Serialized as a 2-element array `[type_index, object]`.

| Index | Type |
|-------|------|
| 0 | `chain_properties_init` |
| 1 | `chain_properties_hf4` |
| 2 | `chain_properties_hf6` |
| 3 | `chain_properties_hf9` (current) |

See [Chain Properties](../governance/chain-properties.md) for the full field reference per version.

---

## `operation` (static variant)

Every operation is serialized as a 2-element array: `[type_id, operation_object]`.

### Regular operations (user-broadcast)

| ID | Operation |
|----|-----------|
| 0 | `vote_operation` *(deprecated)* |
| 1 | `content_operation` *(deprecated)* |
| 2 | `transfer_operation` |
| 3 | `transfer_to_vesting_operation` |
| 4 | `withdraw_vesting_operation` |
| 5 | `account_update_operation` |
| 6 | `validator_update_operation` |
| 7 | `account_validator_vote_operation` |
| 8 | `account_validator_proxy_operation` |
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
| 35 | `committee_worker_create_request_operation` |
| 36 | `committee_worker_cancel_request_operation` |
| 37 | `committee_vote_request_operation` |
| 43 | `create_invite_operation` |
| 44 | `claim_invite_balance_operation` |
| 45 | `invite_registration_operation` |
| 46 | `versioned_chain_properties_update_operation` |
| 47 | `award_operation` |
| 50 | `set_paid_subscription_operation` |
| 51 | `paid_subscribe_operation` |
| 54 | `set_account_price_operation` |
| 55 | `set_subaccount_price_operation` |
| 56 | `buy_account_operation` |
| 58 | `use_invite_balance_operation` |
| 60 | `fixed_award_operation` |
| 61 | `target_account_sale_operation` |

### Virtual operations (blockchain-generated, not broadcastable)

| ID | Operation |
|----|-----------|
| 26 | `author_reward_operation` |
| 27 | `curation_reward_operation` |
| 28 | `content_reward_operation` |
| 29 | `fill_vesting_withdraw_operation` |
| 30 | `shutdown_validator_operation` |
| 31 | `hardfork_operation` |
| 32 | `content_payout_update_operation` |
| 33 | `content_benefactor_reward_operation` |
| 34 | `return_vesting_delegation_operation` |
| 38 | `committee_cancel_request_operation` |
| 39 | `committee_approve_request_operation` |
| 40 | `committee_payout_request_operation` |
| 41 | `committee_pay_request_operation` |
| 42 | `validator_reward_operation` |
| 48 | `receive_award_operation` |
| 49 | `benefactor_award_operation` |
| 52 | `paid_subscription_action_operation` |
| 53 | `cancel_paid_subscription_operation` |
| 57 | `account_sale_operation` |
| 59 | `expire_escrow_ratification_operation` |
| 62 | `bid_operation` |
| 63 | `outbid_operation` |

---

## Transaction Construction

A signed transaction contains:

| Field | Value |
|-------|-------|
| `ref_block_num` | `head_block_number & 0xFFFF` |
| `ref_block_prefix` | bytes 4–7 of `block_id` as little-endian `uint32` |
| `expiration` | UTC time string; max ~60 s from broadcast time recommended |
| `operations` | Array of `[type_id, object]` pairs |
| `extensions` | Always `[]` |
| `signatures` | Array of compact hex-encoded ECDSA signatures |

**Signing:** `sha256(chain_id + serialized_transaction_body)` → compact ECDSA signature over secp256k1.

**Private keys:** WIF format (base58check, version byte `0x80`).

---

## Energy System

Energy is used by award-type operations.

- Stored in basis points: 0–10000 (0%–100%).
- Regenerates at 100% per 24 hours (`CHAIN_ENERGY_REGENERATION_SECONDS = 86400`).
- Current energy: `min(10000, last_energy + elapsed_seconds × 10000 / 86400)`.

---

See also: [Operations Overview](./operations/overview.md), [Virtual Operations](./virtual-operations.md), [Chain Properties](../governance/chain-properties.md).
