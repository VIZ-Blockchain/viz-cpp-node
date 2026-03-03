# VIZ Blockchain — Witness Operations

Spec for implementing witness-related operations in PHP/Node.js libraries.

---

## `witness_update_operation`

**Type ID:** `6`
**Required authority:** `active` of `owner`

Registers or updates a witness. Setting `block_signing_key` to the null key removes the witness from block production contention.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `owner` | `account_name_type` | yes | Witness account name |
| `url` | `string` | yes | Witness website or info URL |
| `block_signing_key` | `public_key_type` | yes | Key used to sign blocks (set to null key to deactivate) |

**Null key** (deactivate witness): `"VIZ1111111111111111111111111111111114T1Anm"`

### JSON Example

```json
[6, {
  "owner": "alice",
  "url": "https://alice.example.com",
  "block_signing_key": "VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"
}]
```

### PHP Example

```php
$op = [
    'type' => 'witness_update_operation',
    'value' => [
        'owner'             => 'alice',
        'url'               => 'https://alice.example.com',
        'block_signing_key' => 'VIZ5hq...',
    ],
];
```

### Node.js Example

```js
const op = ['witness_update', {
    owner: 'alice',
    url: 'https://alice.example.com',
    block_signing_key: 'VIZ5hq...',
}];
```

### Checklist
- [ ] `block_signing_key` must be a valid VIZ public key or the null key
- [ ] Null key = `VIZ1111111111111111111111111111111114T1Anm` (deactivates witness)
- [ ] `url` must be non-empty and < `CHAIN_MAX_URL_LENGTH` (256) bytes
- [ ] Requires `witness_declaration_fee` paid to committee (see chain properties)
- [ ] Sign with `owner`'s active key

---

## `chain_properties_update_operation`

**Type ID:** `25`
**Required authority:** `active` of `owner`

Witness votes on base chain properties (`chain_properties_init` format only). Use `versioned_chain_properties_update_operation` for extended properties.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `owner` | `account_name_type` | yes | Witness account voting |
| `props` | `chain_properties_init` | yes | Proposed chain properties |

### JSON Example

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

### Checklist
- [ ] `account_creation_fee.symbol` must be `VIZ`
- [ ] `min_delegation.symbol` must be `VIZ`
- [ ] `bandwidth_reserve_below.symbol` must be `SHARES`
- [ ] `min_curation_percent` <= `max_curation_percent`
- [ ] All percent fields in basis points (0–10000)
- [ ] Median of all active witness values is used as actual chain property

---

## `versioned_chain_properties_update_operation`

**Type ID:** `46`
**Required authority:** `active` of `owner`

Witness votes on versioned chain properties (supports all hardfork extensions).

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `owner` | `account_name_type` | yes | Witness account voting |
| `props` | `versioned_chain_properties` | yes | Versioned props variant |

### JSON Example (using hf9 = index 3)

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

### Checklist
- [ ] `props` is serialized as a static_variant: `[index, object]`
- [ ] Use index `3` for `chain_properties_hf9` (current latest)
- [ ] See `data-types.md` for full field list per version

---

## `account_witness_vote_operation`

**Type ID:** `7`
**Required authority:** `active` of `account`

Votes for or against a witness to be included in block production.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Voting account |
| `witness` | `account_name_type` | yes | Witness to vote for/against |
| `approve` | `bool` | yes | `true` to add vote, `false` to remove vote |

### JSON Example

```json
[7, {
  "account": "alice",
  "witness": "bob",
  "approve": true
}]
```

### PHP Example

```php
$op = [
    'type' => 'account_witness_vote_operation',
    'value' => [
        'account' => 'alice',
        'witness' => 'bob',
        'approve' => true,
    ],
];
```

### Node.js Example

```js
const op = ['account_witness_vote', {
    account: 'alice',
    witness: 'bob',
    approve: true,
}];
```

### Checklist
- [ ] Account must have SHARES to have meaningful voting weight
- [ ] `approve: false` removes a previously cast vote
- [ ] Top 21 witnesses by vote weight produce blocks
- [ ] Sign with `account`'s active key

---

## `account_witness_proxy_operation`

**Type ID:** `8`
**Required authority:** `active` of `account`

Assigns a proxy account for witness voting. All existing votes are removed when a proxy is set.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account setting the proxy |
| `proxy` | `account_name_type` | yes | Proxy account (empty string `""` to remove proxy) |

### JSON Example

```json
[8, {
  "account": "alice",
  "proxy": "bob"
}]
```

### Checklist
- [ ] Setting `proxy` to `""` (empty string) removes the proxy
- [ ] Cannot set proxy to self
- [ ] Proxy chains are resolved (A→B→C); max depth is limited
- [ ] Setting a proxy removes all direct witness votes
- [ ] Sign with `account`'s active key
