---
title: "Prediction market guides — by role and by feature"
description: "Explainer articles for participants of VIZ (Onix) prediction markets: every role (bettor, market creator, oracle, LP, leverage trader) and every mechanic in plain language, with links to the formal specification."
---

# Prediction market guides (Onix)

Explainer articles for **participants** — not a specification, but "how this works for you". Every role
is explained from the participant's point of view: a short model → step by step → what matters to
understand → links to neighbouring articles. Formal mechanics and parameters are in the
[Specification](../specification), operations are in
[Operations](../../protocol/operations/prediction-markets).

## By role — "you are the participant"

| Role | What it covers |
|------|-------|
| [Bettor](./bettor) | I place a bet. Floating odds, payout proportional to shares, automatic payout on resolution. |
| [Market creator](./market-creator) | I open a question. Starting liquidity, fee, choice of oracle and dispute mode. |
| [Oracle](./oracle) | I announce the outcome. Insurance and risk floor, penalties for getting it wrong, `reliability_score` reputation. |
| [Active LP](./active-lp) | I provide depth for the market curve. Income from fees, principal-protected, price-neutral withdrawal. |
| [Passive LP (lazy pool)](./passive-lp) | I deposit VIZ passively. Income from leverage and fees, shares by equity, FIFO withdrawal. |
| [Leverage trader](./leverage-trader) | I bet on the price with a loan from the pool. Markup + funding, liquidation, force-close when betting closes. |

## By feature — "how the mechanism is built"

| Feature | What it covers |
|------|-------|
| [Why a pool and not odds](./why-pool-not-odds) | A floating price along a curve (CPMM/LMSR) instead of fixed odds. |
| [Oracle and resolution](./resolution) | The resolution timeline: deadlines, early resolution, missed resolution, no-contest. |
| [Disputes](./disputes) | How to challenge an outcome: the grace window, the bond, voting modes, reward/penalty. |
| [Multi-outcome markets](./multi-outcome) | One market for many outcomes, LMSR vs binary CPMM. |
| [Cancelling a bet](./cancel-bet) | Curve-priced exit, cap on the stake, the difference in `forfeit_pool`. |
| [The lazy pool in detail](./lazy-pool) | Shares at the equity price, sources of income, FIFO withdrawal, the free ≥ 0 invariant. |
| [Early exit and deferred claim](./early-exit) | The profit of an early exit is a deferred claim, paid out of the pot at settlement. |
| [Hidden bets (commit-reveal)](./commit-reveal) | Privacy until the reveal, the reveal window, forfeit for not revealing. |
| [Events and metadata](./events-metadata) | How markets are assembled into a match card: event / event_title / child, the parent and the child lines, what the node indexes. |
| [Coupon](./coupon) | Several bets in one transaction: atomicity on write, why this is not a parlay, what breaks the submission. |

## Where to go next

- [Specification](../specification) — formal rules, parameters, object model.
- [Whitepaper](../whitepaper) — the thesis: why liquidity without risk, two types of markets, the flywheel.
- [`pm_*` operations](../../protocol/operations/prediction-markets) — signed consensus operations.
- [Workflows](../workflows) — a single market carried through all the roles.
