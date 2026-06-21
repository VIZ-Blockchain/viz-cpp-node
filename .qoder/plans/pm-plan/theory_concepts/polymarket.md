# Polymarket

**Category:** Business & Platforms  
**Source:** [PM Atlas — Polymarket](https://www.pmatlas.xyz/concepts/polymarket)

---

## Definition

**Quick definition.** A crypto-based prediction market platform on Polygon, enabling peer-to-peer trading of binary event contracts settled via trusted oracles (UMA + Fox News for political markets in 2024).

## Key Insights

- Polymarket is the canonical crypto-native PM. Built on Polygon, uses Gnosis Conditional Token Framework, UMA oracle for resolution disputes. ~1.6M cumulative unique users and $9B+ in election volume during the 2024 cycle (per Galaxy and ASXN).
- Gouker's DFS parallel positions Polymarket and Kalshi as duopoly mirrors of DraftKings and FanDuel circa 2014. State regulatory pushback, industry self-regulation attempts, casino industry opposition · all four DFS-era patterns now apply.
- The Dudley & Magdaleno paper directly tests Polymarket's forecasting accuracy in a non-political domain: Polymarket forecasts for weekly cumulative US flu hospitalizations and monthly measles cases (2025–2026) fail to outperform standard benchmarks · for flu they are dominated by the CDC FluSight ensemble (the optimal combination puts "zero weight on the markets"), and for measles they are outperformed by simple statistical baselines. Two failure modes diagnosed: "placement of probability mass on impossible outcomes (e.g., decreasing values in cumulative forecasts) and low trading volume."
- The infectious disease failure undermines the "PMs as truth machines" framing outside their high-volume sports/politics core · speaks to Sakshi Mishra's category-by-category sustainability question.
- Polymarket structural metrics (cross-referenced across cluster articles): 99.2% of trading volume in political markets historically (ASXN); 166:1 visit/MAU ratio (Lydia Wu); 2% of wallets generate ~90% of volume (sealaunch); 1.3% of political markets liquid enough to be manipulation-resistant (Hall & Paschal).
- Dune analysis of Polymarket's fast crypto markets (Sep 2025 - Mar 2026): 5-minute contracts overtook 15-minute in weeks ($2.3B vs $795M notional); bots control 55-62% of volume across fast markets; Bitcoin drives 77% of turnover. $23.7M in taker fees collected in 83 days via maker-rebate model · structurally converged with a derivatives exchange.
- Camilo: Polymarket has reached network-effect escape velocity. Zero trading fees is a growth feature, not a revenue bug. Every news cycle trojan-horses the platform into the conversation.
- The 2024 election surfaced manipulation pathology (Prosperi): four coordinated accounts controlled 23% of open interest, ~41% of volume appeared to be wash trading. The Detection of Wash Trading (Sethi/Sirolly/Ma/Kanoria) algorithmically identified a 200-wallet cluster generating $113M in volume with $57.86 aggregate losses.
- Lou Kerner's conspiracy: Polymarket's oracle architecture is the manipulation surface. Fox News chosen as a settlement oracle for the 2024 race; UMA token holders could sway disputed resolution votes given UMA's small market cap.
- The Venezuela election dispute (Hall, a16z crypto) is the canonical Polymarket resolution failure: $6M+ traded on the outcome, but when votes were counted the government declared Maduro the winner while opposition + international observers alleged fraud. The platform faced an unresolvable epistemic choice.
- Ukraine map manipulation and the government shutdown contract are two more Hall-cited Polymarket resolution failures, showing that adversarial inputs and OPM-website-as-oracle both fail at scale.
- Polymarket is the offshore arbitrage target (Sethi, Information Contagion): pseudonymous trades that show up on Polymarket get extracted by quant funds and acted on in KYC venues like CME · a cross-platform information leak the legal framework can't reach.
- Lydia Wu reframes Polymarket as crypto media/creator economy rather than pure event trading. Users are older and less focused on maximizing risk-reward than typical crypto traders.

## Notable Quotes

> "Across both settings, prediction markets fail to outperform standard benchmarks."  
> — *Carson Dudley & Reiden Magdaleno, *Prediction Markets Underperform Simple Baselines For Infectious Disease Forecasting**

> "Even when we combine market forecasts with the ensemble, the best combination puts zero weight on the markets."  
> — *Dudley & Magdaleno (on Polymarket vs CDC FluSight for flu)*

> "Polymarket not only becoming the future of news, but also vital infrastructure for the future of financial markets."  
> — *Camilo, *Thoughts on Polymarket Network Effects**

## Where It Matters

Polymarket is both the proof-of-concept that crypto-native PMs scale and the lab where every PM design pathology shows up first (resolution disputes, wash trading, ghost markets, regulatory enforcement). It's the de facto comparison every new platform measures itself against. For Dekant: Polymarket on Polygon is the direct competitor for crypto-native flow · but its binary-only architecture leaves the continuous-payout lane open, and its UMA-oracle design is the manipulation surface Dekant's smooth-kernel resolution avoids.

## Related Concepts

- Platform competition · Polymarket vs Kalshi as the prevailing duopoly
- Network effects · Polymarket as the escape-velocity case
- Election markets · Polymarket's volume engine in 2024
- Oracle design · UMA-based dispute resolution is its mechanism
- Market manipulation / wash trading · heavily documented on Polymarket data
- Information aggregation · both proof and counterexample depending on domain

## Sources

- [Prediction Markets As Follow-Up To DFS: The Parallels Are Strikingly Similar](https://www.ingame.com/prediction-markets-daily-fantasy-sports-parallels/) — Dustin Gouker · May 14 2026 ·
- [Prediction Markets Underperform Simple Baselines For Infectious Disease Forecasting](https://arxiv.org/abs/2605.11220) — Carson Dudley, Reiden Magdaleno · May 11 2026 ·

