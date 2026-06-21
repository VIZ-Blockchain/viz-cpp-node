# Arbitrage

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Arbitrage](https://www.pmatlas.xyz/concepts/arbitrage)

---

## Definition

**Quick definition.** Exploiting price differences for equivalent outcomes across markets or platforms for risk-free profit. In prediction markets, "risk-free" is misleading · most apparent arbs carry resolution risk, oracle risk, or capital-lock risk that can dwarf the spread.

## Key Insights

- Nekt0 catalogs **eight distinct arbitrage strategies** on PMs: classic YES+NO mispricing, cross-platform, range, conditional, time, hedged, resolution, orderflow. Each comes with dollar examples and risk factors.
- Saguillo et al. (arXiv 2508.03474): identify two distinct arbitrage forms on Polymarket · **Market Rebalancing Arbitrage** (within a single market, e.g., YES+NO < $1 mispricings) and **Combinatorial Arbitrage** (across related markets). The naive cross-market comparison is O(2^(n+m)); they use heuristic reductions based on timeliness, topical similarity, and combinatorial relationships. Empirical estimate: **$40M in realized arbitrage profits extracted** from Polymarket via on-chain order-book analysis.
- michaellwy: apparent Polymarket↔Kalshi arb is **rarely risk-free** · different rule definitions, resolution criteria, and oracle systems mean "identical" markets can resolve differently. PMs are **fundamentally non-fungible** due to different referee systems.
- Jon Turek's implied-correlation thesis: Polymarket prices a 60%+ chance of 5% unemployment alongside only a 10% chance of aggressive Fed cuts · but every historical episode of that unemployment spike triggered an average of seven cuts. The historical anchor: rate-cut counts during episodes when unemployment rose 1.2pp+ in a year · 1974-75 (oil shock), 1980-82 (double-dip), 1990-91 (Gulf War), 2001 (dot-com), 2007-09, 2020 (COVID); average ≈ 7 cuts. Cross-market mispricings like this are common and create relative-value trades · Turek's actual proposed trade: NO on unemployment outcomes + YES on Fed cuts.
- Turek's framing on PM correlation arb: relative-value trading + correlation trading is "very familiar to the trading community" via dual digitals, dispersion trading, etc. PM's listed markets are "priced idiosyncratically but many have implied correlation" · the structural ineffiiency is that traders treat each market in isolation.
- Roan: "Polymarket's CLOB creates renewable structural arbitrage by design" · the diagnostic toolkit is Kelly sizing, fill-quality measurement of adverse selection, and probability-term-structure analysis.
- Jeff Opinion's Meta Pool: cross-chain infrastructure to unify PM liquidity. Resolution-aware meta-pools, CredibilityTokens for oracle trust, ConvergenceTokens for divergence hedging. Estimates **$3.4–8.5M in annual efficiency losses** from current fragmentation.
- John Sides on Clinton-Huang research (2,500 political prediction markets, >$2B notional, final 5 weeks of 2024 cycle across IEM, Kalshi, PredictIt, Polymarket): **PredictIt 93%, Kalshi 78%, Polymarket 67% accuracy**, with cross-platform divergences for identical contracts; daily price changes were weakly correlated or negatively autocorrelated; arbitrage opportunities peaked in the final two weeks. CNN/CNBC partnerships create incentives for sensational coverage of thin markets.
- keshav: collateralization solves the capital lock-up problem in long-dated markets, *correcting persistent mispricings like longshot mispricing*. Liquidation risk in binaries is the catch.
- LessWrong "Will Jesus Christ Return": **~$1M lockup needed for 1% return** to fade 3% → 1% · classic textbook example of why time-cost-of-capital destroys arbitrage in long-dated PM markets. The deeper finding (Eric Neyman, Time-Value-of-PM-Cash): late-October 2024 Kamala-in-Kentucky and Trump-in-Massachusetts traded at 0.3% and spiked to 1.5% on election day as No-holders desperately needed cash for other trades · a 5x intra-window arb. "Time value of Polymarket cash" is its own tradable asset, decoupled from the global risk-free rate because moving USD in/out is illegal for Americans and takes days.
- Rasooly & Rozzi (arXiv 2503.03312): randomized field experiment shocking prices across **817 separate markets** to test manipulability. Effects of the trades persisted for **60 days post-intervention** · markets are manipulable but the effects fade. Markets with more traders, more volume, and an external probability anchor are harder to manipulate (a hedge against the Gunitsky regime-power thesis).
- a16z (Immerman & Rodriguez) institutional adoption stages: data → compliance → hedging. The arbitrageur layer is currently between data and compliance · institutions can extract signal from PM mispricings without yet using PMs as the execution venue.

## Notable Quotes

> "Prediction markets are fundamentally non-fungible due to differing referee systems."  
> — *michaellwy*

> "Polymarket's CLOB creates renewable structural arbitrage by design."  
> — *Roan*

> "$40M in profits were extracted through exploitation of these pricing inconsistencies."  
> — *Saguillo et al.*

> "These markets have a high degree of implied correlation and that is not being reflected in the current prices."  
> — *Jon Turek*

> "We find that prediction markets can be manipulated: the effects of our trades are visible even 60 days after they have occurred. However, the effects of the manipulations somewhat fade over time."  
> — *Rasooly & Rozzi*

## Where It Matters

Arbitrage is what would normally enforce price coherence across platforms · but in PMs every "arb" is conditional on resolution agreement, oracle agreement, and capital-availability assumptions. That means the existence of arbitrage strategies (eight of them, per Nekt0) doesn't mean prices converge; it means a narrow set of sophisticated actors capture *that* edge while the broader market remains fragmented. Cross-platform meta-pools, standardized resolution criteria, and tokenized PM positions are the leading attempts to make true risk-free arbitrage exist in the asset class for the first time.

## Related Concepts

- **Cross-platform arbitrage** · the most-studied subset.
- **Temporal / time arbitrage** · the time dimension.
- **Orderflow arbitrage** · large-order-impact strategies.
- **Implied correlation / relative value trading** · multi-market arbs.
- **Resolution criteria / oracle design** · the binding "non-fungibility."
- **Position collateralization** · frees the capital that underpins arbs.
- **Liquidity fragmentation** · what creates the price gaps in the first place.

## Sources

- [Prediction Markets and Implied Correlation](https://cheapconvexity.substack.com/p/prediction-markets-and-implied-correlation) — Jon Turek · Mar 24, 2026
- [Turning Probability into Assets: A Look Ahead at Prediction Market Agents](https://x.com/0xjacobzhao/article/2029229969914904694) — Jacob Zhao · Mar 5, 2026
- [Why Prediction Markets Aren't Gambling? (The Math)](https://x.com/RohOnChain/article/2020565633453412751) — Roan · Feb 9, 2026
- [All Types of Arbitrage on Prediction Markets](https://x.com/Nekt_0/article/2019107985079816395) — Nekt0 · Feb 5, 2026
- [The Perils of Election Prediction Markets](https://goodauthority.org/news/the-perils-of-election-prediction-markets/) — John Sides · Dec 18, 2025
- [Prediction Markets: The Next Level](https://x.com/0xkeshav_/article/1970163909224169626) — keshav · Sep 23, 2025
- [Meta Pool: A Unified Infrastructure for Liquidity and Resolution Trust in Prediction Markets](https://x.com/jeff__opinion/article/1957611486991487486) — Jeff Opinion · Aug 19, 2025
- [Unravelling the Probabilistic Forest: Arbitrage in Prediction Markets](https://arxiv.org/abs/2508.03474) — Saguillo, Ghafouri, Kiffer, Suarez-Tangil · Aug 5, 2025
- [The Risk Behind Arbitrage in Prediction Markets](https://x.com/michael_lwy/status/1925890768654254571) — michaellwy · May 23, 2025

