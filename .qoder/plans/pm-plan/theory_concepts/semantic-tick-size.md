# Semantic Tick Size

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Semantic Tick Size](https://www.pmatlas.xyz/concepts/semantic-tick-size)

---

## Definition

**Quick definition.** The minimum price increment in a prediction market that doubles as a *narrative unit*. Because contracts resolve at $0 or $1, each one-cent move is universally read as a one-percentage-point probability revision · making microstructure noise *appear* informative. Coined by @allquantor.

## Key Insights

- @allquantor's source data: **600 million Polymarket orderbook datapoints**. Headline finding: **~70% of one-cent price moves do not continue in the same direction** · most penny moves are noise that gets reinterpreted as signal because the price *is* a probability.
- The implication is a **contrarian fade strategy**: systematically fade penny moves on PMs and you harvest the overreaction.
- The piece situates this against Paul Tetlock's TradeSports microstructure research: passive limit-order walls slow price discovery while impatient market orders amplify short-term noise. PMs amplify this dynamic because every penny carries narrative weight.
- functionSPACE V1: the $0.01 tick size compounds liquidity fragmentation by adding a **rounding tax** that makes low-probability contracts structurally imprecise. A contract that "should" trade at 0.3% can only be quoted at 0% or 1%, both of which are wrong.
- The combined effect: penny ticks are simultaneously **too large** for low-probability contracts (rounding tax) and **too small** for mid-probability contracts (noise gets narrativized).
- Implicit design implication: prediction markets may benefit from probability-aware variable tick sizes · finer granularity in the tails, coarser in the middle.

## Notable Quotes

> "70% of one-cent price moves do not continue in the same direction."  
> — *@allquantor*

> "Each penny reads as a one-percentage-point probability change, creating overreactions that a contrarian fade strategy can profitably harvest."  
> — *@allquantor*

> "The $0.01 tick size... creates a rounding tax that makes low-probability contracts structurally imprecise."  
> — *functionSPACE*

## Where It Matters

Semantic tick size is one of those concepts that, once named, becomes hard to unsee. It explains why CNN/WSJ headlines about "Polymarket probability of X jumped 4 points" can be entirely noise, why MMs can profitably fade headline moves, and why every continuous-distribution / multi-outcome design proposal is partially motivated by escaping the penny's narrative weight. It's also a structural reason PM prices are *not* directly equal to true probabilities · they're heavily perturbed by microstructure with a probability-shaped projection.

## Related Concepts

- **Bid-ask spread** · penny ticks bound how tight spreads can be.
- **Price discovery** · tick size shapes how price discovery looks at high frequency.
- **Liquidity fragmentation** · the rounding tax compounds across many fragmented contracts.
- **Binary contracts / multi-outcome markets** · the structural environment.
- **Calibration** · semantic tick noise pushes prices away from calibrated probabilities.

## Sources

- [Binary Events: What Happens When You Split One Market Into Twenty](https://x.com/functionspaceHQ/status/2039554933024776516) — functionSPACE · Apr 2, 2026
- [Prediction Markets Have a Semantic Tick Size](https://x.com/ZEITFinance/article/2034314079599243694) — allquantor · Mar 19, 2026

