# Bonding Trades

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Bonding Trades](https://www.pmatlas.xyz/concepts/bonding-trades)

---

## Definition

**Quick definition.** A prediction-market strategy of betting on near-certain outcomes (>95% probability) to earn a small but reliable yield, analogous to holding a zero-coupon bond to maturity. The "yield" is the gap between the market price and the $1 payout. Catastrophic tail risk if the improbable outcome occurs.

## Key Insights

- Stephanie Stacey (FT Alphaville): the canonical example is the **Polymarket "Will Jesus Christ Return" market**, where betting NO at 96% odds implies a **4.76% yield to maturity**.
- Two structural constraints: (1) **illiquidity** · only **$150k executable at one time** on these markets; (2) **catastrophic tail risk** · if the near-impossible outcome occurs, the entire principal is wiped out.
- The yield equation is straightforward: if NO trades at $0.96, you put $0.96 to win $1 → 4.17% return at resolution. The annualized rate depends on time-to-maturity, which is why long-dated near-certain markets have higher implied yields than short-dated ones (locking $1M for two years vs. two months matters).
- outpxce (related): the trader self-identifies as a **"bond mule"** · locking capital for small premiums on near-resolution markets. This is essentially the professional version of bonding trades.
- Connection to position collateralization: the capital lock-up is the binding constraint. If PM positions could be used as DeFi collateral, the implied yield curve on near-certain markets becomes a legitimate fixed-income substitute.
- Connection to gap risk: bonding is the *short volatility* trade. You earn a tiny yield in exchange for accepting that one in a hundred binaries you bond will resolve against you and wipe out years of premium.

## Where It Matters

Bonding trades are PM's organic fixed-income product. Their existence is what FT Alphaville-style observers point to when arguing PMs are converging on finance, not gambling · but their tail risk and capital-lock-up are also why bonding can't scale into a true bond-replacement until position collateralization is solved. They are the cleanest case study for why "stable yield" in prediction markets is a misleading frame: the yield is real, the stability is conditional on a catastrophic-outcome counterfactual that 99% of the time doesn't fire.

## Related Concepts

- **Gap risk** · bonding is the trade most exposed to it.
- **Liquidity provision** · bond mules are providing time-locked capital.
- **Position collateralization** · the key unlock that would scale bonding.
- **Longshot bias** · the inverse of bonding (paying for longshots vs. selling them).
- **Temporal arbitrage** · bonding is the opposite end of the time-decay strategy spectrum.

## Sources

- [How Prediction Market Traders Reinvented the Bond](https://www.ft.com/content/3382195d-b417-4fc8-9c2e-17f38027a635) — Stephanie Stacey (FT Alphaville) · Feb 6, 2026

