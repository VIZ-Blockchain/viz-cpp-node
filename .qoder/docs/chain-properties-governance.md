# Chain Properties: How Witnesses Govern Network Parameters

## Overview

In VIZ, there is no central authority that sets network fees, block sizes, inflation rates, or other critical parameters. Instead, **every active witness publishes their preferred values**, and the blockchain automatically calculates the **median** — the middle value that represents the consensus of all elected witnesses.

Since witnesses are elected by stake-weighted voting from all SHARES holders, chain properties are ultimately governed by the community: users vote for witnesses whose parameter choices align with their vision of the public good.

---

## How It Works

### Step 1: Witnesses Publish Their Preferences

Each witness publishes their preferred chain properties using the `versioned_chain_properties_update_operation`:

```json
["versioned_chain_properties_update", {
    "owner": "witness1",
    "props": [3, {
        "account_creation_fee": "1.000 VIZ",
        "maximum_block_size": 131072,
        "create_account_delegation_ratio": 10,
        "create_account_delegation_time": 2592000,
        "min_delegation": "1.000 VIZ",
        "min_curation_percent": 0,
        "max_curation_percent": 10000,
        "bandwidth_reserve_percent": 1000,
        "bandwidth_reserve_below": "500.000000 SHARES",
        "flag_energy_additional_cost": 0,
        "vote_accounting_min_rshares": 5000000,
        "committee_request_approve_min_percent": 1000,
        "inflation_witness_percent": 2000,
        "inflation_ratio_committee_vs_reward_fund": 5000,
        "inflation_recalc_period": 806400,
        "data_operations_cost_additional_bandwidth": 0,
        "witness_miss_penalty_percent": 100,
        "witness_miss_penalty_duration": 86400,
        "create_invite_min_balance": "10.000 VIZ",
        "committee_create_request_fee": "100.000 VIZ",
        "create_paid_subscription_fee": "100.000 VIZ",
        "account_on_sale_fee": "10.000 VIZ",
        "subaccount_on_sale_fee": "100.000 VIZ",
        "witness_declaration_fee": "10.000 VIZ",
        "withdraw_intervals": 28
    }]
}
```

The `[3, {...}]` format indicates the version — `3` means `chain_properties_hf9` (the latest). Older versions (`0` = init, `1` = hf4, `2` = hf6) are accepted for backward compatibility.

### Step 2: Median Calculation

Every time the witness schedule is updated, the blockchain runs `update_median_witness_props()`. For **each property independently**:

1. Collect the property value from every active witness
2. Sort the values
3. Pick the **median** (the middle value)

```
Example: 5 witnesses set account_creation_fee to:
    0.5 VIZ, 1.0 VIZ, 1.0 VIZ, 2.0 VIZ, 5.0 VIZ
                         ↑
                    median = 1.0 VIZ
```

The algorithm uses `std::nth_element` with position `active.size() / 2`, which selects the value at the middle index after partial sorting.

**Why median?** The median is resistant to extremes. A single witness cannot push a parameter to an absurdly high or low value — they can only shift the median by one position. To change a parameter significantly, a **majority of active witnesses** must agree.

### Step 3: Application

The calculated `median_props` is stored in the `witness_schedule_object` and used across the entire blockchain to enforce rules.

---

## All Governable Properties

### Account & Delegation Rules

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `account_creation_fee` | asset (VIZ) | 1.000 VIZ | Minimum fee to create a new account |
| `create_account_delegation_ratio` | uint32 | 10 | Multiplier: delegation = ratio × fee |
| `create_account_delegation_time` | uint32 (sec) | 30 days | How long creation delegation is locked |
| `min_delegation` | asset (VIZ) | 1.000 VIZ | Minimum amount for any delegation |

**How it's used**: When someone creates a new account, they must pay at least `account_creation_fee` and provide delegation of at least `ratio × fee` in SHARES equivalent. The delegation is locked for `create_account_delegation_time`. This prevents cheap mass account creation (Sybil attacks) while keeping the network accessible.

### Block Size & Bandwidth

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `maximum_block_size` | uint32 (bytes) | 131072 | Maximum block size — controls network throughput |
| `bandwidth_reserve_percent` | int16 (bp) | 1000 (10%) | Extra bandwidth for small accounts |
| `bandwidth_reserve_below` | asset (SHARES) | 500.000000 | Threshold for bandwidth reserve |
| `data_operations_cost_additional_bandwidth` | uint32 (%) | 0 | Extra bandwidth cost for data-heavy operations |

**How it's used**: Transaction bandwidth is allocated proportionally to SHARES. Accounts below `bandwidth_reserve_below` get an additional `bandwidth_reserve_percent` reserve so they can still transact. `maximum_block_size` directly controls how many transactions the network can process per block.

### Inflation & Economics

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `inflation_witness_percent` | int16 (bp) | 2000 (20%) | Witness share of block inflation |
| `inflation_ratio_committee_vs_reward_fund` | int16 (bp) | 5000 (50%) | How remaining inflation is split between committee fund and reward fund |
| `inflation_recalc_period` | uint32 (blocks) | 806400 (28 days) | How often inflation parameters are recalculated |

**How it's used**: Each block creates new tokens (inflation). First, `inflation_witness_percent` goes to the block-producing witness. The remainder is split: `inflation_ratio_committee_vs_reward_fund` percent goes to the committee DAO fund, the rest to the reward fund (used for awards). Witnesses directly control how the economy works.

### Reward System

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `min_curation_percent` | int16 (bp) | 500 (5%) | Minimum curation reward share |
| `max_curation_percent` | int16 (bp) | 500 (5%) | Maximum curation reward share |
| `vote_accounting_min_rshares` | uint32 | 5000000 | Minimum rshares for an award to have effect |
| `flag_energy_additional_cost` | int16 (bp) | 0 | Extra energy cost for downvoting |

**How it's used**: When content receives awards, curation rewards are bounded by `[min_curation_percent, max_curation_percent]`. Awards with fewer than `vote_accounting_min_rshares` rshares produce zero reward (dust filter). `flag_energy_additional_cost` can make downvotes more expensive than upvotes.

### Witness Accountability

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `witness_miss_penalty_percent` | int16 (bp) | 100 (1%) | Vote reduction for missing a block |
| `witness_miss_penalty_duration` | uint32 (sec) | 86400 (1 day) | How long the penalty lasts |

**How it's used**: When a witness misses their scheduled block, their effective votes are reduced by `witness_miss_penalty_percent` for `witness_miss_penalty_duration` seconds. This is self-governing accountability: witnesses vote on how harshly missed blocks are punished.

### Fee Structure

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `committee_create_request_fee` | asset (VIZ) | 100.000 VIZ | Fee to create a DAO proposal |
| `create_paid_subscription_fee` | asset (VIZ) | 100.000 VIZ | Fee to create a paid subscription |
| `account_on_sale_fee` | asset (VIZ) | 10.000 VIZ | Fee to list an account for sale |
| `subaccount_on_sale_fee` | asset (VIZ) | 100.000 VIZ | Fee to list subaccounts for sale |
| `witness_declaration_fee` | asset (VIZ) | 10.000 VIZ | One-time fee for new witness registration |
| `create_invite_min_balance` | asset (VIZ) | 10.000 VIZ | Minimum balance to create an invite |

**How it's used**: All fees go to the **committee fund** (DAO treasury). Witnesses control how expensive various network operations are. Higher fees discourage spam; lower fees improve accessibility. The community decides the balance through witness elections.

### Vesting Withdrawal

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `withdraw_intervals` | uint16 | 28 | Number of daily installments for unstaking |

**How it's used**: When a user unstakes SHARES, the withdrawal happens over `withdraw_intervals` days (one installment per day). Witnesses can make unstaking faster or slower, affecting how liquid the network's governance token is.

---

## The Governance Loop: Users → Witnesses → Parameters

### Users Shape the Network Through Witness Selection

Users cannot directly set chain properties. Instead, they **vote for witnesses** whose published properties match their preferences. This creates a representative governance system:

```
Users (SHARES holders)
    │
    ├── Vote for witnesses who want LOW fees
    │   → More witnesses with low fee props get elected
    │   → Median fees decrease
    │
    ├── Vote for witnesses who want HIGH inflation to reward fund
    │   → More reward-focused witnesses get elected
    │   → Inflation shifts toward reward fund
    │
    └── Vote for witnesses who want STRICT miss penalties
        → More accountability-focused witnesses get elected
        → Miss penalties increase
```

### Why This Is a Public Good Mechanism

Traditional blockchains set parameters through hard-coded values or foundation decisions. VIZ makes **every parameter a public good decision**:

1. **Transparency**: every witness's preferred properties are on-chain and publicly visible
2. **Accountability**: if a witness sets harmful parameters, users can unvote them
3. **Gradual change**: the median shifts slowly — no single witness can cause sudden parameter swings
4. **No single point of failure**: even if some witnesses are compromised, the median protects the network
5. **Aligned incentives**: witnesses earn block rewards, so they're incentivized to keep the network healthy

### Example: How a Fee Change Happens

Suppose the community wants to lower the `committee_create_request_fee` from 100 VIZ to 50 VIZ:

1. Users discuss in community channels that the fee is too high
2. Some witnesses update their properties: `committee_create_request_fee: "50.000 VIZ"`
3. Users shift votes to witnesses who support lower fees
4. As more low-fee witnesses enter the active set, the **median shifts down**
5. Once more than half of active witnesses publish 50 VIZ or less, the median becomes 50 VIZ
6. The new fee takes effect automatically — no hardfork, no governance proposal, no vote counting

### Comparing Governance Models

| Approach | VIZ Median Properties | Token Voting (e.g., Snapshot) | Foundation Governance |
|---|---|---|---|
| **Who decides** | Elected witnesses (indirectly: all SHARES holders) | Token holders directly | Core team / foundation |
| **Resistance to extremes** | Strong (median) | Weak (whale dominance) | N/A (centralized) |
| **Speed of change** | Gradual (median shifts slowly) | Fast (single vote) | Fast or slow (depends on team) |
| **Parameter granularity** | Every parameter independently | Usually binary proposals | Any |
| **Sybil resistance** | Built-in (stake-weighted Fair-DPOS) | Depends on implementation | N/A |
| **Transparency** | Full (all witness props on-chain) | Partial (off-chain voting) | Low |

---

## Versioning and Hardfork Compatibility

Properties were introduced in stages:

| Version | Hardfork | Properties Added |
|---|---|---|
| `chain_properties_init` | Genesis | account_creation_fee, maximum_block_size, delegation params, curation, bandwidth, flag cost, vote min rshares, committee threshold |
| `chain_properties_hf4` | HF4 | inflation_witness_percent, inflation_ratio_committee_vs_reward_fund, inflation_recalc_period |
| `chain_properties_hf6` | HF6 | data_operations_cost_additional_bandwidth, witness_miss_penalty_percent, witness_miss_penalty_duration |
| `chain_properties_hf9` | HF9 | create_invite_min_balance, committee_create_request_fee, create_paid_subscription_fee, account_on_sale_fee, subaccount_on_sale_fee, witness_declaration_fee, withdraw_intervals |

Witnesses publish properties using `versioned_chain_properties` — a variant that accepts any version. The evaluator validates the version against the current hardfork (you can't publish HF9 properties before HF9 activates). Properties from older versions use default values for newer fields.

---

## Summary

Chain properties governance in VIZ is a **continuous, median-based, representative system** where:

- **Witnesses** are the direct governors who publish their preferred parameters
- **Users** are the ultimate governors who elect witnesses based on their published properties
- **The median** ensures no single actor can impose extreme values
- **Every parameter** — from fees to inflation to bandwidth — is a public good decision made collectively
- **Changes happen organically**: as community preferences shift, witness elections shift, and the median follows

This creates a self-regulating network where the "rules of the game" are constantly optimized by the people who have the most at stake.
