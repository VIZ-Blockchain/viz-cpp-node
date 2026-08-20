---
title: "Multi-outcome markets — one question, many options"
description: "A multi-outcome market: one market for several outcomes (which of N wins), priced by LMSR instead of the binary CPMM. The outcome limit is the median parameter pm_max_outcomes; oversized markets are reduced to binary form. The bet, the shares and the payout work as in any pool."
---

# Multi-outcome markets: one question, many options

Not every question boils down to "yes/no". "Who wins a tournament of eight teams?", "which party gets the most votes?" — here there are many outcomes. A multi-outcome market keeps them in **one** market instead of a pile of separate binary ones.

## The gist in two paragraphs

A binary market (two outcomes) is priced along a **CPMM** curve. When there are more outcomes, the market uses **LMSR** (logarithmic market scoring rule): one curve for all outcomes at once, where the price of each depends on how much has been staked on it relative to the others. The sum of the outcomes' "probabilities" stays consistent, and you always see the relative price of every option.

For you as a participant the logic is the same as in any pool: you bet on an outcome, receive shares (weight) at the current price, and on resolution the prize pot is split among those who called it right in proportion to their shares. The difference is under the hood (the pricing formula) and in the limit: the number of outcomes is capped by the network's median parameter `pm_max_outcomes`; if there are more outcomes than the limit, the market is reduced to binary form (for example, "favourite vs the field").

## How it differs from a bundle of binary markets

**One market instead of N.** Instead of ten separate "team X wins: yes/no" markets — one market with ten outcomes. Liquidity is not smeared across ten pools, and the prices are consistent across the options.

**LMSR instead of CPMM.** A binary CPMM holds two sides; LMSR generalises that to many outcomes through a logarithmic cost function. The LMSR "depth" parameter (`lmsr_b`) is derived from the liquidity provided and the number of outcomes — it sets how expensive it is to move the price. The more liquidity, the deeper the market.

**Consistent prices.** In LMSR the outcome prices are linked: one gets more expensive and the rest get relatively cheaper. That is closer to a "probability distribution" than a set of independent binary markets.

**Relation to events.** Large events (a match, a tournament) are often mirrored from external sources as a set of markets under a common `event` key — then the multi-outcome market and the related binary props are grouped on the event page (see the Forecaster client).

## What you need to understand

- **The logic of a bet does not change.** Outcome → shares at the current price → a share of the pot on resolution. Multi-outcome differs in pricing, not in substance.
- **The outcome limit is a network parameter.** `pm_max_outcomes` is median-voted; a market with more outcomes than the limit is created in binary form rather than silently rejected.
- **Depth matters more with many outcomes.** Thin liquidity on a multi-outcome market moves prices more sharply — the LMSR depth is spread across all the options.
- **Entering early on an underpriced option pays off.** Just as in a binary pool: a cheap price means more shares.
- **The payout follows the shares.** No odds fixed in advance; the result comes out of the final distribution.

## Related

- [Why a pool and not odds](./why-pool-not-odds) — how a price is formed in a pool in the first place.
- [Bettor](./bettor) — the life cycle of a bet (the same for multi-outcome markets).
- [Market creator](./market-creator) — how to choose the market type at creation.
- [Specification](../specification) — the LMSR formulas, `lmsr_b`, outcome limits.
