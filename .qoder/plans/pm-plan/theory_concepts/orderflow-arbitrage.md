# Orderflow Arbitrage

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Orderflow Arbitrage](https://www.pmatlas.xyz/concepts/orderflow-arbitrage)

---

## Definition

**Quick definition.** Profiting from temporary price dislocations caused by large orders that push the market away from fair value before it mean-reverts. The PM-specific version exploits **orderbook imbalance** · fading orders that consume one side of the book and reverting to mid before slower traders catch up.

## Key Insights

- st1ne identifies **orderbook imbalance exploitation** as one of five MEV-style edges on Polymarket. Sophisticated actors extract value from structural inefficiencies rather than informational edges.
- Nekt0 lists orderflow arbitrage as one of **eight distinct PM arbitrage strategies** (alongside classic YES+NO mispricing, cross-platform, range, conditional, time, hedged, resolution). Each comes with concrete dollar examples and risk factors.
- The implicit mechanism: when a large taker order hits one side of a thin PM order book, the price overshoots true value. A fast actor with capital on the other side can take the dislocated quote and ride it back to mid · this is the equivalent of equity-microstructure "liquidity-providing" market making but executed event-by-event rather than as a continuous quoting strategy.
- Co-located with @allquantor's semantic-tick-size finding: **~70% of one-cent price moves don't continue** · orderflow noise is the largest single contributor to those reversal events.
- Co-located with sybilpm's "Sniper's Tax": orderflow arbitrage and toxic-flow sniping look similar at the trade level (both are large orders hitting thin books) but diverge in intent. Orderflow arb assumes the move is uninformed; sniping assumes it's informed and acts before the MM can update.

## Notable Quotes

> "Sophisticated actors extract value from structural inefficiencies rather than informational edges."  
> — *st1ne*

## Where It Matters

Orderflow arbitrage is one of the cleanest profit pools for sophisticated PM participants, and it's invisible to retail because it only shows up in fill-quality analysis and reversal patterns. Microstructure-aware platform design (batched auctions, minimum resting times, hidden orders) directly compresses this edge · which is why the practitioners writing about it tend to be also writing about preserving the CLOB status quo.

## Related Concepts

- **Order book** · the venue.
- **Execution quality** · what orderflow arb sells.
- **Adverse selection** · orderflow arb is the *non*-adverse subset of order-impact trading.
- **Semantic tick size** · the source of the reversal pattern.
- **Arbitrage** · parent concept.
- **Cross-platform / time / resolution arbitrage** · sibling strategies.

## Sources

- [Polymarket Is a Hidden MEV Playground. Most Traders Have No Idea.](https://x.com/SolSt1ne/article/2032008002094366899) — st1ne · Mar 13, 2026
- [All Types of Arbitrage on Prediction Markets](https://x.com/Nekt_0/article/2019107985079816395) — Nekt0 · Feb 5, 2026

