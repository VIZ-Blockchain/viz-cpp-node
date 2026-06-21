# Time Arbitrage

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Time Arbitrage](https://www.pmatlas.xyz/concepts/time-arbitrage)

---

## Definition

**Quick definition.** Profiting from a delay between when information becomes known and when the prediction market price adjusts to reflect it. Closely related to *oracle latency arbitrage* · the classic example being trading on news before the on-chain oracle (e.g., UMA) updates the resolution-relevant data.

## Key Insights

- st1ne enumerates **oracle latency arbitrage** as one of five MEV-style edges on Polymarket: trading on news *before* UMA oracle updates. Related: **resolution arbitrage** (front-running outcome settlement) and **dispute sniping** (gaming the UMA dispute process). All three are subsets of the broader "time arbitrage" frame.
- Nekt0 lists time arbitrage as one of **eight distinct PM arbitrage strategies**, with concrete dollar examples and risk factors.
- The PM-specific structural cause: **oracles update slower than information propagates**. Twitter posts, CNBC interviews, and government announcements all hit faster than UMA, Kalshi's manual review, or any other resolution mechanism. Time-arbitrageurs exploit the gap.
- Related (covered under *insider trading* and *toxic flow*): when the time-arb edge becomes large enough, it bleeds into MNPI territory · buying a market on the basis of imminent-but-unannounced information is structurally similar to oracle-latency arb when the resolution signal is itself the news.
- The defense against time arbitrage looks identical to the defense against gap risk: batched auctions, delayed settlement, dynamic spread widening near expected information arrival.

## Notable Quotes

> "Sophisticated actors extract value from structural inefficiencies rather than informational edges."  
> — *st1ne (re: oracle latency arb)*

## Where It Matters

Time arbitrage is what determines who profits from the gap between *real-world resolution* and *on-chain settlement*. On Polymarket it's a continuous revenue stream for actors with low-latency news pipelines; on Kalshi, the same edge exists but in a centralized form (the team that gets news first into the matching engine). Every platform's "fairness" claims are testable against how compressed this window is.

## Related Concepts

- **Orderflow arbitrage** · sibling strategy.
- **Cross-platform arbitrage** · time arbs frequently cross venues.
- **Resolution criteria / oracle design** · directly control the size of the time-arb window.
- **Insider trading** · when the news being front-run is non-public, the line gets crossed.
- **Adverse selection** · time arb is one of its purest forms.
- **Batched auctions** · leading mitigation.

## Sources

- [Polymarket Is a Hidden MEV Playground. Most Traders Have No Idea.](https://x.com/SolSt1ne/article/2032008002094366899) — st1ne · Mar 13, 2026
- [All Types of Arbitrage on Prediction Markets](https://x.com/Nekt_0/article/2019107985079816395) — Nekt0 · Feb 5, 2026

