---
title: "Passive LP — the lazy pool"
description: "The lazy pool is a passive product: you deposit VIZ and earn from leverage markup and fees. Shares are minted at the equity price, withdrawals go through a FIFO queue (free_balance ≥ 0 invariant), and the penalty applies only to rewards. The pool is the counterparty to leverage traders."
---

# Passive LP: the lazy pool

Don't want to pick a particular market and watch its depth — just put VIZ into the **lazy pool**. It is a passive product: your capital automatically feeds the system (leverage first of all) and earns yield, while you monitor nothing.

## The gist in two paragraphs

You deposit VIZ (`pm_lazy_deposit`) and receive pool **shares** at the pool's current equity price (not at face value: the share price is computed from `free + allocated − pending_withdrawals`, so that a new deposit made while capital is deployed does not get an inflated weight). The pool acts as the **counterparty to leverage traders**: a leverage loan is fronted by the lazy pool, and the markup plus the funding rate flow back into the pool as yield. On top of that comes a share of general fees.

Your principal in the pool comes back, but the payout may enter a **FIFO queue** if there is not enough free balance right now (capital is deployed in open leverage positions). The pool's invariant is `free_balance ≥ 0`: the pool never pays out more than is actually free; a withdrawal request is registered and settled as funds return. The early-withdrawal penalty is taken **only from rewards** — the principal is not cut.

## What happens, step by step

**Deposit.** `pm_lazy_deposit` — VIZ goes into the pool and you receive shares at the equity price. From there the pool itself decides where to route the capital (leverage, depth); you do not steer this manually.

**Yield accrues.** Leverage traders pay the markup and funding — that goes into the pool's yield. Your shares grow in value. The income is passive; there is nothing to click.

**Withdrawal — planned or emergency.** `pm_lazy_withdraw` (partial, by shares, or everything). If the pool has enough free balance, the payout is instant. If capital is deployed, a request (`pm_lazy_withdraw_request`) enters the **FIFO queue** and is settled as funds return from leverage/depth. An emergency withdrawal takes a penalty — but **only from accrued rewards**; your principal is not reduced.

**The queue and its order.** Requests are settled in arrival order on every event that returns free balance (leverage close, conversion, a new deposit). This protects the pool from going negative — a lesson from the early design, when an emergency withdrawal could drag `free_balance` below zero.

## What a passive LP needs to understand

- **The pool is the counterparty to leverage.** Unlike a direct market LP (curve depth, principal-protected against the outcome), the lazy pool carries the risk of leverage positions: its capital is borrowed by traders. The yield is higher, but the nature of the risk is different.
- **The share price is equity-based, not face value.** You receive shares at the pool's real value, not 1:1. That distributes yield fairly between old and new depositors.
- **A withdrawal may wait.** If all free balance is deployed, your request enters the queue. That is not a loss — the principal returns as positions unwind; but immediacy is not guaranteed.
- **The penalty hits rewards only.** An early/emergency withdrawal cuts yield, not principal. `free_balance ≥ 0` is a hard invariant.
- **Passivity is both a plus and a minus.** You do not pick markets and do not monitor depth, but you also do not control where the capital goes.

## Roles next to yours

- **Leverage trader** — borrows from your pool to bet on price; their markup is your income.
- **Active LP** — the opposite product in spirit: manual depth in a specific market, principal-protected against the outcome.
- **Market creator** — builds the markets on which leverage and depth operate.

Further reading: "Leverage trader" (who borrows from the pool and how), "Active LP" (how it differs from direct liquidity), "The lazy pool in detail" (equity price, FIFO withdrawal, yield step by step).
