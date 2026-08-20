---
title: "Bettor — how to place bets on VIZ prediction markets"
description: "A bet goes into the shared market pool and moves the floating odds; the prize is split among those who called it right, in proportion to their shares, and the payout is automatic on resolution."
---

# Bettor: how to place bets on VIZ prediction markets

You came to bet on an outcome — "yes/no", "who wins", "will the event happen". On VIZ this does not work like a bookmaker with fixed odds, but as a **shared pool**. Let's go through what exactly happens to your money and how the winnings are formed.

## The gist in two paragraphs

You stake VIZ on one of the outcomes. Your bet goes into the **market pool** and moves the price: the more has been staked on an outcome, the more expensive it becomes and the fewer "shares" (weight) you get for the next token. The odds are not locked in at the moment of the bet — they are **floating**, set by the balance of the pool right now.

When the oracle announces the outcome, the entire pool is split among those who called it right, in proportion to their shares. Lose, and you lose your bet — it goes to the winners. Win, and you take your share of the prize pool. No "locked in 2.5, now wait": the odds are what you see as the current price, and they change as others bet.

## What happens step by step

**The bet.** You choose an outcome and an amount. The node computes along the pool curve how many shares (weight) you are due for that amount at the current price, and records the position. The amount goes into the pool, and the outcome's price shifts up.

**While the market is open.** The price is alive: others bet, the odds float. You can bet more, and on markets where cancellation is allowed you can **cancel** a bet before it closes (`pm_cancel_bet`). Important: cancelling sells your position back along the current curve rather than refunding the nominal. If the market has moved since your bet, you get back less than you put in — that is not a penalty but a fair exit price; you never receive more than your bet (and if the price moved in your favour, the surplus along the curve becomes a deferred claim contingent on the outcome, rather than being handed to the market). Details — "[Cancelling a bet](./cancel-bet)".

**Betting closes.** At `betting_expiration` bets are no longer accepted. After that comes waiting for the outcome from the oracle.

**Resolution.** The oracle announces the winning outcome (`pm_resolve_market`). Payout to the winners is **automatic** and virtual: there is no "claim" button to press, the winnings are credited to your balance on resolution. Losing outcomes are zeroed out, and their money goes into the winners' prize pot.

**If there is no outcome.** The event was cancelled or the source disappeared — the oracle declares **no-contest** (`pm_no_contest`): bets are refunded, nobody wins and nobody loses. And if the oracle stayed silent right up to the deadline, the market is voided as **missed-resolution** — your bets are refunded all the same (and the oracle is penalised for it). Either way, your money never "burns".

## What a bettor needs to understand

- **The odds are not fixed.** The price you see is the state of the pool right now. An early bet on an unpopular outcome buys more shares (you entered cheaper); once the crowd arrives, the price is different. Details — in the article "Why a pool and not odds".
- **The payout is proportional to shares, not "bet × odds".** You split the prize pot with the other winners by position weight. The final multiplier depends on how the bets ended up distributed across outcomes.
- **No manual claiming.** Winnings and refunds arrive automatically on resolution/cancellation.
- **You can challenge the oracle.** If the outcome was announced incorrectly, it can be disputed within the dispute window (`pm_dispute_create`) — see the article on disputes.
- **Liquid balance.** A bet requires free VIZ; staked SHARES do not count. The wallet will tell you if you are short.

## Roles next to you

- **Oracle** — the one who announces the outcome and answers for it with reputation and an insurance bond.
- **Liquidity provider** — the one whose capital sets the depth of the curve (so your bet does not move the price too sharply).
- **Leverage trader** — bets on the **price** with a loan from the pool rather than on the outcome; a separate instrument.

Where to go next, to taste: "Why a pool and not odds" (price mechanics), "Oracle and resolution" (who announces the outcome and how), "Disputes" (how to challenge it).
