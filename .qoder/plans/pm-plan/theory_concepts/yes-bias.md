# Yes Bias

**Category:** Information Theory  
**Source:** [PM Atlas — Yes Bias](https://www.pmatlas.xyz/concepts/yes-bias)

---

## Definition

The systematic tendency for YES shares in prediction markets to be overpriced relative to true probabilities, potentially explaining what was previously attributed to favorite-longshot bias. A 2026 reframe of the canonical PM mispricing.

## Key Insights

- "The yes bias might not exist" · 7,292 resolved Polymarket markets and 28,793 on-chain trades. Finding: traders buy whichever token is *cheaper*, not whichever is labeled YES. The apparent bias is a compound effect: longshot preference channeled through Polymarket's "Will X happen?" question framing, which systematically assigns the longshot to the YES token. Implication: yes-bias and longshot-bias are *measurably the same effect viewed from different sides* (functionSPACE).
- Tick-level order flow across Polymarket and Kalshi: classic favorite-longshot bias may be a *statistical artifact* masking a pervasive yes-bias driven by temporal volatility and incomplete contract-lifecycle controls. Whales are NOT the sharpest participants · heavily capitalized traders systematically bleed EV to small-order traders, likely from ideological conviction rather than informational edge (Deleep, Lee, Bai, Suresh, Dhawan · SSRN).
- "Microstructure of Wealth Transfer": 72.1M Kalshi trades ($18.26B volume). Systematic transfer from takers to makers averaging 1.12% excess on each side. Takers disproportionately buy YES longshots, accepting returns 64pp lower than equivalent NO positions. Transfer emerged only after Kalshi's Oct 2024 legal victory attracted professional algorithmic MMs. Market efficiency varies sharply by category: finance near-efficient, entertainment/media >7pp gaps (Jonathan Becker).

## Notable Quotes

> "Traders buy whichever token is cheaper, not whichever is labeled YES."  
> — *functionSPACE*

> "Takers disproportionately buy YES longshots, accepting returns 64 percentage points lower than equivalent NO positions."  
> — *Jonathan Becker*

> "Whales are not the sharpest participants: heavily capitalized traders systematically bleed expected value to small-order traders."  
> — *Deleep et al.*

## Where It Matters

Yes-bias is a *measurement* finding more than a behavioral one · it tells researchers that you can't separate "people overpay for YES" from "people overpay for cheap longshots" without controlling for which side the question framing assigns to the longshot. For platforms, the practical fix is contract-wording neutrality (e.g., balanced questions like "Will Trump win OR not win?" rather than "Will Trump win?"). For traders, the renewable arbitrage is *sell YES longshots* · especially on Kalshi entertainment/media. For Dekant, yes-bias largely doesn't apply because the curve-drawing primitive has no asymmetric YES/NO labeling · but the underlying behavioral pattern (overpaying for tail outcomes) will reappear as overweighting the tails of the drawn distribution, which the L2-norm CFAMM payout function will price accordingly.

## Related Concepts

- **Longshot bias** · the confound; possibly identical effect under a different lens
- **Calibration** · yes-bias is one input into calibration error
- **Retail flow** · the source of demand for overpriced YES longshots
- **Adverse selection / Toxic flow** · how MMs profit from yes-bias takers
- **Market making** · fading YES longshots is the explicit strategy

## Sources

- [The Yes Bias Might Not Exist](https://x.com/functionspaceHQ/article/2037374271278989409) — functionSPACE · Mar 27, 2026 [FULL_READ]
- [How Wise Is the Crowd? Bias and Edge in Prediction Markets](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6322678) — Deleep, Lee, Bai, Suresh, Dhawan · Feb 28, 2026 [FULL_READ]
- [The Microstructure of Wealth Transfer in Prediction Markets](https://www.jbecker.dev/research/prediction-market-microstructure) — Jonathan Becker · Jan 18, 2026 [FULL_READ]

