# Prediction Market — Workflow by Role (HF14 Onix)

One **canonical scenario** traced through every participant. Each role has its own folder with an
interaction diagram, the **signed operations** it sends, the **virtual operations** that touch it,
and a **tokens sent / received** table for two outcomes:

- **Normal resolve** — oracle resolves, grace passes, `pm_auto_payout` settles. No dispute.
- **Disputed resolve** — oracle resolves **A**, a dispute **overturns to B**, then settlement runs.

All amounts are abstract **VIZ** units (ignore the 3 decimals). All percents are **bp** (10000 = 100.00%).
Settlement is strictly **zero-sum** — no tokens are ever minted; `current_supply` is untouched:

```
Σ winner_payout + oracle_take + creator_take + lp_bonus + LP_principal
        == Σ all bet amounts + LP_principal + forfeit_pool   (+ insurance slash, in a dispute)
```

---

## Canonical market **M** (binary CPMM, A vs B)

| Item | Value |
|------|-------|
| Engine | binary CPMM (`x·y=k`), `weight = tokens_out` |
| Seed liquidity (marketmaker) | **2000** → reserves A=1000 / B=1000 |
| `oracle_fee_percent` (oracle quote) | **1000** (10%) |
| `creator_fee_percent` | **500** (5%) |
| `liquidity_fee_percent` | **500** (5%) |
| `oracle_fixed_fee` (oracle quote) | **10** |
| `dispute_penalty_percent` | **+10000** (slash up to 100% of insurance ×consensus) |

Illustrative chain props: `pm_market_creation_fee` 5, `pm_oracle_registration_fee` 10,
`pm_min_oracle_insurance` 5000, `pm_dispute_fee` 1000, `pm_dispute_reward_multiplier` 30000 (**3×**),
`pm_no_contest_penalty_percent` 5000 (50% of dispute fee), `pm_oracle_penalty_percent` 500 (5% of
insurance on a missed deadline), `pm_lazy_emergency_penalty_percent` 5000 (50% of profit),
`pm_leverage_pool_profit_percent` **R = 10%**, `pm_lazy_alloc_percent` 2000 (20%).

### Roster

| Actor | Role | Stake / action |
|-------|------|----------------|
| **maker** | creator + first LP | seeds 2000 liquidity |
| **orac** | external oracle | insurance 5000; quotes fee 10% + fixed 10 |
| **LP1** | in-market liquidity provider | adds 1000 liquidity |
| **A** | bettor — winner | 100 on **A**, early; weight 100 |
| **C** | bettor — late winner | 100 on **A** at T+85%; weight 100; time-penalty **50%** |
| **B** | bettor — loser | 200 on **B**; weight 200 |
| **D** | leverage **×10** winner | collateral 10 + loan 90 (market **L**) |
| **E** | leverage **×5** liquidated | collateral 20 + loan 80 (market **L**) |
| **LZ1** | lazy-pool depositor | deposits 1000 |
| **disp** | disputer | escrows dispute fee 1000 |

> Curve weights (100 / 100 / 200) are written explicitly to keep the parimutuel arithmetic readable;
> a real CPMM hands out slightly less weight as reserves shift.

---

## Master ledger — NORMAL resolve (A wins)

`losers_sum = 200` (B). Fees off the losers' pool:
`oracle_fee = 200×10% = 20`, `creator_fee = 200×5% = 10`, `liq_fee = 200×5% = 10`, `oracle_fixed = 10`.
`winners_pool = 200 − 20 − 10 − 10 − 10 = 150`. `Σ winning weight = 200` (A 100 + C 100).

- **A**: profit `150×100/200 = 75`, penalty 0 → **payout 175**.
- **C**: profit 75, time-penalty `75×50% = 37` (→ LP) → **payout 138**.
- **LP bonus** = `liq_fee 10 + penalties 37 = 47`, split by time in market: **maker ~31 / LP1 ~16**.
- **oracle_take** = `oracle_fee 20 + fixed 10 = 30`. **creator_take** = `creator_fee 10`.

| Actor | sends | receives | net (this market) |
|-------|-------|----------|-------------------|
| maker | 2000 liquidity + 5 creation-fee | 2000 principal + 10 creator-fee + 31 LP-bonus | **+36** |
| orac | (10 reg-fee, 5000 insurance locked) | 30 oracle-take | **+30** |
| LP1 | 1000 liquidity | 1000 principal + 16 LP-bonus | **+16** |
| A | 100 | 175 | **+75** |
| C | 100 | 138 | **+38** |
| B | 200 | 0 | **−200** |

**Zero-sum:** in `= bets 400 + LP principal 3000 = 3400`; out `= 175+138+0 + 30 + 10 + 47 + 3000 = 3400`. ✔
The 5 creation-fee + 10 reg-fee leave to the **DAO fund** (not part of the market pool).

---

## Master ledger — DISPUTED resolve (oracle said A → overturned to B)

`disp` escrows `dispute_fee 1000`. The verdict overturns to **B**; the oracle is slashed.
With `dispute_penalty_percent = 10000` and consensus strength **100%**: `slash = 5000×100%×100% = 5000`.
Reward carve-out: `reward_target = fee×3 = 3000` → `bonus = 3000 − 1000 = 2000` (≤ slash). Disputer gets
`fee 1000 + bonus 2000 = 3000`. Remainder `slash − bonus = 3000 → forfeit_pool`.

Now **B wins**. `losers_sum = 200` (A 100 + C 100). Fees 20/10/10 + fixed 10.
`winners_pool = 200 − 50 + forfeit 3000 = 3150`. `Σ winning weight = 200` (B).
- **B**: profit `3150×200/200 = 3150` → **payout 3350**.
- **oracle_take** still `30` (the *market* fee is paid from the frozen config even when overturned — the
  punishment is the **insurance slash**, separate money). **creator_take** 10. **LP bonus** = liq 10.

| Actor | sends | receives | net (this market) |
|-------|-------|----------|-------------------|
| maker | 2000 + 5 | 2000 principal + 10 creator-fee + ~6 LP-bonus | **+11** |
| orac | insurance −**5000** slashed | 30 oracle-take | **−4970** |
| LP1 | 1000 | 1000 principal + ~4 LP-bonus | **+4** |
| A | 100 | 0 | **−100** |
| C | 100 | 0 | **−100** |
| B | 200 | 3350 | **+3150** |
| disp | 1000 dispute-fee | 3000 (fee back + 2000 reward) | **+2000** |

**Zero-sum:** in `= bets 400 + LP principal 3000 + dispute_fee 1000 + insurance slash 5000 = 9400`;
out `= B 3350 + oracle 30 + creator 10 + lp_bonus 10 + LP principal 3000 + disputer 3000 = 9400`. ✔
The slash 5000 splits into disputer bonus 2000 + forfeit 3000 (→ B via the winners' pool).

---

## Implementation status (verified against the code)

**Regular operations — all 21 present** in the `operation` variant (`operations.hpp`), validated +
evaluated in `pm_evaluator.cpp`:
`pm_oracle_register`, `pm_oracle_update`, `pm_create_market`, `pm_oracle_accept_market`, `pm_place_bet`,
`pm_commit_bet`, `pm_reveal_bet`, `pm_cancel_bet`, `pm_add_liquidity`, `pm_withdraw_liquidity`,
`pm_resolve_market`, `pm_no_contest`, `pm_dispute_create`, `pm_dispute_vote`, `pm_dispute_resolve`,
`pm_transfer_position`, `pm_lazy_deposit`, `pm_lazy_withdraw`, `pm_leverage_open`, `pm_leverage_close`,
`pm_leverage_convert`. ✔

**Virtual operations** — emitted by `database::process_pm_markets()` / the evaluators:

| Virtual op | Fires? | Trigger (code) |
|------------|--------|----------------|
| `pm_market_accepted` | ✔ | on `pm_oracle_accept_market` (accept) **and** self-oracle `pm_create_market` |
| `pm_payout` | ✔ | **per active bet** at settle — carries `account`, `market_id`, `bet_id`, `side`/`outcome_index`, `amount` (stake), `payout` (**0 on a loss**) |
| `pm_auto_payout` | ✔ | **once per market** at settle — a summary marker (`bets_sum`) alongside the per-bet `pm_payout`s |
| `pm_commit_forfeit` | ✔ | unrevealed commit past `reveal_deadline` |
| `pm_dispute_finalize` | ✔ | committee `voting_end_time` |
| `pm_dispute_auto_close` | ✔ | `auto_close_time` (anti-freeze) |
| `pm_oracle_missed_penalty` | ✔ | oracle missed `result_expiration` |
| `pm_lazy_recall` | ✔ | idle-allocation graduated recall step |
| `pm_batch_settle` | ✔ | epoch boundary |
| `pm_leverage_liquidate` | ✔ | mid-market liquidation: reason **0** opposing-bet, **1** cancel-bet (`cascade_liquidate`) |
| `pm_leverage_resolve` | ✔ | **settlement** of a leveraged position (reason=expiration in `force_close_positions`): carries `market_id`, `outcome_index`, `won` (solvent ⇒ positive), `pool_received`/`bettor_received`, and `leverage` (= `total_bet/collateral`) |

> A leveraged position is force-closed at its `cancel_value` at settlement: the pool takes
> `min(cv, obligation)`, the bettor gets the rest. **`pm_leverage_resolve`** marks that close (positive
> if `cv ≥ obligation`, else collateral lost); **`pm_leverage_liquidate`** is only for the *mid-market*
> opposing-bet / cancel-bet cascades. See [bettor D](bettor-d-leverage-winner/bettor-d-leverage-winner.md).

**API plugin `prediction_market_api.*`** — **21** read methods (`prediction_market_api.hpp`):
`get_market`, `list_markets`, `list_markets_by_oracle`, `list_markets_by_creator`, `get_market_outcomes`,
`get_market_weight_sums`, `get_market_bets`, `get_account_positions`, `get_market_liquidity`,
**`get_account_leverage_positions`**, **`get_market_leverage_positions`**, **`get_creator_ban`**,
`get_oracle`, `list_oracles`, `get_dispute`, `get_dispute_votes`, `get_lazy_pool`, `get_lazy_deposit`,
`get_pm_chain_properties`, `get_market_meta`, `list_markets_by_category`.

> Per-bettor results (`pm_payout`) and leverage settlements (`pm_leverage_resolve`) appear in
> `account_history`; leverage **positions** are now also queryable via `get_account_leverage_positions` /
> `get_market_leverage_positions`, and creator bans via `get_creator_ban`.

---

## Index

| Folder | Role |
|--------|------|
| [marketmaker/](marketmaker/marketmaker.md) | Market maker (creator + first LP) |
| [oracle/](oracle/oracle.md) | Oracle — register → accept (quote) → resolve |
| [oracle-banned/](oracle-banned/oracle-banned.md) | Banned oracle |
| [oracle-dispute-winner/](oracle-dispute-winner/oracle-dispute-winner.md) | Oracle upheld in a dispute |
| [oracle-dispute-loser/](oracle-dispute-loser/oracle-dispute-loser.md) | Oracle overturned + slashed |
| [bettor-a-winner/](bettor-a-winner/bettor-a-winner.md) | Bettor A — early winner |
| [bettor-b-loser/](bettor-b-loser/bettor-b-loser.md) | Bettor B — loser |
| [bettor-c-late-winner/](bettor-c-late-winner/bettor-c-late-winner.md) | Bettor C — late winner (time penalty) |
| [bettor-d-leverage-winner/](bettor-d-leverage-winner/bettor-d-leverage-winner.md) | Bettor D — leverage ×10 winner |
| [bettor-e-leverage-liquidated/](bettor-e-leverage-liquidated/bettor-e-leverage-liquidated.md) | Bettor E — leverage ×5 liquidated |
| [lp-market/](lp-market/lp-market.md) | Liquidity provider in the market |
| [lazy-pool/](lazy-pool/lazy-pool.md) | The lazy liquidity pool itself |
| [lp-lazy-pool/](lp-lazy-pool/lp-lazy-pool.md) | Liquidity provider in the lazy pool |
| [disputer-winner/](disputer-winner/disputer-winner.md) | Disputer — vindicated |
| [disputer-loser/](disputer-loser/disputer-loser.md) | Disputer — fee forfeited |
| [resolver-committee/](resolver-committee/resolver-committee.md) | Committee resolver (stake-weighted) |
| [resolver-account/](resolver-account/resolver-account.md) | Account-mode resolver (centralized) |
| [dispute-auto-close/](dispute-auto-close/dispute-auto-close.md) | Dispute forced to end (anti-freeze) |
