# Longshot Bias

**Category:** Information Theory  
**Source:** [PM Atlas — Longshot Bias](https://www.pmatlas.xyz/concepts/longshot-bias)

---

## Definition

The tendency for low-probability outcomes to be systematically overpriced in prediction markets · bettors pay too much for tail outcomes (relative to true probability), so longshots have negative expected return. Originally documented in horse racing; appears in modern PMs too, though 2026 research argues a confounding "yes bias" may explain much of what's been attributed to favorite-longshot.

## Key Insights

- Three structural reasons PM prices diverge from true probabilities even with rational participants: (1) favorite-longshot bias from Kelly betting, (2) risk-premium distortion from market correlation, (3) risk-neutral forward pricing in long-dated contracts (Lihong).
- Market making in PMs is structurally broken. Cross-venue fragmentation (same contract at 58–67¢ on different platforms). January 2026 Polymarket XRP exploit paid $231K on thin weekend liquidity. Kalshi's structural longshot bias persists. Evidence from 150M Polymarket trades: top 5% skilled traders earned $228M; spread capture barely moves P&L. Passive LPs behave more like *underwriters of terminal risk* than classical market makers (Lotus).
- The yes bias might not exist as a standalone effect. Analysis of 7,292 resolved Polymarket markets and 28,793 on-chain trades: traders buy whichever token is *cheaper*, not whichever is labeled YES. Apparent bias = compound effect of longshot preference channeled through Polymarket's "Will X happen?" framing, which systematically assigns the longshot to the YES token (functionSPACE).
- Tick-level order flow across Polymarket/Kalshi: classic favorite-longshot bias may be a *statistical artifact* masking a pervasive "yes bias" driven by temporal volatility and incomplete contract-lifecycle controls. Whales are NOT the sharpest participants · heavily capitalized traders systematically bleed EV to small-order traders, likely from ideological conviction rather than informational edge (Deleep, Lee, Bai, Suresh, Dhawan · SSRN).
- "Decomposing Crowd Wisdom" Bayesian model on 292M trades across 327K Kalshi/Polymarket contracts: persistent underconfidence in political markets where prices are "chronically compressed toward 50%." Trade-size scale effect Δ = 0.53 [0.29, 0.75] on Kalshi politics doesn't replicate on Polymarket (Δ = 0.11 [-0.15, 0.39]) · platform-specific microstructure (Nam Anh Le).
- 72M Kalshi trades · three biases: longshot bias (5¢ contracts win only 4.18% of the time · implied probability 5% vs realized 4.18%, ~16% relative overpricing), maker-taker asymmetry, YES/NO asymmetry. Finance most efficient (0.17% spread); crypto least (2.69%) (Ranger Global).
- "Microstructure of Wealth Transfer": 72.1M Kalshi trades ($18.26B volume). Systematic wealth transfer from takers to makers averaging 1.12% excess on each side. Takers disproportionately buy YES longshots, accepting returns 64pp lower than equivalent NO positions. Transfer emerged only after Kalshi's Oct 2024 legal victory attracted professional algorithmic MMs. Market efficiency varies sharply by category: finance near-efficient; entertainment/media gaps >7pp (Jonathan Becker).
- "Next Level" · DeFi primitive for borrowing against PM positions. Collateralization solves capital lock-up in long-dated markets; could correct persistent mispricings like longshot mispricing; opens composability with broader financial ecosystem. Flags liquidation risks unique to binary outcomes (keshav).
- Becker microstructure paper (FULL_READ, partial · methodology + headline numbers): on Kalshi, traders accept expected values as low as 43 cents on the dollar for longshot contracts, "worse than a Las Vegas slot machine" (93¢ on the dollar). 72.1M trades, $18.26B volume. Mispricing metric δ_S = mean(outcomes) − mean(implied probabilities). Sports = 72% of Kalshi notional volume, politics 13%, crypto 5%. Three contributions: (1) confirms longshot bias and quantifies by price level, (2) decomposes returns by market role (taker vs maker), (3) identifies YES/NO asymmetry where takers disproportionately favor YES at longshot prices.

## Notable Quotes

> "5c contracts win only 4.18% of the time."  
> — *Ranger Global*

> "Takers disproportionately buy YES longshots, accepting returns 64 percentage points lower than equivalent NO positions."  
> — *Jonathan Becker*

> "Whales are not the sharpest participants: heavily capitalized traders systematically bleed expected value to small-order traders."  
> — *Deleep et al.*

## Where It Matters

Longshot bias is the single most exploitable systematic mispricing in PMs · and the 2026 research suggests it's been *underestimated* because it's been entangled with YES/NO labeling effects. If a PM builder wants to claim "accurate prices," longshot bias must be measured per-platform and per-category. For aggressive traders, the bias is renewable arbitrage (sell longshots, especially YES longshots, on Kalshi entertainment/media categories). For Dekant, the design implication is more subtle: a distribution-market curve doesn't inherit YES/NO labeling at all, so the entire longshot-vs-favorite asymmetry has to be re-examined in continuous space · but the same retail behavior (overpaying for tail outcomes) will reappear as overweighting the tails of the drawn distribution.

## Related Concepts

- **Yes bias** · confound; possibly the *real* explanation
- **Calibration** · the diagnostic against which longshot bias is measured
- **Retail flow / Toxic flow** · who pays the longshot tax
- **Adverse selection** · what MMs are hedging against
- **Market making** · why fading longshots is the obvious LP strategy
- **Kelly criterion** · why rational participants can still produce longshot bias
- **Liquidity fragmentation** · driver of cross-platform longshot price gaps

## Sources

- [Market Probabilities Are NOT Real Probabilities](https://x.com/Lihong062/status/2051017075758686622) — Lihong · May 3, 2026 [FULL_READ]
- [Market Making In PMs Sucks](https://x.com/uselotusai/status/2046639095531614455) — Lotus · Apr 21, 2026 [FULL_READ]
- [The Yes Bias Might Not Exist](https://x.com/functionspaceHQ/article/2037374271278989409) — functionSPACE · Mar 27, 2026 [FULL_READ]
- [How Wise Is the Crowd? Bias and Edge in Prediction Markets](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6322678) — Deleep, Lee, Bai, Suresh, Dhawan · Feb 28, 2026 [FULL_READ]
- [Decomposing Crowd Wisdom: Domain-Specific Calibration Dynamics in Prediction Markets](https://arxiv.org/abs/2602.19520) — Nam Anh Le · Feb 23, 2026 [FULL_READ · abstract]
- [Prediction Market Biases Revealed in 72 Million Trades](https://x.com/Ranger_Global/article/2016867581743747447) — Ranger Global · Jan 29, 2026 [FULL_READ]
- [The Microstructure of Wealth Transfer in Prediction Markets](https://www.jbecker.dev/research/prediction-market-microstructure) — Jonathan Becker · Jan 18, 2026 [FULL_READ]
- [Prediction Markets: The Next Level](https://x.com/0xkeshav_/article/1970163909224169626) — keshav · Sep 23, 2025 [FULL_READ]

