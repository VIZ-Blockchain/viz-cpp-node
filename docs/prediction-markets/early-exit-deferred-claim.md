# Early-exit deferred claim (F1 / #300)

Status: design locked (owner 2026-08-08), implementation in progress on branch `pm`.

## Problem

The market is a hybrid: a CPMM (binary) / LMSR (multi) **curve** for entry and early exit,
and **pari-mutuel** settlement for positions held to resolution. Any round-trip through the
curve (buy then sell before settlement) realizes a trading P&L against the curve depth — the
LPs — exactly like Uniswap impermanent loss. But the design promises LPs **principal
protection** (fee-only, no IL). Those two are in tension.

Two code paths exit against the curve on a binary market:

- **Leverage** (`liquidate_position` / `pm_leverage_close`): always; force-closed at
  settlement; amplified by the pool loan.
- **Regular bet cancel** (`cancel_bet`, F2 curve-priced refund): during the betting window.

Both route `residual = stake − curve_refund` to `forfeit_pool` (signed). When an early exit is
*profitable* (`curve_refund > stake`), `forfeit_pool` goes **negative**. At settlement
`winners_pool = losers_sum − fees + forfeit_pool`; if leverage/early-exit profit outran the
losing stakes, `winners_pool < 0`, is floored to 0, and the shortfall (`uncovered`, F1) is
charged to LP principal — or minted when LP principal is exhausted. Reachable: proven with a
gate-respecting CPMM simulation (one-sided pump, `uncovered = 7316`); Babin's replay corpus hit
it in 1163/1988 pairs.

Root cause: **curve-priced exit pays a bonding-curve value that is not bounded by the losing
pool**, while settlement pays pari-mutuel. The gap lands on the LP.

## Model (locked)

Early exits no longer extract curve value from LPs. Instead an exit records an
**outcome-contingent deferred claim**, funded at settlement from a **bounded slice of the
losing pool**.

### Recorded on exit
`{ position_id, kind (bet|leverage), chosen_outcome, claim_amount, exit_time }`.

### Regular bet cancel
- **Principal returned immediately, unconditionally**: `refund = min(curve_refund, stake)`
  (own money, not borrowed). A cancel can cut losses or break even but never realizes
  curve-profit at cancel time.
- **Profit tail** `max(curve_refund − stake, 0)` → deferred claim on the chosen outcome.

### Leverage close / liquidate
- **Collateral is NOT separately returned** — it is first-loss margin for the pool. The pool
  recovers its obligation (`loan·(1+R) + funding`) from `cv` first; if `cv < obligation`, the
  collateral covers the gap.
- **Residual** `max(cv − obligation, 0)` → deferred claim on the chosen outcome.
- Leverage is therefore a **leveraged directional bet**, not a volatility harvest: you profit
  only if your outcome wins and the bucket has room; a wrong outcome loses the collateral.
- NB two distinct predicates now: **solvency** (`cv ≥ obligation`, governs loan recovery) vs
  **outcome-win** (governs the right to a claim). A position can be solvent yet on the losing
  outcome → pool made whole, claim = 0.

### Settlement
1. `bucket = pm_early_exit_reward_cap_percent × losers_sum / 10000` (default 33%).
2. Collect deferred claims on the **winning outcome only** (losing-outcome claims → 0).
3. Pay them **FIFO by `exit_time`** (first out, first paid) until the bucket is drained; no
   per-position cap (owner 2026-08-08: FIFO ordering + total bucket is the bound). A claim that
   the remaining bucket cannot fully fund is paid partially; the rest is unpaid (haircut).
4. **Any unused bucket returns to the winners' pool** — held winning bets share it pari-mutuel.

### Guarantees
```
paid_claims ≤ bucket = cap · losers_sum
winners_pool = losers_sum − fees − paid_claims + honest_forfeits
             ≥ (1 − cap) · losers_sum − fees  ≥ 0     (cap < 100%)
```
- `uncovered` is **impossible by construction**; no mint; **LP principal never touched**; the
  lazy pool bears no leverage IL.
- A **losing outcome never profits** (owner requirement).
- Held winners receive `≥ (1 − cap)` of the losing pool plus any unused bucket.
- Early exit is a **bounded, contingent discount** (≤ cap, FIFO) vs holding to resolution
  (full pari-mutuel share) → no arbitrage against holding; a deliberate liquidity discount.

## Chain parameter

`pm_early_exit_reward_cap_percent` (uint16, bp, default **3300** = 33% of `losers_sum`).
Median-voted validator param; `validate()` bounds `≤ 10000`. Added to
`chain_properties_pm` + FC_REFLECT + `calc_median` (DONE, single-TU verified).

## Implementation touchpoints (node)

- [x] chain param `pm_early_exit_reward_cap_percent` (struct/validate/reflect/median).
- [ ] object `pm_deferred_claim_object` (+ index by market, by exit_time) — space 30, append at
      end of `object_type` enum (snapshot-safe, like `pm_lazy_withdraw_request`).
- [ ] `cancel_bet`: return `min(curve_refund, stake)`, record profit-tail claim; stop routing a
      negative residual to `forfeit_pool`.
- [ ] `liquidate_position` / `pm_leverage_close`: pool takes obligation, record `cv − obligation`
      claim, tag chosen outcome; drop immediate `bettor_received`; stop negative `forfeit_pool`.
- [ ] settlement (`settle_market`): after force-close, compute `bucket`, pay winning-outcome
      claims FIFO by exit_time, remainder → winners' pool; remove the `uncovered`/F1
      `settle_liquidity` charge path (LP no longer absorbs it).
- [ ] snapshot: include `pm_deferred_claim_object` in allowlist (+ import handler).
- [ ] virtual op `pm_early_exit_claim_paid` (account, market, claim, outcome, ts) for history.
- [ ] read API: `get_deferred_claims(market)` / by account, for clients.

## Client / lib / docs follow-ups
- viz-js-lib / viz-php-lib / viz-python-lib: new chain param in v5 chain_properties_pm
  (serialization lock-step, byte-verify), any new read method / vop.
- Forecaster: notices + operation descriptions (leverage = directional, early exit = bounded
  discount), show pending deferred claim on positions.
- WebVIZWallet: same operation-description updates if surfaced.
- Scientific article: `early-exit choice` with the math (regular vs leverage-from-lazy-pool,
  validator-set reward cap).

## Rejected alternatives (why)
- Smear `uncovered` across all LP / mint (status quo) — breaks LP principal promise.
- Localize only to lazy pool — pool can be exhausted; still an approximation; cancels leak.
- Cap the win at exit — you don't know `losers_sum` at exit time; deferral removes that.
- Full-AMM settlement — abandons the pari-mutuel thesis of VIZ.
The deferred outcome-contingent claim is the only option giving a **hard** LP guarantee while
staying pari-mutuel.
