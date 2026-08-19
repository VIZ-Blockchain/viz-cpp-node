---
title: "Early exit and the deferred claim"
description: "Profit from an early exit (bet cancellation or a curve-priced leverage close) is not paid out immediately: it is booked as a deferred outcome-contingent claim and settled from a bounded slice of the losing pool. The principal comes back right away, and the LP stays protected."
---

# Early exit and the deferred claim

A VIZ market is a hybrid: entry and early exit go through the **curve** (as on an AMM), while positions held to the end are settled **parimutuel** (a common pool). Because of this, "profit from an early exit" is arranged more subtly than it looks. Let's go through why winnings from an early exit do not arrive immediately.

## The gist in two paragraphs

When you exit early — cancel a bet (`pm_cancel_bet`) or close leverage (`pm_leverage_close`) — the curve price of your position may turn out to be **higher** than your stake. But that profit cannot be paid out right now: it is not backed by losers (the market is not resolved yet), and if it were paid out of the curve, the shortfall would fall on liquidity providers — and on VIZ an LP is **principal-protected**. That is why the profitable "tail" of an early exit is not cashed out on the spot.

Instead, the system returns your **principal immediately and unconditionally** (your own money, never more than the stake) and books the profitable tail as an **outcome-contingent deferred claim**. It is settled **at settlement** from a **bounded slice of the losing pool** — that is, only if your chosen outcome won and there is something in the pot to pay from. This keeps token conservation intact and prevents LPs from subsidizing traders.

## How it works

**The principal — immediately.** A cancellation/close returns `min(curve_price, your_stake)` at once. An early exit can cut a loss or get you out flat, but it does **not** realize profit at the moment of exit.

**The profitable tail — into a deferred claim.** The difference `max(curve_price − stake, 0)` is booked as a claim on **the outcome you chose**. Not cash, but "if this outcome wins, we top you up at settlement".

**Settled from a bounded pool.** At resolution the claim is paid from a **bounded slice of the losing pool** (not from the curve, not from LP principal). If there is no loser money behind it, the payout is trimmed. No silent minting.

**A fair entry price (protection against gaming).** The "principal/tail" split is recomputed against the curve depth **at the moment of your bet**, not the current one. That closes the trick of "place a bet → add liquidity yourself → inflate the depth → withdraw a bigger tail": normalization can only decrease the payout, never increase it.

## Why it works this way

- **LPs are promised principal protection.** If early-exit profit were paid from the curve, the shortfall (when profit outruns losing stakes) would be written off against LP principal or minted — both break the guarantee. The deferred claim moves the payout to settlement, where the source is losers, not LPs.
- **Parimutuel is backed only by losers.** A winner's profit is someone else's loss. Before resolution there are no "losers" yet — so there is nothing to pay the profit from right away.

## What you need to understand

- **On an early exit you take back principal, not profit.** The profitable tail waits for resolution.
- **The tail is contingent on the outcome.** If your chosen outcome loses, there is no tail; if it wins, the tail is settled from the pot (within what is available).
- **This protects LPs and conservation.** The mechanism is deliberate, not a wallet limitation.
- **Leverage follows the same principle.** Leverage profit also arrives after resolution, from a bounded pool. See [Leverage trader](./leverage-trader).

## Related

- [Bet cancellation](./cancel-bet) — how the return on an early exit is computed.
- [Leverage trader](./leverage-trader) — why leverage profit is deferred.
- [Active LP](./active-lp) — whose protection this provides.
- [Specification](../specification) — the formal model of the deferred claim and the bounded slice.
