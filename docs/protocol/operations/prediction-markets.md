# Prediction Market Operations (Onix, HF14)

The Onix prediction market lets an **oracle** resolve a **market** whose outcome bettors stake on. Two pricing engines share **one settlement model** — a strictly **zero-sum parimutuel** split: the losers' stakes fund the winners pro-rata by curve **weight**, principal is always returned, and no tokens are ever minted.

- **Binary markets** (`market_type = 0`) price with a **CPMM** (constant-product `x·y=k`); `weight = tokens_out`.
- **Multi markets** (`market_type = 1`) price with a fixed-point **LMSR**; `weight = tokens` from the Q96 curve.

All amounts are `asset` in VIZ (3 decimals). Object references are plain `int64` ids that the evaluator resolves to chainbase objects. Every operation is gated on `has_hardfork(HF14)` and validated against the median `chain_properties_pm` (governance v5).

**Lifecycle:**

```mermaid
flowchart TD
  REG[pm_oracle_register] --> CRE[pm_create_market]
  CRE --> ACC[pm_oracle_accept_market]
  ACC --> ACT([market active])
  ACT --> BET[pm_place_bet / pm_add_liquidity<br/>pm_commit_bet → pm_reveal_bet]
  BET --> EXP{{betting_expiration}}
  EXP --> RES[pm_resolve_market | pm_no_contest]
  RES -. disputed .-> DC[pm_dispute_create]
  DC -->|committee mode| DV[pm_dispute_vote]
  DC -->|account mode| DR[pm_dispute_resolve]
  RES --> GR{{grace window}}
  DV --> GR
  DR --> GR
  GR ==> VOP[[VIRTUAL pm_auto_payout<br/>parimutuel settle · LP principal returned]]
```

> **Op IDs** below are each operation's position in the chain's single `operation` variant, continuing the global numbering after `stakeholder_reward_operation` (ID 65).

---

## Custody & zero-sum invariant

Placing a bet / adding liquidity / escrowing a commit **debits** the account balance; the funds are held against the PM objects (they do not touch `current_supply` — there is **no emission**). At settlement every milli-VIZ is paid back out:

```
Σ winner_payout + oracle_take + creator_take + lp_bonus + LP_principal
    == Σ all bet amounts + LP_principal + forfeit_pool
```

Protocol fees (`pm_market_creation_fee`, `pm_oracle_registration_fee`) are routed to the DAO fund (`dynamic_global_property_object.committee_fund`), exactly like `committee_worker_create_request`.

---

## Regular operations

### `pm_oracle_register_operation` (ID 66)
**Auth:** `active` of `owner`

Registers an oracle with a bonded insurance deposit. `pm_oracle_registration_fee` → DAO fund; `insurance` is locked from the balance.

| Field | Type | Description |
|-------|------|-------------|
| `owner` | `account_name_type` | Oracle account |
| `insurance` | `asset` (VIZ) | Bonded stake, `≥ pm_min_oracle_insurance` |
| `fee_percent` | `uint16_t` | Standing resolution fee (bp, 10000 = 100%), `≤ pm_max_oracle_fee_percent`. Advisory list-price — the binding fee is quoted per market at accept |
| `fixed_fee` | `asset` (VIZ) | Standing per-market fixed fee (`≥ 0`); advisory |
| `rules_url` | `string` | Profile/rules, `≤ MAX_PM_PROFILE_URL_LEN` |
| `auto_accept` | `bool` | If `true`, matching markets go **live at creation** without a manual `pm_oracle_accept` (default `false`) |
| `auto_accept_creator` | `account_name_type` | Restrict auto-accept to this creator; **empty = any creator** |
| `auto_accept_resolver` | `account_name_type` | Required dispute setup for auto-accept: **empty = committee only** (`dispute_mode == 0`); a name = only `dispute_mode == 1` markets whose `dispute_resolver` equals this account |

> **Why the resolver pin matters.** Auto-accept fires only when the market's standing terms (`fee_percent`/`fixed_fee`) are within the oracle's list-price and the chain cap, the oracle is bonded and unbanned, and the dispute setup matches `auto_accept_resolver`. The resolver pin stops a market maker from slipping in a **colluding dispute resolver** under an oracle that would otherwise auto-accept: leave it empty to force public committee disputes, or name the one resolver you trust. A non-matching market simply falls back to the normal manual-accept (pending) flow.

### `pm_oracle_update_operation` (ID 67)
**Auth:** `active` of `owner`

Top-up/withdraw insurance and change policy. All fields optional. Withdrawing below `pm_min_oracle_insurance` or while serving active markets is rejected.

| Field | Type | Description |
|-------|------|-------------|
| `insurance_delta` | `optional<asset>` | Signed: `>0` top-up, `<0` withdraw |
| `fee_percent` | `optional<uint16_t>` | New advisory fee (bp) |
| `fixed_fee` | `optional<asset>` | New fixed fee |
| `rules_url` | `optional<string>` | New rules url |
| `auto_accept` | `optional<bool>` | Toggle auto-accept on/off |
| `auto_accept_creator` | `optional<account_name_type>` | Set the allowed creator (empty = any) |
| `auto_accept_resolver` | `optional<account_name_type>` | Set the required resolver (empty = committee only) |

### `pm_create_market_operation` (ID 68)
**Auth:** `active` of `creator`

Creates a market; the creator seeds the first liquidity and becomes the first LP. `pm_market_creation_fee` → DAO fund. For multi markets, `lmsr_b` must equal the node's `lmsr_b_from_liquidity(liquidity, N)`. If `dispute_mode == 1`, `dispute_resolver` must exist and **must not** equal `oracle` or `creator` (anti self-judging).

| Field | Type | Description |
|-------|------|-------------|
| `creator` / `oracle` | `account_name_type` | Creator; registered oracle (or creator for self-oracle) |
| `market_type` | `uint8_t` | 0 binary (CPMM), 1 multi (LMSR) |
| `outcomes` | `vector<string>` | 2 (binary) or 3..`pm_max_outcomes` labels; each `≤ MAX_PM_OUTCOME_LABEL_LEN` |
| `url` | `string` | Resolution criteria, `≤ MAX_PM_MARKET_TITLE_LEN` |
| `oracle_fee_percent` | `uint16_t` | **Offer ceiling** for the oracle % (bp): the most the creator will pay. The oracle quotes its actual (`≤` this) at accept. Self-oracle: final, `≤ pm_max_oracle_fee_percent` |
| `oracle_fixed_fee` | `asset` (VIZ) | **Offer ceiling** for the oracle fixed fee; the oracle quotes `≤` this at accept |
| `creator_fee_percent` / `liquidity_fee_percent` | `uint16_t` | Creator's own fees (bp), final at create; no governance cap (self-limiting) |
| `liquidity` | `asset` (VIZ) | Seed, `≥ pm_min_liquidity` |
| `lmsr_b` | `share_type` | Multi only: client-computed b (node-checked) |
| `betting_expiration` / `result_expiration` | `time_point_sec` | `result > betting`; `≤ now + pm_max_market_duration` |
| `time_penalty_type/value`, `penalty_curve_type` | `uint8/uint32/uint8` | Late-bet penalty config (profit-only, 1e6 scale) |
| `allow_early_resolution/cancellation/batch/instant_bet` | `bool` | Flags (multi forces `instant_bet=true`; `batch` needs `pm_commit_reveal_enabled`) |
| `endogeneity_tier` | `uint8_t` | 1 econ-data / 2 sports / 3 political |
| `dispute_mode` | `uint8_t` | 0 committee / 1 account |
| `dispute_resolver` | `account_name_type` | Required iff `dispute_mode==1`; ≠ oracle/creator |

### `pm_oracle_accept_market_operation` (ID 69)
**Auth:** `active` of `oracle`

Oracle accepts (`status → active`) or rejects (liquidity refunded to creator; `status → deleted`) a pending market. On accept the oracle **quotes its actual terms** via `oracle_fee_percent` + `oracle_fixed_fee` — each must be `≤` the creator's offer on the market and `oracle_fee_percent ≤ pm_max_oracle_fee_percent`. The quote is **frozen onto the market** and a `pm_market_accepted` virtual op is emitted (so history parsers see the launch + terms). Settlement later reads only these frozen fields — never the live median.

| Field | Type | Description |
|-------|------|-------------|
| `market_id` | `int64` | Pending market |
| `accept` | `bool` | Accept (true) or reject (false) |
| `oracle_fee_percent` | `uint16_t` | Oracle's quoted % (bp); `≤` offer & `≤ pm_max_oracle_fee_percent` |
| `oracle_fixed_fee` | `asset` (VIZ) | Oracle's quoted fixed fee; `≤` offer |

### `pm_place_bet_operation` (ID 70)
**Auth:** `active` of `account`

Places an instant bet on the live curve. `min_tokens` is the slippage floor. `weight` is set from the CPMM/LMSR tokens received.

| Field | Type | Description |
|-------|------|-------------|
| `market_id` | `int64` | Target market |
| `side` | `int8_t` | Binary: 0/1; multi: -1 |
| `outcome_index` | `int16_t` | Multi: 0..N-1; binary: -1 |
| `amount` | `asset` (VIZ) | Stake (`> 0`) |
| `min_tokens` | `share_type` | Slippage floor (0 = none) |
| `mode` | `uint8_t` | 0 instant, 1 batch |

### `pm_commit_bet_operation` (ID 71)
**Auth:** `active` of `account`

Commit-reveal phase 1 (requires `allow_batch` + `pm_commit_reveal_enabled`). Escrows `≥ pm_min_batch_bet`. `commitment = H(market_id ‖ account ‖ side ‖ outcome_index ‖ amount ‖ min_tokens ‖ salt)`. `no_reveal_fee_percent` **must equal** `median(pm_commit_no_reveal_penalty_percent)` (consensus-checked) and is snapshotted on the commit.

### `pm_reveal_bet_operation` (ID 72)
**Auth:** `active` of `account`

Commit-reveal phase 2: reveals the bet and enqueues it for the next batch epoch. `amount ≤ escrow_amount` (surplus refunded). The node recomputes the commitment from the revealed fields + `salt` and rejects a mismatch. A committed bet that is never revealed forfeits `no_reveal_fee_percent` of its escrow (→ `forfeit_pool`) via `pm_commit_forfeit`.

### `pm_cancel_bet_operation` (ID 73)
**Auth:** `active` of `account`

Cancels an open/queued bet (requires `allow_cancellation`). `min_return` is the refund slippage floor.

### `pm_add_liquidity_operation` (ID 74)
**Auth:** `active` of `provider`

Adds liquidity to an active market. Principal is returned unconditionally at settlement plus a pro-rata share of the LP bonus (liquidity fee + time-penalty pool + dust).

### `pm_withdraw_liquidity_operation` (ID 75)
**Auth:** `active` of `provider`

Withdraws liquidity (principal-safe). Locked from `betting_expiration` until resolution. `amount = 0` withdraws the full position.

### `pm_resolve_market_operation` (ID 76)
**Auth:** `active` of `oracle`

Oracle resolves to `winning_outcome`. Opens the dispute grace window (`result_expiration + pm_dispute_grace_sec`); after it elapses `pm_auto_payout` settles.

### `pm_no_contest_operation` (ID 77)
**Auth:** `active` of `oracle`

Oracle voids the market (all bets refunded, LP principal returned). Disputable. A `pm_no_contest_penalty_percent` slice of the dispute fee is slashed from insurance and distributed to refunded bettors.

### `pm_dispute_create_operation` (ID 78)
**Auth:** `active` of `disputer`

Files a dispute within the grace window; escrows `pm_dispute_fee`. Sets the oracle response deadline and (committee mode) the voting/auto-close timers.

### `pm_dispute_vote_operation` (ID 79)
**Auth:** `regular` of `voter` (mirrors `committee_vote_request`)

Committee-mode vote. `vote_outcome = -1` upholds the oracle; otherwise proposes the correct outcome. `vote_percent ∈ [-10000, 10000]` is the conviction weight tallied (by `|vote_percent|`) at finalize.

A committee dispute is an **open public hearing** — there is **no commit-reveal** (a deliberate, permanent choice: the DAO resolves disputes transparently to keep the platform's credibility). Because new arguments surface while voting is open, **a ballot is revisable**: re-sending `pm_dispute_vote` before `voting_end_time` **overwrites** your previous vote (latest ballot wins). The live tally is visible via `get_dispute_votes`.

### `pm_dispute_resolve_operation` (ID 80)
**Auth:** `active` of `resolver`

Account-mode verdict by the market's configured `dispute_resolver`. May slash `penalty_amount` of insurance and ban the oracle/creator until the given times.

### `pm_transfer_position_operation` (ID 81)
**Auth:** `active` of `from`

Reassigns all/part of a bet's `weight` to another account (no market impact). `memo` is plaintext or `#`-prefixed ECIES (standard VIZ memo).

### `pm_lazy_deposit_operation` (ID 82)
**Auth:** `active` of `account`

Deposits into the lazy liquidity pool (allocation-only in HF14, no leverage). Mints pool shares (MasterChef accounting, `reward_per_share` scaled 1e9).

### `pm_lazy_withdraw_operation` (ID 83)
**Auth:** `active` of `account`

Burns pool shares to withdraw principal + pending rewards. `emergency = true` before `unlock_time` applies `pm_lazy_emergency_penalty_percent` to the profit (penalty stays in the pool, added to `reward_per_share`).

---

## Virtual operations

Emitted by the PM consensus logic — either by a signed operation's evaluator (`pm_market_accepted`, the leverage vops) or by the per-block deadline processor `process_pm_markets()` when a market reaches an **expiration / deadline / dispute-grace / epoch boundary** (bounded at `pm_processing_cap_per_block`, oldest-deadline-first). They appear in account history but are never signed.

| ID | Operation | Trigger |
|----|-----------|---------|
| 84 | `pm_batch_settle_operation` | Epoch boundary (`head_block % pm_batch_epoch_blocks == 0`): queued bets executed on the epoch-open snapshot |
| 85 | `pm_commit_forfeit_operation` | `reveal_deadline` passed unrevealed: penalty → `forfeit_pool`, rest refunded |
| 86 | `pm_auto_payout_operation` | Dispute grace elapsed: parimutuel settlement + LP principal returned |
| 87 | `pm_dispute_finalize_operation` | Committee voting ended: tally decides; oracle penalty applied; market re-resolved or upheld |
| 88 | `pm_dispute_auto_close_operation` | Oracle never responded: anti-freeze refund, insurance slashed → DAO |
| 89 | `pm_oracle_missed_penalty_operation` | Oracle missed `result_expiration`: insurance slashed → DAO, all bets refunded |
| 90 | `pm_lazy_recall_operation` | Graduated recall of idle lazy-pool allocations |
| 94 | `pm_leverage_liquidate_operation` | Evaluator — mid-market leverage liquidation (opposing-bet `0` / cancel-bet `1` cascade) |
| 95 | `pm_leverage_resolve_operation` | Settlement — leveraged position force-closed: `outcome_index`, `won`, `pool_received`/`bettor_received`, `leverage` |
| 96 | `pm_market_accepted_operation` | Evaluator — market went live: oracle accepted, self-oracle, or auto-accept; frozen oracle terms + `self_oracle` |
| 97 | `pm_payout_operation` | Settlement — per active bet: `amount` (stake), `side`/`outcome_index`, `payout` (**0 on a loss**) |

> IDs 91–93 are the *regular* ops `pm_leverage_open`/`pm_leverage_close`/`pm_leverage_convert` (see the
> spec). Per-bettor results are `pm_payout`; the per-market `pm_auto_payout` remains a settlement marker.

---

## Settlement (parimutuel, zero-sum)

On `pm_auto_payout` the winning side is paid from the losing stakes:

```
losers_sum   = Σ amount of losing bets
oracle_fee   = floor(losers_sum × oracle_fee_bp   / 10000)     // bp, 10000 = 100%
creator_fee  = floor(losers_sum × creator_fee_bp  / 10000)
liq_fee      = floor(losers_sum × liquidity_fee_bp / 10000)
oracle_fixed = min(oracle_fixed_fee, losers_sum − fees)        // funded from pool, never minted
winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee − oracle_fixed + forfeit_pool

for each winning bet i (by curve weight):
    profit_i  = floor(winners_pool × weight_i / Σ weight)
    penalty_i = floor(profit_i × time_penalty_i / 1_000_000)   // profit only → LP
    payout_i  = amount_i + profit_i − penalty_i

LP: principal returned UNCONDITIONALLY + pro-rata share of (liq_fee + Σpenalty + rounding dust)
```

**Edge cases:** all bets on the winner → `winners_pool` is only `forfeit_pool` (each winner refunded principal); no winning tokens → whole pool becomes LP bonus; void result → full refund + LP principal. The pure split is unit-tested for exact conservation in `tests/pm/parimutuel_test.cpp`.

---

## Design decision: real depth only (no virtual/phantom liquidity)

A *virtual* (phantom) liquidity offset — reserves added to the pricing curve to flatten price impact but backed by **no real capital** and deleted at settlement (optionally median-voted) — is value-conservative in the closed bet→cancel→settle loop (the vAMM technique) and is tempting as a cold-start stabilizer for thin markets. **Onix deliberately does not implement it.** Lazy-Pool auto-allocation already delivers the same launch-smoothing with **real** capital that earns fees, has an accountable owner, and follows demand per market.

Phantom depth is rejected because, applied carelessly, it harms market structure and trust:

1. **Forgeable depth** — a thin or manipulated market can be dressed to look deep and liquid, eroding the price signal that real, costly capital would carry.
2. **Distorted information aggregation** — flattening the weight curve weakens the reward for early correct information and makes the price unresponsive to news (a stale forecast). The right depth is per-market and volume-dependent; one governance constant cannot track it.
3. **Conditional solvency** — solvent only while never redeemed or used as collateral. The moment it backs a cancel, early withdrawal, or leverage loan it must be excluded everywhere or it leaks real money (e.g. leverage sized/recovered against fake depth → real bad debt to pool depositors).
4. **No owner, no yield, no accountability** — it bears no risk and earns no fee for anyone real, deleting the retail safe-yield product on the markets it touches.

Onix keeps **only real numbers**: every unit of depth is real capital — redeemable, fee-earning, accountable — provided through the Lazy Pool (`pm_lazy_deposit` + auto-allocation). The conscious tradeoff is to forgo a cheap virtual stabilizer in favour of price-signal integrity and the solvency of every real-money path.
