# Calibration

**Category:** Information Theory  
**Source:** [PM Atlas — Calibration](https://www.pmatlas.xyz/concepts/calibration)

---

## Definition

When stated probabilities match empirical frequencies · events given 70% odds happen 70% of the time. The first-order quality metric for any probabilistic forecaster (human, model, or market), and the basis for Brier-score evaluation.

## Key Insights

- Kalshi sports monthly volume grew 80x to $14.4B in March 2026. NCAA March Madness = $3.3B notional. In-game prices correlate 0.99+ with FanDuel, but Kalshi taker fees (up to 3.5% at midpoint) and thin in-game liquidity (76% depth decline vs pre-game) limit institutional execution (Kunal Doshi).
- Polymarket headline Brier of 0.047 masks category-specific failures: sports markets 0.325 (worse than coin flip); 99% volume in last hours. PMs only "work" on ~2% of contracts. CNN/WSJ broadcasting illiquid odds = whale trades laundered through newsrooms (Mandloi).
- Raw PM probabilities must be adjusted for contract wording mismatches and economic relevance before use in equity analysis. Polymarket's 51% tariff refund probability → 35% effective for Logitech margin impact; 29.5% FDA approval → $5.4B EV uplift for Eli Lilly (Smallwood).
- Noisy traders are NOT dumb money. Synthesizes Snowberg/Wolfers/Zitzewitz, INSEAD's BIN model, Wharton's cognitive search framework. Noisy traders *fund the probability space* rather than serve as exit liquidity. Binary CLOBs vs continuous probability markets decompose and harness noise differently (functionSPACE).
- Tetlock's Superforecasting → Polymarket: Brier scores and calibration are the operational metrics. Foxes vs hedgehogs (Ahnianchykau).
- "Decomposing Crowd Wisdom" · Bayesian hierarchical model fit to 292M trades across 327k binary contracts on Kalshi/Polymarket. Decomposes calibration into four components · universal horizon effect, domain-specific biases, domain-by-horizon interactions, trade-size scale effect · that together explain 87.3% of calibration variance on Kalshi. Persistent underconfidence in political markets where prices are "chronically compressed toward 50%" (generalises across both exchanges). Large trades amplify this on Kalshi but not Polymarket → platform-specific microstructure. Bayesian model achieves 96.3% posterior predictive coverage (Nam Anh Le).
- "What if we're capturing the wrong signal?" · binary markets flatten complex beliefs; Vanderbilt: PredictIt 93% accuracy vs 67% on high-volume platforms; more liquidity ≠ better signal (Jo).
- ForecastBench: GPT-4.5 Brier 0.101 vs superforecasters 0.081. LLMs improving ~0.016 Brier/year, parity by late 2026. Some models *game* the benchmark by copying PM prices rather than reasoning independently (Forecasting Research Institute).
- Nam Anh Le arXiv full read (abstract): on Kalshi, calibration decomposes into "four components (a universal horizon effect, domain-specific biases, domain-by-horizon interactions and a trade-size scale effect) that together explain 87.3% of calibration variance." The trade-size scale effect: large trades on Kalshi politics have Δ = 0.53 (95% CI [0.29, 0.75]) on underconfidence · but Polymarket only shows Δ = 0.11 (95% CI [-0.15, 0.39]), so the effect is platform-specific. Bayesian hierarchical model confirms with "96.3% posterior predictive coverage." Conclusion: "Consumers of prediction market prices who treat them as face-value probabilities will systematically misinterpret them, and the direction of misinterpretation depends on what is being predicted, when and by whom."
- Smallwood "Can Polymarket Make You a Better Equity Analyst?" (FULL_READ) provides two case-study calibrations: (1) Logitech tariff exposure · raw Polymarket tariff-refund probability 51% → adjusted to 35% because the contract resolves on a legal refund event while Logitech GM depends on broader economic relief. (2) Retatrutide FDA approval · raw 29.5% → 22.1% effective because the contract resolves on *any* FDA approval (could be a narrow approval that doesn't change the equity thesis). The translation discipline ("mapping haircut") is what makes PM prices useful in actual analyst workflows. Final figure: 0.61% of Lilly market cap as the probability-weighted EV uplift from early 2026 approval.
- Mandloi full read: Brier.fyi platform analyzed 84,000+ Polymarket/Kalshi/Manifold/Metaculus questions. Cross-platform sports Brier 0.325 explicitly worse than coin flip (0.25). Polymarket's BTC-$100K market resolved Yes but had Brier 0.4909 because it was confidently wrong for months. Kamala Dem nomination market: Brier 0.9098 despite resolving Yes. Polymarket 2024 presidential market: $3.6B volume, 63,000 unique monthly traders. Vanderbilt Bayesian study comparing Polymarket to swing-state polls: Polymarket more accurate in every single swing state.

## Notable Quotes

> "Polymarket's headline Brier score of 0.047 masks category-specific failures like sports markets scoring 0.325 (worse than a coin flip)."  
> — *Mandloi*

> "Persistent underconfidence in political markets, where prices are chronically compressed toward 50%."  
> — *Nam Anh Le*

> "Some models game the benchmark by copying prediction market prices rather than reasoning independently."  
> — *Forecasting Research Institute*

## Where It Matters

Calibration is what every "Brier" headline reduces to, but the 2026 work is decomposing the headline number into more useful diagnostics: domain-specific bias, horizon effects, trade-size effects, platform microstructure. Builders should publish *decomposed* calibration metrics, not aggregate Brier scores, or they'll be accused of laundering bad sports calibration through good political calibration. Underconfidence in political markets (prices compress toward 50%) is a specific actionable mispricing · a curve-drawing market like Dekant could potentially fix this by giving traders a richer way to express bimodal or skewed beliefs than "buy YES at 52¢."

## Related Concepts

- **Brier score** · the metric
- **Forecasting accuracy** · the umbrella claim
- **Wisdom of crowds** · the folk theory calibration tests
- **Superforecasting** · the human-skill benchmark
- **Noise decomposition** · companion technique for separating signal from microstructure
- **Longshot bias / Yes bias** · known calibration failures
- **Adverse selection** · driver of platform-level calibration differences
- **Distribution markets** · argued as a higher-resolution calibration surface

## Sources

- [From Betting to Trading: How Kalshi Is Reshaping Sports Markets](https://x.com/Kunallegendd/status/2044780671839952949) — Kunal Doshi · Apr 16, 2026 [FULL_READ]
- [Polymarket Is Not a Truth Machine](https://www.thetokendispatch.com/p/polymarket-is-not-a-truth-machine) — Mandloi · Apr 11, 2026 [FULL_READ]
- [Can Polymarket Make You a Better Equity Analyst?](https://theagenticanalyst.substack.com/p/can-polymarket-make-you-a-better) — Smallwood · Apr 9, 2026 [FULL_READ]
- [Noisy Traders Are Not Dumb Money](https://x.com/functionspaceHQ/article/2032397043579462028) — functionSPACE · Mar 13, 2026 [FULL_READ]
- [The Book That Predicted Polymarket](https://x.com/mikita_crypto/article/2029939210350645729) — Ahnianchykau · Mar 6, 2026 [FULL_READ]
- [Decomposing Crowd Wisdom: Domain-Specific Calibration Dynamics in Prediction Markets](https://arxiv.org/abs/2602.19520) — Nam Anh Le · Feb 23, 2026 [FULL_READ · abstract]
- [What If We're Capturing the Wrong Signal?](https://x.com/TideMarkets/article/2016912289941827801) — Jo · Jan 29, 2026 [FULL_READ]
- [How Well Can Large Language Models Predict the Future?](https://forecastingresearch.substack.com/p/ai-llm-forecasting-model-forecastbench-benchmark) — Forecasting Research Institute · Oct 8, 2025 [FULL_READ]

