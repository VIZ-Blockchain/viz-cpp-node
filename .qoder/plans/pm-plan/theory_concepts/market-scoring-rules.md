# Market Scoring Rules

**Category:** Mechanism Design  
**Source:** [PM Atlas — Market Scoring Rules](https://www.pmatlas.xyz/concepts/market-scoring-rules)

---

## Definition

A class of automated market maker mechanisms that subsidize trade by penalizing a market maker according to a proper scoring rule. Traders profit by moving prices closer to their beliefs, ensuring the market maker absorbs losses in exchange for eliciting honest probability estimates. LMSR is the most widely used instance.

## Key Insights

- Market scoring rules (MSRs) generalize *single-shot* proper scoring rules into a *sequential trading* mechanism: each trader pays to change the market's reported probability, and the market maker's payoff is the proper-scoring-rule difference between consecutive reports.
- XO Labs (Apr 2026) · *Market Making for Prediction Markets: A Probability-Space Approach.* Adapts the Avellaneda-Stoikov market-making framework to prediction markets. **Classical models fail** because prediction market prices are bounded probabilities (0 to 1), not unbounded asset prices, creating non-constant volatility and guaranteed terminal convergence at 0 or 1.
- XO Labs iteration sequence: a logit-space transformation lost $1,114 in backtest, then a probability-space engine with inventory-skewed spreads, volatility regime detection, and multi-outcome coordination turned profitable at $453. The piece provides the full mathematical framework and open problems.
- Jo Lim's Strait of Hormuz analysis (Mar 2026) · binary order-book markets hit an architectural ceiling on granular multi-outcome risk. LMSR / CLMSR (continuous LMSR) offers protocol-native liquidity, coherent pricing, and capital efficiency for events without underlying assets. Walks through a WTI crude oil scenario where scoring-rule markets reward *precise thesis expression* over simple directional bets.
- The Jo Lim piece is the clearest articulation in the corpus of why MSR/CLMSR is needed: traditional options solve granularity for tradable assets, but markets without underlyings (geopolitical events, weather, election margins) need a protocol-native pricing mechanism · that mechanism is an MSR.
- Powell, Hanson, Laskey & Twardy (2013) experimentally validate that MSR-based combinatorial structures improve forecasting accuracy and calibration vs flat (single-question) structures. Their DAGGRE platform used LMSR over a Bayesian-network-defined state space with explicit conditional events ("A given B") and Boolean combinations. The paper notes: "Economic theory suggests that the greater expressivity of combinatorial prediction markets should improve accuracy by capturing dependencies among related questions" · confirmed empirically on a murder-mystery task with Bayesian-network ground truth.
- MSR's key economic property: the market maker's *total loss is bounded* across all trades · for LMSR it's `b · ln(n)` for n outcomes. This is what makes subsidized long-tail markets viable without infinite capital exposure.
- The classic LMSR cost function `C(q) = b · log(Σ exp(q_i / b))` is identical to the softmax · gemchanger's piece links this to the entire ML / Bayesian regime detection toolkit.
- An MSR is *not* the same as a CLOB. Polymarket migrated *away* from MSR-based AMMs to CLOB once binary markets had sufficient depth (Melee). The MSR-vs-CLOB question is settled for liquid binaries (CLOB wins) but open for thin, long-tail, and continuous markets.
- The XO Labs work surfaces an underappreciated subtlety: probability-bounded prices mean *standard market-making intuitions don't transfer* · inventory risk, volatility, and convergence behavior are qualitatively different from FX or equities.
- Combinatorial MSRs (Hanson 2003+) elicit joint probabilities, not just marginals · but the experimental literature is mostly limited to small numbers of base events because the state space explodes.

## Notable Quotes

> "Classical models fail because prediction market prices are bounded probabilities (0 to 1) rather than unbounded asset prices, creating non-constant volatility and guaranteed terminal convergence."  
> — *XO Labs, *Market Making for Prediction Markets**

> "Automated market scoring rules (LMSR/CLMSR) offer protocol-native liquidity, coherent pricing, and capital efficiency for events without underlying assets."  
> — *Jo Lim, *The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap**

> "Scoring-rule markets reward precise thesis expression over simple directional bets."  
> — *Jo Lim, *ibid.**

> "Economic theory suggests that the greater expressivity of combinatorial prediction markets should improve accuracy by capturing dependencies among related questions."  
> — *Powell, Hanson, Laskey & Twardy, *Combinatorial Prediction Markets: An Experimental Study**

## Where It Matters

Market scoring rules are the *only* mechanism class designed from first principles for prediction markets · every other AMM is borrowed from spot-token DEXes and breaks under bounded probabilities. The category is moving back toward MSRs (CLMSR, kernel scoring rules) for the use cases CLOB cannot serve well: thin markets, long-tail questions, and continuous-distribution outcomes. Dekant's L2-norm CFAMM is in this lineage · a scoring-rule-style AMM for continuous distributions.

## Related Concepts

- Proper scoring rules · the formal foundation MSRs are built on
- LMSR · the canonical MSR (logarithmic case)
- Combinatorial prediction markets · MSRs scale to richer outcome spaces
- Order book · the alternative microstructure (Polymarket post-2022)
- Market making · MSRs are *automated* market makers
- Liquidity provision · MSR's bounded loss enables subsidized liquidity
- Multi-outcome markets · MSRs handle n>2 outcomes natively
- Adverse selection · informed traders extract from the MSR subsidy
- Bid-ask spread · MSR has a structural spread tied to the `b` parameter

## Sources

- [Market Making for Prediction Markets: A Probability-Space Approach](https://x.com/xolabs_/status/2045161249588269257) — XO Labs · X
- [The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap](https://x.com/jolimmmm/article/2036119259420807216) — Jo Lim · X
- [Combinatorial Prediction Markets: An Experimental Study](https://link.springer.com/chapter/10.1007/978-3-642-40381-1_22) — Powell, Hanson, Laskey & Twardy · Springer

