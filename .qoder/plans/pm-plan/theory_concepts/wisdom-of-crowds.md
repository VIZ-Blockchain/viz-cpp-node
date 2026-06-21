# Wisdom of Crowds

**Category:** Information Theory  
**Source:** [PM Atlas — Wisdom of Crowds](https://www.pmatlas.xyz/concepts/wisdom-of-crowds)

---

## Definition

The phenomenon where aggregated group estimates often outperform individual experts in forecasting accuracy. The Surowiecki/Galton folk theory PMs are marketed on · but empirical 2026 research significantly qualifies it: PM accuracy may be informed-minority-driven, not crowd-driven.

## Key Insights

- Prediction markets are the next stage in the history of expression (print → radio → social media → markets) · markets demand speakers bear consequence for being wrong. Hayek on price as coordination, Taleb on skin in the game, Hanson on futarchy. Staked speech out-trusts cheap talk in an AI-saturated information environment (Abhitej).
- Polymarket is not a truth machine: headline Brier 0.047 hides sports Brier 0.325 (worse than coin flip); 99% of volume in last hours. PMs work on ~2% of contracts (binary, high-profile, short-term, millions at stake). CNN/WSJ broadcasting illiquid odds = whale trades laundered through newsrooms (Mandloi).
- ~3% of accounts drive most price discovery. PM accuracy is *informed minority*, not wisdom of crowds. Remaining accounts contribute volume but minimal information · their losses fund the informed minority (Gomez-Cram, Guo, Jensen, Kung).
- Tiered framework for evaluating PM reliability: financialized economic indicators highest, speculative prop bets lowest. Three use cases: triangulating against polls, nowcasting delayed econ data, hedging event risk. Federal Reserve paper validates Kalshi data quality (Isar Bhattacharjee).
- Tetlock's Superforecasting → Polymarket: forecasting skill is measurable, trainable, outperforms expert punditry. Foxes vs hedgehogs, Good Judgment Project, Brier scores, calibration. Polymarket operationalized Tetlock's framework at scale (Ahnianchykau).
- Skin-in-the-game accountability produces more accurate signals than commentary-based analysis. Price movements anticipate news before official announcements. COVID-19 was a case where markets *underperformed* (JP).
- "What if we're capturing the wrong signal?" · binary markets flatten complex beliefs into coin flips, losing the precision that separates superforecasters from average predictors. 2024 French trader whale ($30M moving election odds); Vanderbilt: PredictIt 93% vs 67% on high-volume platforms (Jo).
- Hayek on information aggregation via price signals; thick vs thin markets; when markets work (elections, scientific replication) and when they struggle; the oracle problem; corporate forecasting and futarchy (a16z podcast).
- Wisdom of crowds theory → decentralized oracle mechanisms. PMs could systematize event probabilities to expand financial markets like derivatives historically did · but current implementations face liquidity fragmentation, oracle incentives, complexity (Luca Prosperi).
- 2024 Biden-Trump race: Polymarket priced in Biden's withdrawal probability while polls measured only head-to-head support (fil).
- Luca Prosperi "Crypto Prediction Markets" (FULL_READ) on the foundations: Galton's 1907 ox-weight experiment (800 estimates averaging 1,197 lbs vs actual 1,198 lbs) is the canonical wisdom-of-crowds anchor. Cites Wolfers & Zitzewitz's "Interpreting Prediction Market Prices as Probabilities" (model: equilibrium = mean of belief distribution under log utility and normal risk aversion). Modeling implications: under moderate risk aversion + symmetric beliefs you get longshot bias *as an artifact*; wealth-weighted aggregation can outperform unweighted means if accurate forecasters compound wealth.
- fil "The Art of Forecasting" (FULL_READ) frames the four channels · expert commentary, traditional polls, social media, prediction markets · on two axes: grassroots vs top-down, and dilettantism vs expertise. Polls ask "who will you vote for?" (intention); PMs ask "who do you think will win?" (expectation). The two diverge sharply when hedging is possible (a Trump-supporter who bets on Biden to hedge regret). Bloomberg added Polymarket odds to its terminal in August 2024 · adoption signal.
- a16z podcast "Prediction Markets · Everything You Need to Know" (FULL_READ · notes only, audio is timestamped) covers: Hayek's information aggregation, when thick vs thin markets work, the oracle problem, why most internal corporate prediction markets failed, scientific-replication markets (Dreber/Pfeiffer/Almenberg studies), futarchy. The mid-discussion claim: PMs are public goods that suffer from undersupply absent subsidies, particularly for niche topics.
- JP "Ahead of the Headlines" (FULL_READ): elaborates the chapter-opening claim that PMs are "mirrors of belief, distilled into probability." During Joe Biden's withdrawal, "the Polymarket market jumped literally the moment he posted this announcement tweet" · testable as a sub-second reaction-time experiment. Markets work because incentives reward research and punish confident wrong answers, the opposite of media incentives.
- Bhattacharjee "How to Use Prediction Markets as a High Quality Info Source" (FULL_READ): four-tier reliability framework: (1) Category 1 · financialized econ data (CPI, S&P, weather) · high liquidity, verifiable, sophisticated participation; (2) Category 2 · political/macro outcomes (elections, recession forecasts) · somewhat financialized; (3) Category 3 · sports · moderate liquidity, less sophisticated; (4) Category 4 · true prop bets (Powell mentions, sports microevents) · low liquidity, unsophisticated, large swings. Categories 1-2 are useful; 3-4 are "harmless but should be regulated thoughtfully." Three legitimate uses: data triangulation, nowcasting, hedging. Cites Susquehanna and other quant firms as the sophisticated counterparty class operating quietly in Categories 1-2.

## Notable Quotes

> "Prediction market accuracy is not the wisdom of crowds. Roughly 3% of accounts drive most price discovery."  
> — *Gomez-Cram et al.*

> "Only markets demand that speakers bear consequence for being wrong."  
> — *Abhitej*

> "Markets work on roughly 2% of listed contracts."  
> — *Vaidik Mandloi*

## Where It Matters

Wisdom of crowds is the folk pitch but 2026 empirical work has destabilized it. The "informed minority" finding doesn't kill the *outcome* (PMs are accurate on a band of contracts) but kills the *mechanism* claim (it's not many small bets averaging, it's a few sharps anchored by skin in the game). For PM builders, this changes who you optimize for: sharps need execution quality, data feeds, and predictable surveillance; retail flow is the *fuel* that subsidizes them, not the source of signal. For Dekant, the distribution-market thesis is partially a play to give the informed minority *more dimensions* on which to express edge, since a single price point can only encode so much information.

## Related Concepts

- **Information aggregation** · the mechanism wisdom-of-crowds is supposed to instantiate
- **Forecasting accuracy** · the testable claim
- **Calibration / Brier score** · how the claim is operationalized
- **Superforecasting** · the human-skill complement
- **Adverse selection** · what informed-minority dynamics produce
- **Distribution markets** · proposed higher-resolution alternative to binary crowd-pricing
- **Endogeneity** · what can break wisdom-of-crowds in self-referential settings

## Sources

- [Predictions Are The New Expression](https://x.com/abhitejxyz/status/2047353485700825546) — Abhitej · Apr 24, 2026 [FULL_READ]
- [Polymarket Is Not a Truth Machine](https://www.thetokendispatch.com/p/polymarket-is-not-a-truth-machine) — Mandloi · Apr 11, 2026 [FULL_READ]
- [Prediction Market Accuracy: Crowd Wisdom Or Informed Minority?](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6617059) — Gomez-Cram, Guo, Jensen, Kung · Apr 1, 2026 [FULL_READ]
- [How to Use Prediction Markets as a High Quality Info Source](https://uncover.substack.com/p/how-to-use-prediction-markets-as) — Isar Bhattacharjee · Mar 30, 2026 [FULL_READ]
- [The Book That Predicted Polymarket](https://x.com/mikita_crypto/article/2029939210350645729) — Ahnianchykau · Mar 6, 2026 [FULL_READ]
- [Ahead of the Headlines: Prediction Markets and the Collective Mind](https://jprz1321.substack.com/p/ahead-of-the-headlines-prediction) — JP · Feb 25, 2026 [FULL_READ]
- [What If We're Capturing the Wrong Signal?](https://x.com/TideMarkets/article/2016912289941827801) — Jo · Jan 29, 2026 [FULL_READ]
- [Prediction Markets · Everything You Need to Know](https://a16zcrypto.com/posts/podcast/prediction-markets-explained/) — Chokshi, Tabarrok, Kominers · Sep 25, 2025 [FULL_READ · podcast notes]
- [Crypto Prediction Markets](https://dirtroads.substack.com/p/63-crypto-prediction-markets) — Luca Prosperi · Oct 11, 2024 [FULL_READ]
- [The Art of Forecasting](https://paragraph.com/@filarm/the-art-of-forecasting) — fil · Sep 30, 2024 [FULL_READ]

