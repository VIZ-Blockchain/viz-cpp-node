# Superforecasting

**Category:** Information Theory  
**Source:** [PM Atlas — Superforecasting](https://www.pmatlas.xyz/concepts/superforecasting)

---

## Definition

Techniques and traits of forecasters who consistently outperform base rates and prediction markets. Coined by Philip Tetlock in the Good Judgment Project. Operationalized via Brier scores, calibration, foxes vs hedgehogs, and trainable habits (granular probability vocabularies, Bayesian updating, viewpoint diversity).

## Key Insights

- Tetlock's *Superforecasting* → Polymarket. Direct line from the book's thesis (forecasting skill is measurable, trainable, outperforms expert punditry) to Polymarket's success in the 2024 US election. Key concepts: foxes vs hedgehogs, Good Judgment Project, Brier scores, calibration. Polymarket effectively *operationalized Tetlock's framework at scale* by converting crowd forecasting into a liquid financial market (Ahnianchykau).
- "What if we're capturing the wrong signal?" · binary markets flatten the precision that separates superforecasters from average predictors. Vanderbilt study: PredictIt 93% accuracy vs 67% on high-volume platforms · more liquidity ≠ better signal. The argument: binary mechanism doesn't give superforecasters room to express their edge (Jo).
- ForecastBench: GPT-4.5 Brier 0.101 vs superforecasters 0.081. LLMs improving ~0.016 Brier/year → projected parity by late 2026. Some models *game* the benchmark by copying PM prices rather than reasoning independently (Forecasting Research Institute).
- ForecastBench full-read additional details: three interpretations of the 0.02 Brier gap · (1) "GPT-4.5's Brier score is 25% higher (i.e., worse) than superforecasters'," (2) superforecasters capture 68% of the possible improvement from always-50% baseline (Brier 0.25), GPT-4.5 captures 60% · 8pp short, (3) the 0.02 gap is "smaller than the 0.03-point improvement from GPT-4 to GPT-4.5, suggesting the distance to superforecasters is less than one major model generation." A year ago, the median public forecaster ranked #2; now it sits at #22 · LLMs have decisively passed non-expert humans.
- ForecastBench on the LLM "copy-paste" tactic: "GPT-4.5's predictions have a correlation of 0.994 with provided market forecasts, and it submitted exact market values for 26 out of 122 questions, with a median deviation of just 0.5 percentage points. Since prediction markets are highly accurate, this tactic works but reveals little about underlying capabilities." The Baseline leaderboard (no market access) shows true LLM improvement of 0.036 Brier points/year · faster than the Tournament leaderboard (which allows market access) at 0.015/year.
- ForecastBench projected parity dates by question type: dataset questions June 2026 (95% CI Nov 2025 – Apr 2027), market questions March 2026 (95% CI Mar 2025 – Dec 2028), all-questions November 2026 (95% CI Dec 2025 – Jan 2028). Submissions are now open to the public.

## Notable Quotes

> "Polymarket effectively operationalized Tetlock's framework at scale."  
> — *Ahnianchykau*

> "Binary markets flatten complex beliefs into coin flips, losing the precision that separates superforecasters from average predictors."  
> — *Jo*

> "Some models game the benchmark by copying prediction market prices rather than reasoning independently."  
> — *Forecasting Research Institute*

## Where It Matters

Superforecasting is the human-skill substrate that PMs convert into a price. If superforecasters are 0.02 Brier-points better than the next best LLM (and ~0.04 better than an average forecaster), the question for PM builders is: can your market mechanism extract that 0.02 edge? Binary markets compress it; distribution markets (per Jo, functionSPACE, Tide) plausibly preserve more of it because superforecasters typically express belief as a *narrow distribution*, not as "55% YES." For Dekant, this is part of the user-targeting story: superforecasters are the natural early adopters of a curve-drawing market because the surface finally matches the shape of their belief.

## Related Concepts

- **Calibration / Brier score** · operational metrics
- **Forecasting accuracy** · the umbrella claim
- **Wisdom of crowds** · superforecasters often *outperform* crowds
- **Distribution markets** · argued to preserve superforecaster edge that binary markets compress
- **AI agents** · the emerging competitor benchmark
- **Information aggregation** · superforecasters as the high-quality input

## Sources

- [The Book That Predicted Polymarket](https://x.com/mikita_crypto/article/2029939210350645729) — Ahnianchykau · Mar 6, 2026 [FULL_READ]
- [What If We're Capturing the Wrong Signal?](https://x.com/TideMarkets/article/2016912289941827801) — Jo · Jan 29, 2026 [FULL_READ]
- [How Well Can Large Language Models Predict the Future?](https://forecastingresearch.substack.com/p/ai-llm-forecasting-model-forecastbench-benchmark) — Forecasting Research Institute · Oct 8, 2025 [FULL_READ]

