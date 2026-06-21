# Market Making

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Market Making](https://www.pmatlas.xyz/concepts/market-making)

---

## Definition

**Quick definition.** Continuously quoting bid and ask prices to facilitate trading, earning the spread while managing inventory risk. In prediction markets, classical market-making frameworks break because prices are bounded probabilities (0–1) and outcomes resolve discontinuously to one or zero.

## Key Insights

- semaji.eth's three-part series ranks market-making difficulty: Indian options (easy), crypto (medium), **prediction markets (legendary)** · gap risk is worse than any other asset class because informed counterparties can have near-perfect information and take out entire order books.
- Sportsbook spreads tighten through counterparty-aware pricing (price-discriminate against winners); prediction markets compensate via **maker competition and order-book transparency**. PM prices are 100–300 bps better on liquid markets but long-tail markets have 10–50% spreads (taetaehoho).
- Polymarket fast markets: 19 algo addresses extract consistent profits through paired trading and maker execution while **69% of retail traders lose money** (Kunal Doshi, "A Game of Volatility").
- Rose-Berman ("Kalshi's Favorite Lie"): the exchange-isn't-your-counterparty pitch is a **bigger fiction than it admits**. Every Kalshi trade has a maker and a taker; takers cross spreads, makers harvest them, Kalshi takes a fee from both. A fair coin-flip ($0.50 contract) becomes a structural loss because (1) Kalshi's fee = 0.07 × C × P × (1-P) peaks at 50/50, and (2) you have to cross a maker spread to enter and a second maker spread to exit. By the time a 50/50 contract resolves, the maker has taken ~3.4% in expectation regardless of who wins.
- Rose-Berman: maker dominance is **structural, not stylistic** · only the people with the fastest systems and best models can profitably maintain quotes; everyone else is takers paying tribute. Kalshi has no stake in *who* wins but has a strong stake in users *trading* · meaning losing-money entertainment, like a casino, is the business.
- Kalshi has ~23 active market makers; top three provide 70% of election-contract liquidity. Markets those firms ignore are dead on arrival (Melee).
- XO Labs tried Avellaneda–Stoikov in **logit space** and lost $1,114 in backtest; iterated to a probability-space engine with inventory-skewed spreads, volatility regime detection, and multi-outcome coordination · profitable at $453.
- Ranger Global on YES/NO minting and merging invariant: depth can expand whenever matched counterparties exist; **probability-scaled dynamic fees** shrink near 0 and 1. PM traders systematically underreact to spot moves by 10–20%; **latency <100 ms captures 73% of arbitrage profits**.
- January 2026 Polymarket XRP exploit paid **$231K on thin weekend liquidity** (Lotus); same contract trades at 58–67 cents simultaneously on different platforms (cross-venue fragmentation).
- Kalshi reprices faster than Polymarket (**median 7-second lead**) but Polymarket needs **3–4x more volume** to move prices comparably · Kyle-style market-impact analysis shows the speed-vs-depth tradeoff between CLOB-on-prem and on-chain order books. The Pantera/Sui dataset specifics: Kalshi leads initial repricing in **80% of large moves**; for 20pp+ swings Kalshi leads in 85.7%; Polymarket's median Kyle-LD is 10⁶·⁹⁶ vs. Kalshi's 10⁶·⁴² (3-4× more notional needed to move prices over 60s).
- Pantera/Sui's hypothesis on why Polymarket has deeper concentrated liquidity despite lower volume: Polymarket's **~3-second delay on marketable orders in live sports** reduces adverse selection for LPs, letting them quote tighter prices with lower risk. Kalshi's immediate execution exposes resting liquidity to faster information shocks.
- Becker analysis of 72.1M Kalshi trades / $18.26B volume: systematic **wealth transfer from takers to makers averages 1.12% excess returns on each side**; takers disproportionately buy YES longshots, accepting -64-percentage-point worse returns vs. equivalent NO positions. Variation by category is enormous: Finance has a 0.17pp gap (efficient), Sports 2.23pp, Crypto 2.69pp, Entertainment 4.79pp, Media 7.28pp, World Events 7.32pp.
- Becker's **inflection point**: before October 2024 (Kalshi's legal victory over CFTC + Q4 election boom), the maker-taker gap was *inverted* · takers earned +2.0% and makers lost 2.0%. Volume went from $30M in Q3 2024 to $820M in Q4 2024. The arrival of sophisticated algorithmic makers was the cause; "wealth transfer from takers to makers is not inherent to PM microstructure; it requires sophisticated market makers, and sophisticated market makers require sufficient volume to justify participation."
- Della Vedova on 222M Polymarket trades: **forecasting accuracy does not predict profitability**. Traders who pick the right side still lose money because they arrive late; automated traders with near-random directional skill profit by **paying 2.52 cents less per contract**.
- Polymarket has structurally converged with a derivatives exchange: $23.7M in taker fees in 83 days, maker-rebate fee model, bots controlling 55–62% of volume (Dune).
- Avoiding toxic flow vs. capturing retail flow is the central tension. semaji.eth uses the Jane Street coffee question to illustrate: conditional on someone trading with you, you should be **less confident** your trade was good.
- 4casters: raising fees from 0.5% to 0.75% had no material impact on volume · sports bettors are **less price-sensitive** than assumed. The sportsbook square-vs-sharp distinction maps onto Kalshi (square, 3.5% fee) vs. sharp markets that will thrive offshore.
- "Bond mule" strategy (outpxce) locks capital for small premiums on near-resolution markets · a market-making variant that monetizes time decay rather than spread.
- Daedalus Research's Black-Scholes-for-PM paper proposes a logit jump-diffusion stochastic kernel that exposes belief volatility, jump intensity, and correlation as quotable risk factors.
- Human Invariant proposes **priority batch auctions** (cancels → makers → takers) to shift competition from latency to price accuracy, letting market makers tighten quotes. FCFS creates a latency war and a co-location arms race; if cancels are prioritized ahead of makers, MMs can react to news before snipers fill at stale prices, which directly tightens spreads.
- sybilpm's MM expected-profit equation: **E[π] = (s/2 · V(s)) − P_news · L_snipe**. In traditional markets L_snipe is a few ticks; in PMs it's 80 cents on the dollar. When P_news is high, the sniping term dominates and the MM has two options: widen s, or pull quotes · both make the market worse.
- sybilpm on the "liquidity mirage": at 3am Tuesday a reasonable order book is visible, but it'll vanish the instant news drops. Polymarket and Kalshi spend millions on liquidity rewards that, in practice, route retail LPs into being "exit liquidity for snipers" · rewards never sized to cover 80-cents-per-contract gap risk.
- Sethi/Sirolly wash-trading paper: market makers' network signature is **heterophily** (they trade with many random counterparties), while wash-traders show **homophily** (they trade with their collusive clique). Their iterative network-redistribution algorithm flagged a 200-wallet cluster (all names starting "MAY") that did 116M shares / $113M volume but netted a $57 aggregate loss · distinguishing wash trading from MM activity that superficially looks similar.
- Bender (Citizens / Juice Reel data, Mar 2026): the **median PM user ROI is -8%** (vs. -5% for legal U.S. sportsbook bettors), but users trading >$500K since July 2025 had **median +2.6% ROI** · consistent with sharp-bettor returns. Drivers: prediction markets are open to all (sportsbooks ban sharps), and sharper competition means weaker median outcomes. One professional bettor: "We all want to be on the other side of the public; that's the dream. Being a market maker is highly attractive. We all want to be DraftKings and FanDuel."

## Notable Quotes

> "Conditional on someone trading with you, you should be less confident your trade was good."  
> — *semaji.eth, "The Liquidity Problem in Prediction Markets, Part I"*

> "Gap risk is effectively worse than any other asset class because informed counterparties can have near-perfect information and take out entire order books."  
> — *semaji.eth*

> "Polymarket has structurally converged with a derivatives exchange."  
> — *Dune, "Anatomy of Polymarket's Fastest Markets"*

> "Forecasting accuracy does not predict profitability."  
> — *Della Vedova*

> "You weren't providing liquidity to a forecaster. You were exit liquidity for a bot."  
> — *sybilpm, on MMs being snapped during news jumps*

> "The wealth transfer from takers to makers is not inherent to prediction market microstructure; it requires sophisticated market makers, and sophisticated market makers require sufficient volume to justify participation."  
> — *Becker*

> "Wash traders trade with other wash traders in their collusive clique, while market makers neither know nor care who their counterparties are."  
> — *Sethi/Sirolly*

## Where It Matters

Market making is the single profession that determines whether a prediction market exists at all. Because no retail trader will repeatedly cross a >20% spread, the supply of professional MMs essentially *defines* which categories of question can be traded. The empirical record (Becker, Della Vedova, Ranger Global) shows that maker P&L overwhelmingly comes from execution edge and fee rebates, not directional accuracy · which means the platform's job is less about onboarding "smart traders" and more about engineering microstructure (CLOB rules, batched auctions, maker rebates, dynamic spreads) that lets makers stay solvent.

## Related Concepts

- **Liquidity provision** · capital supply side; MMs are the active counterpart.
- **Adverse selection / toxic flow** · the binding constraint on MM profitability.
- **Bid-ask spread** · the unit economics of MMing.
- **Retail flow** · the desirable counterparty.
- **Gap risk** · why classical MM models break in binaries.
- **Batched auctions / continuous double auction** · alternative microstructures designed to protect MMs.
- **Execution quality** · what MMs sell.
- **Kelly criterion** · sizing under information edge, applied to inventory management.
- **LMSR / market scoring rules** · automated MM substitute.
- **Hedging** · MMs require it; PM-native hedging substrate is missing for many binaries.

## Sources

- [Sportsbooks vs Prediction Markets - Market Structure and Its Effects](https://x.com/0xtaetaehoho/status/2054215076471873575) — taetaehoho · May 12, 2026
- [A Game of Volatility](https://x.com/Kunallegendd/status/2054191944944038084) — Kunal Doshi · May 12, 2026
- [Kalshi's Favorite Lie](https://howgamblingworks.substack.com/p/kalshis-favorite-lie) — Isaac Rose-Berman · Apr 29, 2026
- [Why Every Prediction-Market Terminal Will Fail (and the Two That Won't)](https://x.com/0xCryptoNomads/status/2048664550225170605) — cryptonomads · Apr 27, 2026
- [The Problem With CLOBs](https://x.com/meleemarkets/status/2046318159225897289) — Melee · Apr 21, 2026
- [Anatomy Of A New Asset Class I: How Markets Turn Capital Into Probability](https://x.com/Ranger_Global/status/2046547375649427572) — Ranger Global · Apr 21, 2026
- [Market Making In PMs Sucks](https://x.com/uselotusai/status/2046639095531614455) — Lotus · Apr 21, 2026
- [What Most People Get Wrong About Prediction Markets](https://x.com/dgt10011/status/2045952603301851173) — Jeff Park · Apr 20, 2026
- [Market Making for Prediction Markets: A Probability-Space Approach](https://x.com/xolabs_/status/2045161249588269257) — XO Labs · Apr 17, 2026
- [Faster, Shorter, More Automated: Anatomy of Polymarket's Fastest Markets](https://x.com/Dune/status/2044045952730726863) — Dune · Apr 14, 2026
- [The Two Kinds of Prediction Markets](https://x.com/4castersbet/status/2042268072266920213) — 4casters · Apr 9, 2026
- [Leverage in Prediction Markets](https://x.com/0xMims/status/2041498106181627918) — Darren · Apr 7, 2026
- [Parimutuel Prediction Markets](https://x.com/meleemarkets/status/2041244791716110409) — Melee · Apr 6, 2026
- [Is Polymarket a Retail Product or a Pro Trading Venue?](https://x.com/sealaunch_/article/2037228250116522082) — sealaunch intelligence · Mar 27, 2026
- [Prediction Markets vs. Sports Betting: Market Dynamics, ROI by Cohorts, and Competitive Implications](https://citizens.bluematrix.com/docs/pdf/3beb4505-5a8b-49da-89df-ebdb104ebea9.pdf) — Jordan Bender · Mar 23, 2026
- [Noisy Traders Are Not Dumb Money](https://x.com/functionspaceHQ/article/2032397043579462028) — functionSPACE · Mar 13, 2026
- [Polymarket Doesn't Have a Money Problem. It Has a Plumbing Problem.](https://x.com/ZEITFinance/article/2031500989576949770) — @allquantor · Mar 11, 2026
- [The Sniper's Tax](https://sybilpm.substack.com/p/the-snipers-tax) — sybilpm · Mar 8, 2026
- [Turning Probability into Assets: A Look Ahead at Prediction Market Agents](https://x.com/0xjacobzhao/article/2029229969914904694) — Jacob Zhao · Mar 5, 2026
- [23 Reasons Prediction Markets Are Broken Today](https://x.com/linfluence/status/2026757563518431696) — Alexander Lin · Feb 26, 2026
- [Your Hedge Fund's Sharpe Ratio Is a Lie. Prediction Markets Are the Only Place It Can't Hide.](https://x.com/gemchange_ltd/article/2026300422483271733) — gemchanger · Feb 25, 2026
- [Minimum Viable Liquidity](https://fiftycentdollars.substack.com/p/minimum-viable-liquidity) — Adhi Rajaprabhakaran · Feb 24, 2026
- [How to Solve Insider Trading in Prediction Markets](https://www.dopaminemarkets.com/p/how-to-solve-insider-trading-in-prediction) — Shreyas Hariharan · Feb 10, 2026
- [Who Profits from Prediction Markets? Execution, Not Information](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6191618) — Joshua Della Vedova · Feb 7, 2026
- [The Super Bowl of Prediction Markets: Kalshi and Polymarket's Battle for Price vs Liquidity](https://panteraresearchlab.xyz/research/the-super-bowl-of-prediction-markets-kalshi-and-polymarkets-battle-for-price-vs-liquidity/) — Ally Zach, Danning Sui · Feb 5, 2026
- [Prediction Market Biases Revealed in 72 Million Trades](https://x.com/Ranger_Global/article/2016867581743747447) — Ranger Global · Jan 29, 2026
- [The Option Value of Waiting in Prediction Markets](https://x.com/0xnagu/article/2016620973391564951) — 0xnagu · Jan 28, 2026
- [On Prediction Markets](https://x.com/outpxce/status/2013397772074905950) — outpxce · Jan 20, 2026
- [The Microstructure of Wealth Transfer in Prediction Markets](https://www.jbecker.dev/research/prediction-market-microstructure) — Jonathan Becker · Jan 18, 2026
- [Liquidity in Prediction Markets and the Rise of a New Asset Class](https://x.com/Ranger_Global/status/2008177990971122003) — Ranger Global · Jan 5, 2026
- [The Case For Alternative Ordering Mechanisms in Prediction Markets](https://www.humaninvariant.com/blog/pm-ordering) — Human Invariant · Nov 12, 2025
- [The Detection of Wash Trading](https://rajivsethi.substack.com/p/the-detection-of-wash-trading) — Rajiv Sethi · Nov 12, 2025
- [Toward Black–Scholes for Prediction Markets](https://arxiv.org/pdf/2510.15205) — Shaw Dalen, Daedalus Research Team · Oct 17, 2025
- [The Liquidity Problem in Prediction Markets, Part II: Adverse Selection in Prediction Markets](https://x.com/semajeth/status/1975211193418563697) — semaji.eth · Oct 6, 2025
- [Who Are You Really Playing Against?](https://x.com/thejay/article/1968392782998835518) — Jay Malavia · Sep 18, 2025
- [The Liquidity Problem in Prediction Markets, Part I: Adverse Selection and Market Making](https://x.com/semajeth/status/1967598604006134024) — semaji.eth · Sep 15, 2025
- [The Liquidity Problem in Prediction Markets: Part 0](https://x.com/semajeth/status/1966144230826414225) — semaji.eth · Sep 11, 2025
- [Predicting Our Own Demise](https://reducibleerrors.com/prediction-markets/) — Agustin Lebron · Aug 17, 2025
- [No, You Can't Bet on Everything and That's Okay](https://rnikhil.com/2024/12/18/prediction-market-crypto) — Nikhil R · Dec 18, 2024
- [Why Prediction Markets Aren't Popular](https://worksinprogress.co/issue/why-prediction-markets-arent-popular/) — Nick Whitaker, J. Zachary Mazlish · May 17, 2024

