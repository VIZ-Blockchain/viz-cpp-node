# Distribution Markets

**Category:** Information Theory  
**Source:** [PM Atlas — Distribution Markets](https://www.pmatlas.xyz/concepts/distribution-markets)

---

## Definition

Markets that trade on full probability distributions rather than single binary yes/no outcomes. Traders express a *shape* of belief (mean, variance, skew, multi-modal structure) and are paid by how close the realized outcome falls to the shape they staked on. Paradigm's Dave White paper (Dec 2024) formalized the modern version; Dekant is the first onchain production implementation.

## Key Insights

- Binary yes/no PMs are incomplete: they flatten nuanced beliefs into coin flips and pay the same whether you were barely right or sharply right. Distribution-native markets reward precision · pay more for being closer to the actual outcome. 130x volume growth from early 2024 to late 2025 = category's credibility moment (Tide manifesto, "Make Precision Pay").
- "What if we're capturing the wrong signal?" · binary markets flatten complex beliefs, losing precision that separates superforecasters from average predictors. 2024 French trader whale ($30M moving election odds) and Vanderbilt study (PredictIt 93% vs 67% on high-volume platforms) · more liquidity ≠ better signal (Jo).
- Information Vectors thesis: binary event contracts fragment liquidity and flatten beliefs into 1-bit structures. *Achieving 8-bit resolution requires 256 separate markets.* Proposes treating beliefs as vectors over probability distributions on a shared liquidity surface. Traders express full distributions; rewarded for *variance compression* (reducing entropy), not just final outcome correctness (functionSPACE).

## Notable Quotes

> "Achieving 8-bit resolution requires 256 separate markets."  
> — *functionSPACE*

> "Don't pick a side. Draw a curve."  
> — *paraphrased thesis across all three sources*

> "Pay more for being closer to the actual outcome."  
> — *Tide*

## Where It Matters

This is the entire premise of Dekant. The three articles all converge on the same diagnosis (binary contracts under-resolve belief, fragment liquidity, and underpay precision) and propose essentially the same solution architecture (distribution-native / vector-of-beliefs / continuous-outcome markets). The volume growth cited (130x) plus the academic underconfidence-in-political-markets finding (prices compressing to 50%) form the empirical case that this category has tailwinds. For builders, the open implementation questions are: (1) what's the AMM invariant that supports a distribution surface? (L2-norm CFAMM, partition-of-unity smooth kernels), (2) what's the UX so retail can express a curve without a math degree? (sliders, drag-curve, presets), (3) how do you settle when realized outcome is continuous? (binned payouts, kernel-weighted partition-of-unity).

## Related Concepts

- **Information aggregation** · distribution markets aggregate at higher dimension
- **Price discovery** · argued to be higher-bandwidth in continuous space
- **Forecasting accuracy / Calibration** · the metric distribution markets claim to improve
- **Wisdom of crowds** · distribution markets aggregate richer crowd signals
- **Liquidity provision** · single shared liquidity surface across the whole distribution
- **Superforecasting** · distribution markets reward the precision skill that separates superforecasters from amateurs
- **LMSR / market scoring rules** · the mechanism family naturally extended to multi-outcome

## Sources

- [What If We're Capturing the Wrong Signal?](https://x.com/TideMarkets/article/2016912289941827801) — Jo · Jan 29, 2026 [FULL_READ]
- [Information Vectors: An Intro to Composable Beliefs](https://x.com/functionspaceHQ/article/2014809461647671570) — functionSPACE · Jan 24, 2026 [FULL_READ]
- [Manifesto: Make Precision Pay](https://x.com/TideMarkets/article/2008508916616098086) — Tide · Jan 6, 2026 [FULL_READ]

