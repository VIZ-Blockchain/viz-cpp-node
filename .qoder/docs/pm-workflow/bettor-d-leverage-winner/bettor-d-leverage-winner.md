# Bettor D — leverage ×10 winner

Part of [PM Workflow](../README.md). **D** opens a **×10** boosted position: **10 collateral + 90 loan**
from the [lazy pool](../lazy-pool/lazy-pool.md) = **100** staked on side A. Shown in an isolated
leverage market **L** (binary, `pm_leverage_enabled=true`, `R = 10%`) so the loan/interest math stays
clean. When A wins, D keeps the upside on the full 100 after repaying the pool its loan + interest.

Key figures: `pool_profit = loan 90 × R 10% = 9`; `liquidation_threshold (obligation) = 90 × 1.10 = 99`.

## Interaction diagram

```mermaid
flowchart LR
  D -->|pm_leverage_open<br/>collateral 10 + loan 90| POS[(pm_leverage_position<br/>total_bet 100, obligation 99)]
  POOL[(lazy pool)] -.loan 90.-> POS
  POS --> L[(market L, side A)]
  L ==>|settle: force_close at cancel_value| VR[[pm_leverage_resolve won=true, leverage=10]]
  VR -->|min(cv,obligation) 99| POOL
  VR -->|cv 200 − 99 = 101| D
```

## Operations it sends (signed)
- `pm_leverage_open` — `collateral 10`, `loan 90`, `outcome_index 0 (A)`, `min_tokens`, `max_slippage_percent`.
  Pool: `free_balance −= 90`, `leverage_fund_used += 90`.
- (optional) `pm_leverage_close` — voluntary, only if `cancel_value ≥ obligation`; or
  `pm_leverage_convert` — pay off the loan + a profit-share fee to turn it into a plain bet.

## Virtual operations that touch it
- `pm_leverage_resolve` at settlement — `force_close_positions` unwinds the position at its
  `cancel_value`; the pool takes `min(cv, obligation) = 99` (loan 90 + interest 9), the bettor gets
  `cv − pool_received`. The vop carries `won` (solvent ⇒ true), `outcome_index`, `pool_received`,
  `bettor_received`, and **`leverage`** (= `total_bet / collateral` = 10).
- `pm_leverage_liquidate` — only if the position is liquidated **mid-market** (opposing bet / cancel
  cascade) before settlement — the losing path of [bettor E](../bettor-e-leverage-liquidated/bettor-e-leverage-liquidated.md).

## Tokens — normal resolve (A wins)
| sends | receives | net |
|-------|----------|-----|
| collateral **10** | cancel_value 200 − obligation 99 = **101** | **+91** |

A profitable position closes at its `cancel_value` (here 200 — the curve value of its A-tokens after the
market moved its way). The pool reclaims its `obligation 99` (loan 90 + **9 interest**); D keeps the rest
on just 10 of its own → **+91**. Pool net: **+9**. (Not a parimutuel share — leverage settles by
liquidation, never via `pm_auto_payout`.)

## Tokens — disputed resolve (overturned to B)
| sends | receives | net |
|-------|----------|-----|
| collateral 10 | 0 | **−10** |

The overturn flips A→B, so the winning position becomes a **loss**: it is settled lost, the pool is
repaid from D's collateral first, and D forfeits its 10. (Leverage magnifies both directions; the
`expiration_buffer` normally force-closes positions before a contested resolution can flip them.)

## Notes
- Zero-sum (market L): in `= D 10 + pool 90 + counterparty 100 = 200`; out `= D 101 + pool 99 = 200`. ✔
- The pool only ever fronts `pm_leverage_fund_percent` of its `free_balance`, capped per position.

## Code verification & how to observe
| Element | In code | Where |
|---------|---------|-------|
| `pm_leverage_open` (collateral + pool loan, obligation, kill-switch) | ✔ | `pm_leverage_open_evaluator` |
| `pm_leverage_close` (voluntary, `cv ≥ obligation`) | ✔ | `pm_leverage_close_evaluator` |
| `pm_leverage_convert` (pay loan + fee → normal bet) | ✔ | `pm_leverage_convert_evaluator` |
| settlement = force-close at `cancel_value` | ✔ | `force_close_positions` → `liquidate_position(reason=2)` |
| vop `pm_leverage_resolve` (settlement) | ✔ | carries `market_id`, `outcome_index` (A), `won=true`, `pool_received` 99, `bettor_received` 101, `leverage` 10 |
| vop `pm_leverage_liquidate` | — | only for *mid-market* opposing/cancel cascades, not this settlement |

**Observe via plugin:** **`get_account_leverage_positions(D, …)`** / **`get_market_leverage_positions(L, …)`**
(your `pm_leverage_position`: `collateral`, `loan`, `liquidation_threshold`, `status`, `bettor_received`);
the realized close is the `pm_leverage_resolve` vop in `account_history`; pool via `get_lazy_pool`.
