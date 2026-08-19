---
title: "Active LP — direct market liquidity"
description: "A direct LP sets the curve depth of a specific market, earns from fees and late-bet penalties, and is principal-protected: withdrawal is price-neutral, and settle_liquidity returns the principal unconditionally. Leverage does not touch the direct LP."
---

# Active LP: direct market liquidity

You give a market depth: you put VIZ into the curve of a specific market so that bets do not move the price too sharply. In return you receive a share of the fees. The key difference from a "market-maker-as-banker" model: on VIZ, direct liquidity is **principal-protected** — the outcome cannot put you in the red.

## The gist in two paragraphs

Your capital (`pm_add_liquidity`) enters the market's curve and determines how smoothly bets move the price. The deeper the pool, the more comfortable large players are and the larger the volume — and therefore the fees. You earn a **percentage of bets** (the market fee) plus a share of **late-bet penalties** (the anti-sniping penalty), which accrue in favor of LPs.

Your principal does not depend on who won. Withdrawal (`pm_withdraw_liquidity`) is **price-neutral**: a proportional shrink of the reserves returns your principal plus accrued fees without moving the curve (a round trip does not change the price). And at market settlement, `settle_liquidity` returns each LP's principal **unconditionally**, plus a bonus (fees, remainders undistributed to winners, the penalty pool). LP = principal-protected + fee income.

## What happens, step by step

**You add liquidity.** `pm_add_liquidity` — VIZ goes into the market's curve and you receive a pool share proportional to your contribution. Your deposit does not shift the price (you add symmetrically). The minimum contribution is `pm_min_liquidity` (a governed parameter, 100 VIZ by default): **the same floor as creating a market**, and it applies to topping up an open market as well. The reason is technical — every call creates a SEPARATE liquidity row (contributions are not merged into one position) and settlement walks all of them, so cheap micro-deposits cannot be allowed. If you want to add less, add less often and in larger amounts.

**While the market is open.** A fee is withheld from bets and distributed to LPs by share. Late bets pay the anti-sniping penalty, which also goes in favor of liquidity. Your income accrues as turnover grows.

**You withdraw (optionally).** `pm_withdraw_liquidity`, partially or fully. The withdrawal is price-neutral: you get back your principal plus earned fees, and the curve does not shift. No impermanent loss as in classic AMMs: an in-and-out round trip neither moves the price nor eats your capital.

**Settlement.** Once the market is resolved, liquidity returns **on its own**, through per-block settlement. `settle_liquidity` hands each LP the principal unconditionally plus a bonus. Liquidity is locked from the close of betting until settlement (while payouts are computed), then released.

## What an active LP needs to understand

- **Principal-protected refers to the outcome, not to everything under the sun.** You do not lose principal because of who won the market. Your income is fees and penalties; outcome risk is not shifted onto you.
- **You are not the counterparty to leverage.** Leverage traders' loans are fronted by the **lazy pool** (a passive product), not by your direct liquidity. Leverage does not touch your principal. (There is a subtle design nuance about covering leverage overprofit — it is localized to the pool, not to direct LPs.)
- **Depth = volume = income.** Thin liquidity scares off large bettors; your contribution directly affects market quality and, through volume, your fees.
- **The lock during settlement is normal.** From the close of betting until payouts are computed, liquidity is locked; that is the settlement procedure, not a loss. Afterwards the principal returns.
- **Income is realized at settlement.** Earned fees (earned_fee) are finalized when the market resolves; before resolution they are reflected, but they are booked in the return.

## Roles next to yours

- **Market creator** — provides the initial liquidity (they are a direct LP too) and sets the fee.
- **Bettor** — pays the fee that makes up your income.
- **Passive LP (lazy pool)** — a different product: passive capital that fronts leverage; not to be confused with direct market liquidity.

Further reading: "Passive LP (lazy pool)" (how it differs and where the leverage risk sits), "Why a pool, not odds" (how the curve works), "Market creator" (how the fee and depth are set).
