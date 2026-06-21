# Parlays

**Category:** Business & Platforms  
**Source:** [PM Atlas — Parlays](https://www.pmatlas.xyz/concepts/parlays)

---

## Definition

**Quick definition.** Multi-leg bets combining outcomes across several events. Higher payoffs at lower probability, sportsbook-grade product feature that PMs are racing to replicate without fragmenting liquidity.

## Key Insights

- Sakshi Mishra (a1research) lists parlays among the five infrastructure unlocks to reach $1T: liquidity sustainability (subsidized MM transitioning to self-sustaining), discovery/UX, trade expressiveness (leverage faces unique gap risk in binary markets), permissionless market creation, and multi-tier oracle resolution.
- Mason Nystrom's GTM thesis: successful PMs will focus on three characteristics · high leverage (parlays, perps, intraday events), highly frequent markets (retention), and high market outcome values (capital attraction). "Prediction market wars have only begun."
- Chicken (voliti.co) proposes covariance markets as the cleanest parlay solution: instead of creating separate markets for all AND/OR combinations (which fragments liquidity), one covariance market between two base markets enables all 8 joint combinations while keeping liquidity concentrated.
- Mike (mikhryc0x) frames parlays through the derivatives-on-stocks analogy: PMs are evolving a second layer of risk instruments. Covers three hedging use cases (crypto risk hedging via binary price markets, attention markets like Trendle as sentiment hedges, cross-platform hedging enabled by DeFi composability · Gondor lending against PM positions, DFlow tokenizing Kalshi contracts as SPL tokens).
- Tim.0x's local-maxima diagnosis: Polymarket and Kalshi have PMF but are stuck. Three barriers: insufficient liquidity (small trades materially reprice markets), lack of parity with sportsbooks on parlays, and inability to resolve complex outcomes (Time Person of the Year resolving to "other").
- aaronjmars taxonomy: 14 PM mechanism types beyond standard binary, including covariance markets and quantum markets ("capital-efficient parallel conditionals" · a different architectural answer to the parlay problem).
- The DraftKings/FanDuel parlay edge is the implicit benchmark: sportsbooks charge fat parlay overrounds (a parlay of two 50% events sometimes priced as if it were 18% rather than 25%). PMs that match parlay UX while preserving fair pricing could capture sharp money from sportsbooks.
- mph's supercycle thesis acknowledges parlays as a critical product gap. Polymarket's pricing edge over sportsbooks on single events doesn't extend to multi-leg bets without dedicated parlay infrastructure.

## Notable Quotes

> "Successful prediction markets will focus on three characteristics: high leverage (parlays, perps, intraday events), highly frequent markets, and high market outcome values."  
> — *paraphrased from Mason Nystrom, *Prediction Markets GTM Approaches**

> "The prediction market wars have only begun."  
> — *Mason Nystrom, *Prediction Markets GTM Approaches**

## Where It Matters

Parlays are the most strategically important product gap between PMs and sportsbooks. They're how casual users express conviction across multiple correlated bets (same-game parlays, "Trump wins AND inflation > 3%"), and they're how platforms monetize square retail. Solving parlays without fragmenting liquidity is one of the standing design challenges. For Dekant: continuous markets implicitly support a richer expression than parlays · drawing a curve over a joint distribution is itself a multi-leg parametric bet · but the UX framing would need to translate.

## Related Concepts

- Covariance markets · the proposed liquidity-preserving parlay architecture
- Liquidity fragmentation · the failure mode parlay design must avoid
- Conditional tokens · Gnosis CTF natively supports parlay-like position combinations
- Gap risk · binary parlays compound jump risk multiplicatively
- Hedging · parlays as multi-leg directional or correlation bets
- Cross-platform arbitrage · parlay legs can be arbed across venues with different pricing

## Sources

- [Are We on the Brink of a Prediction Market Supercycle?](https://x.com/mphrediction/status/2043419029378048164) — mph · Apr 12 2026 ·
- [Prediction Markets: The Path to $1 Trillion and What Needs to Happen Next](https://x.com/a1research__/article/2022340314770375024) — Sakshi Mishra · Feb 14 2026 ·
- [How Prediction Markets Turn Into Risk Instruments](https://x.com/mikhryc0x/article/2020802941091741972) — Mike · Feb 9 2026 ·
- [Why Prediction Markets Are Where They Are](https://tim0x.substack.com/p/why-prediction-markets-are-where) — Tim.0x · Jan 5 2026 ·
- [A Small Prediction Market Design Taxonomy](https://x.com/aaronjmars/article/1991975420669915328) — aaronjmars · Nov 22 2025 ·
- [Prediction Markets GTM Approaches](https://x.com/masonnystrom/status/1950395287975186715) — Mason Nystrom · Jul 31 2025 ·
- [How to Offer Prediction Market Parlays Without Fragmenting Liquidity](https://x.com/voliti_co/status/1946232456459255869) — Chicken · Jul 18 2025 ·

