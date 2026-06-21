# Nowcasting

**Category:** Information Theory  
**Source:** [PM Atlas — Nowcasting](https://www.pmatlas.xyz/concepts/nowcasting)

---

## Definition

Using prediction market prices as real-time proxies for economic indicators that are officially reported with a delay (inflation, employment, GDP). Treats continuously updated market odds as a high-frequency data source · turning monthly econ releases into a live ticker.

## Key Insights

- Tiered framework for evaluating PM reliability: financialized economic indicators rank *highest*, speculative prop bets lowest. Three concrete use cases: (1) triangulating against traditional polls, (2) nowcasting delayed economic data in real time, (3) hedging event risk. Federal Reserve paper validating Kalshi's data quality is cited as anchoring the case. Tetlock's forecasting research grounds the methodology (Isar Bhattacharjee, "How to Use Prediction Markets as a High Quality Info Source").
- Dan Schwarz analyzes ~13,500 Polymarket contracts: >80% volume in sports/crypto/elections; accuracy on "useful" markets hasn't improved since early 2025. AI chatbots may supersede PMs as the primary forecasting interface, leaving PMs to serve an *epistemic role as common-knowledge infrastructure*. Nowcasting is exactly the kind of "useful" application where the gap between aspiration (real-time econ indicator) and reality (low volume, thin liquidity, narrow contract definitions) is widest (Asterisk).
- Schwarz full-read on the "news interpretation" / nowcasting category: 1,647 markets with $1.25B total volume · but 85% concentrated in US federal interest rate markets. Median market volume *declined* from $49K (early 2025) to $13K (end 2025). Schwarz's verdict: while predicting interest rates is valuable, "CME futures, Bloomberg consensus, and professional economists already do it. The same is true for other indicators with high trading volume on Polymarket and Kalshi: inflation, unemployment, commodity prices, mortgage rates." The PM advantage is *speed* · on March 11, 2026, FT reported Iran war escalation news → Polymarket odds of inflation ≥2.8% rose to 90%+ in real time, much faster than the Cleveland Fed inflation nowcast (~2-week lag).
- Bhattacharjee full-read on the practical workflow: "First, it's super easy to access and interpret. If you want to use traditional 'market pricing mechanisms' to estimate data like inflation, you either need proprietary tools or you need to do some calculations to figure out inflation breakeven prices (e.g., TIPS vs Bonds pricing). This is a pretty big faff. For a prediction market, you literally open it up and read the number." Second advantage: continuity. He compares against the Cleveland Fed inflation nowcast · "it's taken around 2 weeks for the inflation nowcast from the Cleveland Fed to catch up with the oil news from Iran. The prediction markets on the other hand are instantaneous."
- Bhattacharjee identifies hedging-via-nowcasting use cases: short a dollar-store stock + long recession on Polymarket as a covered position. Weather/natural-disaster events provide more structural hedging opportunities. The "more I've looked at these prediction markets, the more hedging opportunities and risk offsetting opportunities start to become apparent."
- The Feb 2026 Federal Reserve Board study (cited by Bhattacharjee and by Mitts/Ofir): "Our results suggest that Kalshi markets provide a high-frequency, continuously updated, distributionally rich benchmark that is valuable to both researchers and policymakers."

## Notable Quotes

> "Financialized economic indicators rank highest, speculative prop bets lowest."  
> — *Isar Bhattacharjee*

> "Accuracy on 'useful' markets hasn't improved since early 2025."  
> — *Dan Schwarz*

## Where It Matters

Nowcasting is the most respectable application of PMs in the "info finance" pitch · it's what gets you a Federal Reserve citation rather than a CFTC complaint. The Kalshi CPI markets are the canonical example: a continuously updated probability over the next BLS print, available between releases, that could in principle reduce policy lag for traders and central banks alike. The empirical limits are real, though: Adhi Rajaprabhakaran's "Minimum Viable Liquidity" piece (analyzing the same 149 Kalshi CPI markets) found that *volume explains <1% of variance in forecast accuracy* on CPI markets, suggesting that even on the most respectable nowcasting target, the standard "thicker book = better signal" intuition is wrong. For builders, nowcasting is a high-prestige but low-volume contract category · useful as a credibility flag, not as a revenue line.

## Related Concepts

- **Forecasting accuracy** · the metric that determines whether nowcasting is real
- **Information aggregation** · the underlying mechanism
- **Endogeneity** · what could break nowcasting if Fed policy reacts to PM prices
- **Info finance** · the broader umbrella nowcasting sits inside
- **Probability infrastructure** · nowcasting is the canonical "embed PM odds in another product" use case
- **Calibration** · nowcasting needs strong calibration on point estimates, not just directional accuracy
- **Minimum viable liquidity** · the empirical constraint on CPI markets specifically

## Sources

- [Are Prediction Markets Good for Anything?](https://asteriskmag.com/issues/14/are-prediction-markets-good-for-anything) — Dan Schwarz · Apr 1, 2026 [FULL_READ]
- [How to Use Prediction Markets as a High Quality Info Source](https://uncover.substack.com/p/how-to-use-prediction-markets-as) — Isar Bhattacharjee · Mar 30, 2026 [FULL_READ]

