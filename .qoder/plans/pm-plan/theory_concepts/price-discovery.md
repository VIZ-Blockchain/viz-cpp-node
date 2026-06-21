# Price Discovery

**Category:** Information Theory  
**Source:** [PM Atlas — Price Discovery](https://www.pmatlas.xyz/concepts/price-discovery)

---

## Definition

The process through which trading activity reveals the fair value or true probability of an event. In prediction markets, price discovery is *higher-bandwidth* than in equities because contracts reference explicit, time-bounded events · but several papers in 2025–26 show that PM prices systematically diverge from true probabilities even with rational traders.

## Key Insights

- Market prices are NOT real probabilities. Three structural reasons prices diverge from true probabilities even with rational participants: favorite-longshot bias from Kelly betting, risk-premium distortion from market correlation, and risk-neutral forward pricing in long-dated contracts. Markets still outperform individuals because they weight *capital-backed* beliefs (Lihong).
- LessWrong "Will Jesus Return in an Election Year?" (FULL_READ) sharpens this: Polymarket's 3% trade price for Jesus 2025 wasn't about belief · it was about the *time value of Polymarket cash*. "No" sellers needed cash for other markets during 2024 election cycle. Harris-in-Kentucky shot from 0.3% to 1.5% on election day not because the underlying probability changed but because traders needed liquidity. The "correct" no-arb price for the Jesus market actually reflects the implied carry cost · replacing it with "This Market Will Resolve No At The End Of 2025" would price the time value alone.
- Prediction markets make information *legible* in a way stocks/options do not · when someone bets big on an attack on Maduro, everyone immediately knows what the bet is about. Legibility is a feature even when it surfaces uncomfortable implications (Andrew Courtney).
- CLOBs beat constant-product AMMs for binary events. The YES/NO minting and merging invariant lets depth expand whenever matched counterparties exist; probability-scaled dynamic fees shrink near 0 and 1 (Ranger Global). Regression of PM midpoints against BTC spot finds PM traders systematically underreact to spot moves by 10–20%, and latency under 100ms now captures 73% of arbitrage profits.
- Binary prediction markets are consumer-wrapped binary options. Minsky's "vega wedge" is the structural overcharge binary hedgers pay when they replicate via vanilla options (a structural overcharge (magnitude varies by asset; the specific ~4.8%/7-20% figures are unverified)). PMs can undercut this tax in deep-volume categories but lack a Black-Scholes-equivalent pricing language (0xturbanurban).
- LMSR-based AMMs structurally failed for prediction markets: in a binary market that resolves to 0 or 1, impermanent loss becomes permanent · the pool inevitably holds worthless shares on the losing side, and trading fees cannot offset a guaranteed structural loss. Polymarket migrated from LMSR AMM to CLOB in late 2022 (Melee).
- Roughly 3% of accounts drive most price discovery (Gomez-Cram et al.). PM accuracy is informed-minority-driven, not crowd-driven.
- Information contagion: insider trades on Polymarket may leak into regulated oil and stock futures markets · quant funds extract signal from pseudonymous crypto trades and act on it in KYC venues, without breaking existing laws (Sethi).
- "Semantic tick size": Polymarket orderbook analysis on 600M datapoints · ~70% of one-cent price moves do not continue in the same direction. The minimum price increment doubles as a *narrative unit* because each penny reads as a 1pp probability change, creating overreactions that contrarian fade strategies can profitably harvest (allquantor).
- Hedge-fund alpha hides nowhere in prediction markets: binary resolution eliminates the unobservable noise that obscures strategy quality in traditional finance. LMSR's mathematical identity with softmax bridges quant finance and PM pricing (gemchanger).
- "Minimum viable liquidity": 149 CPI markets on Kalshi (2021–26) · trading volume explains <1% of variance in forecast accuracy. MVL = Cost of Expertise / Price Gap. Platforms should prioritize *breadth over depth*, running many thin markets rather than concentrating volume in few (Adhi Rajaprabhakaran).
- Polymarket vs Kalshi NFL markets (2025): Kalshi reprices faster (median 7-second lead); Polymarket has deeper liquidity (3–4x more volume needed to move prices comparably). Kyle-style market-impact analysis (Pantera Research Lab).
- Prediction markets don't bend reality. Unlike stock markets, PMs lack causal mechanisms through which odds could influence the events they forecast · they're thermometers, not thermostats (Adhi Rajaprabhakaran response to Kyla Scanlon).
- AI underperforms humans in PMs because edge comes from *embodied, local knowledge* (monitoring flights, calling embassies) · not synthesizing public information. 5 of 6 AI models in Kalshi "Prediction Arena" are underwater (Mehmet Avci).
- 72M Kalshi trades · three persistent biases: longshot bias (5c contracts win 4.18%), maker-taker asymmetry (makers outperform at 80/99 price levels), YES/NO asymmetry (YES buyers -1.02% vs NO buyers +0.83%). Finance markets are most efficient (0.17% spread); crypto least (2.69%) (Ranger Global).
- Election market accuracy varies dramatically across platforms: PredictIt 93%, Kalshi 78%, Polymarket 67%. Cross-platform price divergences near Election Day are large; CNN/CNBC media partnerships create incentives for sensational coverage of thin markets (John Sides).
- Polymarket arbitrage: ~$40M in profits extracted through within-market and cross-market mispricings (Saguillo, Ghafouri, Kiffer, Suarez-Tangil 2025).
- Batched auctions > CLOBs: no practical social benefit from sub-second reaction times; batching redirects trader effort to meaningful questions while reducing zero-sum speed competition (Will Howard).
- Exchange model beats sportsbook: Betfair's ~3% overround vs bookmakers' ~12% · peer-to-peer produces fairer pricing and welcomes all winners, unlike sportsbooks that limit successful bettors (Jay Malavia).
- 817-market field experiment: prices can be manipulated with effects persisting for months, though they gradually fade. Markets with more traders, higher volume, and external probability anchors prove more resistant (Rasooly & Rozzi 2025).
- Will Jesus return? Polymarket at 3% with $100k wagered · buying No to 1% requires locking $1M for 1% annual return (worse than Treasuries). The piece's bigger insight is the carry-trade interpretation of long-dated "obviously wrong" markets: they price the implied interest rate on PM dollars (LessWrong).
- Pantera Research NFL study (FULL_READ): "notional trading volume has expanded by an order of magnitude" 2025→2026, "weekly aggregate volume across major prediction market platforms now regularly reaches billions of dollars." Polymarket = offchain order book + onchain settlement; Kalshi = fully off-chain. Critically, Polymarket applies a special rule for live sports markets that distinguishes its market microstructure from Kalshi's.
- Kevin Heavey's *Futarchy as Trustless Joint Ownership* (FULL_READ): reframes futarchy not as decision-making, but as the only mechanism that gives minority DAO shareholders enforceable rights. Decision markets prevent majority looting because conditional ABC-if-proposal-passes prices fall, so the proposal fails. Coin price is "the fairest and most elegant objective function." Token voting DAOs without futarchy provide only "the illusion of joint ownership."

## Notable Quotes

> "70% of one-cent price moves do not continue in the same direction."  
> — *allquantor, "Prediction Markets Have a Semantic Tick Size"*

> "Trading volume explains less than 1% of variance in forecast accuracy."  
> — *Adhi Rajaprabhakaran, "Minimum Viable Liquidity"*

> "Prediction markets lack causal mechanisms through which odds could influence the events they forecast"  
> — *thermometers rather than thermostats." · Adhi Rajaprabhakaran*

## Where It Matters

Price discovery is the product. The CLOB-beat-AMM result (Melee) is the canonical reason Polymarket migrated and why anyone building a binary PM today defaults to order books · *but the same argument doesn't apply to distribution markets*, where multiple correlated outcomes share a liquidity surface and LMSR/L2-norm CFAMM-style invariants are again viable. The "semantic tick size" and "minimum viable liquidity" findings should reset what builders chase: thicker books don't improve signal; better contract specification and informed-trader recruitment do. For Dekant: the curve-drawing primitive is partly an answer to "the binary market flattens 8 bits to 1" · a richer surface gives informed minorities more dimensions on which to express their edge.

## Related Concepts

- **Information aggregation** · price discovery is the surface; aggregation is the process
- **Forecasting accuracy** · measures price-discovery quality
- **Market making / LMSR** · the mechanism that produces discovery in AMMs
- **Adverse selection / Toxic flow** · the cost MMs pay for the discovery service
- **Longshot bias / Yes bias** · systematic distortions to discovery
- **Legibility / Endogeneity** · second-order effects of discovered prices on the underlying event
- **Distribution markets** · argued as higher-dimensional discovery
- **Minimum viable liquidity** · counterintuitive limit on how much depth helps
- **Semantic tick size** · the narrative-unit distortion specific to binary PMs

## Sources

- [Market Probabilities Are NOT Real Probabilities](https://x.com/Lihong062/status/2051017075758686622) — Lihong · May 3, 2026 [FULL_READ]
- [Legibility](https://whirligigbear.substack.com/p/legibility) — Andrew Courtney · Apr 27, 2026 [FULL_READ]
- [Predictions Are The New Expression](https://x.com/abhitejxyz/status/2047353485700825546) — Abhitej · Apr 24, 2026 [FULL_READ]
- [Anatomy Of A New Asset Class I: How Markets Turn Capital Into Probability](https://x.com/Ranger_Global/status/2046547375649427572) — Ranger Global · Apr 21, 2026 [FULL_READ]
- [The Bane Of Binaries: What Prediction Markets Are Missing](https://x.com/0xturbanurban/status/2044433520806830101) — 0xturbanurban · Apr 15, 2026 [FULL_READ]
- [Why AMMs Failed Prediction Markets](https://x.com/meleemarkets/status/2043766417342755259) — Melee · Apr 13, 2026 [FULL_READ]
- [Prediction Market Accuracy: Crowd Wisdom Or Informed Minority?](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6617059) — Gomez-Cram, Guo, Jensen, Kung · Apr 1, 2026 [FULL_READ]
- [Information Contagion](https://rajivsethi.substack.com/p/information-contagion) — Rajiv Sethi · Mar 31, 2026 [FULL_READ]
- [Prediction Markets Have a Semantic Tick Size](https://x.com/ZEITFinance/article/2034314079599243694) — allquantor · Mar 19, 2026 [FULL_READ]
- [Ahead of the Headlines: Prediction Markets and the Collective Mind](https://jprz1321.substack.com/p/ahead-of-the-headlines-prediction) — JP · Feb 25, 2026 [FULL_READ]
- [Your Hedge Fund's Sharpe Ratio Is a Lie. Prediction Markets Are the Only Place It Can't Hide.](https://x.com/gemchange_ltd/article/2026300422483271733) — gemchanger · Feb 25, 2026 [FULL_READ]
- [Minimum Viable Liquidity](https://fiftycentdollars.substack.com/p/minimum-viable-liquidity) — Adhi Rajaprabhakaran · Feb 24, 2026 [FULL_READ]
- [Polymarket Is Not a Casino. Why Prediction Markets Are Finance, Not Gambling](https://x.com/13_niakris/article/2025614474263093627) — Niakris · Feb 23, 2026 [FULL_READ]
- [The Super Bowl of Prediction Markets: Kalshi and Polymarket's Battle for Price vs Liquidity](https://panteraresearchlab.xyz/research/the-super-bowl-of-prediction-markets-kalshi-and-polymarkets-battle-for-price-vs-liquidity/) — Ally Zach, Danning Sui · Feb 5, 2026 [FULL_READ]
- [Prediction Markets Don't Bend Reality](https://fiftycentdollars.substack.com/p/prediction-markets-dont-bend-reality) — Adhi Rajaprabhakaran · Feb 3, 2026 [FULL_READ]
- [Is AI Any Good at Predicting?](https://reachavci.substack.com/p/is-ai-any-good-at-predicting) — Mehmet Avci · Feb 2, 2026 [FULL_READ]
- [Prediction Market Biases Revealed in 72 Million Trades](https://x.com/Ranger_Global/article/2016867581743747447) — Ranger Global · Jan 29, 2026 [FULL_READ]
- [Information Vectors: An Intro to Composable Beliefs](https://x.com/functionspaceHQ/article/2014809461647671570) — functionSPACE · Jan 24, 2026 [FULL_READ]
- [Manifesto: Make Precision Pay](https://x.com/TideMarkets/article/2008508916616098086) — Tide · Jan 6, 2026 [FULL_READ]
- [The Perils of Election Prediction Markets](https://goodauthority.org/news/the-perils-of-election-prediction-markets/) — John Sides · Dec 18, 2025 [FULL_READ]
- [Who Are You Really Playing Against?](https://x.com/thejay/article/1968392782998835518) — Jay Malavia · Sep 18, 2025 [FULL_READ]
- [Unravelling the Probabilistic Forest: Arbitrage in Prediction Markets](https://arxiv.org/abs/2508.03474) — Saguillo, Ghafouri, Kiffer, Suarez-Tangil · Aug 5, 2025 [FULL_READ · abstract: empirically finds ~$40M of executed profits from Market Rebalancing and Combinatorial Arbitrage on Polymarket using on-chain order-book data]
- [Many Prediction Markets Would Be Better Off as Batched Auctions](https://antidiluvian.substack.com/p/many-prediction-markets-would-be) — Will Howard · Aug 2, 2025 [FULL_READ]
- [Will Jesus Christ Return in an Election Year?](https://www.lesswrong.com/posts/LBC2TnHK8cZAimdWF/will-jesus-christ-return-in-an-election-year) — LessWrong · Mar 25, 2025 [FULL_READ]
- [How Manipulable Are Prediction Markets?](https://arxiv.org/abs/2503.03312) — Rasooly & Rozzi · Mar 5, 2025 [FULL_READ · abstract]
- [Futarchy as Trustless Joint Ownership](https://www.umbraresearch.xyz/writings/futarchy) — Kevin Heavey · Oct 28, 2024 [FULL_READ]
- [Unveiling Polymarket: The Positioning, Expansion, and Shadows of Crypto Prediction Markets](https://research.mintventures.fund/2024/10/09/Unveiling-Polymarket-The-Positioning-Expansion-and-Shadows-of-Crypto-Prediction-Markets/) — Lydia Wu · Oct 9, 2024 [FULL_READ]
- [Deep Dive #8 | Decentralized Prediction Markets](https://ampbura.substack.com/p/deep-dive-8-decentralized-prediction) — Amp Burapachaisri · Feb 23, 2024 [FULL_READ]
- [Prediction Markets Explained](https://alternativeassets.substack.com/p/prediction-markets-explained) — Stefan von Imhof · Feb 4, 2024 [FULL_READ]

