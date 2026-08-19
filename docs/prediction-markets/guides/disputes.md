---
title: "Disputes — how to challenge an outcome"
description: "If the oracle announced an incorrect outcome, it can be challenged within the grace window (pm_dispute_create, bond fee). A vote resolves the dispute (committee or account mode); an incorrect resolution penalises the oracle, and the disputer risks their bond. Payouts may be recomputed."
---

# Disputes: how to challenge an outcome

An oracle is a person or a service, and it can make a mistake or cheat. A dispute is the safety catch: the mechanism participants use to challenge an announced outcome while the payouts can still be recomputed. Let's go through how it works and what each side risks.

## The gist in two paragraphs

Once the oracle has announced the outcome (`pm_resolve_market`), a **dispute window** opens (the grace period, `pm_dispute_grace_sec`, on the order of 12 hours). Within that window any participant can file a dispute (`pm_dispute_create`) by putting up a **bond fee** (around 1000 VIZ) — that is the price of having the network review the result, and the protection against spam from empty challenges.

The dispute is resolved by a **vote** (`pm_dispute_vote`, with the active key) — in one of the modes set by the market creator: committee (trusted arbiters) or account voting. If the dispute finds the resolution incorrect, the outcome is corrected, payouts are recomputed, **the oracle is penalised** (bond/reputation), and the disputer gets a reward. If the resolution is found correct, the disputer loses the bond. There is money at stake on both sides, which is why challenges are filed on the merits and not at random.

## How it works step by step

**Resolution and the start of the window.** The oracle announced the outcome → payouts were credited → the dispute grace timer started. While it runs, the result is not final.

**Filing a dispute.** `pm_dispute_create` with the bond fee. The market moves into dispute status (the payout is deferred/flagged) and a dispute record appears naming the oracle and the initiator. The `pm_dispute_opened` vop is emitted (it lands in the history of both the oracle and the disputer).

**Voting.** Participants of the mode vote with `pm_dispute_vote` (active key). The mode and its membership are set at market creation (committee or accounts). Votes are collected until the response/decision deadline.

**Finalisation.** The dispute is closed (`pm_dispute_finalize`) according to the vote, or auto-closed (`pm_dispute_auto_close`) if the oracle did not respond in time. The result:
- **Resolution incorrect** → the outcome is changed, payouts are recomputed for the correct outcome, the oracle is penalised, and the disputer is rewarded.
- **Resolution correct** → the result stands, the disputer loses the bond, and the oracle's reputation is confirmed.

**Effect on payouts.** While the dispute runs, the final payouts on the challenged market are not considered final. After finalisation, settlement proceeds on the confirmed outcome.

## What to understand

- **The window is limited.** A challenge is only possible during the grace period after the announcement. Miss it and the outcome is final. On an early resolution (`allow_early_resolution`) the window collapses to the moment of resolution, but disputers still keep the full `pm_dispute_grace_sec` from the announcement.
- **The bond cuts both ways.** The disputer risks the fee, the oracle risks the bond and reputation. That makes a dispute expensive for lying and cheap for telling the truth.
- **The creator sets the mode.** Committee mode is faster and more predictable (trusted arbiters); account mode is more decentralised. Check a market's mode before a large bet.
- **The vote uses the active key.** As do most participant PM operations (`pm_dispute_vote`, `pm_dispute_create`).
- **A dispute is a last resort.** For an honest oracle with a good `reliability_score` disputes are rare; systematically losing them drags down its reputation and insurance.

## Roles nearby

- [Oracle](./oracle) — the one whose resolution is being challenged; carries the bond and the reputation.
- [Bettor](./bettor) — can initiate a dispute if the outcome was announced incorrectly.
- [Market creator](./market-creator) — sets the dispute mode and window at creation.

More on the topic: [Oracle](./oracle) (what it is penalised for and how reputation is computed), [Bettor](./bettor) (the life cycle of a bet), [Specification](../specification) (the formal dispute rules and deadlines).
