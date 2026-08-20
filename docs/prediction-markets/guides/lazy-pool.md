---
title: "The lazy pool in detail — shares, equity price, FIFO withdrawal"
description: "Lazy pool mechanics: shares are minted at the equity price (free+allocated−pending_withdrawals), yield comes from leverage markup + funding + fees, withdrawals go through a FIFO queue with the free_balance ≥ 0 invariant, and the emergency-withdrawal penalty applies only to rewards."
---

# The lazy pool in detail: shares, equity price, withdrawal

This is a deep dive into the mechanics of the lazy pool. If you need the "why, and is this product for me" overview — start with the [Passive LP](./passive-lp) role; here we cover exactly how shares, yield and withdrawals are computed.

## Shares at the equity price

When you deposit (`pm_lazy_deposit`), the pool mints you **shares** not at a 1:1 face value, but at the current **equity price**:

> equity = free_balance + allocated − pending_withdrawals

- **free_balance** — the pool's free VIZ, not deployed right now.
- **allocated** — capital put to work (leverage loans, depth).
- **pending_withdrawals** — withdrawals already promised but not yet paid out (the FIFO queue).

Share price = equity / total number of shares. Why this way: if you counted `free_balance` alone, a new depositor arriving while capital is deployed would get an inflated weight in rewards (capital at work is not visible in free). The equity price splits yield fairly between old and new LPs. If equity ≤ 0 (an edge case), it falls back to 1:1.

## Where the yield comes from

- **Leverage markup.** A leverage loan is fronted by the pool; the fixed markup returns as yield.
- **Funding rate.** While a leverage position is open, funding accrues from it in favor of the pool.
- **Fees.** A share of the system's general fees.

Yield is reflected in the rising value of your share — nothing to claim or reinvest manually.

## Withdrawal: FIFO queue and the free ≥ 0 invariant

A withdrawal (`pm_lazy_withdraw`, partial by shares or full) burns your shares immediately, but the payout depends on the free balance:

- **Enough free_balance** → the payout is instant, as before.
- **Not enough** (capital sits in open leverage) → a `pm_lazy_withdraw_request` is registered in the **FIFO queue**, and the pool's `pending_withdrawals` field grows.

Requests are settled **in arrival order** on every event that returns free balance: leverage close/liquidation, conversion, a new deposit. Even when no capital returns, the queue still makes progress every block — the node's per-block cron pays up to `pm_settle_rows_per_block` queued requests, so a waiting withdrawal always inches forward. The hard invariant is **free_balance ≥ 0**: the pool physically cannot pay out more than is free. That is a lesson from the early design, when an emergency withdrawal could drag the balance negative (the pool handed out capital that had not yet returned).

To read the queue: `get_lazy_withdraw_requests(account)`; the pool state — `get_lazy_pool`; your position — `get_lazy_deposit`.

## Emergency withdrawal and the penalty

An emergency withdrawal takes a **penalty from accrued rewards only**; the principal is not cut. That is, you always get back what you put in (possibly through the queue), and haste costs you part of the yield, not capital.

## What you need to understand

- **Shares are equity-priced, not face-value.** A fair split of yield; with capital deployed, this is critical.
- **A withdrawal may wait in the queue.** Not a loss — the principal returns as leverage unwinds; immediacy is not guaranteed.
- **free_balance ≥ 0 is the law.** The pool never goes negative; payouts beyond the free balance enter the FIFO queue.
- **The penalty hits rewards, not principal.** An emergency exit costs less in money than in nerves.
- **The pool is the counterparty to leverage.** The nature of the risk differs from a direct market LP (see [Active LP](./active-lp)).

## Related

- [Passive LP](./passive-lp) — the role overview (is it for you).
- [Leverage trader](./leverage-trader) — who borrows from the pool and pays the markup.
- [Active LP](./active-lp) — direct market liquidity, for comparison.
- [Specification](../specification) — equity formulas, the withdrawal queue, invariants.
