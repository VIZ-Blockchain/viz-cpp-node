---
title: "Oracle and resolution — how an outcome is announced"
description: "The market resolution mechanism: resolution only after betting closes, the result_expiration deadline, early resolution (allow_early_resolution), missed-resolution void after grace, no-contest when there is no outcome. Payouts are automatic."
---

# Oracle and resolution: how an outcome is announced

This article is about the **mechanism** of resolution — the timeline and the rules common to all participants. For the duties and risks of the oracle itself there is a separate article, [Oracle](./oracle); here we look at how a market gets from betting close to payouts.

## The gist in two paragraphs

A market can only be resolved **after betting closes** (`betting_expiration`) — while bets are still coming in, announcing an outcome is pointless. A market has a `result_expiration` deadline by which the oracle must announce the result (`pm_resolve_market`). On resolution, payouts to those who called it right are credited **automatically** (virtual `pm_payout`), losing outcomes are zeroed out and their money goes into the winners' prize pot.

If there is no outcome — the event was cancelled, the source disappeared, a draw with no winner — the market is closed as **no-contest** (`pm_no_contest`): bets are refunded, nobody wins and nobody loses. And if the oracle stayed silent past the deadline and the grace period, the market dies as **missed-resolution** (the oracle is penalised). There is also an early path: markets with `allow_early_resolution` can be closed sooner if the outcome is already known for sure.

## Market timeline

**1. Open.** Bets are coming in, the price floats along the curve. Resolution is forbidden.

**2. Betting closes (`betting_expiration`).** Bets are no longer accepted. The window opens in which the oracle can (and must) announce the outcome.

**3. Resolution (`pm_resolve_market`).** The oracle announces the winning outcome. The node splits the pool: payouts are automatic, there is nothing to claim by hand. For the resolution the oracle gets its fee (capped by a median parameter).

**4. Dispute window.** After the announcement comes the grace period (`pm_dispute_grace_sec`) in which the outcome can be challenged (see [Disputes](./disputes)). Until the dispute is finalised the payouts are not final.

**5. Settlement.** The confirmed outcome is settled: LP liquidity is returned (principal-protected) and the prize pot is distributed.

## Special paths

**Early resolution (`allow_early_resolution`).** If the market was created with this flag and the outcome is already known for certain, the oracle closes it early. `result_expiration` then shifts to the moment of resolution (the dispute window collapses to "now + grace"), but disputers still keep the full `pm_dispute_grace_sec` from the announcement. A late resolution (after `result_expiration`), on the contrary, does not extend the window.

**No-contest (`pm_no_contest`).** There is no outcome — the market is cancelled and bets are refunded. This is not a penalty on participants: their money does not "burn" because a source went quiet. The oracle must declare no-contest in time if there is not going to be a result.

**Missed-resolution (void on deadline).** The oracle did not announce an outcome and did not declare no-contest before `result_expiration` + grace → the cron voids the market and the oracle is slashed. Important: the void only fires **after** `result_expiration + pm_dispute_grace_sec` (the same cutoff as the settle-sweep) — so that the oracle has a real resolution window rather than a race against the deadline (this was fixed by the reachability fix; otherwise a fixed-deadline market without the early flag was impossible to resolve).

## What to understand

- **Resolution only after betting closes.** Not before; the early path is a separate market flag.
- **Payouts are automatic.** No "claim", no buttons: winnings and refunds arrive on resolution/cancellation.
- **Silence is punished.** No outcome → no-contest in time. Simply "doing nothing" = missed-resolution and a slashing of the oracle.
- **The outcome is not final until the grace period ends.** Within the dispute window payouts can be recomputed.
- **Leverage does not wait for resolution.** Leverage positions are closed at the price as of `betting_expiration`, independently of the oracle's verdict — see [Leverage trader](./leverage-trader).

## Roles and links

- [Oracle](./oracle) — who announces the outcome and what they answer with.
- [Disputes](./disputes) — how to challenge an announced result.
- [Bettor](./bettor) — what resolution looks like from the bet's side.
- [Specification](../specification) — the formal deadlines and cutoffs.
