# Kelly Criterion

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Kelly Criterion](https://www.pmatlas.xyz/concepts/kelly-criterion)

---

## Definition

**Quick definition.** A formula for optimal bet sizing that maximizes long-run growth by balancing edge against risk of ruin. In prediction markets Kelly is the canonical bet-sizing framework for traders with a perceived edge, but full-Kelly is widely considered too aggressive in practice · fractional Kelly is the working standard.

## Key Insights

- outpxce (seven-figure PM trader): uses **fractional Kelly sizing** as core practice. Trades non-English-media OSINT and Telegram for edge; locks capital as a "bond mule" for small premiums on near-resolution markets. Notes edge has compressed as space matured.
- Jacob Zhao's AI-agents framework: compares **Kelly criterion vs. fixed-fraction bet sizing**. Argues AI agents will become dominant market participants within two years.
- Roan: Kelly position sizing is one of three core tools (with fill-quality adverse-selection measurement and probability term structure) for building a repeatable PM edge.
- Lihong's structural critique: market probabilities are NOT real probabilities. Three reasons they diverge even with rational participants: **favorite-longshot bias from Kelly betting**, risk-premium distortion from market correlation, risk-neutral forward pricing in long-dated contracts. The Kelly mechanism itself biases prices toward favorites and away from longshots because risk-averse Kelly bettors underweight tail outcomes.
- gemchanger ("Sharpe Ratio Is a Lie"): the full quant-finance toolkit (Deflated Sharpe Ratio, combinatorial purged CV, Black-Litterman, Bayesian regimes, ML) transfers to PMs because **binary resolution eliminates the unobservable noise that obscures strategy quality** elsewhere. PMs are the purest testbed for investment theory; Kelly is the natural sizing rule given clean signal.
- LMSR has a mathematical identity with the softmax function · bridging quant finance and PM pricing in ways that make Kelly's information-theoretic foundation directly applicable.
- Implicit across all sources: Kelly is necessary but insufficient. Sizing rules must be paired with **execution-quality controls** (Della Vedova) because being right about edge but bad at fills still loses money.

## Notable Quotes

> "Edge has compressed as space matured."  
> — *outpxce*

> "Prediction markets are the purest testing environment for investment theory because binary resolution eliminates the unobservable noise that obscures strategy quality in traditional finance."  
> — *gemchanger*

## Where It Matters

Kelly is how serious traders translate their probability estimates into capital allocation. Its salience in PMs is unusually high because binary outcomes make the "edge" calculation cleaner than in any other asset class · you can compute realized vs. expected returns trade-by-trade without the residual factor-exposure noise that haunts equities. But Lihong's point matters: if many participants use Kelly, market prices themselves bend toward favorites, which is partial structural explanation for longshot bias.

## Related Concepts

- **Adverse selection** · the binding risk Kelly sizes against.
- **Forecasting accuracy / calibration** · the inputs Kelly depends on.
- **Longshot bias** · Lihong argues Kelly *causes* part of this bias.
- **Market making** · analogous sizing problem from the LP side.
- **AI agents** · Kelly is one of two leading sizing rules for autonomous traders.
- **Execution quality** · required complement to Kelly.

## Sources

- [Market Probabilities Are NOT Real Probabilities](https://x.com/Lihong062/status/2051017075758686622) — Lihong · May 3, 2026
- [Turning Probability into Assets: A Look Ahead at Prediction Market Agents](https://x.com/0xjacobzhao/article/2029229969914904694) — Jacob Zhao · Mar 5, 2026
- [Your Hedge Fund's Sharpe Ratio Is a Lie. Prediction Markets Are the Only Place It Can't Hide.](https://x.com/gemchange_ltd/article/2026300422483271733) — gemchanger · Feb 25, 2026
- [Why Prediction Markets Aren't Gambling? (The Math)](https://x.com/RohOnChain/article/2020565633453412751) — Roan · Feb 9, 2026
- [On Prediction Markets](https://x.com/outpxce/status/2013397772074905950) — outpxce · Jan 20, 2026

