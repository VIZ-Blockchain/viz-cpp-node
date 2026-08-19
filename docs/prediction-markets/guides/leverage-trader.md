---
title: "Leverage trader — betting on price with a loan from the pool"
description: "Leverage: betting on the movement of an outcome's PRICE with a loan from the lazy pool. Flat markup + funding rate, sweep liquidation, force-close at the betting close (it does not wait for the oracle), cannot be opened when less than 24h remain until betting_expiration. Profit is a deferred claim paid after resolution."
---

# Leverage trader: betting on price with a loan from the pool

Leverage is a separate instrument on top of a market. You are betting not "on the outcome until the end", but **on the movement of the price** of an outcome, and you borrow from the lazy pool to amplify the position. This is closer to margin trading than to an ordinary bet.

## The gist in two paragraphs

You post collateral and open a leverage position (`pm_leverage_open`): the system borrows the missing amount from the **lazy pool** and places the enlarged size on the chosen outcome. While the outcome's price moves your way, unrealized profit grows; against you, the position approaches **liquidation**. For the loan you pay the pool a **flat markup (~10%)** and a **funding rate** over time; that is the income of passive LPs.

Leverage **does not wait for the oracle**: the position is force-closed at the price at the moment betting closes (`betting_expiration`) — leverage settles at the market price, not at the announced outcome. If the price hits the threshold earlier, the position is liquidated by a "sweep". Leverage profit is booked as a **deferred claim** and paid out after the market resolves (from a bounded pool of losers), not instantly.

## What happens, step by step

**Opening.** `pm_leverage_open`: you post collateral and set the leverage. The pool issues the loan (`pool.free_balance -= loan`), and the total size is bet on the outcome. You can only open if **at least 24 hours** remain until `betting_expiration` — otherwise the position has no room to live, and the opening is rejected.

**While open.** The outcome's price drifts with bets. Your way — profit grows; against you — liquidation approaches. The funding rate accrues in favor of the pool. You pay for leverage as long as you hold the position.

**Closing at will.** `pm_leverage_close` — you exit at the current price: the loan is returned to the pool and you take your share (`cancel_value`). The spread/floor remainder (`curve_residual`) is routed into the market's `forfeit_pool` (it goes to winners at settlement) — money does not "freeze".

**Liquidation.** If the price reaches the threshold, the position is closed automatically (sweep). The collateral repays the loan to the pool; whatever is left follows the settlement rules.

**Force-close at the betting close.** If you did not close it yourself, at `betting_expiration` the position is force-closed at that moment's price. Leverage **does not depend on the oracle's resolution**: it is about price, not about "who turns out to be right".

**Profit payout.** Leverage profit is not instant cash: it is a **deferred claim** settled after the market resolves, out of a bounded pool of losing stakes/forfeits (with a cap on the payout). This way the system does not mint tokens out of thin air.

## What a leverage trader needs to understand

- **You bet on price, not on the outcome.** Leverage closes at the price when betting closes, not at the oracle's announcement. You can call the "price" right and never see the outcome — those are different things.
- **The loan comes from the pool, and it is not free.** The flat markup plus the funding rate go to the lazy pool. Holding a position for a long time is expensive; funding works against you over time.
- **Liquidation is real.** A move against you closes the position by force, and the collateral repays the loan. Leverage amplifies both profit and loss.
- **The 24-hour window.** You cannot open leverage if less than a day remains until betting closes — the position needs room to exist and to close correctly.
- **Profit arrives after resolution.** Do not count on instantly withdrawing leverage winnings: they are booked as a deferred claim and settled at market settlement, within the available pool.
- **An instrument for those who understand it.** Leverage is more complex than an ordinary bet; if you want a simple bet on the outcome, see the "Bettor" article.

## Roles next to yours

- **Passive LP (lazy pool)** — the one you borrow from; your markup and funding are their income.
- **Bettor** — bets on the outcome without a loan and without liquidation; the simpler path.
- **Oracle** — resolves the market; it affects your leverage indirectly (profit is settled after resolution), but the leverage close is tied to price, not to their verdict.

Further reading: "Passive LP (lazy pool)" (the other side of your loan), "Leverage — the mechanism" (markup and liquidation formulas), "Early exit and the deferred claim" (how and when leverage profit arrives).
