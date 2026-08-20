---
title: "Why a pool and not odds"
description: "There are no fixed odds on VIZ: the price of an outcome is set by the pool curve (CPMM for binary markets, LMSR for multi-outcome ones), and the payout is a share of the prize pot proportional to the shares bought. The odds float until betting closes."
---

# Why a pool and not "odds"

"What are the odds on this outcome?" is the most common question. On VIZ the honest answer is: **there are no fixed odds**, and that is a principle rather than an oversight. Let's go through how a pool differs from a bookmaker and how your winnings come out of it.

## The gist in two paragraphs

At a bookmaker the odds are named by **the shop** — it is the counterparty, it carries the risk and it earns on the margin. Someone has to be "the house". On a blockchain there is no "house": otherwise it would be an intermediary again, exactly the thing the ecosystem removes. So a VIZ market is a **shared pool**, and the price of an outcome is set by us, by our own bets.

The price is neither pulled out of thin air nor locked in: it is set by the **pool curve** (CPMM for binary markets, LMSR for multi-outcome ones). Every bet moves the price: the more has been staked on an outcome, the more expensive it is and the fewer "shares" (weight) the next token buys. On resolution the losers' prize pot is split among those who called it right **in proportion to their shares**. Nothing is promised as "×2.5" in advance — the multiplier comes out of how everyone ended up positioned.

## How it works

**The price is the state of the curve right now.** When you bet, the node computes along the curve how many shares your amount is due at the current price. An early bet on an unpopular outcome means you entered cheaply and got a lot of shares. Once the crowd arrives, the price is different.

**The simple "kitty" intuition.** Roughly: 100 Ƶ in total on "Yes", 300 Ƶ on "No", and "Yes" happens. The winners take back their own and split the losing 300 Ƶ by shares. A 10 Ƶ bet (a tenth of the "Yes" pool) → about 40 Ƶ on the way out. The exact number comes from the curve (a smooth price instead of the steps of a pure kitty), but the direction is the same: the fewer people on the winning side, the fatter the share.

**The odds float until betting closes.** Since the payout is built out of live bets, it changes while the market is open. A large player enters on your side — your share of the pool shrinks and the result will be **lower** than it looked when you bet. That is not the interface lying to you, it is the nature of a pool. That is why the client shows an estimate, not "fixed odds".

**Depth smooths things out.** Liquidity (direct from LPs plus the creator's starting stake) determines how sharply a bet moves the price. A deep market means a smooth price, a thin one means jumps. Liquidity works while the market is alive and comes back at settlement (principal-protected).

## What you need to understand

- **No house means no fixed odds.** The price is set by all the participants together; you see the current estimate, not a promise.
- **The payout is a share of the pot, not "bet × odds".** The result depends on the final distribution of bets across outcomes, not on the number you saw when you entered.
- **Entering early pays off more on an underpriced outcome.** More shares per token while the price is low.
- **Do not bet more than you are ready to lose.** Floating odds can move against you before betting even closes.
- **The parameters live on chain, not on a website.** Fees, penalties, limits — median voting by delegates, not the will of a shop.

## Where to go next

- [Bettor](./bettor) — how to place a bet step by step.
- [Active LP](./active-lp) — who sets the depth of the curve and why it is principal-protected.
- [Multi-outcome markets](./multi-outcome) — when there are more than two outcomes (LMSR).
- [Specification](../specification) — the formal formulas of the curve and of settlement.
