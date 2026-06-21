# Liquidity Fragmentation

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Liquidity Fragmentation](https://www.pmatlas.xyz/concepts/liquidity-fragmentation)

---

## Definition

**Quick definition.** The dispersion of trading capital across multiple independent order books when a single question is split into many binary contracts. In prediction markets this creates *ghost markets* where tail outcomes receive little or no volume, reducing the information captured by the market structure as a whole.

## Key Insights

- functionSPACE V1 (36,777 Polymarket events): volume follows an **extreme Pareto distribution** · top 3 markets capture **>75% of trading activity** regardless of event size; the rest are largely untradeable ghost markets. The $0.01 tick size compounds the problem by adding a "rounding tax" that makes low-probability contracts structurally imprecise.
- functionSPACE V2 (18,863 multi-market events, split continuous vs. categorical): both types concentrate **90% of volume in top 5–6 markets**, but **ghost markets are largely a *categorical* phenomenon**. Continuous events (price brackets, weather ranges, margin %) distribute volume more evenly and survive the liquidity cliff longer at high N. **Continuous events overtook categorical by event count in 2026 Q1** · making the case for continuous-distribution primitives apply to a growing share of the platform.
- Lotus: **cross-venue fragmentation** is its own problem · same contract trading at **58–67 cents** on different platforms. The January 2026 Polymarket XRP exploit paid **$231K on thin weekend liquidity** precisely because fragmentation prevents cross-venue MM defense.
- Lotus also reports that across 150M Polymarket trades, the **top 5% of skilled traders earned $228M** while spread capture barely moves P&L · most LP P&L is event-driven, not microstructure-driven, which is what fragmentation does.
- The structural argument running through all three pieces: **binary contracts impose a fragmentation tax that continuous-distribution markets don't pay**. This is the same conclusion the distribution-markets and covariance-markets literature reaches from the design side.

## Notable Quotes

> "The top 3 markets capture over 75% of trading activity regardless of event size."  
> — *functionSPACE V1*

> "Ghost markets turn out to be largely a categorical phenomenon."  
> — *functionSPACE V2*

> "Same contract at 58-67 cents on different platforms."  
> — *Lotus*

## Where It Matters

Liquidity fragmentation is the structural argument *for* multi-outcome markets, continuous-distribution markets, covariance markets, and parlays-without-fragmentation. Every multi-binary representation of a single underlying question pays a Pareto tax · only the top few contracts get traded, the rest are dead. Builders working on distribution primitives (Dekant, functionSPACE's vision, Limitless's CLOB-on-distributions) are explicitly attacking this tax.

## Related Concepts

- **Binary contracts** · the structural cause.
- **Multi-outcome / distribution markets** · the proposed fix.
- **Minimum viable liquidity** · many fragmented markets fall below MVL.
- **Long-tail markets** · fragmentation kills them.
- **Cross-platform arbitrage** · fragmentation across venues.
- **Semantic tick size** · the $0.01 tick is the rounding tax that compounds fragmentation.
- **Covariance markets** · explicit solution for the parlay-fragmentation case.

## Sources

- [Binary Events V2: Does Liquidity Trade The Tails?](https://x.com/functionspaceHQ/status/2048536153662636173) — functionSPACE · Apr 27, 2026
- [Market Making In PMs Sucks](https://x.com/uselotusai/status/2046639095531614455) — Lotus · Apr 21, 2026
- [Binary Events: What Happens When You Split One Market Into Twenty](https://x.com/functionspaceHQ/status/2039554933024776516) — functionSPACE · Apr 2, 2026

