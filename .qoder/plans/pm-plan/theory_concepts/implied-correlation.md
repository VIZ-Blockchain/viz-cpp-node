# Implied Correlation

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Implied Correlation](https://www.pmatlas.xyz/concepts/implied-correlation)

---

## Definition

**Quick definition.** The degree to which related prediction-market outcomes *should* move together based on economic logic. When markets price correlated events inconsistently, the implied correlation differs from the historically-justified correlation · and a relative-value trade exists.

## Key Insights

- Jon Turek frames implied correlation as a direct import from established trading literature: "relative value trading, dual digitals, dispersion trading" · concepts "very familiar to the trading community" that he argues map cleanly onto PMs.
- Turek's structural claim: correlation creates either **leverage** (juicing returns on a directional view by stacking correlated bets) **or arbitrage** (when historical correlation diverges from market-implied correlation), and both are live opportunities on Polymarket now.
- The Polymarket worked example is more granular than originally summarized: **30–35% recession probability** (matches Goldman sell-side estimates vs. an economist baseline of 15–20%); within the unemployment distribution, **65% chance unemployment rises to 5%** and **35% chance it rises to 5.5%**.
- Despite that distribution, the Fed-rate market is pricing "a baseline of no cuts, with an even bias towards a hike and a cut as the next move" · and "only a 20% chance that the Fed cuts more than two times this year".
- Turek's historical reference set for unemployment rising **+1.2% or more in a year** (i.e. 5.5%): 1974–75 oil shock, 1980–82 double-dip, 1990–91 Gulf War, 2001 dot-com bust, 2007–09 Great Recession, 2020 COVID. **Average rate cuts across those six episodes: seven.**
- Macro context Turek cites: US economy "shed 92k jobs" last month, ~6 months of weak payroll growth, falling JOLTs job openings, and a fresh "energy shock from the war in Iran" · all pushing the unemployment-skew higher, but the Fed-cut market hasn't repriced accordingly.
- The structural cause Turek names explicitly: PM contracts are "priced idiosyncratically" · each market with its own MMs, retail base, narrative cycle, and no cross-market arbitrageur ensuring economic coherence. This is a renewable source of relative-value trades for anyone with a coherent macro/economic model.
- Trade construction Turek proposes: **long "YES" on Fed rate cuts + short "NO" on unemployment outcomes** · captures payouts in *either* macro outcome because the legs hedge each other where history says they should be correlated.
- The natural product layer is covariance markets (which would let traders express implied-correlation views directly) and AI agents (which can scan thousands of pairs faster than humans).
- Connection to st1ne's "conditional probability arbitrage across correlated markets" MEV edge · the trader-side counterpart to building covariance infrastructure.
- The pattern resembles **classic equity pair-trading** but operates over discrete events with cleaner economic linkages.

## Notable Quotes

> "Polymarket currently prices a 60%+ chance of 5% unemployment this year alongside only a 10% chance of aggressive Fed rate cuts, even though every historical episode of that unemployment spike triggered an average of seven cuts."  
> — *Jon Turek*

> "Cross-market mispricings like this are common and create attractive relative value trades."  
> — *Jon Turek*

> "These markets have a high degree of implied correlation and that is not being reflected in the current prices. Even at a high level, if the unemployment rate went to 5% this year, which prediction markets assume to be a +60% chance outcome, there is no way that 'on hold' + 'next move hike' should be a combined 50% chance on Polymarket."  
> — *Jon Turek*

> "What we have is a lot of listed markets that are being priced idiosyncratically but many of them have an implied correlation to them."  
> — *Jon Turek*

## Where It Matters

Implied correlation is the cleanest argument for why prediction markets · *as a network of independently-priced contracts* · fail to aggregate information coherently across related events. Each market may be individually well-calibrated, but the joint distribution is incoherent. This is partly what limits the asset class's use for serious hedging and macro signaling, and it's the empirical case for both covariance-market infrastructure and cross-market AI agents.

## Related Concepts

- **Relative value trading** · the strategy that consumes implied-correlation mispricing.
- **Covariance markets** · the proposed primitive for expressing it directly.
- **Arbitrage** · parent category.
- **Information aggregation** · what implied correlation reveals is broken in current PMs.
- **AI agents** · scaling enabler.

## Sources

- [Prediction Markets and Implied Correlation](https://cheapconvexity.substack.com/p/prediction-markets-and-implied-correlation) — Jon Turek · Mar 24, 2026

