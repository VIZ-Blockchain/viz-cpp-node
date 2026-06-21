# Multi-Outcome Markets

**Category:** Mechanism Design  
**Source:** [PM Atlas — Multi-Outcome Markets](https://www.pmatlas.xyz/concepts/multi-outcome-markets)

---

## Definition

Prediction markets where an event is divided into multiple mutually exclusive outcome ranges (e.g., price brackets), each tradeable. Contrasts with binary markets that only ask yes/no questions. The natural shape for any question with more than two answers · price, time, magnitude, count.

## Key Insights

- functionSPACE's analysis of 36,777 Polymarket events found that when continuous questions are split into many binaries (the workaround for the lack of native multi-outcome markets), volume follows an extreme Pareto distribution · top 3 markets capture >75% of volume, leaving a fat tail of ghost markets with essentially no liquidity.
- The $0.01 tick size compounds the multi-outcome problem on stacked binaries: at low probabilities, the tick is a 50%+ relative move · making bracket prices structurally imprecise.
- Jo Lim (Strait of Hormuz, Mar 2026) explicitly frames multi-outcome markets as the missing architecture: binary order-book markets hit a ceiling on granular, multi-outcome risk. Traditional options solve this for tradable assets, but events without underlying assets need a different mechanism · that mechanism is the **automated market scoring rule (LMSR / CLMSR)**.
- The Jo Lim WTI crude oil scenario walks through how scoring-rule multi-outcome markets let traders express *precise theses* (e.g., "I think oil ends between $85 and $90") rather than just directional bets ("up or down"). This is the core informational advantage of multi-outcome markets.
- Multi-outcome markets *are* implementable as either (a) stacked independent binaries (Polymarket pattern · suffers fragmentation), (b) coherent LMSR/CLMSR over partitioned outcome space (preferred for thin markets), or (c) order books over a fixed bracket grid (Kalshi pattern for some markets).
- Coherent multi-outcome pricing requires that bracket prices sum to ~$1 · independent binary stacks frequently *don't* sum to $1 because each is independently quoted by separate liquidity. This is a price-discovery defect.
- Capital efficiency: in a coherent multi-outcome MSR, capital deployed by an LP supports *all* brackets simultaneously · you don't need separate capital per bracket. In stacked binaries, capital is partitioned, which is why ghost markets emerge.
- Continuous-outcome markets (Dekant's category) are the natural extension of multi-outcome markets · instead of N discrete brackets, you have a continuous curve over the outcome space, sampled at any granularity.
- Semantic tick size · the per-bracket pricing tick reads as a percentage-point probability revision in the binary stack; in a continuous market it reads as a density value, which is harder to UX but more expressive.
- Multi-outcome markets are the conceptual midpoint between binaries (N=2) and continuous distributions (N=∞). The category is moving in the continuous direction as event types diversify (functionSPACE's V2 piece notes continuous events overtook categorical in Polymarket's 2026Q1 data).

## Notable Quotes

> "Binary order-book prediction markets hit an architectural ceiling when pricing granular, multi-outcome risk."  
> — *Jo Lim, *The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap**

> "Scoring-rule markets reward precise thesis expression over simple directional bets."  
> — *Jo Lim, *ibid.**

> "The $0.01 tick size compounds the problem, creating a rounding tax that makes low-probability contracts structurally imprecise."  
> — *functionSPACE, *Binary Events**

## Where It Matters

Multi-outcome is the unfilled middle of the prediction-market design space. Polymarket's stacked-binary workaround is empirically broken (ghost markets, fragmentation, incoherent price sums), and the natural successor is either coherent CLMSR-style multi-outcome AMMs or continuous-distribution markets like Dekant. The case for moving to continuous is strong: as soon as you have ≥10 brackets, you're already approximating a curve, and the partition-of-unity machinery generalizes cleanly.

## Related Concepts

- Binary contracts · the alternative primitive (and the source of fragmentation)
- Market scoring rules · the AMM mechanism that handles multi-outcome natively
- Liquidity fragmentation · what stacked binaries produce
- Liquidity provision · multi-outcome MSRs unify capital across brackets
- Order book · Kalshi's pattern for some multi-outcome events
- Semantic tick size · the tick is per-bracket in stacked binaries

## Sources

- [Binary Events: What Happens When You Split One Market Into Twenty](https://x.com/functionspaceHQ/status/2039554933024776516) — functionSPACE · X
- [The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap](https://x.com/jolimmmm/article/2036119259420807216) — Jo Lim · X

