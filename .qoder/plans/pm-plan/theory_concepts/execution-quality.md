# Execution Quality

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Execution Quality](https://www.pmatlas.xyz/concepts/execution-quality)

---

## Definition

**Quick definition.** How favorably a trader transacts relative to the prevailing market price. The defining empirical finding of 2026 prediction-market research is that **execution quality outweighs directional forecasting skill** as a determinant of returns.

## Key Insights

- Della Vedova (222M Polymarket trades): forecasting accuracy does *not* predict profitability. Traders who pick the right side still lose because they arrive late at unfavorable prices; **automated traders profit by paying 2.52 cents less per contract** despite near-random directional skill.
- Kunal Doshi: 19 algo addresses on Polymarket fast crypto extract consistent profit through paired trading and maker execution; 69% of retail lose. Execution edge dominates the P&L.
- Isaac Rose-Berman: a fair coin-flip becomes a **3.4% expected loss after fees** on Kalshi · execution costs convert otherwise neutral expected value into a structural loss for retail. The mechanism: retail almost always crosses the maker spread as the taker; Kalshi takes 0.07 × C × P × (1-P) which peaks at 50/50 from both sides. "Maker dominance is structural, not stylistic · only the people with the fastest systems and best models can profitably maintain quotes."
- Cryptonomads (10,000 automated trades): most PM terminals are "execution mirages." Only institutional API rails and trader-native terminals survive · execution infrastructure becomes the moat.
- Ranger Global: **latency <100 ms captures 73% of arbitrage profits**. The execution-quality battle is at the millisecond level. PM traders systematically underreact to spot moves by 10–20%, leaving a renewable execution edge for fast actors.
- Daedalus Research: academic conversation must move beyond forecast accuracy to **execution quality, market structure, and trader welfare**.
- Kunal Doshi on Kalshi sports: in-game depth declines **76%** vs. pre-game; taker fees up to 3.5% at midpoint · institutional execution is currently bottlenecked by these microstructure properties.
- Pantera/Sui Kyle-λ measurement: **Polymarket's median liquidity depth is 10⁶·⁹⁶ vs. Kalshi's 10⁶·⁴²** · 3-4× more notional volume needed to move Polymarket prices over a 60-second horizon. Execution-quality tradeoff is structural: Kalshi prices respond faster to information (median 7s lead in 80% of large moves) but Polymarket fills with less slippage.
- Pantera/Sui design hypothesis: Polymarket's **~3-second delay on marketable orders in live sports** reduces LP adverse selection, allowing tighter quotes · this is an explicit execution-quality intervention at the matching-engine level.
- Becker on execution-vs-information: at 1-cent contracts takers win only 0.43% of the time against an implied 1% probability · a **−57% mispricing** entirely captured by makers. Pure execution edge from controlling who's the LP.
- sybilpm: in batch auctions "the money that used to go to 'I was 50ms faster' now goes to 'I was right about the price'" · the design space for fixing PM execution quality is microstructure-level, not infrastructure-level.

## Notable Quotes

> "Forecasting accuracy does not predict profitability."  
> — *Della Vedova*

> "Automated traders profit by paying 2.52 cents less per contract."  
> — *Della Vedova*

> "Most prediction market terminals fail through execution mirages."  
> — *cryptonomads*

> "Takers exhibit negative excess returns at 80 of 99 price levels. Makers exhibit positive excess returns at the same 80 levels."  
> — *Becker*

> "Polymarket requires roughly 3–4× more notional volume to generate a comparable price move over a 60-second horizon."  
> — *Pantera Research Lab*

## Where It Matters

This is the single most important finding for builders: **the product that captures the asset class is the one that delivers superior execution, not superior research**. Every existing terminal that displays charts and odds without first solving order routing, fill quality, slippage, and latency is fighting on the wrong axis. The corollary for platforms is that maker-rebate fee structures and matching-engine design (latency, batched vs. continuous) determine which traders become profitable · which determines who comes back.

## Related Concepts

- **Market making** · execution quality from the LP side.
- **Adverse selection / toxic flow** · what execution edge guards against.
- **Retail flow** · the cohort that systematically gets worse execution.
- **Cross-platform arbitrage** · execution edge across venues.
- **Bid-ask spread** · the unit of execution quality.
- **Temporal arbitrage** · execution edge over time.

## Sources

- [A Game of Volatility](https://x.com/Kunallegendd/status/2054191944944038084) — Kunal Doshi · May 12, 2026
- [Kalshi's Favorite Lie](https://howgamblingworks.substack.com/p/kalshis-favorite-lie) — Isaac Rose-Berman · Apr 29, 2026
- [Why Every Prediction-Market Terminal Will Fail (and the Two That Won't)](https://x.com/0xCryptoNomads/status/2048664550225170605) — cryptonomads · Apr 27, 2026
- [Anatomy Of A New Asset Class I: How Markets Turn Capital Into Probability](https://x.com/Ranger_Global/status/2046547375649427572) — Ranger Global · Apr 21, 2026
- [What Happens When Institutional Liquidity Enters Prediction Markets](https://x.com/DaedalusRsch/status/2046219948452979151) — Daedalus Research · Apr 20, 2026
- [From Betting to Trading: How Kalshi Is Reshaping Sports Markets](https://x.com/Kunallegendd/status/2044780671839952949) — Kunal Doshi · Apr 16, 2026
- [Who Profits from Prediction Markets? Execution, Not Information](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6191618) — Joshua Della Vedova · Feb 7, 2026

