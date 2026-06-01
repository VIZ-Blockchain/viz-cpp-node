# Chain Properties

Chain properties are the network's governable parameters: fees, block size, inflation rates, penalty rules, and more. There is no central authority that sets them — every active validator publishes their preferred values and the blockchain applies the **median** across all active validators.

---

## How It Works

### 1. Validators Publish Preferences

Each validator submits their preferred parameters via `versioned_chain_properties_update_operation`:

```json
[46, {
  "owner": "alice",
  "props": [3, {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 131072,
    ...
  }]
}]
```

The `[3, {...}]` indicates version 3 (`chain_properties_hf9`, the current format).

### 2. Median Calculation

On every validator schedule update, the blockchain calls `update_median_witness_props()`. For **each property independently**, it:
1. Collects values from every active validator.
2. Sorts them.
3. Picks the **median** (index `active.size() / 2`).

```
Example — 5 validators vote account_creation_fee:
  0.5, 1.0, 1.0, 2.0, 5.0 VIZ
              ↑
         median = 1.0 VIZ
```

The median resists extremes: a single validator cannot cause a sudden large shift; a majority must agree to move any parameter significantly.

### 3. Application

The resulting `median_props` object is stored in the `validator_schedule_object` and enforced across all block processing.

---

## All Governable Properties

### Account and Delegation

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `account_creation_fee` | asset (VIZ) | 1.000 VIZ | Minimum fee to create a new account |
| `create_account_delegation_ratio` | uint32 | 10 | Required delegation = ratio × fee |
| `create_account_delegation_time` | uint32 (s) | 2592000 (30d) | How long creation delegation is locked |
| `min_delegation` | asset (VIZ) | 1.000 VIZ | Minimum amount for any SHARES delegation |

### Block Size and Bandwidth

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `maximum_block_size` | uint32 (bytes) | 131072 | Maximum block size; controls throughput |
| `bandwidth_reserve_percent` | uint16 (bp) | 1000 (10%) | Extra bandwidth for small accounts |
| `bandwidth_reserve_below` | asset (SHARES) | 500.000000 | Threshold to qualify for bandwidth reserve |
| `data_operations_cost_additional_bandwidth` | uint32 (%) | 0 | Extra bandwidth multiplier for data operations (custom_operation) |

### Inflation and Economics

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `inflation_validator_percent` | uint16 (bp) | 2000 (20%) | Validator share of block inflation |
| `inflation_ratio_committee_vs_reward_fund` | uint16 (bp) | 5000 (50%) | Split of remaining inflation: committee fund vs reward fund |
| `inflation_recalc_period` | uint32 (blocks) | 806400 (~28d) | How often inflation is recalculated |

Inflation flow: `block_reward × inflation_validator_percent` → validator. Remainder split: `inflation_ratio_committee_vs_reward_fund` → committee fund; the rest → award reward fund.

### Reward System

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `min_curation_percent` | uint16 (bp) | 500 (5%) | Minimum curation reward share from content payouts |
| `max_curation_percent` | uint16 (bp) | 500 (5%) | Maximum curation reward share |
| `vote_accounting_min_rshares` | uint32 | 5000000 | Minimum rshares for an award to produce non-zero reward |
| `flag_energy_additional_cost` | uint16 (bp) | 0 | Extra energy cost for downvotes/flags |

### Validator Accountability

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `validator_miss_penalty_percent` | uint16 (bp) | 100 (1%) | Vote weight reduction on missed block |
| `validator_miss_penalty_duration` | uint32 (s) | 86400 (1d) | How long the miss penalty lasts |

### Fees

All fees go to the committee fund (DAO treasury).

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `committee_create_request_fee` | asset (VIZ) | 100.000 VIZ | Fee to create a committee funding request |
| `create_paid_subscription_fee` | asset (VIZ) | 100.000 VIZ | Fee to create a paid subscription |
| `account_on_sale_fee` | asset (VIZ) | 10.000 VIZ | Fee to list an account for sale |
| `subaccount_on_sale_fee` | asset (VIZ) | 100.000 VIZ | Fee to list subaccount creation rights for sale |
| `validator_declaration_fee` | asset (VIZ) | 10.000 VIZ | One-time fee for validator registration |
| `create_invite_min_balance` | asset (VIZ) | 10.000 VIZ | Minimum invite balance |

### Vesting Withdrawal

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `withdraw_intervals` | uint16 | 28 | Number of daily installments for SHARES unstaking |

---

## Property Versions

Properties were introduced in hardfork stages:

| Version | Index | Hardfork | Fields added |
|---------|-------|----------|--------------|
| `chain_properties_init` | 0 | Genesis | account_creation_fee, maximum_block_size, delegation params, curation, bandwidth, flag cost, vote min rshares, committee threshold |
| `chain_properties_hf4` | 1 | HF4 | inflation_validator_percent, inflation_ratio_committee_vs_reward_fund, inflation_recalc_period |
| `chain_properties_hf6` | 2 | HF6 | data_operations_cost_additional_bandwidth, validator_miss_penalty_percent, validator_miss_penalty_duration |
| `chain_properties_hf9` | 3 | HF9 | create_invite_min_balance, committee_create_request_fee, create_paid_subscription_fee, account_on_sale_fee, subaccount_on_sale_fee, validator_declaration_fee, withdraw_intervals |

Use version index 3 (`chain_properties_hf9`) for all new validator property submissions.

---

## Governance Loop

```
SHARES holders → vote for validators
Validators → publish preferred property values
Blockchain → takes median of active set
Median → applied as live network rules
```

Changing a parameter requires a **majority of active validators** to publish the new value. The process:
1. Community discusses desired change (e.g., lower fees).
2. Validators update their published properties.
3. Users shift votes toward validators publishing the desired values.
4. Once a majority of active validators publish the new value, the median shifts.
5. New value takes effect automatically — no hardfork or governance vote needed.

---

## Reading Current Properties

```json
{ "method": "database_api.get_chain_properties", "params": [] }
```

Returns the current median properties in effect. See [Database API](../plugins/database-api.md#get_chain_properties).

---

See also: [Validators](../protocol/operations/validators.md), [Database API](../plugins/database-api.md), [Staking and DAO](./staking-and-dao.md).
