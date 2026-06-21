# Chain Properties: How Validators Govern Network Parameters

## Overview

In VIZ, there is no central authority that sets network fees, block sizes, inflation rates, or other critical parameters. Instead, **every active validator publishes their preferred values**, and the blockchain automatically calculates the **median** — the middle value that represents the consensus of all elected validators.

Since validators are elected by stake-weighted voting from all SHARES holders, chain properties are ultimately governed by the community: users vote for validators whose parameter choices align with their vision of the public good.

---

## How It Works

### Step 1: Validators Publish Their Preferences

Each validator publishes their preferred chain properties using the `versioned_chain_properties_update_operation`:

```json
["versioned_chain_properties_update", {
    "owner": "validator1",
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
        "inflation_validator_percent": 2000,
        "inflation_ratio_committee_vs_reward_fund": 5000,
        "inflation_recalc_period": 806400,
        "data_operations_cost_additional_bandwidth": 0,
        "validator_miss_penalty_percent": 100,
        "validator_miss_penalty_duration": 86400,
        "create_invite_min_balance": "10.000 VIZ",
        "committee_create_request_fee": "100.000 VIZ",
        "create_paid_subscription_fee": "100.000 VIZ",
        "account_on_sale_fee": "10.000 VIZ",
        "subaccount_on_sale_fee": "100.000 VIZ",
        "validator_declaration_fee": "10.000 VIZ",
        "withdraw_intervals": 28
    }]
}
```

The `[3, {...}]` format indicates the version — `3` means `chain_properties_hf9`. Older versions (`0` = init, `1` = hf4, `2` = hf6) are accepted for backward compatibility. **Newer versions exist:** `4` = `chain_properties_hf13` and `5` = `chain_properties_pm` (HF14 Prediction Markets — adds the PM consensus params, see [the dedicated section below](#prediction-market-parameters-hf14)). A validator must publish the highest version whose hardfork is active.

### Step 2: Median Calculation

Every time the validator schedule is updated, the blockchain runs `update_median_validator_props()`. For **each property independently**:

1. Collect the property value from every active validator
2. Sort the values
3. Pick the **median** (the middle value)

```
Example: 5 validators set account_creation_fee to:
    0.5 VIZ, 1.0 VIZ, 1.0 VIZ, 2.0 VIZ, 5.0 VIZ
                         ↑
                    median = 1.0 VIZ
```

The algorithm uses `std::nth_element` with position `active.size() / 2`, which selects the value at the middle index after partial sorting.

**Why median?** The median is resistant to extremes. A single validator cannot push a parameter to an absurdly high or low value — they can only shift the median by one position. To change a parameter significantly, a **majority of active validators** must agree.

### Step 3: Application

The calculated `median_props` is stored in the `validator_schedule_object` and used across the entire blockchain to enforce rules.

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
| `inflation_validator_percent` | int16 (bp) | 2000 (20%) | Validator share of block inflation |
| `inflation_ratio_committee_vs_reward_fund` | int16 (bp) | 5000 (50%) | How remaining inflation is split between committee fund and reward fund |
| `inflation_recalc_period` | uint32 (blocks) | 806400 (28 days) | How often inflation parameters are recalculated |

**How it's used**: Each block creates new tokens (inflation). First, `inflation_validator_percent` goes to the block-producing validator. The remainder is split: `inflation_ratio_committee_vs_reward_fund` percent goes to the committee DAO fund, the rest to the reward fund (used for awards). Validators directly control how the economy works.

### Reward System

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `min_curation_percent` | int16 (bp) | 500 (5%) | Minimum curation reward share |
| `max_curation_percent` | int16 (bp) | 500 (5%) | Maximum curation reward share |
| `vote_accounting_min_rshares` | uint32 | 5000000 | Minimum rshares for an award to have effect |
| `flag_energy_additional_cost` | int16 (bp) | 0 | Extra energy cost for downvoting |

**How it's used**: When content receives awards, curation rewards are bounded by `[min_curation_percent, max_curation_percent]`. Awards with fewer than `vote_accounting_min_rshares` rshares produce zero reward (dust filter). `flag_energy_additional_cost` can make downvotes more expensive than upvotes.

### Validator Accountability

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `validator_miss_penalty_percent` | int16 (bp) | 100 (1%) | Vote reduction for missing a block |
| `validator_miss_penalty_duration` | uint32 (sec) | 86400 (1 day) | How long the penalty lasts |

**How it's used**: When a validator misses their scheduled block, their effective votes are reduced by `validator_miss_penalty_percent` for `validator_miss_penalty_duration` seconds. This is self-governing accountability: validators vote on how harshly missed blocks are punished.

### Fee Structure

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `committee_create_request_fee` | asset (VIZ) | 100.000 VIZ | Fee to create a DAO proposal |
| `create_paid_subscription_fee` | asset (VIZ) | 100.000 VIZ | Fee to create a paid subscription |
| `account_on_sale_fee` | asset (VIZ) | 10.000 VIZ | Fee to list an account for sale |
| `subaccount_on_sale_fee` | asset (VIZ) | 100.000 VIZ | Fee to list subaccounts for sale |
| `validator_declaration_fee` | asset (VIZ) | 10.000 VIZ | One-time fee for new validator registration |
| `create_invite_min_balance` | asset (VIZ) | 10.000 VIZ | Minimum balance to create an invite |

**How it's used**: All fees go to the **committee fund** (DAO treasury). Validators control how expensive various network operations are. Higher fees discourage spam; lower fees improve accessibility. The community decides the balance through validator elections.

### Vesting Withdrawal

| Property | Type | Default | What It Controls |
|---|---|---|---|
| `withdraw_intervals` | uint16 | 28 | Number of daily installments for unstaking |

**How it's used**: When a user unstakes SHARES, the withdrawal happens over `withdraw_intervals` days (one installment per day). Validators can make unstaking faster or slower, affecting how liquid the network's governance token is.

### Prediction Market Parameters (HF14)

`chain_properties_pm` (variant `5`) inherits everything above and appends the Onix Prediction Market consensus params. They are median-voted exactly like every other property — there is **no separate PM transaction**. Categories (defaults in parentheses):

> **All PM percentages use the project-wide bp scale: 10000 = 100.00%** (hundredths of a percent), like `min_curation_percent` and the other `*_percent` properties. There is no permille (‰) anywhere in PM.

- **Oracle / market economics:** `pm_oracle_registration_fee` (10 VIZ), `pm_min_oracle_insurance` (5000 VIZ bond), `pm_market_creation_fee` (5 VIZ), `pm_min_liquidity` (100 VIZ), `pm_max_outcomes` (10), `pm_max_market_duration` (1 year), **`pm_max_oracle_fee_percent` (500 = 5%)**, `pm_default_time_penalty_percent` (50), `pm_max_time_penalty` (1e6). *(There is no aggregate fee cap — see below.)*
- **Disputes:** `pm_dispute_fee` (1000 VIZ), `pm_dispute_grace_sec` (12 h), `pm_oracle_dispute_response_sec` (12 h), `pm_dispute_auto_close_sec` (14 d), `pm_dispute_vote_period_sec` (3 d), `pm_dispute_approve_min_percent` (1000 bp), `pm_oracle_penalty_percent` (500 bp), `pm_no_contest_penalty_percent` (5000 = 50% of the dispute fee), `pm_dispute_reward_multiplier` (30000 = 3× — a bp **multiplier** where 10000 = 1×, floored at 1× so a vindicated disputer recovers its fee, capped at 100×).
- **Batch / commit-reveal:** `pm_batch_epoch_blocks` (20), `pm_reveal_window_blocks` (200), `pm_commit_no_reveal_penalty_percent` (2000 = 20%), `pm_min_batch_bet` (1 VIZ), `pm_commit_reveal_enabled` (true).
- **Cron budget:** `pm_processing_cap_per_block` (200) — bound on deterministic per-block PM work.
- **Lazy pool:** `pm_lazy_pool_enabled` (true), `pm_lazy_alloc_percent` (2000 bp), `pm_lazy_max_total_alloc_percent` (7000 bp), `pm_lazy_lock_sec` (7 d), `pm_lazy_recall_step_percent` (1000 bp), `pm_lazy_emergency_penalty_percent` (5000 = 50%).
- **Leverage** (kill-switch **off** by default): `pm_leverage_enabled` (false), `pm_leverage_fund_percent` (10), `pm_leverage_max_per_position_bp` (20), `pm_leverage_pool_profit_percent` (10), `pm_leverage_safety_margin_percent` (1), `pm_leverage_max_slippage_percent` (10), `pm_leverage_min_market_liquidity` (5000 VIZ), `pm_leverage_max_position_ratio_percent` (5), `pm_leverage_expiration_buffer_sec` (1 d), `pm_leverage_m_factor_percent` (50), `pm_conversion_profit_cost_percent` (50).

#### Market fees: who sets them, and the single governed cap

A market charges up to **three** resolution fees (bp, 10000 = 100%). At settlement they are deducted from the **losers' pool**, and the remainder is paid to winners:

| Fee on the market | Set by | Goes to | Cap |
|---|---|---|---|
| `oracle_fee_percent` | the **oracle** (quoted at accept) | the oracle | ≤ `pm_max_oracle_fee_percent` (500 = 5%) **and** ≤ the creator's offered ceiling |
| `creator_fee_percent` | the creator (at create) | the creator | — (self-limiting) |
| `liquidity_fee_percent` | the creator (at create) | the LPs (time-weighted) | — (self-limiting) |

**Only one governed fee cap:** `pm_max_oracle_fee_percent`. The oracle is the neutral third party, so its fee is bounded. The creator's own `creator_fee`/`liquidity_fee` have **no governance cap** — a market that takes too much just becomes unattractive and loses bettors (market forces). The old aggregate `pm_max_total_fee` was **removed** as a redundant knob.

The only hard limit on the other two is **solvency**: `oracle + creator + liquidity ≤ 100%` (10000 bp), checked statically in `validate()` so the winners' pool can never go negative.

#### How the fee terms are fixed (offer → quote → freeze)

The market maker and the oracle negotiate on-chain, and the agreed terms are **frozen into the market object**, so a later median shift can never change a live market's economics:

1. **Create** — the creator publishes an **offer ceiling**: `oracle_fee_percent` + `oracle_fixed_fee` are the *most* it will pay the oracle, alongside its own `creator_fee_percent`/`liquidity_fee_percent`.
2. **Accept** — the external oracle **quotes its actual terms** on `pm_oracle_accept_market` (≤ the offer, and `oracle_fee_percent ≤ pm_max_oracle_fee_percent`). The quote is the oracle's price list / reputation, not a bribe. It is frozen onto the market, the status flips to active, and a **`pm_market_accepted` virtual op** is emitted so history-parsing scripts see the launch + terms. A **self-oracle** market freezes its own terms and emits the same vop automatically at creation.
3. **Resolve** — settlement reads only the **frozen** market fields; it never consults the live median. So the median cap matters only at the moment the oracle commits (register / accept), exactly when consent is given.

> **Note on validation layers.** `validate()` is a static check (no chain state): it bounds each fee to ≤ 10000 bp and the solvency sum to ≤ 10000. The governed `pm_max_oracle_fee_percent` cap is enforced in the **evaluator** (which can read the median) at register/accept — not in `validate()`. So size the oracle fee against the median cap, not just the static 100% ceiling.

---

## The Governance Loop: Users → Validators → Parameters

### Users Shape the Network Through Validator Selection

Users cannot directly set chain properties. Instead, they **vote for validators** whose published properties match their preferences. This creates a representative governance system:

```
Users (SHARES holders)
    │
    ├── Vote for validators who want LOW fees
    │   → More validators with low fee props get elected
    │   → Median fees decrease
    │
    ├── Vote for validators who want HIGH inflation to reward fund
    │   → More reward-focused validators get elected
    │   → Inflation shifts toward reward fund
    │
    └── Vote for validators who want STRICT miss penalties
        → More accountability-focused validators get elected
        → Miss penalties increase
```

### Why This Is a Public Good Mechanism

Traditional blockchains set parameters through hard-coded values or foundation decisions. VIZ makes **every parameter a public good decision**:

1. **Transparency**: every validator's preferred properties are on-chain and publicly visible
2. **Accountability**: if a validator sets harmful parameters, users can unvote them
3. **Gradual change**: the median shifts slowly — no single validator can cause sudden parameter swings
4. **No single point of failure**: even if some validators are compromised, the median protects the network
5. **Aligned incentives**: validators earn block rewards, so they're incentivized to keep the network healthy

### Example: How a Fee Change Happens

Suppose the community wants to lower the `committee_create_request_fee` from 100 VIZ to 50 VIZ:

1. Users discuss in community channels that the fee is too high
2. Some validators update their properties: `committee_create_request_fee: "50.000 VIZ"`
3. Users shift votes to validators who support lower fees
4. As more low-fee validators enter the active set, the **median shifts down**
5. Once more than half of active validators publish 50 VIZ or less, the median becomes 50 VIZ
6. The new fee takes effect automatically — no hardfork, no governance proposal, no vote counting

### Comparing Governance Models

| Approach | VIZ Median Properties | Token Voting (e.g., Snapshot) | Foundation Governance |
|---|---|---|---|
| **Who decides** | Elected validators (indirectly: all SHARES holders) | Token holders directly | Core team / foundation |
| **Resistance to extremes** | Strong (median) | Weak (whale dominance) | N/A (centralized) |
| **Speed of change** | Gradual (median shifts slowly) | Fast (single vote) | Fast or slow (depends on team) |
| **Parameter granularity** | Every parameter independently | Usually binary proposals | Any |
| **Sybil resistance** | Built-in (stake-weighted Fair-DPOS) | Depends on implementation | N/A |
| **Transparency** | Full (all validator props on-chain) | Partial (off-chain voting) | Low |

---

## Versioning and Hardfork Compatibility

Properties were introduced in stages:

| Version | Hardfork | Properties Added |
|---|---|---|
| `chain_properties_init` | Genesis | account_creation_fee, maximum_block_size, delegation params, curation, bandwidth, flag cost, vote min rshares, committee threshold |
| `chain_properties_hf4` | HF4 | inflation_validator_percent, inflation_ratio_committee_vs_reward_fund, inflation_recalc_period |
| `chain_properties_hf6` | HF6 | data_operations_cost_additional_bandwidth, validator_miss_penalty_percent, validator_miss_penalty_duration |
| `chain_properties_hf9` | HF9 | create_invite_min_balance, committee_create_request_fee, create_paid_subscription_fee, account_on_sale_fee, subaccount_on_sale_fee, validator_declaration_fee, withdraw_intervals |
| `chain_properties_hf13` | HF13 | (validator/consensus tuning fields inherited by PM) |
| `chain_properties_pm` | HF14 | **~40 Prediction Market params** (oracle/market economics, disputes, batch/commit-reveal, cron budget, lazy pool, leverage) — see [Prediction Market Parameters](#prediction-market-parameters-hf14) |

Validators publish properties using `versioned_chain_properties` — a variant that accepts any version. The evaluator validates the version against the current hardfork (you can't publish HF9 properties before HF9 activates). Properties from older versions use default values for newer fields.

---

## Summary

Chain properties governance in VIZ is a **continuous, median-based, representative system** where:

- **Validators** are the direct governors who publish their preferred parameters
- **Users** are the ultimate governors who elect validators based on their published properties
- **The median** ensures no single actor can impose extreme values
- **Every parameter** — from fees to inflation to bandwidth — is a public good decision made collectively
- **Changes happen organically**: as community preferences shift, validator elections shift, and the median follows

This creates a self-regulating network where the "rules of the game" are constantly optimized by the people who have the most at stake.
