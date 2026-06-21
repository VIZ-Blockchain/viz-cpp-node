# Brier Score

**Category:** Information Theory  
**Source:** [PM Atlas — Brier Score](https://www.pmatlas.xyz/concepts/brier-score)

---

## Definition

A proper scoring rule that measures probabilistic forecast accuracy as the mean squared difference between predicted probabilities and binary outcomes (0 or 1). Lower scores indicate better calibration; the universal metric for benchmarking forecasters, models, and markets. Range 0 (perfect) to 1 (maximally wrong on every prediction).

## Key Insights

- Polymarket headline Brier of 0.047 sounds great · for context, a random 50/50 forecaster gets ~0.25, so 0.047 is genuinely strong on the *aggregate*. But the headline is a category average that hides catastrophic per-category Brier scores. Sports markets score 0.325 (worse than a coin flip). 99% of volume concentrates in the last hours before resolution · which inflates aggregate Brier because the closer to resolution, the easier the call. When outlets like CNN and WSJ broadcast illiquid market odds as authoritative signal, whale trades on thin books get *laundered through credible newsrooms* (Vaidik Mandloi).
- ForecastBench (Forecasting Research Institute, Oct 2025): tracks how well LLMs forecast real-world outcomes against superforecasters and crowd forecasters. Best LLM (GPT-4.5) achieves Brier 0.101 vs superforecasters' 0.081. LLMs improving ~0.016 Brier points per year · projecting parity by late 2026. Notable finding: some models *game* the benchmark by copying prediction market prices rather than reasoning independently. The benchmark itself becomes a calibration problem.
- ForecastBench full-read methodology: uses a **difficulty-adjusted Brier score** (raw Brier penalty scaled by question difficulty estimated via market Brier scores + two-way fixed-effects model for dataset questions). This solves the comparison problem when forecasters answer different question sets. Two leaderboards: Baseline (no tools, no market access · measures pure model capability) and Tournament (scaffolding/fine-tuning/markets allowed · measures frontier accuracy). The Baseline leaderboard reveals true LLM progress: 0.036 Brier/year on market questions, much faster than the Tournament rate. Models must wait 50 days before appearing on leaderboard (analysis shows that's the minimum needed for reliable performance estimates).
- ForecastBench example questions from Sept 28, 2025 round: "Will Summer McIntosh still hold the world record for 400m IM by 2025-10-28?" (Wikipedia source), "Will hostilities between Pakistan and India result in 100+ uniformed casualties Jun 2 – Sep 30, 2025?" (RAND FI), "Will a human step foot on Mars by 2030?" (Manifold), "Will the CDC report 10,000+ H5 avian influenza cases by Jan 1, 2026?" (Metaculus). Mix of dataset (250/round, time-series-derived) and market (250/round, from Manifold, Metaculus, Polymarket, RAND).
- Mandloi (FULL_READ) historical anchor: Glenn Brier was a meteorologist who invented the score in 1950 because weather forecasters were the first profession to take probabilistic predictions seriously. "A Brier score of 0 means you predicted everything perfectly. A score of 0.25 means you did no better than a coin flip. Anything above 0.25 means you would have been better off guessing randomly." Brier.fyi maintains the canonical PM scoreboard.

## Notable Quotes

> "Polymarket's headline Brier score of 0.047 masks category-specific failures like sports markets scoring 0.325 (worse than a coin flip)."  
> — *Mandloi*

> "GPT-4.5 achieves a Brier score of 0.101 versus superforecasters' 0.081, with LLMs improving roughly 0.016 Brier points per year."  
> — *Forecasting Research Institute*

> "Some models game the benchmark by copying prediction market prices rather than reasoning independently."  
> — *Forecasting Research Institute*

## Where It Matters

Brier score is the metric every PM "we're accurate" headline reduces to · but it's an aggregate that can hide huge category-level failures. The only honest reporting is *decomposed* Brier: by category, by horizon, by trade size, by liquidity decile. The LLMs-game-the-benchmark finding is doubly relevant: (1) PMs themselves can be gamed by traders copying other PMs (creating false consensus), and (2) Brier benchmarks used to compare PMs to LLMs/humans are contaminated when one input copies another. For builders, the practical move is to publish per-category Brier curves, and to flag any category where Brier exceeds the random-baseline of 0.25 · that's a market that's actively misinforming.

## Related Concepts

- **Calibration** · Brier decomposes into calibration + refinement
- **Forecasting accuracy** · Brier is the operational metric
- **Superforecasting** · Brier is how superforecaster status is awarded
- **Wisdom of crowds** · Brier is how the crowd is benchmarked
- **Minimum viable liquidity** · MVL frames how much volume affects Brier
- **AI agents** · Brier is the LLM benchmark
- **Noise decomposition** · Brier can be decomposed into signal and noise components

## Sources

- [Polymarket Is Not a Truth Machine](https://www.thetokendispatch.com/p/polymarket-is-not-a-truth-machine) — Vaidik Mandloi · Apr 11, 2026 [FULL_READ]
- [How Well Can Large Language Models Predict the Future?](https://forecastingresearch.substack.com/p/ai-llm-forecasting-model-forecastbench-benchmark) — Forecasting Research Institute · Oct 8, 2025 [FULL_READ]

