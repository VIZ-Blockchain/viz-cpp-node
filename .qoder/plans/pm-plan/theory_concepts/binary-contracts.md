# Binary Contracts

**Category:** Mechanism Design  
**Source:** [PM Atlas — Binary Contracts](https://www.pmatlas.xyz/concepts/binary-contracts)

---

## Definition

Prediction market contracts that resolve to exactly one of two values, typically $1 (yes) or $0 (no), based on whether a specified event occurs. The binary structure eliminates ambiguity at resolution and enables direct probabilistic interpretation of prices · but it also forces continuous questions into a Pareto-distributed grid of independent order books.

## Key Insights

- functionSPACE analyzed 36,777 Polymarket events: when one continuous question is split into ~20 binaries, the top 3 markets capture >75% of volume regardless of event size · the bottom of the distribution is structurally untradeable "ghost markets."
- The $0.01 tick size compounds the fragmentation problem · at very low probabilities, a one-cent tick is a 50%+ relative move, creating a "rounding tax" that makes low-probability contracts structurally imprecise.
- functionSPACE V2 (Apr 2026) split events into continuous (price brackets, weather ranges) vs categorical (teams, candidates): both concentrate ~90% of volume in top 5–6 markets, but ghost markets are largely a *categorical* phenomenon. Continuous events distribute volume more evenly and survive the liquidity cliff at high N. Continuous events overtook categorical by count in 2026Q1 · the continuous-distribution primitive applies to a growing share of the platform.
- Kalshi's revenue formula `fee = 0.07 × C × P × (1-P)` peaks at 50% probability · economically incentivizes trading near coinflip markets, structurally more poker-rake than sportsbook (Schneider analysis of 203M trades across $41.7B volume).
- Sports comprise **82% of Kalshi's volume** · despite the CFTC-regulated derivatives positioning, it functions as a sports betting platform.
- Binary payoffs create *clean* basis risk to truth · Jeff Park argues this precision is what differentiates prediction markets from other derivatives, making them ideal for hedging exposure to specific outcomes.
- Minsky's "vega wedge" · binary hedgers replicating exposure via vanilla options pay a structural overcharge (a structural overcharge (magnitude varies by asset; the specific ~4.8%/7-20% figures are unverified)). Prediction markets can undercut that tax in deep categories (0xturbanurban).
- The binary structure makes prediction markets the purest testing environment for investment theory · binary resolution eliminates the unobservable noise that obscures strategy quality elsewhere (gemchanger).
- LMSR's pricing function is mathematically identical to softmax · bridges quant finance and prediction market pricing.
- Leverage is hard on binaries because binary outcomes resolve *instantly*, skipping the intermediate prices a liquidation engine needs · this is "gap risk." dYdX's TRUMPWIN perp on election night 2024 broke under real conditions despite sophisticated safeguards (Ruzicka).
- Three current approaches to leverage on binaries: constrain leverage (1x–1.5x), engineer around gap risk (dynamic fees, circuit breakers), or ship and iterate.
- Berkeley Blockchain primer: order books translate bids/asks into probabilities. Polymarket (offshore, crypto-native, public onchain) vs Kalshi (CFTC-regulated, USD, private activity) is the canonical comparator.
- Jo Lim (Strait of Hormuz 2026): binary order-book markets hit an architectural ceiling on granular, multi-outcome risk. LMSR/CLMSR offers protocol-native liquidity and coherent pricing for events without underlying assets.

## Notable Quotes

> "Volume follows an extreme Pareto distribution: the top 3 markets capture over 75% of trading activity regardless of event size, leaving a large fraction as untradeable ghost markets."  
> — *functionSPACE, *Binary Events**

> "Kalshi functions more like a poker rake than a sportsbook, charging fees via the formula fee = 0.07 × C × P × (1-P), which incentivizes trading near 50% probability."  
> — *Sam Schneider, *What's Kalshi's Revenue?**

> "Binary outcomes resolve instantly, skipping the intermediate prices that liquidation engines need to function."  
> — *Nick Ruzicka, *Everyone's Promising 20x Leverage on Prediction Markets**

> "Binary payoffs create clean basis risk to truth."  
> — *Jeff Park, *What Most People Get Wrong About Prediction Markets**

## Where It Matters

The binary contract is the dominant primitive of the category · every Polymarket and Kalshi market is a stack of binaries. But the architecture forces continuous questions (price, magnitude, distribution) into a fragmented grid that systematically wastes liquidity in the tails. This is exactly the gap that motivates continuous-outcome markets like Dekant. The binary structure is also what makes leverage and lending hard (gap risk) · every collateralization scheme described in the literature has to engineer around the discontinuity at resolution.

## Related Concepts

- Multi-outcome markets · direct alternative to stacked binaries
- Liquidity fragmentation · the cost of the binary primitive on continuous questions
- Gap risk · the binary structure is what creates discontinuous resolution
- Semantic tick size · the $0.01 increment reads as a percentage point of probability
- Market scoring rules / LMSR · the AMM mechanism most often paired with binaries
- Order book · Polymarket's CLOB shipped *because* binary AMMs failed
- Adverse selection · binaries make insider edges binary too
- Hedging · binary basis risk is what makes them clean hedging instruments

## Sources

- [Binary Events V2: Does Liquidity Trade The Tails?](https://x.com/functionspaceHQ/status/2048536153662636173) — functionSPACE · X
- [Polls Are Dead. Long Live Prediction Markets.](https://x.com/CalBlockchain/status/2047460674532790499) — Blockchain at Berkeley · X
- [What Most People Get Wrong About Prediction Markets](https://x.com/dgt10011/status/2045952603301851173) — Jeff Park · X
- [The Bane Of Binaries: What Prediction Markets Are Missing](https://x.com/0xturbanurban/status/2044433520806830101) — 0xturbanurban · X
- [Binary Events: What Happens When You Split One Market Into Twenty](https://x.com/functionspaceHQ/status/2039554933024776516) — functionSPACE · X
- [The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap](https://x.com/jolimmmm/article/2036119259420807216) — Jo Lim · X
- [What's Kalshi's Revenue? Analyzing All 203 Million Trades on Kalshi.](https://read.technically.dev/p/whats-a-prediction-market) — Sam Schneider · Technically.dev
- [Your Hedge Fund's Sharpe Ratio Is a Lie. Prediction Markets Are the Only Place It Can't Hide.](https://x.com/gemchange_ltd/article/2026300422483271733) — gemchanger · X
- [Everyone's Promising 20x Leverage on Prediction Markets. Here's Why It's Hard.](https://medium.com/@nick.c.ruzicka/everyones-promising-20x-leverage-on-prediction-markets-here-s-why-it-s-hard-8684d9a77e11) — Nick Ruzicka · Medium

