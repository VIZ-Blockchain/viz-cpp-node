---
title: "Oracle — how to announce an outcome and answer for it"
description: "The oracle announces the market result (pm_resolve_market), carries an insurance bond and a reputation (reliability_score). Penalties for an incorrect/missed resolution; the risk-floor hides an underfunded oracle from the listings."
---

# Oracle: how to announce an outcome and answer for it

An oracle is the account that tells the chain how the event ended. Who gets the prize pool depends on it. That is why an oracle on VIZ does not merely "press a button": it carries a **money bond** and a **reputation**, and answers with both for honesty and timeliness.

## The gist in two paragraphs

You announce the winning outcome with the `pm_resolve_market` operation (only after betting closes). Payouts to those who called it right are credited automatically. To be trusted with markets and money, you hold **insurance** — a VIZ bond tied to your oracle account. If insurance sags below the **risk-floor** relative to the volume of bets you serve, the node **hides your markets from the listings** — the mechanism protects bettors from an undercapitalised oracle.

For mistakes and silence you are penalised out of your accrued reputation and bond: an incorrect resolution, challenged and overturned by a dispute; a missed deadline (`result_expiration`), when the market dies as missed-resolution. Your **reputation** (`reliability_score`, 0..10000 bp) is made up of accuracy, dispute verdicts, responsiveness and punctuality — and is visible to everyone choosing an oracle.

## What you do step by step

**Get ready.** Fund insurance on the oracle account (`pm_oracle_update`) — keep it with a margin above the risk-floor, otherwise your markets will be hidden. The floor rule: a market is visible if insurance ≥ the threshold and ≥ a multiple of the sum of bets under your management.

**Wait for betting to close.** Resolution is only possible after `betting_expiration`. Not before (there is no point announcing an outcome while bets are still coming in). If the market was created with `allow_early_resolution` and the outcome is already known for sure, you can close it early — the dispute window then collapses to the moment of resolution.

**Announce the outcome.** `pm_resolve_market` with the winning outcome. The node splits the pool: those who called it right are paid automatically (virtual `pm_payout`), and losing outcomes are zeroed out. For the resolution you are credited an **oracle fee** (capped by a network median parameter, at most `pm_max_oracle_fee_percent`).

**If there is no outcome.** The event was cancelled, the source disappeared, a draw with no winner → instead of resolving you declare **no-contest** (`pm_no_contest`): bets are refunded, nobody wins and nobody loses. That is an honest exit, not a penalty — but it has to be done in time, before the deadline.

**Get through the dispute window.** After the announcement, bettors can challenge the outcome within the grace period (`pm_dispute_create`). If the dispute finds you were right, your reputation is confirmed; if your resolution is found incorrect, you are penalised. Details — in the article on disputes.

## What you are penalised for

- **Incorrect resolution.** You announced the wrong outcome and a dispute confirmed it → penalty from the bond/reputation, payouts are recomputed.
- **Missed deadline.** You did not announce the outcome before `result_expiration` → the market dies as missed-resolution and you are slashed. Do not stay silent: if the source did not give a result, declare no-contest.
- **Systematic slowness.** Late (but completed) resolutions drag down the punctuality factor in `reliability_score`. Being late is no longer the same as being on time.

## What an oracle needs to understand

- **Insurance is trust in numbers.** Keep it above the floor with a margin; it sags from accumulated slashing and grows with the volume you serve. An underfunded oracle disappears from the listings — the markets seem to "vanish".
- **Reputation is public and composite.** `reliability_score` = accuracy + dispute verdicts + responsiveness + punctuality − penalties (decaying penalty-stamps) − bans, shrunk toward the average when the number of resolutions is small. In the interface it is shown as a percentage (bp/100).
- **A resolution is irreversible in effect, but disputable.** Payouts go out immediately, but the dispute window can cancel them and punish you. If you got it wrong, it is more honest to initiate the correction yourself than to wait for a slashing.
- **Timeliness = money.** A no-contest on time is better than silence until the deadline. Automate resolution if you serve many markets.

## Roles next to you

- **Market creator** — chooses you as the oracle and sets the dispute window.
- **Bettor** — trusts you with the outcome and can challenge it.
- **Disputer** — a participant in the dispute who checks your verdict.

More on the topic: "Disputes" (how an outcome is challenged and what it means for the oracle), "Market creator" (who appoints you and how), "Oracle and resolution" (deadlines, missed-resolution, no-contest in detail).
