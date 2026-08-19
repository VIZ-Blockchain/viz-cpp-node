---
title: "Market creator — how to open a question on VIZ"
description: "How to create a prediction market: set the question and outcomes, put up initial liquidity, choose an oracle and a dispute mode, set the fee. Liquidity is principal-protected and comes back on its own at settlement."
---

# Market creator: how to open a question

You formulate the question people will bet on: "who wins", "will the event happen by a date", "yes/no". Any account can create a market on VIZ — it is an ordinary signed `pm_create_market` operation, with no permissions and no moderation. Let's go through what you set and what you answer for.

## The gist in two paragraphs

A market is a question + a set of outcomes + a **liquidity curve** that the price is computed along. You put up the initial liquidity (your own stake), set the market fee, choose an **oracle** (who announces the outcome) and a **dispute mode** (how the outcome can be challenged). Once created, the market lives on its own: people bet, the price floats, at the deadline the oracle announces the result, and the node computes the payouts automatically.

Your initial stake does not "burn" — direct market liquidity is **principal-protected**: it sets the depth of the curve, earns from fees and is **returned to you in full** at settlement (plus your accrued share of fees). You are not the counterparty to the bets and you do not risk principal on the outcome — that is what makes a VIZ market different from a "bookmaker" who can go into the red.

## What you set at creation

**Question and outcomes.** The text of the question, the metadata (title, event, tags, image link). The market type: **binary** (two outcomes, CPMM curve) or **multi-outcome** (up to 64 outcomes, LMSR). A multi-outcome market is convenient when there are many outcomes (which of N wins); details — in the article on multi-outcome markets.

**Initial liquidity.** How much VIZ you put into the curve. The more, the "deeper" the market: bets move the price more smoothly, and large players can enter without sharp jumps. This is your principal-protected capital (see the article on the active LP).

**Market fee.** The percentage withheld from bets and paid to liquidity providers (you and other LPs) as income. Capped from above by a network median parameter.

**Oracle.** The account responsible for announcing the outcome. That can be you yourself or a trusted specialised oracle (for example, one mirroring Polymarket/Kalshi). The oracle carries an **insurance bond** and a reputation: it is penalised for an incorrect or missed resolution — see the article on the oracle.

**Dispute mode and window.** How the outcome can be challenged after it is announced: by a committee or by an account vote, and within what grace period. This protects bettors from an incorrect resolution.

**Deadlines.** `betting_expiration` — when bets stop being accepted; `result_expiration` — the deadline by which the oracle must announce the outcome. The `allow_early_resolution` option lets the oracle close the market earlier if the outcome is already known.

## What happens after creation

- **The market is open.** People bet, fees accrue to you as an LP. You can add more liquidity or withdraw part of it (price-neutral, with no loss of principal).
- **Betting closes.** At `betting_expiration` bets stop being accepted.
- **Resolution.** The oracle announces the outcome. The node splits the pool: those who called it right are paid automatically, and your LP principal + share of fees are returned at settlement **unconditionally**.
- **If there is no outcome.** The oracle declares **no-contest** (event cancelled, source disappeared) → bets are refunded to the bettors, your liquidity comes back to you, and the oracle is not penalised. But if the oracle **stayed silent** until the deadline (`result_expiration` + grace), the market dies as **missed-resolution**: bets are still refunded, but the oracle is penalised (see "[Oracle and resolution](./resolution)").

## What a creator needs to understand

- **You are not the bookmaker.** Direct market liquidity does not cover the winnings out of your own pocket — the prize pool is formed by the losing bets. Your principal is protected.
- **The oracle is a critical choice.** Whether the market is resolved honestly depends on it. A bad oracle = disputes and penalties. Take a proven one, or be the oracle yourself and keep insurance above the risk-floor, otherwise your market will be hidden from the listings.
- **The fee is a balance.** A higher fee = more income for you, but a more expensive market for the players and less volume. A lower one is more attractive to bettors.
- **Depth decides.** Thin liquidity = sharp price jumps on every bet, which scares off large players. The initial stake sets the quality of the market.
- **Metadata matters.** A market with no on-chain title/tags is harder to find in clients and harder for automation to resolve.

## Roles next to you

- **Bettor** — the one who bets on your outcomes.
- **Oracle** — announces the result; you choose it at creation.
- **Active LP** — can add depth to your market on top of your initial stake.

More on the topic: "Oracle and resolution" (who to choose and what they answer for), "Active LP" (the mechanics of liquidity and principal return), "Disputes" (dispute modes), "Multi-outcome markets" (when there are more than two outcomes).
