---
title: "Coupon — several bets in one transaction"
description: "A coupon packs N bets (pm_place_bet) into a single signed VIZ transaction. The transaction is atomic: any invalid leg rejects the whole coupon, there is no half-way state. This is not a parlay — the legs are independent and the payouts are not multiplied together."
---

# Coupon: several bets in one transaction

A coupon is a way to bet on several outcomes at once without signing each bet separately. You collect lines by tapping outcomes, set the amounts and send everything in **one transaction**.

## The gist in two paragraphs

Technically a coupon is N `pm_place_bet` operations inside one signed VIZ transaction. A transaction on chain is **atomic on write**: it is either applied in full or rejected in full. So if even one leg is invalid — the market has already closed, the balance does not cover the sum of all legs, the slippage protection kicked in — **the whole coupon fails**. There is no such state as "half the bets went through", and you never have to untangle a partial result.

At the same time a coupon is **not a parlay (not an accumulator)**. Each leg is an ordinary standalone bet into the pool of its own market, with its own payout. The odds are not multiplied together: the winnings on one leg do not depend on whether the others came in. A coupon saves signatures and makes a set of bets simultaneous — but it does not create an "all outcomes must hit" bundle. A real bundle with multiplication is a separate protocol primitive, and at the bet level it does not exist yet.

## How it works

**Collecting.** In the event feed and on the event page (the "Lines" view) a tap on an outcome adds a leg to the coupon: market, outcome, amount (1 Ƶ by default) and your slippage protection. Tapping the same outcome again updates the leg instead of creating duplicates. The coupon lives locally in the browser — until it is sent it does not touch the chain in any way.

**Checks before sending.** A coupon assembled yesterday may still carry a market whose betting has already closed. The chain will reject such a leg — and the whole transaction with it, so the client checks every leg when the coupon is opened, using the same rule as the node (the market is active and either open-ended or its deadline has not passed yet), marks the dead ones and does not allow sending until they are removed. The liquid balance is checked separately, against the **sum of all legs**: the pool only accepts bets in free VIZ, staked shares do not count.

**Sending.** All legs are signed with one key (active) and go out as one transaction. There is no network fee — the limit is set by the account's energy, and by that measure a transaction of N bets is cheaper than N separate ones.

**What happens next.** Once written, each leg lives its own life: its own share in the pool of its own market, its own resolution, its own automatic payout. In "My activity" they show up as ordinary bets.

## What you need to understand

- **All or nothing — on write, not on the outcome.** The coupon's atomicity is about landing in a block, not about calling it right. One leg lost — the others still count and still pay.
- **Payouts are not multiplied together.** This is not a parlay. If you want more risk, increase the amount of a leg, not the number of legs.
- **One dead leg breaks the submission.** A closed market, not enough free VIZ for the sum of all legs, a triggered minimum-shares protection — and the chain rejects the entire coupon. The client highlights such legs in advance.
- **Hidden bets do not go into a coupon.** Commit-reveal and batch mode are separate paths with their own reveal window; only ordinary ("instant") bets are collected into a coupon.
- **The coupon is stored in the browser.** Until you send it, it is a draft on your device: the chain knows nothing about it, and it will not be there on another device.

## Related

- [Bettor](./bettor) — the life cycle of the single bet that a coupon is made of.
- [Why a pool and not odds](./why-pool-not-odds) — why the price of a leg floats and what the minimum-shares protection is for.
- [Events and metadata](./events-metadata) — how the lines of one match are assembled into an event card, which is a convenient place to build a coupon from.
- [Hidden bets (commit-reveal)](./commit-reveal) — the path that is not part of a coupon, and why.
- [`pm_*` operations](../../protocol/operations/prediction-markets) — the `pm_place_bet` operations the transaction is built from.
