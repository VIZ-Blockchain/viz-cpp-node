# LMSR (Logarithmic Market Scoring Rule)

**Category:** Mechanism Design  
**Source:** [PM Atlas — LMSR (Logarithmic Market Scoring Rule)](https://www.pmatlas.xyz/concepts/lmsr-logarithmic-market-scoring-rule)

---

## Definition

An automated market maker that prices trades using a logarithmic cost function, guaranteeing bounded loss to the subsidizer. Robin Hanson's LMSR is the canonical prediction-market AMM · but in practice it failed for binary contracts and Polymarket migrated to a CLOB in late 2022.

## Key Insights

- **LMSR failed for production binary markets.** In a binary market that resolves to 0 or 1, impermanent loss becomes *permanent*: the pool inevitably holds worthless shares on the losing side, and trading fees cannot offset a guaranteed structural loss. Polymarket's late-2022 migration from an LMSR AMM to a central limit order book is the inflection point where the industry recognized this (Melee, *Why AMMs Failed Prediction Markets*).
- Dalen / Daedalus Research formalizes LMSR's structural problem in a clean line: "For cost-function market makers such as LMSR, providing more liquidity increases worst-case loss; without external subsidies, they are expected to run at a deficit proportional to the liquidity they offer." This is why LMSR works in subsidized academic / forecasting-tournament contexts but bleeds capital in production.
- LMSR is mathematically identical to the **softmax function** · this bridges quant finance and prediction-market pricing, and is why ML-derived methods (Bayesian regime detection, factor models, machine learning) transfer directly (gemchanger).
- Chen & Pennock (*Designing Markets for Prediction*, Jan 2025) position LMSR as the canonical example among automated market makers designed for "(effectively infinite) liquidity for prediction markets." Their framing: "Many automated market maker mechanisms have been designed to provide (effectively infinite) liquidity for prediction markets; much effort has been put into understanding manipulation in prediction markets and designing prediction mechanisms to achieve incentive compatibility."
- Shaw Dalen / Daedalus Research (Oct 2025) proposes a *Black–Scholes for prediction markets*: a unified stochastic kernel (logit jump-diffusion) that treats traded probability `pt = S(xt)` as a Q-martingale. The risk-neutral drift is pinned down by `µ(t,x) = -[½S''(x)σ_b²(t,x) + ∫(S(x+z)-S(x)-S'(x)χ(z))ν_t(dz)] / S'(x)`. What remains tradable: belief volatility `σb`, jump intensity, and cross-event correlation/co-jumps. Defines a derivative menu (belief variance/volatility swaps, correlation swaps, corridor variance, threshold/first-passage notes) on top of an LMSR-compatible state.
- Dalen's central thesis on why LMSR alone isn't enough: "Today, venues execute via scoring rules or AMMs or CLOBs, but there is no shared stochastic model for how probabilities evolve across time, shocks, or related events. Without a kernel, makers cannot isolate 'belief risk' (level, volatility, jumps, co-movement) or lay it off in a standard way; spreads widen around news; inventory near the 0/1 boundaries becomes hard to manage."
- Powell, Hanson, Laskey & Twardy (2013) ran the canonical experimental study on combinatorial prediction markets using the DAGGRE platform · a Bayesian-network-driven murder-mystery experiment where participants traded conditional and Boolean events. Their experimental result: "economic theory suggests that the greater expressivity of combinatorial prediction markets should improve accuracy by capturing dependencies among related questions" · empirically validated against flat structures. LMSR's log cost function decomposes naturally over the combinatorial state space.
- Baheet's game-theory explainer: LMSR works *because* it's a proper scoring rule · truth-telling is the dominant strategy. Builders need economists, not just engineers.
- Jo Lim's Strait of Hormuz piece argues LMSR/CLMSR is uniquely suited to events *without underlying assets* · the protocol-native liquidity property (you don't need a counterparty) is what makes scoring-rule markets viable for granular, multi-outcome risk pricing.
- LMSR's "bounded loss" property is the killer feature for *subsidized* markets · a market designer puts up capital `b · ln(n)` and that's the max they can lose, irrespective of trade volume. This is what makes long-tail markets economically viable.
- The liquidity parameter `b` is the key design knob: high `b` = tight prices, more capital risk, lower price sensitivity. Low `b` = volatile prices, less capital risk, more responsive to small trades.
- **Open problem:** LMSR's structural-loss issue for binaries doesn't apply identically to continuous outcomes or to non-binary multi-outcome markets · there's an emerging design conversation (CLMSR, scoring-rule AMMs for continuous distributions) that Dekant's L2-norm CFAMM is part of.

## Notable Quotes

> "In a binary market that resolves to 0 or 1, impermanent loss becomes permanent: the pool inevitably holds worthless shares on the losing side, and trading fees cannot offset a guaranteed structural loss."  
> — *Melee, *Why AMMs Failed Prediction Markets**

> "For cost-function market makers such as LMSR, providing more liquidity increases worst-case loss; without external subsidies, they are expected to run at a deficit proportional to the liquidity they offer."  
> — *Dalen et al., *Toward Black–Scholes for Prediction Markets**

> "Today, venues execute via scoring rules or AMMs or CLOBs, but there is no shared stochastic model for how probabilities evolve across time, shocks, or related events."  
> — *Dalen et al., *ibid.**

> "Uses LMSR's mathematical identity with the softmax function to bridge quant finance and prediction market pricing."  
> — *gemchanger, *Your Hedge Fund's Sharpe Ratio Is a Lie**

> "Many automated market maker mechanisms have been designed to provide (effectively infinite) liquidity for prediction markets."  
> — *Chen & Pennock, *Designing Markets for Prediction**

## Where It Matters

LMSR is the mechanism every prediction-market designer benchmarks against. The orthodoxy is "LMSR for low-liquidity / long-tail, CLOB for liquid markets" · Polymarket validated this by migrating to CLOB once liquidity was sufficient. But the failure of LMSR on binaries does *not* generalize to continuous markets, where the partition-of-unity structure (and Dekant's L2-norm CFAMM specifically) keeps the AMM solvent. LMSR remains the conceptual ancestor of every cost-function AMM in the space, and the softmax identity is what lets traditional quant tools transfer directly. Dalen's logit jump-diffusion framework is a strong candidate for the missing institutional pricing kernel sitting on top of an LMSR-compatible state.

## Related Concepts

- Proper scoring rules · LMSR is the cost-function dual of the log scoring rule
- Market scoring rules · LMSR is the canonical instance
- Market making · LMSR is an *automated* market maker (no inventory risk in the usual sense)
- Order book · Polymarket abandoned LMSR for CLOB
- Combinatorial prediction markets · LMSR decomposes naturally for combinatorial pricing
- Liquidity provision · LMSR's `b` parameter sets subsidized liquidity
- Incentive compatibility · LMSR inherits IC from the log scoring rule

## Sources

- [Why AMMs Failed Prediction Markets](https://x.com/meleemarkets/status/2043766417342755259) — Melee · X
- [Your Hedge Fund's Sharpe Ratio Is a Lie. Prediction Markets Are the Only Place It Can't Hide.](https://x.com/gemchange_ltd/article/2026300422483271733) — gemchanger · X
- [Toward Black–Scholes for Prediction Markets](https://arxiv.org/pdf/2510.15205) — Shaw Dalen, Daedalus Research Team · arXiv
- [The Game Theory Behind Prediction Markets](https://x.com/Baheet_/status/1965758390430208066) — Baheet · X
- [Designing Markets for Prediction](https://bpb-us-e1.wpmucdn.com/sites.harvard.edu/dist/b/845/files/2025/01/aim10.pdf) — Yiling Chen, David M. Pennock · Harvard
- [Combinatorial Prediction Markets: An Experimental Study](https://link.springer.com/chapter/10.1007/978-3-642-40381-1_22) — Powell, Hanson, Laskey & Twardy · Springer

