---
title: "Bet cancellation — exit at the curve price"
description: "pm_cancel_bet sells the position back at the current curve (curve-priced) instead of refunding face value. The amount you receive is never more than your stake (cap-at-stake): a losing difference goes into the market's forfeit_pool, while a profitable excess becomes an outcome-contingent deferred claim. Cancellation is possible before betting closes, if the market allows it."
---

# Bet cancellation: exit at the curve price

Changed your mind before the market closes? On markets where it is allowed, a bet can be cancelled (`pm_cancel_bet`). But it is important to understand: this is **not** "give me my money back", it is selling the position back at the current price. Let's go through how much comes back and why.

## The gist in two paragraphs

A cancellation is an exit at the **current curve** (curve-priced), not a refund of face value. You sell your shares (weight) at the price that has formed by the moment of cancellation. If the market has shifted since your bet, you get back **less** than you put in — that is a fair exit price, not a penalty. And conversely: what you receive is **never more than your stake** (cap-at-stake) — curve profit cannot be cashed out at the moment of cancellation.

What happens to the difference depends on its direction. If the curve pays out **less** than the stake (the price moved against you) → the shortfall (`curve_residual`) **stays with the market**, goes into the `forfeit_pool` and is distributed to winners at settlement. If it comes out **higher** than the stake (the price moved in your favor) → the excess is neither given to the market nor paid immediately: it becomes your **outcome-contingent deferred claim** and is settled at settlement if your outcome won (details — "[Early exit and the deferred claim](./early-exit)"). No tokens are minted or burned in the process. You can cancel only before betting closes (`betting_expiration`) and only if the market was created with cancellation allowed.

## How it works

**Selling at the curve.** The node computes what your shares are worth at the pool's current reserves and returns that amount. The price is the same curve that every bet has moved; your exit nudges it slightly back as well.

**Cap-at-stake.** The immediate return is capped above by your original stake. Even if your outcome's price has risen and "on the curve" the shares are worth more, you will get back no more than you put in. The curve excess is not lost: it is carried over into a **deferred claim** and settled at resolution if your outcome won. A cancellation is an exit, not a way to take profit instantly.

**Residual → forfeit_pool.** If the curve pays out less than face value, the difference (`curve_residual`) is not lost into nowhere: it is routed into the market's `forfeit_pool` and goes to winners at settlement. Tokens are preserved — this is part of the construction that maintains conservation (no "burning" and no silent minting).

## What you need to understand

- **The return ≠ face value.** If the market has shifted since your bet, you get back less. That is the price of liquidity, not a punishment.
- **You cannot profit on a cancellation instantly.** Cap-at-stake: the most you receive is your own stake. Curve profit becomes a deferred claim and arrives at resolution, if your outcome won.
- **A losing difference stays with the market.** If the exit pays less than the stake, the shortfall goes to winners through the forfeit_pool, not "into thin air". A profitable excess, conversely, is locked in for you as a deferred claim.
- **Only before the close and only where allowed.** After `betting_expiration` there is no cancellation; on markets without the cancellation flag — likewise.
- **Leverage closes differently.** Exiting a leverage position (`pm_leverage_close`) has its own mechanics (loan, cancel_value); there the residual is also routed into the forfeit_pool. See [Leverage trader](./leverage-trader).

## Related

- [Bettor](./bettor) — the life cycle of a bet, where cancellation is one of the paths.
- [Early exit and the deferred claim](./early-exit) — where the profitable tail of a cancellation goes.
- [Why a pool, not odds](./why-pool-not-odds) — why the price (and the exit price) floats.
- [Specification](../specification) — the curve-priced cancel formula and the residual route.
