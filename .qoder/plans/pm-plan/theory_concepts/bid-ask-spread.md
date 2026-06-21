# Bid-Ask Spread

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Bid-Ask Spread](https://www.pmatlas.xyz/concepts/bid-ask-spread)

---

## Definition

**Quick definition.** The difference between the highest price a buyer will pay and the lowest price a seller will accept · the cost of immediacy. In prediction markets the spread reflects adverse-selection risk, gap risk, and inventory holding costs in addition to the usual MM compensation, which is why headline spreads can be 10–50% on long-tail contracts.

## Key Insights

- XO Labs's classical Avellaneda–Stoikov adaptation broke because **PM prices are bounded (0–1) probabilities with non-constant volatility and guaranteed terminal convergence**. Logit-space transformation lost $1,114 in backtest; probability-space engine with inventory-skewed spreads, regime detection, and multi-outcome coordination was profitable at $453.
- @allquantor's **"semantic tick size"** thesis: on 600M Polymarket datapoints, **~70% of one-cent price moves do not continue in the same direction**. Because each penny reads as a 1-percentage-point probability revision, PMs *narrativize microstructure noise*. A contrarian fade strategy harvests this overreaction.
- Sam Schneider on Kalshi: fee formula **fee = 0.07 × C × P × (1-P)** peaks at 50/50 · fees incentivize trading near the meaningful middle, and effectively widen the "true" spread retail pays.
- Rose-Berman makes the spread-as-tax economics explicit on Kalshi: every trade has a maker and a taker, fees apply both directions, and retail is structurally the taker because they don't have the systems to MM. A 50/50 coin-flip becomes a **3.4% expected loss** purely from crossing the maker spread + fees, regardless of who wins.
- Marinson ("Seeing Like a Market"): the cost differential between PMs and derivatives is **"apparatus rent"** paid for constructed dealer infrastructure. **FOMC compressed from a 12pp gap (March 2024) to <2pp (Feb 2026) at $3M** · driven by liquidity compression, not vol expansion. High-VRP categories (BTC 4.83%, elections) have crossed the threshold; silver (VRP −10.19%) has not and never will under current structure. The 87-contract / 2.89M-trade dataset found 30 PM wins / 12 at threshold / 45 losses.
- Marinson's BTC natural experiment: 5 BTC contracts at identical 4.08% VRP · PM wins at every strike where volume is sufficient ($100k/$13.3M, $105k/$7.1M, $110k/$4.9M, $150k/$32.8M) and loses only at the thin one ($125k/$2.2M). Liquidity is the only variable.
- Della Vedova: automated traders profit by paying **2.52 cents less per contract** than retail. The spread *is* the wealth-transfer mechanism.
- Becker on the spread-as-wealth-transfer category gradient: Finance gap 0.17pp, Sports 2.23pp, Crypto 2.69pp, Weather 2.57pp, Entertainment 4.79pp, Media 7.28pp, World Events 7.32pp. Category-level differences in spread economics swamp any platform-level differences.
- Human Invariant: FCFS order matching forces **defensive spread widening**; priority batch auctions (cancels → makers → takers) let MMs tighten quotes. The mechanism: in FCFS the MM's quote is fillable until they cancel; if news arrives faster than they can cancel, they're filled at stale prices. Priority batch auctions let cancels execute before takers, so stale quotes get pulled rather than picked off.
- sybilpm on dynamic spread economics: MM expected profit **E[π] = (s/2 · V(s)) − P_news · L_snipe**. In news-sensitive markets the sniping term dominates and spreads must widen to compensate; for "Will Israel strike Lebanon today?" type contracts the equilibrium spread is wide enough that retail can't trade economically.
- Jay Malavia: peer-to-peer exchange overround is ~3% (Betfair) vs. ~12% for traditional bookmakers · the spread differential is the exchange-vs-house-edge model difference made numerical.
- Taetaehoho: PM prices are 100–300 bps better than sportsbooks on liquid markets, but long-tail markets have **10–50% spreads** (where pro MM competition is absent).
- Ranger Global: finance markets have the tightest spreads (**0.17%**); crypto the widest (**2.69%**) · efficiency correlates strongly with informed-trader presence.
- Lotus: the same contract trades at **58–67 cents on different venues simultaneously** · cross-venue spread fragmentation is its own asset.
- Hall & Paschal complement: **bid-ask spreads exceed 20% on most political contracts**; only 1.3% are liquid enough to be manipulation-resistant. The spread is the most diagnostic stat for a market's status.

## Notable Quotes

> "70% of one-cent price moves do not continue in the same direction."  
> — *@allquantor*

> "Each penny reads as a one-percentage-point probability change, creating overreactions that a contrarian fade strategy can profitably harvest."  
> — *@allquantor, "Prediction Markets Have a Semantic Tick Size"*

> "Automated traders profit by paying 2.52 cents less per contract."  
> — *Della Vedova*

> "The cost differential is not a quality discount. It is apparatus rent. Same distributional content. Different transmission cost."  
> — *Lauris Marinson*

> "FCFS creates wider spreads and other negative externalities."  
> — *Human Invariant*

## Where It Matters

The spread is the single best summary statistic for a market's health: it bakes in liquidity depth, MM competition, adverse-selection risk, gap risk, and fee structure. Headline averages mask huge cross-section variation · finance markets at 17 bps next to entertainment at 700+ bps · which is why category mix is a strategic decision for every platform. The semantic-tick-size finding from @allquantor is particularly load-bearing: the same 1-cent move that traders read as new information is, 70% of the time, noise the MM can fade.

## Related Concepts

- **Market making** · spread is the MM's revenue stream.
- **Adverse selection** · wide spreads compensate for it.
- **Semantic tick size** · explains why PM spreads carry narrative weight beyond the price.
- **Execution quality** · measured against the spread mid.
- **Batched auctions / continuous double auction** · design choices that shape spread width.
- **Liquidity provision / order book** · the venue and capital that produce spreads.
- **Gap risk / hedging** · risks the spread must price in.

## Sources

- [Market Making for Prediction Markets: A Probability-Space Approach](https://x.com/xolabs_/status/2045161249588269257) — XO Labs · Apr 17, 2026
- [Prediction Markets Have a Semantic Tick Size](https://x.com/ZEITFinance/article/2034314079599243694) — allquantor · Mar 19, 2026
- [What's Kalshi's Revenue? Analyzing All 203 Million Trades on Kalshi.](https://read.technically.dev/p/whats-a-prediction-market) — Sam Schneider · Mar 12, 2026  (paywalled intro only)
- [Seeing Like a Market: Event Contracts and Market Topology](https://www.slampaper.xyz/) — Lauris Marinson · Mar 1, 2026
- [Who Profits from Prediction Markets? Execution, Not Information](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6191618) — Joshua Della Vedova · Feb 7, 2026
- [The Case For Alternative Ordering Mechanisms in Prediction Markets](https://www.humaninvariant.com/blog/pm-ordering) — Human Invariant · Nov 12, 2025
- [Who Are You Really Playing Against?](https://x.com/thejay/article/1968392782998835518) — Jay Malavia · Sep 18, 2025

