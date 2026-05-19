# Validator Operations

---

## `witness_update_operation` (ID 6)

**Auth:** `active` of `owner`

Registers or updates a validator node. Setting `block_signing_key` to the null key removes the validator from block production.

| Field | Type | Description |
|-------|------|-------------|
| `owner` | `account_name_type` | Validator account name |
| `url` | `string` | Validator website or info URL (non-empty, max 256 bytes) |
| `block_signing_key` | `public_key_type` | Key used to sign produced blocks |

```json
[6, {
  "owner": "alice",
  "url": "https://alice.example.com",
  "block_signing_key": "VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"
}]
```

- **Null key** (deactivate): `"VIZ1111111111111111111111111111111114T1Anm"` — removes from block production without deleting the validator record.
- Broadcasting this operation requires `witness_declaration_fee` (paid to the committee fund).

---

## `chain_properties_update_operation` (ID 25)

**Auth:** `active` of `owner`

Votes on base chain properties (`chain_properties_init` format). The on-chain value is the median across all active validators.

| Field | Type | Description |
|-------|------|-------------|
| `owner` | `account_name_type` | Validator casting the vote |
| `props` | `chain_properties_init` | Proposed chain parameters |

```json
[25, {
  "owner": "alice",
  "props": {
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
    "committee_request_approve_min_percent": 1000
  }
}]
```

- All percent fields are in basis points (0–10000).
- `min_curation_percent` must be ≤ `max_curation_percent`.
- Use `versioned_chain_properties_update_operation` (ID 46) for extended HF9+ properties.

---

## `versioned_chain_properties_update_operation` (ID 46)

**Auth:** `active` of `owner`

Votes on versioned chain properties supporting all hardfork extensions. Preferred over `chain_properties_update_operation` for current nodes.

| Field | Type | Description |
|-------|------|-------------|
| `owner` | `account_name_type` | Validator casting the vote |
| `props` | `versioned_chain_properties` | Versioned props serialized as `[index, object]` |

```json
[46, {
  "owner": "alice",
  "props": [3, {
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
}]
```

- `props` is a static variant: use index `3` for `chain_properties_hf9` (current).
- See [Data Types](../data-types.md#versioned_chain_properties) for the full field list per version index.

---

## `account_witness_vote_operation` (ID 7)

**Auth:** `active` of `account`

Votes for or removes a vote from a validator. The top 21 validators by cumulative vote weight produce blocks.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Voting account |
| `witness` | `account_name_type` | Validator to vote for |
| `approve` | `bool` | `true` to add vote, `false` to remove vote |

```json
[7, {
  "account": "alice",
  "witness": "bob",
  "approve": true
}]
```

- Voting weight is proportional to the voter's SHARES stake.
- `approve: false` removes a previously cast vote.

---

## `account_witness_proxy_operation` (ID 8)

**Auth:** `active` of `account`

Delegates all validator voting to a proxy account. All existing direct votes are removed when a proxy is set.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account setting the proxy |
| `proxy` | `account_name_type` | Proxy account; `""` removes the proxy |

```json
[8, {
  "account": "alice",
  "proxy": "bob"
}]
```

- `proxy: ""` (empty string) removes the proxy and restores direct voting.
- Cannot set proxy to self.
- Proxy chains are resolved transitively (A→B→C); maximum chain depth is enforced.
- Setting a proxy removes all direct validator votes.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Chain Properties](../../governance/chain-properties.md).
