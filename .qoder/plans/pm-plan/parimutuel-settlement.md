# Plan: Unified Parimutuel Settlement (make Binary settle like Multi)

> **STATUS: ✅ IMPLEMENTED.** Binary now settles parimutuel in [api.php](module/api.php) (resolve block ~1661 + dispute-recalc block ~2105), and the docs (spec §5/§8/§10, whitepaper §2.2, betting-rules, governance roadmap) are updated. This file is retained as the design rationale.
>
> Make **Onix Binary** use the same **parimutuel settlement** as Onix Multi: a winner's payout becomes a *proportional share of the losers' pool by weight*, instead of the absolute `payout = weight`. The CPMM stays as the **pricing/probability engine**; only settlement changes.
>
> 🇷🇺 Russian: [plan_unified_parimutuel_binary-ru.md](plan_unified_parimutuel_binary-ru.md). Related: [plan_batch_commit_reveal_betting.md](plan_batch_commit_reveal_betting.md) §6.1 (this change makes binary snapshot-safe too).

## 1. Motivation

Today the two market types settle differently (verified in code):

| | Current Binary — [api.php:1664](module/api.php#L1664) | Multi — [lmsr_math.php:249](module/lmsr_math.php#L249) |
|---|---|---|
| Payout | `payout = weight` (absolute) | `payout = amount + winners_pool × weight/Σweight` |
| Solvency basis | curve invariant `Σweight ≤ winning_reserve + winning_bets` | parimutuel (capped at `losers_sum`) |

Problems with binary's absolute-weight model:
- **Residual is not routed to bettors.** Winners get `Σweight`, LP gets `L + fees`; the leftover (`losers' stakes − winners' profit`) is not distributed to anyone (see [betting-rules](betting-rules-and-system-overview.md) §785 — "surplus = 19 mVIZ" goes uncredited).
- **A lone/edge winner can receive *less* than their stake** (betting-rules:788 — bet 1000 → weight 981).
- **Absolute weight must be curve-priced**, so snapshot/batch pricing is unsafe for binary → blocks front-run protection (the entire binary branch of [batch plan §6.1](plan_batch_commit_reveal_betting.md)).

## 2. The change

Binary keeps the CPMM for **pricing** (probability display + assigning `weight = tokens_received`), and switches **settlement** to parimutuel — byte-for-byte the Multi model:

```
// On resolution (side A wins):
losers_sum           = b_bets_sum                       // all losing-side bets
oracle_fee           = floor(losers_sum × oracle_fee‰   / 1000)
creator_fee          = floor(losers_sum × creator_fee‰  / 1000)
liq_fee              = floor(losers_sum × liquidity_fee‰ / 1000)
winners_pool         = losers_sum − oracle_fee − creator_fee − liq_fee
total_winning_weight = Σ weight over winning-side bets

for each winning bet i:
    profit_share = floor(winners_pool × weight_i / total_winning_weight)
    penalty      = floor(profit_share × time_penalty_i / 1_000_000)   // profit only
    payout_i     = bet_amount_i + profit_share − penalty

LP: principal returned UNCONDITIONALLY + time-weighted share of liq_fee (+ penalty pool)
```

`weight` is now purely a **relative claim** (a ratio device); reserves/`k` are a **pure pricing engine** (the analogue of `q` in LMSR). This is literally the existing `lmsr_math.php` `settle()` applied to weights that come from the CPMM instead of LMSR.

## 3. What it fixes

1. **One settlement model for both types:** *"the AMM assigns weights; losers fund winners pro-rata by weight."* Binary uses CPMM weights, Multi uses LMSR weights. Same `settle()`.
2. **Exact money conservation, no leak:** `out = L + winning_bets + winners_pool + fees = L + winning_bets + losing_bets = L + all_bets = in`. The previously-uncredited residual now goes to winners.
3. **Trivial LP guarantee** (same as Multi): total payout is capped at `losers_sum`; LP principal is untouched. The curve invariant `Σweight ≤ reserves` is **no longer needed** for solvency.
4. **Fixes the weird edge:** all bets on the winner → `winners_pool = 0` → each winner gets their `bet_amount` back (refund), never less.
5. **Snapshot/batch pricing becomes safe for binary** → full front-run immunity for binary too (see §6).

## 4. Conservation proof

```
Money IN  = L (LP) + a_bets + b_bets
Money OUT = L (LP principal) + Σ(winning bet_amount) + winners_pool + (oracle+creator+liq fees)
          = L + winning_bets + (losers_sum − fees) + fees
          = L + winning_bets + losing_bets
          = L + a_bets + b_bets = Money IN   ✓ (exact, for any weights)
```
Solvency holds for **any** weights → off-curve (snapshot) weights are harmless. This is the property binary currently lacks.

## 5. Edge cases (mirror Multi / spec §6)

| Scenario | Outcome |
|----------|---------|
| All bets on the winner (`losers_sum = 0`) | `winners_pool = 0` → every winner refunded `bet_amount`. LP principal returned. |
| No bets on the winner | `winners_pool` undistributed → LP bonus. |
| Single winner | Receives `bet_amount + winners_pool`. |
| Zero volume | LP principal returned; no fees, no payouts. |

## 6. Impact on batch / commit-reveal ([batch plan §6.1](plan_batch_commit_reveal_betting.md))

The binary/multi split collapses: since payout is now capped at `losers_pool` regardless of weights, **both types can use epoch-open snapshot pricing for the batch** → full intra-epoch manipulation immunity for binary too. `min_tokens` still gates slippage. The "Binary = live curve + price band" carve-out and the `batch_price_band_permille` param become unnecessary.

## 7. Tradeoff (must be acknowledged)

| | Current Binary | After (parimutuel) |
|---|---|---|
| Payout known | **at bet time** (fixed odds — `weight` locked) | at resolution (floating share) |
| Model | fixed-odds | parimutuel (horse-racing / Multi style) |

The only real cost: a bettor's payout is no longer fixed at bet time — it depends on the final `total_winning_weight` and `losers_sum`. This is the standard parimutuel tradeoff, already accepted in Multi; it rewards early/underdog bettors with a larger weight share. **Decision needed:** accept the move from fixed-odds to floating-odds binary.

## 8. Reserves & k

- Reserves are still updated on each bet for **pricing only** (probability + `weight`); `k` stays constant under betting (unchanged from spec §5).
- Reserves no longer gate payout. LP principal is returned as a separate unconditional line (like the Multi subsidy), not "reconstructed from reserves."

## 9. Code changes

- [api.php](module/api.php) binary resolve block (1655–1742) and the dispute recalculation block (2086–2166): replace `raw_payout = weight` with the parimutuel block from [lmsr_math.php:224-280](module/lmsr_math.php#L224) (`settle()`), passing CPMM `weight` as the token field. Both binary and multi can share one `settle()` helper.
- Apply the same edge-case handling already present in `lmsr_math.php`.
- Remove the now-unneeded binary-specific surplus/solvency assumptions.

## 10. Spec sections to update

| Section | Change |
|---------|--------|
| §5 Onix Binary | Settlement is now parimutuel; `weight` = relative claim, not VIZ-denominated payout. |
| §6 Onix Multi | Note both types share one settlement model. |
| §7 Fee Structure | Already losers-funded; unify wording across types. |
| §10 Resolution & Payout | Binary payout formula → `bet_amount + winners_pool × weight/Σweight`. |
| batch plan §6.1 | Binary now snapshot-safe; drop the live-curve/price-band carve-out and `batch_price_band_permille`. |
