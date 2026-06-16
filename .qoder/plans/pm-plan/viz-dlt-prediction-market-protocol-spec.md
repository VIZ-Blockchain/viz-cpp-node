# Onix Prediction Market — VIZ DLT Protocol Specification

> Node-side (consensus-level) design for implementing the Onix Protocol as **first-class operations and ChainBase objects** on VIZ DLT — not `custom_json`, not smart contracts. Grounded in the VIZ node docs: [data-types](../viz-cpp-node/docs/protocol/data-types.md), [operations overview](../viz-cpp-node/docs/protocol/operations/overview.md), [escrow](../viz-cpp-node/docs/protocol/operations/escrow.md), [committee](../viz-cpp-node/docs/governance/committee.md), [chain-properties](../viz-cpp-node/docs/governance/chain-properties.md), [database-schema](../viz-cpp-node/docs/advanced/database-schema.md), [virtual-operations](../viz-cpp-node/docs/protocol/virtual-operations.md), [plugin-development](../viz-cpp-node/docs/development/plugin-development.md).
>
> Settlement model (both market types): **the AMM assigns weights (CPMM binary / LMSR multi); losers fund winners pro-rata by weight** — see [betting-rules](betting-rules-and-system-overview.md), [plan_unified_parimutuel_binary](plan_unified_parimutuel_binary.md). Front-running mitigation (instant default + opt-in batch/commit-reveal): [plan_batch_commit_reveal_betting](plan_batch_commit_reveal_betting.md).

## 0. Conventions

- **Amounts:** `asset` (VIZ, 3 decimals) in operation params; stored internally as `share_type` (int64 satoshi). Permille fees are `uint16` basis-of-1000.
- **Accounts:** `account_name_type` (≤16 bytes). **Stake weight** for any voting = `effective_vesting_shares = vesting_shares − delegated_vesting_shares + received_vesting_shares` (same as committee voting).
- **Auth levels** (mirroring VIZ): financial actions → `active`; stake-weighted dispute voting → `regular` (same as `committee_vote_request`).
- **Time:** `time_point_sec`. Deterministic deadlines processed during block application via `by_<time>` indexes (same pattern as vesting withdrawals / escrow ratification).
- **New op IDs** start at **64** (regular) and **96** (virtual) to avoid collision with existing IDs (0–63). New `chain_object_types` IDs appended after the existing registry.
- **Hardfork:** all new objects/operations/chain-property version are gated behind a new hardfork (`CHAIN_HARDFORK_PM`); see [hardfork-management](../viz-cpp-node/docs/advanced/hardfork-management.md).

---

## 1. ChainBase Objects

Each object is `chainbase::object<type_id, T>` with a `shared_multi_index_container`. All financial sums are `share_type`. Indexes follow the VIZ convention (`by_id` unique + secondary indexes by account name / status / time for O(log N) lookups and bounded cron scans).

### 1.1 `pm_oracle_object`

Registered oracle: bonded insurance, fee policy, reputation counters.

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `owner` | account_name_type | Oracle account |
| `insurance` | share_type | Locked insurance bond (VIZ) |
| `fee_permille` | uint16 | Per-market % fee on losers' pool (≤ `pm_max_oracle_fee_permille`) |
| `fixed_fee` | share_type | Per-market fixed fee (paid by creator at acceptance) |
| `rules_url` | string (≤255) | Oracle policy/terms |
| `active_since` | time_point_sec | Registration time |
| `last_active_time` | time_point_sec | Last accept/resolve |
| `banned_until` | time_point_sec | 0 = not banned; >0 = temp; `time_point_sec::maximum()` = permanent |
| `markets_accepted` … `bans_received` | uint32 / share_type | The 14 reputation counters (accepted, resolved, no_contest, missed, disputes_received/lost/won/auto_closed, dispute_responses_missed, total_volume_resolved, total_insurance_slashed, avg_resolution_time, penalty_stamps) |

**Indexes:** `by_id`; `by_owner` (unique); `by_status` (banned/active); `by_insurance` (for min-bond checks).

> Reputation **score** is *computed on read* in the API plugin (not stored) — same approach as the prototype and as `compute_oracle_reliability_score`.

### 1.2 `pm_market_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK; referenced by bets/liquidity |
| `creator` | account_name_type | Market maker |
| `oracle` | account_name_type | Designated oracle |
| `market_type` | uint8 | 0 = binary (CPMM), 1 = multi (LMSR) |
| `outcome_count` | uint8 | 2 for binary; 3–10 for multi (≤ `pm_max_outcomes`) |
| `url` | string (≤255) | Question / resolution criteria |
| `status` | uint8 | −1 deleted, 0 waiting, 1 active, 2 closed, 3 resolved |
| `payout_status` | uint8 | 0 none, 1 calculated/pending, 2 paid, 3 disputed |
| `created_time` / `betting_expiration` / `result_expiration` | time_point_sec | Lifecycle timestamps |
| `resolved_outcome` | int16 | −1 unresolved/no-contest; else outcome index/side |
| **Binary CPMM** | | |
| `reserve_a`, `reserve_b`, `k` | share_type / uint128 | CPMM pricing state (`k` = `reserve_a·reserve_b`) |
| `a_bets_sum`, `b_bets_sum` | share_type | Stakes per side (for parimutuel + UI) |
| **Multi LMSR** | | |
| `lmsr_b` | share_type | Liquidity parameter (`= subsidy / ln(N)`) |
| `lmsr_subsidy` | share_type | LP subsidy (returned unconditionally) |
| **Common** | | |
| `bets_sum`, `liquidity_sum` | share_type | Totals |
| `oracle_fee_permille`, `creator_fee_permille`, `liquidity_fee_permille` | uint16 | Resolution-time fees from losers' pool |
| `oracle_fixed_fee` | share_type | Snapshotted from oracle at acceptance |
| `liquidity_fee_earned` | share_type | Already-paid LP fees (early withdrawals) |
| `forfeit_pool` | share_type | Non-revealed commit penalties → winners' pool |
| `time_penalty_type`, `time_penalty_value`, `penalty_curve_type` | uint8/uint32 | Late-bet penalty config |
| `allow_early_resolution`, `allow_cancellation`, `allow_batch` | bool | Flags |
| `allow_instant_bet` | bool | Default `true`. When `false`, instant `pm_place_bet` (mode=0) is rejected — every order MUST go through batch / commit-reveal (anti-MEV). Multi-outcome markets MUST keep this `true` until LMSR batch settlement is implemented. Mutual constraint: `allow_instant_bet || allow_batch` MUST be true at creation, otherwise the market would be unbettable. |
| `endogeneity_tier` | uint8 | 1 econ-data / 2 sports / 3 political (reflexivity-risk tag) |
| `current_epoch` | uint32 | Batch epoch counter |
| **Dispute routing** | | |
| `dispute_mode` | uint8 | 0 = **committee** (public stake vote); 1 = **account** (centralized resolver) |
| `dispute_resolver` | account_name_type | Resolver account when `dispute_mode==1` (e.g. a regulator multisig); empty for committee |

**Indexes:** `by_id`; `by_creator`; `by_oracle`; `by_status`; `by_betting_expiration` `(status, betting_expiration, id)`; `by_result_expiration` `(status, result_expiration, id)` (bounded cron scan for missed-resolution penalty); `by_payout_status`.

### 1.3 `pm_outcome_object` (multi only)

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `outcome_index` | uint8 | 0…N−1 |
| `label` | string (≤64) | Display label |
| `q` | share_type | LMSR quantity |
| `bets_sum` | share_type | Stakes on this outcome (parimutuel losers/winners base) |
| `weight_sum` | share_type | Σ token weights on this outcome (parimutuel denominator) |
| `bets_count` | uint32 | Count |

**Indexes:** `by_id`; `by_market_outcome` `(market, outcome_index)` unique.

### 1.4 `pm_bet_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `account` | account_name_type | Bettor |
| `side` | int8 | Binary: 0/1; multi: −1 (use `outcome_index`) |
| `outcome_index` | int16 | Multi: 0…N−1; binary: −1 |
| `amount` | share_type | Stake |
| `weight` | share_type | Tokens (CPMM/LMSR) — relative claim |
| `price` | uint64 | Entry price (display, precision 1e6) |
| `time_penalty` | uint32 | Penalty ratio at placement (precision 1e6) |
| `mode` | uint8 | 0 instant, 1 batch, 2 commit-reveal |
| `epoch` | uint32 | Settlement epoch (batch/reveal) |
| `status` | uint8 | 0 active, 1 cancelled, 2 refunded, 3 resolved, 5 queued, 6 revealed-pending |
| `resolved_amount` | share_type | Final payout (set at resolution) |
| `created_time` | time_point_sec | Placement/submit time (time-penalty basis) |

**Indexes:** `by_id`; `by_market` `(market, id)`; `by_account` `(account, id)`; `by_market_account` `(market, account, id)`; `by_market_outcome` `(market, outcome_index, id)` and/or `(market, side, id)` (for Σweight); `by_epoch` `(market, epoch, status)` (batch settlement).

### 1.5 `pm_liquidity_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `provider` | account_name_type | LP (0/empty = Lazy Pool) |
| `amount` | share_type | Principal |
| `weight_a`, `weight_b` | share_type | Binary reserve shares (for principal-safe withdrawal) |
| `b_share` | share_type | Multi: share of `lmsr_b`/subsidy |
| `sec_to_expiration` | uint32 | Time-weight basis at deposit |
| `deposit_time` | time_point_sec | |
| `earned_fee` | share_type | Paid-out fees (early withdrawal) |
| `status` | uint8 | 0 active, 3 resolved/closed |

**Indexes:** `by_id`; `by_market` `(market, id)`; `by_provider` `(provider, id)`.

### 1.6 `pm_commit_object` (commit-reveal)

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `account` | account_name_type | Committer |
| `commitment` | sha256 | `H(market‖account‖side/outcome‖amount‖min_tokens‖salt)` |
| `escrow_amount` | share_type | Locked stake |
| `no_reveal_fee_permille` | uint16 | Penalty rate **snapshotted at commit** (consensus-checked, see §3.6) |
| `commit_time` | time_point_sec | |
| `reveal_deadline` | time_point_sec | |
| `status` | uint8 | 0 committed, 1 revealed, 2 forfeited |

**Indexes:** `by_id`; `by_market`; `by_account`; `by_reveal_deadline` `(status, reveal_deadline, id)` (bounded forfeit cron).

### 1.7 `pm_dispute_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK (one active dispute per market) |
| `disputer` | account_name_type | Filer |
| `dispute_fee` | share_type | Escrowed fee |
| `filed_time` | time_point_sec | |
| `oracle_response_deadline` | time_point_sec | |
| `dispute_mode` | uint8 | Copied from market (0 committee / 1 account) |
| `voting_end_time` | time_point_sec | Committee mode: stake-vote tally time |
| `auto_close_time` | time_point_sec | Anti-freeze fallback |
| `proposed_outcome` | int16 | Disputer's claim |
| `status` | uint8 | 0 open, 1 oracle-wrong, 2 oracle-right, 3 auto-closed |

**Indexes:** `by_id`; `by_market` (unique active); `by_voting_end` `(status, voting_end_time, id)`; `by_auto_close` `(status, auto_close_time, id)`.

### 1.8 `pm_dispute_vote_object` (committee mode only)

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `voter` | account_name_type | Any SHARES holder |
| `vote_outcome` | int16 | Outcome the voter believes correct; −1 = uphold oracle |
| `vote_percent` | int16 | Conviction / penalty intensity (−10000…10000) |
| `time` | time_point_sec | Last update |

**Indexes:** `by_id`; `by_market_voter` `(market, voter)` unique; `by_voter` `(voter, id)`.

> Voting weight is read at **tally time** as `effective_vesting_shares` (live), exactly like committee requests — votes store only the choice + percent, not a snapshotted weight.

### 1.9 Lazy Liquidity Pool (optional, phase 2)

- `pm_lazy_pool_object` (singleton): `total_shares`, `free_balance`, `allocated_balance`, `reward_per_share`, params.
- `pm_lazy_deposit_object` (per user): `account`, `shares`, `reward_snapshot`, `unlock_time`.
- `pm_lazy_allocation_object` (per market): `market`, `amount`, `recalled_amount`, `check_step`, `status`.

Indexes mirror the prototype's MasterChef accounting. Deferred to a later phase; not required for v1.

---

## 2. Object Type Registry additions

Append to `chain_object_types.hpp` (after existing IDs): `pm_oracle`, `pm_market`, `pm_outcome`, `pm_bet`, `pm_liquidity`, `pm_commit`, `pm_dispute`, `pm_dispute_vote`, `pm_lazy_pool`, `pm_lazy_deposit`, `pm_lazy_allocation`. All registered via `db.add_core_index<...>()` during `initialize_indexes()` (these are **consensus** objects, not plugin-only).

---

## 3. Regular Operations (user-broadcast)

All carry standard validation in `validate()` (static) + `do_apply()` (stateful, in an evaluator). `has_hardfork(CHAIN_HARDFORK_PM)` gates all of them.

### 3.1 Roles → who sends what

| Role | Operations |
|------|-----------|
| **Oracle** | `pm_oracle_register`, `pm_oracle_update`, `pm_oracle_accept_market`, `pm_resolve_market`, `pm_no_contest` |
| **Market maker / creator** | `pm_create_market`, `pm_add_liquidity`, `pm_withdraw_liquidity` (creator is auto first LP) |
| **User / bettor** | `pm_place_bet`, `pm_commit_bet`, `pm_reveal_bet`, `pm_cancel_bet`, `pm_transfer_position`, `pm_dispute_create`, `pm_add_liquidity` |
| **Stakeholder (committee dispute)** | `pm_dispute_vote` (any SHARES holder) |
| **Resolver account (centralized dispute)** | `pm_dispute_resolve` |

### 3.2 `pm_oracle_register` (ID 64) — auth: `active`

| Field | Type | Constraints |
|-------|------|-------------|
| `owner` | account_name_type | Must exist; not already registered |
| `insurance` | asset (VIZ) | ≥ `pm_min_oracle_insurance` |
| `fee_permille` | uint16 | ≤ `pm_max_oracle_fee_permille` |
| `fixed_fee` | asset (VIZ) | ≥ 0 |
| `rules_url` | string | ≤255 |

Charges `pm_oracle_registration_fee` → committee fund. Locks `insurance` from `owner` balance.

### 3.3 `pm_oracle_update` (ID 65) — auth: `active`
Top-up/withdraw insurance (withdraw blocked while oracle has active accepted markets or below min), change `fee_permille`/`fixed_fee`/`rules_url`.

### 3.4 `pm_create_market` (ID 66) — auth: `active`

| Field | Type | Constraints |
|-------|------|-------------|
| `creator` | account_name_type | Payer of creation fee + initial liquidity |
| `oracle` | account_name_type | Registered oracle (or `creator` if self-oracle) |
| `market_type` | uint8 | 0/1 |
| `outcomes` | vector<string> | size 2 (binary) or 3…`pm_max_outcomes` (multi) |
| `url` | string | resolution criteria, ≤255 |
| `oracle_fee_permille`, `creator_fee_permille`, `liquidity_fee_permille` | uint16 | each ≤ caps; Σ ≤ `pm_max_total_fee_permille` |
| `liquidity` | asset (VIZ) | ≥ `pm_min_liquidity` (binary reserves) / ≥ subsidy floor (multi) |
| `betting_expiration`, `result_expiration` | time_point_sec | future; `result > betting`; ≤ `pm_max_market_duration` |
| `time_penalty_type`, `time_penalty_value`, `penalty_curve_type` | uint8/uint32/uint8 | within bounds |
| `allow_early_resolution`, `allow_cancellation`, `allow_batch` | bool | |
| `allow_instant_bet` | bool | default `true`; when `false` requires `allow_batch=true` (else rejected); ignored / forced `true` for `market_type==1` (multi LMSR has no batch path yet) |
| `endogeneity_tier` | uint8 | 1–3 |
| `dispute_mode` | uint8 | 0 committee / 1 account |
| `dispute_resolver` | account_name_type | required & must exist iff `dispute_mode==1`; ignored for committee |

Charges `pm_market_creation_fee` → committee fund. Locks `liquidity`. Binary: `reserve_a=reserve_b=liquidity/2`, `k`. Multi: `lmsr_b=liquidity/ln(N)`, creates N `pm_outcome_object`. Creator inserted as first `pm_liquidity_object`. Self-oracle → status 1 immediately (insurance check); else status 0.

### 3.5 `pm_oracle_accept_market` (ID 67) — auth: `active` of `oracle`
`{oracle, market_id, accept}`. accept→ status 0→1, transfer `oracle_fixed_fee` creator→oracle, snapshot fee onto market, trigger lazy allocation. reject→ status 0→−1, refund liquidity to creator. Requires oracle insurance ≥ min.

### 3.6 Betting

**`pm_place_bet` (ID 68)** — auth: `active`

| Field | Type | Notes |
|-------|------|-------|
| `account` | account_name_type | |
| `market_id` | id | status==1, time<betting_expiration |
| `side` / `outcome_index` | int8 / int16 | per market_type |
| `amount` | asset (VIZ) | >0 |
| `min_tokens` | share_type | slippage floor (0 = none) |
| `mode` | uint8 | 0 instant (default), 1 batch |

instant: apply to CPMM/LMSR immediately, mint `weight`, update reserves/q/sums. batch (requires `allow_batch`): enqueue (status 5, `epoch`), lock amount; settled by virtual `pm_batch_settle` (§4).

> **`allow_instant_bet=false` enforcement.** When the market's `allow_instant_bet` flag is `false`, the chain MUST reject `mode=0` with `pm_instant_bet_disabled`. The bettor's only paths are `mode=1` (batch) or `pm_commit_bet` → `pm_reveal_bet`. Conversely the off-chain platform UX must hide the instant option and pre-select batch on these markets.

**`pm_commit_bet` (ID 69)** — auth: `active`

| Field | Type | Notes |
|-------|------|-------|
| `account`, `market_id` | | requires `allow_batch` & `pm_commit_reveal_enabled` |
| `commitment` | sha256 | binds account+market |
| `escrow_amount` | asset (VIZ) | ≥ `pm_min_batch_bet` |
| `no_reveal_fee_permille` | uint16 | **must equal `pm_commit_no_reveal_penalty_permille`** (consensus check) — the user explicitly agrees to the current chain-voted rate, which is then snapshotted on the commit |

**`pm_reveal_bet` (ID 70)** — auth: `active`. `{commit_id, side/outcome, amount, salt, min_tokens}`; verify hash; refund surplus; enqueue for the next epoch boundary (§4). Unrevealed → virtual `pm_commit_forfeit`.

**`pm_cancel_bet` (ID 71)** — auth: `active`. Requires `allow_cancellation`, status active/queued, betting open. Binary: reverse CPMM; multi: reverse LMSR; refund.

### 3.7 Liquidity

**`pm_add_liquidity` (ID 72)** / **`pm_withdraw_liquidity` (ID 73)** — auth: `active`. Binary: proportional reserve add / principal-safe reverse (block if `reserve<weight`); multi: increase/decrease `lmsr_b` (subsidy floor enforced). Post-betting-expiration withdrawal locked until resolution.

### 3.8 Resolution

**`pm_resolve_market` (ID 74)** — auth: `active` of `oracle`. `{oracle, market_id, winning_outcome, decision_url}`. Preconditions: status 1 (if `allow_early_resolution`) or 2; `time ≤ result_expiration`. Sets status 3, payout_status 1 (pending), starts `pm_dispute_grace`. **Parimutuel settlement** computed and stored on bets' `resolved_amount` (see §settlement below). Actual transfer is deferred to virtual `pm_auto_payout` after grace (so disputes can freeze it).

**`pm_no_contest` (ID 75)** — auth: `active` of `oracle`. Refund all bets+LP (principal), penalty `pm_no_contest_penalty_permille` of dispute fee from insurance distributed to participants. Disputable (3-outcome).

**Settlement (both types, computed at resolve):**
```
losers_sum   = Σ amount of non-winning bets
winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee   (fees = permille × losers_sum)  + forfeit_pool
payout_i     = bet_amount_i + floor(winners_pool × weight_i / total_winning_weight) − time_penalty(profit)
LP: principal returned unconditionally + time-weighted share of liq_fee (+ no-winner bonus)
```

### 3.9 Disputes

**`pm_dispute_create` (ID 76)** — auth: `active`. `{disputer, market_id, proposed_outcome}`. Preconditions: disputer has a bet; within `pm_dispute_grace`; no open dispute. Escrow `pm_dispute_fee`. Sets payout_status 3 (frozen). Creates `pm_dispute_object` copying `dispute_mode`. Sets `oracle_response_deadline`, `auto_close_time = now + pm_dispute_auto_close`. Committee mode: `voting_end_time = now + pm_dispute_vote_period`.

**Committee mode (`dispute_mode==0`) — public stake-weighted vote:**

**`pm_dispute_vote` (ID 77)** — auth: `regular` of `voter`. `{voter, market_id, vote_outcome, vote_percent}`. Any SHARES holder. Upsert into `pm_dispute_vote_object`. `vote_outcome` = correct outcome (or −1 to uphold oracle); `vote_percent` ∈ [−10000,10000] = conviction/penalty intensity. Tally is deterministic at `voting_end_time` via virtual `pm_dispute_finalize` (§4): stake-weighted per-outcome rshares, participation threshold `pm_dispute_approve_min_percent`, winning outcome = argmax; **penalty scaled by consensus strength** (`winning_rshares / max_rshares`); bans scaled likewise. (Algorithm = [committee-dao-and-prediction-markets.md](committee-dao-and-prediction-markets.md) §Resolution.)

**Account mode (`dispute_mode==1`) — centralized resolver:**

**`pm_dispute_resolve` (ID 78)** — auth: `active` of `dispute_resolver`. `{resolver, market_id, correct_outcome, penalty_amount, ban_oracle, ban_oracle_until, ban_creator, ban_creator_until}`. Only the market's `dispute_resolver` account may call. Used for **regulated markets** where a named body (e.g. a country's commission multisig) decides. Recalculate payouts / slash insurance / apply bans per fields.

> **Both modes converge** on the same post-verdict recalculation path (delete pending payouts → flip/replace winner → regenerate parimutuel payouts → slash insurance/reward disputer → unfreeze). Only *who decides* differs: the whole SHARES electorate vs one configured account.

### 3.10 `pm_transfer_position` (ID 79) — auth: `active`
`{from, bet_id, to, amount, memo}`. Reassign all/part of a bet's `weight` to `to` (same market/outcome). No market impact. `memo`: plaintext, or `#`-prefixed ECIES-encrypted via VIZ account memo keys.

---

## 4. Virtual Operations (deterministic, block-generated)

Generated during block application by scanning `by_<time>`/`by_epoch` indexes with **bounded per-block work** (cap N per block; defer remainder — same pattern as committee payouts every 200 blocks and vesting withdrawals).

| ID | Virtual op | Trigger | Effect |
|----|-----------|---------|--------|
| 96 | `pm_batch_settle` | Epoch boundary (`block % pm_batch_epoch_blocks == 0`) per market with a queue | Aggregate queued+revealed bets into one CPMM/LMSR transition per side/outcome at a uniform price; `min_tokens` gate; mint weights |
| 97 | `pm_commit_forfeit` | `reveal_deadline` passed, unrevealed | penalty = `escrow × no_reveal_fee_permille/1000` → `market.forfeit_pool`; refund rest |
| 98 | `pm_auto_payout` | Resolved + grace elapsed, no open dispute | Credit `resolved_amount` to winners, fees to oracle/creator, principal+fees to LPs, subsidy return |
| 99 | `pm_dispute_finalize` | Committee `voting_end_time` | Tally stake-weighted votes; apply verdict + scaled penalty/bans; recalc payouts |
| 100 | `pm_dispute_auto_close` | `auto_close_time` reached, unresolved | Full refund + oracle penalty + disputer fee return (anti-freeze) |
| 101 | `pm_oracle_missed_penalty` | `result_expiration` passed, status 2 | Slash `pm_oracle_penalty_percent` of insurance; refund all; distribute bonus |
| 102 | `pm_lazy_recall` | Lazy pool graduated-recall step | Recall idle allocation (phase 2) |

---

## 5. Chain Properties (consensus parameters)

Set **only** via the existing `versioned_chain_properties_update_operation` (ID 46). Add a new version **index 4 (`chain_properties_pm`)** which **inherits all `chain_properties_hf9` fields** and appends the PM fields below. Validators publish; the network applies the **per-field median** across active validators (same machinery as all other chain properties — no separate transaction, no new op).

| Property | Type | Default | Constraints / notes |
|----------|------|---------|---------------------|
| `pm_oracle_registration_fee` | asset (VIZ) | 10.000 | → committee fund |
| `pm_min_oracle_insurance` | asset (VIZ) | 5000.000 | bond floor |
| `pm_market_creation_fee` | asset (VIZ) | 5.000 | → committee fund |
| `pm_min_liquidity` | asset (VIZ) | 100.000 | binary/multi seed floor |
| `pm_max_outcomes` | uint8 | 10 | 2–10 |
| `pm_max_market_duration` | uint32 (s) | 31536000 | ≤ 1 year |
| `pm_max_oracle_fee_permille` | uint16 | 50 | per-fee cap |
| `pm_max_total_fee_permille` | uint16 | 100 | Σ(oracle+creator+liq) cap |
| `pm_default_time_penalty_percent` | uint16 | 50 | window % of duration |
| `pm_max_time_penalty` | uint32 | 1000000 | 100% of profit (precision 1e6) |
| `pm_dispute_fee` | asset (VIZ) | 1000.000 | to open dispute |
| `pm_dispute_grace_sec` | uint32 | 43200 | 12 h |
| `pm_oracle_dispute_response_sec` | uint32 | 43200 | 12 h |
| `pm_dispute_auto_close_sec` | uint32 | 1209600 | 14 d (anti-freeze) |
| `pm_dispute_vote_period_sec` | uint32 | 259200 | 3 d (committee mode) |
| `pm_dispute_approve_min_percent` | uint16 | 1000 | participation threshold (bp of total SHARES) |
| `pm_oracle_penalty_percent` | uint16 | 500 | insurance slashed on missed deadline (bp) |
| `pm_no_contest_penalty_permille` | uint16 | 500 | of dispute fee |
| `pm_dispute_reward_multiplier` | uint16 | 3000 | ‰ — disputer reward carve-out |
| `pm_batch_epoch_blocks` | uint32 | 20 | ~60 s |
| `pm_reveal_window_blocks` | uint32 | 200 | ~10 min (liveness) |
| `pm_commit_no_reveal_penalty_permille` | uint16 | 200 | **20%** → winners' pool; carried & checked in `pm_commit_bet` |
| `pm_min_batch_bet` | asset (VIZ) | 1.000 | anti-dust |
| `pm_commit_reveal_enabled` | bool | true | global kill-switch |
| `pm_lazy_pool_*` | … | … | phase 2 (allocation %, max %, lock, recall) |

> **Open governance question** (see §7.1): putting ~25 PM params into the validator median set materially enlarges what every validator must publish. Consider a smaller median-voted subset + a dedicated `pm_params_object` updated by a separate mechanism.

---

## 6. API Plugin: `prediction_market_api`

Read-only JSON-RPC plugin over the **consensus** objects (mirrors `committee_api`/`database_api`; objects live in `chain`, the plugin only queries indexes). Deps: `json_rpc`, `chain`. Method name = `prediction_market_api.<m>`. All list methods paginated (`from`, `limit ≤ 1000`). Heavy reads cached by the webserver (cleared per applied block).

| Method | Params | Returns / index used |
|--------|--------|----------------------|
| `get_market` | `market_id` | market + outcomes (`by_id`) |
| `list_markets` | `status, from, limit` | markets (`by_status`) |
| `list_markets_by_oracle` / `_by_creator` | `account, from, limit` | (`by_oracle`/`by_creator`) |
| `get_market_bets` | `market_id, from, limit` | bets (`by_market`) |
| `get_account_positions` | `account, from, limit` | bets (`by_account`) **+ computed `expected_payout` & `expected_payout_approx`** (parimutuel estimate; exact for resolved) |
| `get_market_weight_sums` | `market_id` | `a_weight_sum/b_weight_sum` or per-outcome `weight_sum`/`bets_sum` — feeds client `market_math.js` (no server business logic) |
| `get_oracle` | `account` | oracle obj + **computed reliability/trust score** |
| `list_oracles` | `from, limit, sort` | (`by_status`/`by_insurance`) |
| `get_dispute` | `market_id` | dispute obj |
| `get_dispute_votes` | `market_id, from, limit` | committee votes + live tally (`by_market_voter`) |
| `get_liquidity` | `market_id` / `account` | LP positions |
| `get_pm_chain_properties` | — | current median PM params (from `validator_schedule_object.median_props`) |
| `get_lazy_pool` | — | pool + caller deposit (phase 2) |

> The frontend already computes pricing/payouts locally ([market_math.js](../market_math.js)); the API only needs to expose **raw object state** (reserves, `q`, `lmsr_b`, per-side/outcome `bets_sum`+`weight_sum`, fees) so the client can mirror the on-chain math without trusting a server. Settlement itself is consensus (never client/server).

**Real-time:** optional `set_market_applied_callback` via the webserver block callback for live odds.

---

## 7. Review — contentious / hard points to resolve before node implementation

### 7.1 Chain-property surface (governance design)
~25 PM params in the validator median set is a lot for every validator to track and publish. **Options:** (a) median-vote all (consistent, but heavy + slow to change); (b) median-vote a small core (fees caps, insurance floor, dispute fee/periods) and fix the rest at hardfork; (c) a separate `pm_params_object` updated by committee vote or a 2/3 validator supermajority. **Decision needed.**

### 7.2 Floating-point determinism in consensus — **RESOLVED**
**Status:** resolved by [docs/lmsr-fixed-point-spec.md](lmsr-fixed-point-spec.md). LMSR `exp`/`ln` are now computed in Q96 fixed-point on big integers (PHP `gmp`, JS `BigInt`, future C++ `boost::multiprecision::int256_t`) using a frozen Taylor / atanh series with a fixed iteration count and a single banker's-rounding step in `exp` range reduction. The PHP and JS reference implementations produce **bit-identical** outputs across `tests/lmsr_fixed_vectors.json` (all 71 functional vectors); the C++ plugin author follows §6 of the fixed-point spec and runs the same vector file in CI. Binary CPMM remains integer-only and unchanged.

### 7.3 Batch / commit-reveal as virtual ops
Uniform-price batch settlement (`pm_batch_settle`) and per-epoch processing must be (a) deterministic, (b) bounded per block, (c) ordered vs same-block instant bets and liquidity ops. Need a defined in-block op ordering and an epoch-open vs live-curve decision (see [plan_batch_commit_reveal_betting](plan_batch_commit_reveal_betting.md) §6.1; with parimutuel it can be epoch-open snapshot for full immunity). **Spec the exact ordering + caps.**

### 7.4 Committee dispute voting — sybil, quorum, cost, vote-buying
- **Quorum:** `pm_dispute_approve_min_percent` of *total* SHARES is hard to reach for niche markets → many disputes fall through to auto-close. Tune, or use participating-stake-relative thresholds.
- **Vote storage:** one `pm_dispute_vote_object` per voter per dispute — unbounded; cap or require min stake to vote.
- **Plutocracy / vote-buying:** whales decide outcomes; delegation can concentrate. Acceptable? (Same property as all VIZ governance.)
- **Front-running the tally:** votes are public; consider commit-reveal for dispute votes too (KBC mitigation, see [FORECASTER-FIT](../.qoder/docs/theory_concepts/FORECASTER-FIT.md)).
- **Penalty scaling formula** (consensus_strength) and **tie-breaking** must be exactly specified (integer math).

### 7.5 Insurance & escrowed funds custody
Where do locked funds live? Options: (a) dedicated `share_type` fields on objects (oracle.insurance, dispute.dispute_fee, bet.amount) decremented from account balance — simplest, but they're invisible to generic balance tooling; (b) reuse the escrow subsystem. Recommend (a) with explicit virtual-op accounting so account_history reflects locks/unlocks.

### 7.6 Time-driven processing cost
Auto-payout, forfeit, dispute-finalize, missed-penalty, lazy-recall all scan time indexes every block. With many markets this is per-block work. **Cap per block + defer** (like committee 200-block cadence). Define caps and a fairness/ordering rule so no market starves.

### 7.7 Bandwidth / energy for high-frequency betting
Betting is `active`-auth and consumes bandwidth (∝ stake). Batch/commit-reveal add 2 ops per bet. Confirm bandwidth model tolerates active markets; consider whether PM ops need a distinct cost class (`data_operations_cost_additional_bandwidth` analog).

### 7.8 Regulated (account) vs committee dispute — legal binding
`dispute_mode==1` points to a `dispute_resolver` account (e.g. a national commission multisig). On-chain it's just an account with `active` auth over `pm_dispute_resolve`. KYC/whitelisting of that account, and which oracles/markets a regulated client surfaces, are **client-layer** concerns — the protocol stays neutral (same op set for both). Confirm this separation is acceptable and that a regulated resolver can also `ban`/slash.

### 7.9 Snapshots & replay
All new objects must be (a) included in `snapshot` plugin serialization, (b) deterministically rebuildable on replay, (c) `FC_REFLECT`-ed. New op/object IDs must be stable across the hardfork. Confirm snapshot format extension + reindex requirement.

### 7.10 Precision & dust
VIZ asset = 3 decimals; fees are permille; LMSR/CPMM produce remainders. Define rounding (floor) and dust routing (→ committee fund) consistently with §settlement. Ensure `Σ payouts ≤ pool` always holds under integer floor (conservation proven for parimutuel — keep it exact).

### 7.11 Multi-outcome cancellation & LMSR reverse
`pm_cancel_bet` for multi reverses LMSR (`lmsr_sell_return`); confirm it can't be gamed (buy/sell churn) and interacts safely with `weight_sum`/`bets_sum` accounting.

### 7.12 Frozen byte-length caps for variable-length fields
Variable-length on-chain strings (`pm_oracle.profile_url`, `pm_market.title`, `pm_outcome.label`, `pm_resolve_market.decision_url`, `pm_dispute.reason`, etc.) MUST have a byte-length cap defined as a chain constant. **This is not an anti-spam concern** — economic gating (`pm_market_creation_fee`, oracle registration cost, `active`-auth bandwidth) already prevents abusive volume. The reason is purely **consensus-mechanical**:

1. **ChainBase allocation determinism.** `chainbase::allocator<char>` strings without a hard cap force the C++ author to invent one; two nodes with different invented caps produce different `fc::raw::pack` byte length → different object hash on the same logical input → fork.
2. **Block-size envelope.** A single op must fit inside `MAX_TRANSACTION_SIZE`/`MAX_BLOCK_SIZE` deterministically across builds.
3. **Snapshot / replay byte-stability** (see §7.9): variable-length fields without a known maximum can disagree on field offsets after a node upgrade.
4. **Operation cost model.** VIZ bandwidth/fee charges by serialized byte size; without a cap the fee ceiling itself is undefined.

**Required output:** a frozen table of constants in §1 / §2 (illustrative starting values, freeze before hardfork):
```
MAX_PM_DECISION_URL_LEN     = 256
MAX_PM_PROFILE_URL_LEN      = 256
MAX_PM_DISPUTE_REASON_LEN   = 1024
MAX_PM_MARKET_TITLE_LEN     = 256
MAX_PM_OUTCOME_LABEL_LEN    = 64
MAX_PM_OUTCOMES_PER_MARKET  = 16
```
Evaluators reject ops exceeding the cap; ChainBase fields use `fc::shared_string` with the cap enforced on insert.

---

## 8. Phasing recommendation

1. **Phase 1 (binary-only, integer-safe):** objects, oracle, create/accept, instant bet, liquidity, resolve, both dispute modes, auto-payout, missed-penalty. No LMSR (avoids §7.2), no batch/commit-reveal. Ships a usable consensus market.
2. **Phase 2:** deterministic LMSR (resolve §7.2) → multi markets; batch + commit-reveal (§7.3); lazy pool.
3. **Phase 3:** shared/category liquidity, automated data oracles, advanced governance of PM params.

---

🇷🇺 По запросу сделаю русскую версию (`-ru`) и/или раскрою любой раздел (точные C++ сигнатуры объектов/эвалуаторов, бинарную сериализацию операций, или детальный алгоритм tally для committee-диспута).
