# Forecasting Accuracy

**Category:** Information Theory  
**Source:** [PM Atlas — Forecasting Accuracy](https://www.pmatlas.xyz/concepts/forecasting-accuracy)

---

## Definition

Measuring how well predicted probabilities match actual outcome frequencies over many events. Operationalized in PMs through Brier scores, calibration curves, and head-to-head comparisons against ARIMA, FluSight ensembles, polls, expert panels, and LLMs.

## Key Insights

- Prediction markets *underperform* simple baselines on infectious disease forecasting: Polymarket was "dominated by the FluSight ensemble" for flu, and "outperformed by simple statistical baselines" for measles. Two diagnosed failure modes: probability mass placed on impossible outcomes (e.g., decreasing values in cumulative forecasts) and low trading volume. The best ensemble combination "puts zero weight on the markets" (Dudley & Magdaleno, May 2026).
- "Polymarket is not a truth machine": headline Brier score of 0.047 masks category-specific failures · sports markets score 0.325 (worse than a coin flip). 99% of volume concentrates in the final hours before resolution. PMs only "work" on ~2% of listed contracts (binary, high-profile, short-term, millions at stake). CNN/WSJ broadcasting illiquid market odds as authoritative signal launders whale trades through credible newsrooms (Vaidik Mandloi).
- ~13,500 Polymarket contracts analyzed: >80% of volume goes to sports/crypto/elections. Accuracy on "useful" markets hasn't improved since early 2025. AI chatbots may supersede PMs as the primary forecasting interface, leaving markets to serve an epistemic role as common-knowledge infrastructure (Dan Schwarz, Asterisk).
- ForecastBench: best LLM (GPT-4.5) achieves Brier 0.101 vs superforecasters' 0.081, with LLMs improving ~0.016 Brier/year, projecting parity by late 2026. Some models *game* the benchmark by copying PM prices rather than reasoning (Forecasting Research Institute).
- LLM-as-updater framing: distinguish cold prediction from updating; AI tools deployed alongside humans, not replacing them (OddChain).
- Tetlock's Superforecasting framework operationalized at scale by Polymarket. Foxes vs hedgehogs, Good Judgment Project, Brier scores, calibration · the book "predicted Polymarket" (Mikita Ahnianchykau).
- 2024 election platform-level accuracy: PredictIt 93%, Kalshi 78%, Polymarket 67% (Clinton & Huang via Sides). Cross-platform divergence near Election Day was significant; suggests "more liquidity" doesn't mean "better signal."
- Minimum viable liquidity finding: trading volume explains <1% of variance in forecast accuracy on 149 Kalshi CPI markets (2021–26). MVL = Cost of Expertise / Price Gap (Adhi Rajaprabhakaran).
- Political prediction market quality (6-month empirical analysis): only 1.3% of political markets are liquid enough to be manipulation-resistant; bid-ask spreads exceed 20% on most contracts; only 53% of resolved US elections appeared on both Kalshi and Polymarket. Four-part blueprint: stock relevant questions, cross-subsidize political liquidity from sports profits, deploy AI MMs where human interest insufficient, standardize contract definitions (Hall & Paschal).
- Five-point diagnostic for distinguishing gambling from systematic trading. Three trader archetypes by profitability. Polymarket's CLOB creates renewable structural arbitrage by design. Kelly position sizing, adverse-selection measurement via fill quality, probability term structure (Roan).
- AI underperforms humans because edge is *embodied/local* · five of six AI models in Prediction Arena underwater after three weeks (Mehmet Avci).
- "Nielsen moment": coordination value > accuracy. Polymarket/Golden Globes, Polymarket/WSJ, Kalshi/CNN · once PMs become shared reference points, displacement is hard regardless of methodological superiority (Avci).
- "Make Precision Pay" · binary markets flatten beliefs into coin flips and pay the same whether you were barely right or sharply right. Distribution-native markets reward precision (Tide).
- "What if we're capturing the wrong signal?" · binary markets flatten complex beliefs; Vanderbilt study: PredictIt 93% accuracy vs 67% on high-volume platforms · more liquidity ≠ better signal (Jo).
- Conditional PMs fail to translate probability assessments into meaningful policy guidance despite being their central theoretical appeal · "prediction markets are mediocre" (LessWrong).
- 2024 election: Polymarket priced in Biden's withdrawal probability while polls measured only head-to-head support (fil) · illustrates the speed advantage on multi-branch questions.
- PMs aren't unpopular due to regulation but due to demand-side issues · they need savers, gamblers, or sharps; PMs attract none (zero-sum so no savers, long resolution so no gamblers, too small so no sharps) (Whitaker & Mazlish).
- Combinatorial PMs (Powell, Hanson, Laskey, Twardy 2013) · extending to conditional events and Boolean combos improves accuracy by eliciting joint distributions rather than only marginals.
- Mandloi specifics from full read: Polymarket BTC-to-$100K market had Brier 0.4909 (worse than coin flip) despite *correctly resolving Yes* · was confidently wrong for months. Kamala Harris Democratic nomination market: Brier 0.9098 · "so bad that it is hard to overstate." "A single correct call tells you almost nothing." Brier.fyi analyzed 84,000+ questions; Polymarket science/economics A, politics B+, culture/tech worse, sports D (0.325). 80% of all volume in elections + sports. Median Polymarket question resolves in 4 days; average 19 days.
- Schwarz/Asterisk specifics: 5 categories of value (risk monitoring, news interpretation, policy outcomes, accountability, novel info). Only risk monitoring has working supply+demand balance (2,821 markets, $3.8B volume, median $82K). 90% of Kalshi volume is sports. 85% of "news interpretation" volume is just US Fed rate markets. 66% of accountability market volume is Epstein speculation. 196 tariff-policy markets ($144M) are the rare bright spot for genuinely novel info. Markets >90 days do show volume → accuracy correlation; markets <90 days don't.
- Schwarz on AI substitution: "Try searching Polymarket for probabilities, versus asking Claude about it. I wager you'll prefer Claude's take, even if it is less accurate." Claims AI chatbots will become the primary distribution layer for forecasts, with PMs serving as common-knowledge infrastructure.
- ForecastBench full-read additions: GPT-4 (Mar 2023) Brier 0.131 → GPT-4.5 (Feb 2025) Brier 0.101 (one-generation gap to superforecasters). The benchmark uses a "difficulty-adjusted Brier score" to compare across non-overlapping question sets. Most LLMs *copy market prices when given them* · GPT-4.5 outputs correlated 0.994 with market forecasts, exact matches on 26/122 questions. Baseline (no market access) leaderboard shows true LLM improvement at 0.036 Brier/year · faster than Tournament rate.
- Hall/Paschal "Building the Truth Machine" (FULL_READ): 98.7% of political markets are "ghost towns" with wide spreads and almost no counterparty. But "less liquid markets are not necessarily less accurate. Sometimes markets stay illiquid precisely because they're already accurate, and there's no incentive for new money to enter."
- John Sides / Vanderbilt (Clinton & Huang) full-read: 2,500+ political prediction markets across IEM, Kalshi, PredictIt and Polymarket, with $2B+ transacted over the final 5 weeks of 2024. PredictIt 93% > Kalshi 78% > Polymarket 67% on accuracy. "Prices for identical contracts diverged across exchanges, daily price changes were weakly correlated or negatively autocorrelated, and arbitrage opportunities peaked in the final two weeks before Election Day." The conclusion challenges the assumption that PMs "necessarily efficiently and accurately aggregate information about political outcomes."
- LessWrong "Prediction Markets Are Mediocre" (FULL_READ): the conditional-market case for policy guidance fails because the market gives a number that comes too late and too vaguely to change decisions. 56% chance of tariffs in Trump's first year was technically correct, but useless on election day. Adding more liquidity helps marginally; adding a second-order market about the first-order market doesn't help at all because "both of them are trying to capture the same core fact about reality."

## Notable Quotes

> "Polymarket's headline Brier score of 0.047 masks category-specific failures like sports markets scoring 0.325 (worse than a coin flip), and 99% of volume concentrates in the final hours before resolution."  
> — *Vaidik Mandloi*

> "More liquidity doesn't mean better signal."  
> — *Jo, "What If We're Capturing the Wrong Signal?"*

> "Trading volume explains less than 1% of variance in forecast accuracy."  
> — *Adhi Rajaprabhakaran*

## Where It Matters

Forecasting accuracy is the *advertised* value prop of every PM platform, but the 2026 empirical research catalog tells a much more conditional story. PMs are accurate on a narrow band of contracts: binary, high-profile, short-term, with millions in volume. Outside that band, they often lose to ARIMA, FluSight, or expert panels. For builders this implies: don't sell "PMs are smarter than experts" · sell "PMs are the shared coordination layer for a narrow set of high-stakes binary questions, plus a UI for the broader info-finance vision." For Dekant: distribution markets attack the binary flattening problem head-on; if Brier on binary sports is 0.325, a continuous outcome representation paid by distance-from-truth could plausibly tighten that.

## Related Concepts

- **Calibration** · the diagnostic
- **Brier score** · the metric
- **Wisdom of crowds** · the folk theory; partly debunked by 3%-of-accounts finding
- **Superforecasting** · Tetlock's framework that the markets operationalize
- **Minimum viable liquidity** · the counterintuitive limit
- **Distribution markets** · proposed higher-resolution form
- **AI agents** · emerging competitor / participant
- **Endogeneity / Legibility** · reasons PM prices can diverge from "truth"

## Sources

- [Prediction Markets Underperform Simple Baselines For Infectious Disease Forecasting](https://arxiv.org/abs/2605.11220) — Dudley & Magdaleno · May 11, 2026 [FULL_READ · abstract]
- [Polymarket Is Not a Truth Machine](https://www.thetokendispatch.com/p/polymarket-is-not-a-truth-machine) — Vaidik Mandloi · Apr 11, 2026 [FULL_READ]
- [Are Prediction Markets Good for Anything?](https://asteriskmag.com/issues/14/are-prediction-markets-good-for-anything) — Dan Schwarz · Apr 1, 2026 [FULL_READ]
- [Can LLMs Beat the Market?](https://www.oddchain.com/p/can-llms-beat-the-market) — OddChain · Mar 19, 2026 [FULL_READ]
- [The Book That Predicted Polymarket](https://x.com/mikita_crypto/article/2029939210350645729) — Ahnianchykau · Mar 6, 2026 [FULL_READ]
- [Ahead of the Headlines: Prediction Markets and the Collective Mind](https://jprz1321.substack.com/p/ahead-of-the-headlines-prediction) — JP · Feb 25, 2026 [FULL_READ]
- [Minimum Viable Liquidity](https://fiftycentdollars.substack.com/p/minimum-viable-liquidity) — Adhi Rajaprabhakaran · Feb 24, 2026 [FULL_READ]
- [Building the Truth Machine](https://freesystems.substack.com/p/building-the-truth-machine) — Andy Hall, Elliot Paschal · Feb 13, 2026 [FULL_READ]
- [Why Prediction Markets Aren't Gambling? (The Math)](https://x.com/RohOnChain/article/2020565633453412751) — Roan · Feb 9, 2026 [FULL_READ]
- [Is AI Any Good at Predicting?](https://reachavci.substack.com/p/is-ai-any-good-at-predicting) — Avci · Feb 2, 2026 [FULL_READ]
- [The Nielsen Moment for Prediction Markets](https://reachavci.substack.com/p/the-nielsen-moment-for-prediction) — Avci · Jan 12, 2026 [FULL_READ]
- [Manifesto: Make Precision Pay](https://x.com/TideMarkets/article/2008508916616098086) — Tide · Jan 6, 2026 [FULL_READ]
- [The Perils of Election Prediction Markets](https://goodauthority.org/news/the-perils-of-election-prediction-markets/) — John Sides · Dec 18, 2025 [FULL_READ]
- [How Well Can Large Language Models Predict the Future?](https://forecastingresearch.substack.com/p/ai-llm-forecasting-model-forecastbench-benchmark) — Forecasting Research Institute · Oct 8, 2025 [FULL_READ]
- [Prediction Markets Are Mediocre](https://www.lesswrong.com/posts/opzJP7QHgyHzMth5o/prediction-markets-are-mediocre) — LessWrong · Apr 5, 2025 [FULL_READ]
- [The Art of Forecasting](https://paragraph.com/@filarm/the-art-of-forecasting) — fil · Sep 30, 2024 [FULL_READ]
- [Why Prediction Markets Aren't Popular](https://worksinprogress.co/issue/why-prediction-markets-arent-popular/) — Whitaker & Mazlish · May 17, 2024 [FULL_READ]
- [Combinatorial Prediction Markets: An Experimental Study](https://link.springer.com/chapter/10.1007/978-3-642-40381-1_22) — Powell, Hanson, Laskey, Twardy · Sep 16, 2013 [FULL_READ · Springer abstract; full chapter paywalled]

