# Cross-Platform Arbitrage

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Cross-Platform Arbitrage](https://www.pmatlas.xyz/concepts/cross-platform-arbitrage)

---

## Definition

**Quick definition.** Trading the same event across different prediction-market platforms to profit from price discrepancies. In practice it's rarely risk-free because different platforms resolve the "same" event under different rules and oracles, leaving residual basis risk.

## Key Insights

- Lotus: same contract priced at **58–67 cents on different platforms simultaneously**. Cross-venue fragmentation is its own asset, and conventional MMing fails partly because of it.
- Kunal Doshi (Kalshi vs. FanDuel): in-game prices correlate at **0.99+** with FanDuel; Kalshi monthly volume grew **80× to $14.4B in March 2026** (NCAA March Madness alone = $3.3B). But Kalshi taker fees up to **3.5% at midpoint** and **76% in-game depth decline vs. pre-game** currently limit institutional execution. Valuation comp: Kalshi $20B as exchange vs. sportsbooks at 2–4× revenue.
- Pantera/Sui's NFL dataset (282 games, Sep 2025–Jan 2026) gave concrete numbers to cross-platform arb potential: Kalshi $1.3B notional vs. Polymarket $359M for the same games; Kalshi leads in 80% of large moves with median **7-second lead**; but Polymarket has **3-4× more concentrated liquidity** by Kyle-λ. The arb opportunity is in the latency gap *and* the depth differential.
- st1ne identifies five MEV-style edges, one of which is cross-platform conditional probability arbitrage across correlated markets. Polymarket is "a hidden MEV playground."
- Saguillo et al.: **~$40M extracted from Polymarket via single-market and cross-related-market arbs**, identified via blockchain tx analysis. Two distinct types: Market Rebalancing Arbitrage (within a market when YES+NO < $1) and Combinatorial Arbitrage (across topically related markets). Their analysis required heuristic reductions to bring the O(2^(n+m)) comparison down to feasible.
- Hall & Paschal's overlap data: only **70 of 131 (53%) resolved US elections appeared on both Kalshi and Polymarket**, and overlap drops to **18%** among 1,826 active electoral events. Cross-platform arb is structurally constrained because the same event simply isn't listed on both venues most of the time. They explicitly call for an ISDA-equivalent for PM contract definitions.
- Sethi ("Information Contagion"): the 15-minute spike in oil/stock futures preceding Trump's March 24, 2026 Iran-negotiations tweet shows arbitrage *across* PM and traditional venues · quant funds extracted the signal from Polymarket and acted in regulated derivatives, profiting without breaking any law. This is the most consequential form of cross-platform arb because it routes around KYC.
- Mike's risk-instruments piece: cross-platform composability is live via Gondor (lending against PM positions) and DFlow (tokenizing Kalshi contracts as SPL tokens). Liquidity fragmentation, execution risk, UX are barriers.
- Nekt0 lists cross-platform arb as one of eight strategies, with dollar examples and specific risk factors.
- Cryptonomads: most PM terminals fail at execution; the cross-platform layer is exactly where institutional API rails win.
- The implicit thesis across these pieces: **the same event being available on Kalshi (regulated, KYC) and Polymarket (offshore, pseudonymous) is the largest single source of structural arbitrage in the asset class** · and information contagion across this boundary (Sethi) is what regulators are now watching.
- michaellwy: each Polymarket↔Kalshi pair is fundamentally non-fungible because of different referee systems. "Identical" contracts on Polymarket (UMA oracle) and Kalshi (CFTC-designated contract market) can resolve differently · eliminating the "risk-free" element of arbitrage in many cases.

## Notable Quotes

> "The same contract at 58-67 cents on different platforms."  
> — *Lotus*

> "In-game prices correlate at 0.99+ with FanDuel."  
> — *Kunal Doshi*

> "Information flows freely across platforms even when regulatory frameworks differ."  
> — *Rajiv Sethi*

> "Only 70 of 131 resolved elections appeared on both platforms; among 1,826 active electoral events, overlap drops to 18%."  
> — *Hall & Paschal*

## Where It Matters

Cross-platform arbitrage is what makes the multi-venue prediction-market world *behave* like a single market · without it, Polymarket and Kalshi quotes could drift indefinitely. But the arbitrage is bounded by capital lock-up (positions don't settle until resolution), oracle divergence (Kalshi's CFTC settlement vs. UMA on Polymarket can differ), and KYC frictions (the same trader cannot always access both venues legally). Every "meta-pool" or "unified PM liquidity" proposal in 2026 is effectively an attempt to internalize cross-platform arbitrage on behalf of the network.

## Related Concepts

- **Arbitrage** · the parent concept.
- **Liquidity fragmentation** · what creates the cross-platform spread.
- **Resolution criteria / oracle design** · what limits "risk-free."
- **Time arbitrage / orderflow arbitrage** · co-located strategies.
- **Execution quality** · what arbitrageurs sell as a service to slower flow.
- **Platform competition** · the meta-game.

## Sources

- [Why Every Prediction-Market Terminal Will Fail (and the Two That Won't)](https://x.com/0xCryptoNomads/status/2048664550225170605) — cryptonomads · Apr 27, 2026
- [Market Making In PMs Sucks](https://x.com/uselotusai/status/2046639095531614455) — Lotus · Apr 21, 2026
- [From Betting to Trading: How Kalshi Is Reshaping Sports Markets](https://x.com/Kunallegendd/status/2044780671839952949) — Kunal Doshi · Apr 16, 2026
- [Polymarket Is a Hidden MEV Playground. Most Traders Have No Idea.](https://x.com/SolSt1ne/article/2032008002094366899) — st1ne · Mar 13, 2026
- [How Prediction Markets Turn Into Risk Instruments](https://x.com/mikhryc0x/article/2020802941091741972) — Mike · Feb 9, 2026
- [All Types of Arbitrage on Prediction Markets](https://x.com/Nekt_0/article/2019107985079816395) — Nekt0 · Feb 5, 2026
- [Unravelling the Probabilistic Forest: Arbitrage in Prediction Markets](https://arxiv.org/abs/2508.03474) — Saguillo, Ghafouri, Kiffer, Suarez-Tangil · Aug 5, 2025

