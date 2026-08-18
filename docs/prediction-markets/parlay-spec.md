# Parlay (accumulator) & system bets — design specification

Status: **design locked on scope & pricing** (owner 2026-08-18): leg quoting = **execution price**
of the stake's virtual size (q#600=A); first implementation round = **binary legs + plain parlay
only**, M-of-N systems are phase 2 (q#601=A). Implementation starts after mainnet launch
(q#593=B). The governance parameters below are **new** `pm_parlay_*` / `pm_system_*` median
parameters introduced by this feature — nothing existing changes; the listed defaults are the
proposed launch values. Consensus-level primitive, F1 rigor: every money path below must survive
the adversarial checklist before implementation starts.

## Problem

The coupon shipped in the Forecaster client (one transaction carrying N independent
`pm_place_bet` operations) is a *multi-bet*, not a parlay: each leg settles on its own, wins and
losses are independent. A **real parlay (accumulator/экспресс)** is a single stake on the
*conjunction* of N outcomes: it pays only if **every** leg wins, and the potential payout
multiplies the legs' odds. A **system bet "M of N"** is the standard generalization: the stake is
split across all C(N,M) M-leg sub-parlays, so the ticket survives up to N−M losing legs.

Parimutuel markets have no fixed odds — a leg's final coefficient is known only when its pool
closes. So a naive "multiply final parimutuel coefficients" parlay cannot be funded by the legs'
own pools: a cross-market conjunction payout is not backed by any single market's losers. The
parlay needs an explicit counterparty and a price fixed at bet time.

## Design summary

- **Counterparty: the Lazy Pool** — the same inventory-bearing fund that already fronts leverage
  loans. A parlay is a side bet against the pool at curve prices; it does **not** touch the legs'
  curves or pools.
- **Price fixed at bet time** from each leg's live curve (CPMM for binary, LMSR-softmax for
  multi): combo price `P = Π p_i`, potential payout `W = S · (1 − pm_parlay_margin) / P`, capped.
- **All-or-nothing settlement** driven by the legs' regular oracle resolutions: any leg lost →
  ticket dead immediately; a voided (no-contest) leg is *excluded* (its `p_i` multiplied back
  in — the bookmaker standard); all remaining legs won → pool pays `W` automatically after the
  last leg settles. No claim operation, consistent with `pm_payout` auto-payout philosophy.
- **Worst-case escrow**: the pool locks `W − S` at open, so every open ticket is fully funded by
  construction; the stake `S` enters `pool.free_balance` immediately.

## Mechanics

### Opening: `pm_parlay_open`

```
pm_parlay_open {
  account,
  legs: [ { market_id, side (binary) | outcome_index (multi) }, ... ],
  amount,            // stake S, liquid VIZ
  min_payout,        // slippage guard on W (curve may move between quote and inclusion)
  extensions
}
```

Validation / evaluator gates (all loud `FC_ASSERT`s):

1. `2 ≤ legs.size() ≤ pm_parlay_max_legs`; all `market_id` distinct.
2. Every leg market: status 1 (active), betting still open **with at least
   `pm_parlay_min_time_left` seconds** to that leg's `betting_expiration` (anti-sniping: parlays
   are priced on the live curve, so late steam on a nearly-closed leg is the cheapest attack).
3. Every leg market allows instant bets (`allow_instant_bet`), is **not** hidden below the
   oracle risk-floor, and its curve depth passes the manipulation gate (below).
4. `pm_parlay_enabled` median kill-switch is on; the pool has capacity (below).
5. `S ≥ pm_min_bet`; account has liquid `S` (same funding rules as `pm_place_bet`).

**Leg price `p_i`** is the **execution price of the leg's proportional virtual size**, not the
mid: quote the curve for a hypothetical instant bet of `S` on that side/outcome and use the
resulting average price. Mid-price quoting hands an attacker the spread for free; execution
pricing makes moving a thin curve *before* opening a parlay pay the mover's own slippage first.
The virtual quote does **not** mutate the curve.

**Combo payout**:

```
P      = Π p_i                    (0 < p_i < 1, so P ∈ (0,1))
W_raw  = S · (1 − pm_parlay_margin) / P
W      = min(W_raw, pm_parlay_max_payout, S · pm_parlay_max_multiplier)
FC_ASSERT(W ≥ min_payout)         // user slippage guard
FC_ASSERT(W > S)                  // a parlay that cannot profit is a mis-click, reject
```

**Funding at open** (single balanced move, conservation-exact):

```
account.balance      -= S
pool.free_balance    += S
pool.parlay_fund_used += (W − S)        // worst-case escrow, W − S > 0 by the assert above
pool.free_balance    -= (W − S)
```

Capacity gate: `parlay_fund_used + (W − S) ≤ free-only base × pm_parlay_fund_percent` — the same
free-only base rule the owner fixed for leverage (q#566=A): obligations are measured against
`free_balance` only, never NAV.

### Object

```
pm_parlay_object {
  id, account,
  legs: [ { market_id, side, outcome_index, price_ppm,   // p_i fixed at open, parts-per-million
            state } ],                                    // 0 pending | 1 won | 2 lost | 3 void
  stake, payout,                 // S, W (asset)
  margin_ppm_at_open,
  opened_at,
  status,                        // 0 open | 1 won(paid) | 2 lost | 3 refunded(all-void)
  last_settled_leg_count
}
```

Indexes: `by_id`, `by_account`, and **`by_market_leg` (market_id → parlay ids)** so per-market
resolution can find affected tickets without scanning. The per-market fan-out is bounded by
`pm_parlay_max_open_per_market` (cap enforced at open via bounded index probe — counter-free,
see the commit-cap precedent M4 and the computed-vs-counter rule).

### Settlement

Hooked into the same per-block `process_pm_markets()` walk that already finalizes payouts —
parlay legs react to the leg market reaching **settled** state (post dispute-grace), not to the
raw resolve, so dispute reversals are automatically respected:

- **Leg lost** → ticket `status = 2` immediately: release the escrow
  (`parlay_fund_used -= (W − S)`, `free_balance += (W − S)`). The stake already sits in the pool
  — it *is* the pool's revenue on lost tickets. Emit `pm_parlay_lost` virtual op.
- **Leg void** (no-contest / missed-resolution void) → `state = 3`; payout shrinks:
  `W' = W · p_i` (multiply the excluded leg's price back in), clamped `W' = max(W', S)`; release
  the escrow delta. If **all** legs void → refund `S` (`status = 3`, pool pays back the stake,
  full escrow released). Emit `pm_parlay_leg_void`.
- **Leg won** → `state = 1`; when the **last** pending leg settles won: pay
  `pool.free_balance -= W; account.balance += W;` release escrow bookkeeping
  (`parlay_fund_used -= (W − S)`; the extra `W − S` was already carved out of free at open, so
  paying `W` nets free_balance `−S` versus pre-open — exactly the pool's loss on a won ticket).
  `status = 1`, emit `pm_parlay_won` (per-account virtual op for account_history).

Work per settled market is bounded: at most `pm_parlay_max_open_per_market` tickets touched, each
O(legs) ≤ `pm_parlay_max_legs`. No unbounded per-block loops (audit class H3/M3).

**Invariants** (debug-asserted, snapshot-import verified like the TOKEN anchor):

1. `parlay_fund_used == Σ_open (W_i − S_i)` — recomputable by walking open tickets.
2. `pool.free_balance ≥ 0` always (FIFO-queue rule untouched; parlay payouts go through the same
   "never below zero" discipline — escrow guarantees the funds exist).
3. Ticket terminal states are absorbing; `last_settled_leg_count` monotonic.

### System bets "M of N"

One operation, `pm_system_open`, same leg rules plus `2 ≤ M < N ≤ pm_parlay_max_legs` and
`C(N,M) ≤ pm_system_max_combos` (e.g. 256 — keeps worst-case settlement work and escrow math
trivially bounded). Semantics: stake `S` splits into `C(N,M)` equal sub-stakes, each sub-parlay
priced/capped exactly as above from the same fixed `price_ppm` set; escrow = Σ over combos.
Stored as one object (legs + M + per-combo derived data computed on settlement, not stored).
"7 из 8" = M=7, N=8, 8 combos. Settlement: on last leg settle, count won/void legs, enumerate
combos arithmetically (no recursion), pay Σ of winning combos' payouts. Refund/void/shrink rules
apply per combo. Deferred to **phase 2 of implementation** but specified now so the object layout
and params don't churn (snapshot-layout lesson: batch B → redeploy-only-by-snapshot).

## Adversarial review (pre-implementation)

| Attack / failure class | Vector here | Mitigation in this design |
|---|---|---|
| Curve manipulation (the main one) | Push a thin leg's curve, buy the parlay at distorted `p_i`, unwind | Execution-price quoting (mover pays own slippage), `pm_parlay_min_depth` gate per leg (min curve liquidity), `pm_parlay_margin` house edge, hard caps `max_payout`/`max_multiplier`, `min_time_left` window |
| Unbounded accumulation (#141 class) | `parlay_fund_used` grows, later subtracted | Escrow released on every terminal transition, recomputable invariant 1, clamp at 0 with loud ilog on mismatch |
| Sign-flip / underflow | `W − S`, `W' = W·p_i` shrink, refunds | `W > S` asserted at open; void-shrink clamped at `S`; all subtractions clamped `max(x,0)` + debug-assert |
| Missing floor/assert | "escrow covers payout by construction" | Explicit debug-assert on invariant 1 each maintenance block + snapshot-import re-check (anchor pattern) |
| DoS / per-block work | Many tickets on one market; many legs | `max_open_per_market` (bounded index probe), `max_legs`, `max_combos`, settlement O(tickets×legs) bounded |
| Governance extremes (F3 class) | Median sets margin=0 / multiplier=10^9 | `validate()` bounds on **every** new parameter (margin ≤ 20%, multiplier ≤ 10000×, legs ≤ 16, combos ≤ 1024, percent params bp-checked ≤10000) — and every param **wired into the median loop** (retention-param lesson) |
| Oracle/dispute interplay | Pay before dispute settles, then reversal | Legs react to *settled* (post-grace) state only, same cutoff as `pm_payout` sweep |
| Self-dealing LP | Bettor is also pool depositor | No special path needed: pool P&L is socialized exactly like leverage; margin + caps bound extraction |
| Snapshot round-trip | New object/fields lost on import | Full-reflect export; import with `contains()` guards; forward-only counters get seeds or are recomputable (invariant 1 is recomputable — preferred) |

## New governance parameters (chain_properties, next version bump)

`pm_parlay_enabled` (kill-switch, default **off** — leverage precedent),
`pm_parlay_margin` (bp, default 500 = 5%, bound ≤ 2000),
`pm_parlay_max_legs` (default 8, bound 2..16),
`pm_parlay_max_multiplier` (default 1000×, bound ≤ 10000),
`pm_parlay_max_payout` (VIZ, default 100k),
`pm_parlay_fund_percent` (bp of pool free, default 2000, bound ≤ 5000),
`pm_parlay_min_depth` (VIZ, default 1000),
`pm_parlay_min_time_left` (sec, default 3600),
`pm_parlay_max_open_per_market` (default 1000, bound ≤ 10000),
`pm_system_max_combos` (default 256, bound ≤ 1024).

All ten must appear in: `validate()` with bounds, the median-vote loop, `get_pm_chain_properties`,
serializers (C++ ⇄ js ⇄ php ⇄ python lock-step — vop/param drift lesson from P1), and the
snapshot export/import of `chain_properties_pm`.

## Client surface (after node lands)

Coupon screen grows a mode switch: **Multi** (today's N independent bets) / **Экспресс** (one
`pm_parlay_open`) / **Система M из N** (phase 2). The coupon already collects legs in exactly the
right shape; the parlay quote (`Π p_i`, potential payout, caps) is computable client-side from
the same curve reads the bet form uses, with `min_payout` as the slippage guard. Read API:
`get_account_parlays`, `get_market_parlays` (newest-first default per q#383=A), parlay card in
activity (History/Active tabs).

## Decisions log

- **q#600=A (2026-08-18):** leg price = execution price of the stake's virtual size on the live
  curve (not mid) — the curve manipulator pays their own slippage first.
- **q#601=A (2026-08-18):** first round = binary legs + plain parlay; M-of-N systems and multi
  (LMSR) legs are phase 2. Object layout for systems is still specified above so the state shape
  doesn't churn between rounds.
- Launch defaults for the new `pm_parlay_*` parameters (margin 500 bp, max_payout 100k VIZ,
  max_multiplier 1000×, max_legs 8, kill-switch default **off**) stand as proposed unless the
  owner overrides specific values before implementation; all are median-votable post-launch
  anyway, the defaults only seed the very first median.
