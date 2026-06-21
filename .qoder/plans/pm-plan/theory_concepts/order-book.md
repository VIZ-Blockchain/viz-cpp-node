# Order Book

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Order Book](https://www.pmatlas.xyz/concepts/order-book)

---

## Definition

**Quick definition.** A list of outstanding buy and sell orders at various prices, showing available liquidity at each level. The central limit order book (CLOB) is the dominant trading mechanism on Polymarket and Kalshi after the industry abandoned LMSR AMMs in 2022.

## Key Insights

- Polymarket's late-2022 migration from LMSR AMM to CLOB was the moment the industry recognized binary outcomes break AMM economics · impermanent loss becomes *permanent* because the pool ends up holding worthless losing-side shares (Melee).
- CLOBs solved capital destruction but introduced a new pathology: passive participation is impossible; **only professional MMs can quote** (Melee).
- Human Invariant on FCFS matching: all four major orderbook PMs (Polymarket, Kalshi, Opinion, Limitless) use FCFS with off-chain matching; FCFS creates a latency war ("co-locate with the matching engine") and forces MMs to widen spreads defensively. Priority batch auctions (cancels → makers → takers) shift competition from latency to price accuracy.
- Pantera/Sui empirical comparison (NFL 2025/26 season, 282 games, $1.7B notional across both venues): Kalshi total volume $1.3B, Polymarket $359M; Kalshi avg ~19,024 trades/game vs. Polymarket 2,718; Kalshi avg bet $224.83 vs. Polymarket $469.88. **Kalshi leads price adjustments by a median 7 seconds in ~80% of large moves** (85.7% for 20pp+ swings). Polymarket's median Kyle-implied LD is 10⁶·⁹⁶ vs. Kalshi's 10⁶·⁴² · **3–4× more notional needed to move Polymarket prices over a 60s horizon**.
- Pantera/Sui hypothesis: Polymarket's ~3-second delay on marketable orders in live sports reduces LP adverse selection, supporting deeper concentrated liquidity near the prevailing price; Kalshi's immediate execution exposes resting liquidity to information shocks, forcing more conservative quoting.
- Ranger Global on CLOB microstructure: YES/NO minting and merging invariant lets depth expand whenever matched counterparties exist; probability-scaled dynamic fees shrink near 0 and 1; latency <100 ms now captures **73% of arbitrage profits**.
- @allquantor on 600M+ datapoints: surface symmetry at top-of-book hides systematic **ask-side skew at deeper levels**; medium-to-large orders hit liquidity cliffs.
- st1ne identifies five MEV-style edges on Polymarket: oracle latency arbitrage, resolution arbitrage, dispute sniping, orderbook imbalance exploitation, conditional probability arbitrage. Polymarket is "a hidden MEV playground."
- Sam Schneider (Technically) on 203M Kalshi trades / $41.7B volume: fee = 0.07 × C × P × (1-P) · fees peak at 50/50, incentivizing trading near the meaningful middle. **Sports = 82% of total volume**. (Paywalled past intro.)
- Will Howard ("Many PMs Would Be Better Off as Batched Auctions"): there is no practical social benefit to sub-second reaction times in PMs; batching would redirect trader effort from speed to substance. Concrete claim: "for almost all markets there is no social utility to having a reaction time faster than about 1 second, and for many markets the socially useful reaction time could be a day or more." When Biden dropped out in 2024, Polymarket jumped *the moment* he tweeted · a system someone built solely because the PM existed; no broader benefit to that 500ms.
- Howard on what batch auctions would unlock: (1) effort reallocated to longer timescales improves accuracy, (2) high-frequency tomfoolery is filtered, (3) ergonomics: average trader can submit, walk away, and trust a fair clearing price emerges, (4) easier to read a single batch-cleared price than a noisy mid.
- Cryptonomads (10,000 automated trades): most PM terminals fail through "execution mirages, non-synthesizing research layers, and strategy tabs that are deck screenshots." Only institutional API rails and trader-native terminals survive.
- Jay Malavia: exchange order books deliver Betfair's ~3% overround vs. ~12% for traditional bookmakers · peer-to-peer order books welcome winners; sportsbooks limit them.
- Niakris's case that PMs are finance: peer-to-peer CLOB mechanics, skin-in-the-game pricing, hedging use cases, gambling-suppressive UX.
- Roan: Polymarket's CLOB creates "renewable structural arbitrage by design" · five-point diagnostic, three trader archetypes.
- Pantera/Sui design matrix: Polymarket runs off-chain matching with on-chain settlement on Polygon PoS (~2s blocks) and a ~3s marketable-order delay in live sports; Kalshi runs fully off-chain matching with immediate settlement. Both have public limit-order books but only Polymarket has on-chain transparency of flow.

## Notable Quotes

> "Kalshi reportedly has 23 active market makers with the top three providing 70% of election-contract liquidity, meaning any market those firms ignore is dead on arrival."  
> — *Melee*

> "Sub-second reaction times serve no social benefit."  
> — *Will Howard*

> "Polymarket is a hidden MEV playground."  
> — *st1ne*

> "Kalshi leads price adjustments by a median of ~7 seconds, exhibiting faster and more volatile reactions to in-game information shocks. Polymarket, despite lower volume and open interest, consistently exhibits higher implied liquidity depth."  
> — *Pantera Research Lab*

> "The information environment doesn't benefit for knowing that Joe Biden stepped down 500ms sooner."  
> — *Will Howard*

> "FCFS creates a latency war to update prices as close to real time as possible."  
> — *Human Invariant*

## Where It Matters

The CLOB is currently the consensus microstructure for prediction markets · but every empirical study finds the same downside: it concentrates power in a tiny set of pro market makers, leaves long-tail markets empty, and rewards latency arbitrage in ways that look indistinguishable from MEV. Alternatives (LMSR, parimutuel, batched auctions, scoring rules, covariance markets) all exist precisely as reactions to CLOB pathologies, and the design space for "post-CLOB prediction markets" is the most active frontier in 2026.

## Related Concepts

- **Continuous double auction** · the canonical CLOB matching rule.
- **Batched auctions** · leading alternative to the CLOB's FCFS matching.
- **Liquidity provision / market making** · order books are the venue.
- **Bid-ask spread** · the headline output.
- **Execution quality** · what CLOBs are graded on.
- **LMSR / market scoring rules** · pre-CLOB AMM mechanisms.
- **Cross-platform arbitrage / orderflow arbitrage** · strategies that live on top of order books.

## Sources

- [Why Every Prediction-Market Terminal Will Fail (and the Two That Won't)](https://x.com/0xCryptoNomads/status/2048664550225170605) — cryptonomads · Apr 27, 2026
- [Polls Are Dead. Long Live Prediction Markets.](https://x.com/CalBlockchain/status/2047460674532790499) — Blockchain at Berkeley · Apr 23, 2026
- [The Problem With CLOBs](https://x.com/meleemarkets/status/2046318159225897289) — Melee · Apr 21, 2026
- [Anatomy Of A New Asset Class I: How Markets Turn Capital Into Probability](https://x.com/Ranger_Global/status/2046547375649427572) — Ranger Global · Apr 21, 2026
- [Why AMMs Failed Prediction Markets](https://x.com/meleemarkets/status/2043766417342755259) — Melee · Apr 13, 2026
- [The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap](https://x.com/jolimmmm/article/2036119259420807216) — Jo Lim · Mar 24, 2026
- [Polymarket Is a Hidden MEV Playground. Most Traders Have No Idea.](https://x.com/SolSt1ne/article/2032008002094366899) — st1ne · Mar 13, 2026
- [What's Kalshi's Revenue? Analyzing All 203 Million Trades on Kalshi.](https://read.technically.dev/p/whats-a-prediction-market) — Sam Schneider · Mar 12, 2026  (paywalled, intro only)
- [Polymarket Doesn't Have a Money Problem. It Has a Plumbing Problem.](https://x.com/ZEITFinance/article/2031500989576949770) — @allquantor · Mar 11, 2026
- [Polymarket Is Not a Casino. Why Prediction Markets Are Finance, Not Gambling](https://x.com/13_niakris/article/2025614474263093627) — Niakris · Feb 23, 2026
- [Why Prediction Markets Aren't Gambling? (The Math)](https://x.com/RohOnChain/article/2020565633453412751) — Roan · Feb 9, 2026
- [The Super Bowl of Prediction Markets: Kalshi and Polymarket's Battle for Price vs Liquidity](https://panteraresearchlab.xyz/research/the-super-bowl-of-prediction-markets-kalshi-and-polymarkets-battle-for-price-vs-liquidity/) — Ally Zach, Danning Sui · Feb 5, 2026
- [Liquidity in Prediction Markets and the Rise of a New Asset Class](https://x.com/Ranger_Global/status/2008177990971122003) — Ranger Global · Jan 5, 2026
- [Who Are You Really Playing Against?](https://x.com/thejay/article/1968392782998835518) — Jay Malavia · Sep 18, 2025
- [Many Prediction Markets Would Be Better Off as Batched Auctions](https://antidiluvian.substack.com/p/many-prediction-markets-would-be) — Will Howard · Aug 2, 2025
- [Mechanisms for Prediction Markets](https://paragraph.com/@mechanism.institute/prediction-markets) — Raye Hadi, Sofia Cossar, Ori Shimony · Aug 22, 2024

