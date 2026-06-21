# Impact Markets

**Category:** Governance & Decisions  
**Source:** [PM Atlas — Impact Markets](https://www.pmatlas.xyz/concepts/impact-markets)

---

## Definition

**Quick definition.** Markets that price assets *conditional on* specific events occurring. Example: "What does BTC trade at if the Fed cuts 75bp?" rather than "Will the Fed cut 75bp?" Where prediction markets aggregate event probabilities, impact markets aggregate event-conditional asset valuations. In a separate strand of usage, "impact markets" also refers to markets pricing the expected social impact of projects to direct funding toward the most effective ones.

## Key Insights

- **Galaxy/Pokorny framing**: prediction markets reveal "this event has 60% probability." Impact markets reveal "and if it happens, BTC trades at $X." Spot markets reveal current prices but cannot directly express conditional valuations. Impact markets are the missing piece · they collapse the multi-step inference of "probability × asset-correlation × position-sizing" into direct price discovery.
- **Mechanism**: instead of trading $0/$1 conditional tokens, traders buy/sell the **asset itself in a conditional state**. The trade only settles if the event occurs; if it doesn't, the position is reverted or otherwise nullified. The market structure is closer to options or conditional perpetuals than to binary outcomes.
- **The multi-step inference problem Impact Markets solve**: today, a trader holding BTC with a view on Fed policy must (1) gather probabilistic odds from prediction markets, (2) feed them into proprietary models to estimate BTC impact, (3) execute separate trades on spot/futures markets, and (4) hope their correlation assumptions hold when the event resolves. Impact markets collapse this into one trade.
- **Burden transfer**: impact markets shift correlation-estimation from every individual end user to the market's price discovery process. End users still face the risk that the market-implied conditional price is wrong, but this is a fundamentally different (and smaller) risk than building a bespoke correlation model from scratch · "analogous to the difference between estimating implied volatility yourself versus reading it off an options chain."
- **Cleaner hedging**: a BTC holder worried about an election outcome can lock in a conditional BTC price for the specific scenario in a single trade · minimizing basis risk between event view and asset exposure. Standard prediction-market "hedging" requires running parallel positions in event probabilities and asset spot, hoping correlations behave.
- **Information gap closed**: impact markets surface the market's collective view on conditional asset valuations bound to specific events. This information doesn't currently exist in an explicitly discoverable form · neither prediction markets nor spot markets reveal it. Spreading impact-market liquidity makes it discoverable.
- Galaxy frames impact markets as the **necessary prerequisite for decision markets** to scale beyond crypto: surface conditional asset valuations (impact) → use them to govern capital allocation (decision). Impact is the information layer; decision is the governance layer.
- **Practical hedging examples** Galaxy cites: BTC | Fed cuts 75bp; Nvidia | AI-skeptic candidate wins election; M&A target equity | regulatory approval. These are exactly the conditional scenarios that hedge funds and portfolio managers currently model internally and trade through complicated synthetic positions.
- **Different second meaning of "impact markets"**: the older Effective Altruism / Lightcone Infrastructure usage · markets that price *expected social impact* of nonprofit projects or research, with funding directed to projects predicted to be most impactful. This is closer to a retrofunding mechanism (pay for past impact) than a conditional pricing mechanism. Onprediction's editorial framing focuses on the Galaxy/Pokorny conditional-asset usage.
- **Token structure for impact markets needs to be asset-denominated, not $0/$1**. This is the key architectural distinction from standard prediction-market conditional tokens · the conditional payout is in the asset (BTC, equity, stablecoin USD) rather than a synthetic outcome share. Implementation likely requires either tokenized asset wrappers or oracle-based settlement at the event's resolution.
- **Liquidity provisioning is harder** than standard prediction markets: market makers must hedge against both the event probability and the asset's price path under each event state. This is closer to options market-making than binary-options market-making.
- **Why Galaxy is bullish**: the "next frontier" framing positions impact markets as the breakout use case after Polymarket and Kalshi. They build on the same infrastructure (oracles, conditional tokens, CLOBs) but unlock institutional hedging demand that today's binary markets cannot serve · closing the gap between "knowing what might happen" and "knowing what it means."
- **Why this may be hard**: impact markets need both deep prediction-market liquidity on the underlying event AND deep spot/derivatives liquidity on the underlying asset. The cross-product of these two liquidity surfaces shrinks the viable market design space significantly. They'll likely launch first for very liquid asset/event pairs (BTC × Fed cuts, BTC × election) before generalizing.
- **Adoption sequencing**: most plausible first use cases are institutional hedging products (sophisticated buyers with explicit conditional views, willing to pay basis-risk reduction). Retail adoption would follow standardized products built on top, similar to how options retail followed institutional options.
- **Limitations the Galaxy piece names**: validity of the conditional state, oracle reliability under tail conditions, the challenge of pricing things that have never traded conditionally before. The first wave of impact markets will likely focus on event/asset pairs where both sides have liquid reference markets.

## Notable Quotes

> "Prediction markets aggregate probabilities for whether events will occur. Impact Markets answer the next question: 'What happens to this company or asset [if] this event occurs?' This separation allows each market type to specialize while creating a more complete information set."  
> — *Zack Pokorny, Galaxy Research*

> "This architecture solves a multi-step inference problem that plagues the market: Traders must gather probabilistic odds from prediction markets, feed them into proprietary models to estimate asset impacts, then execute separate trades on exchanges… Impact Markets collapse this entire workflow into direct price discovery of conditional valuations where trades only settle if the given event actually occurs."  
> — *Zack Pokorny*

> "Impact markets transfer the burden of correlation estimation from every individual end user to the market's price discovery process… The distinction is analogous to the difference between estimating implied volatility yourself versus reading it off an options chain."  
> — *Zack Pokorny*

> "While prediction markets reveal event probabilities and spot markets reveal current prices, Impact Markets answer the question neither can directly: what this asset trades at if that event occurs."  
> — *Zack Pokorny*

> "Impact and Decision Markets stand to extend [the prediction-market insight] by revealing what those events are worth and, in some cases, letting markets directly determine which actions organizations take."  
> — *Zack Pokorny*

## Where It Matters

Impact markets are the cleanest articulation of "prediction markets as financial primitives" · the path from event contracts to a real hedging instrument used by serious capital. They are also the natural complement to Dekant's continuous-outcome prediction market: where Dekant prices the *distribution* of an outcome variable (e.g., BTC price at year-end), impact markets price an asset *conditional on a discrete event* (e.g., BTC if the Fed cuts 75bp). The two designs answer different questions and can coexist; impact markets are the conditional analog, Dekant is the unconditional distributional one.

## Related Concepts

- **Conditional tokens** · Impact markets generalize conditional tokens from $0/$1 payouts to asset-denominated payouts.
- **Decision markets** · Impact markets are the information layer; decision markets are impact markets wired to binding governance.
- **Futarchy** · Asset-futarchy is a special case of impact markets where the conditional state is "proposal passes vs. fails."
- **Hedging** · Impact markets are explicitly framed as cleaner hedging tools than parallel prediction-market + spot positions.
- **Price discovery** · Impact markets close an information gap that neither prediction markets nor spot markets fill.
- **Distribution markets** · Distribution markets (Paradigm / White, 2024 · what Dekant implements) price unconditional distributions over outcomes; impact markets price asset valuations conditional on discrete events. They are complementary, not competing.
- **Oracle design** · Impact market settlement requires both event resolution AND asset-price-at-event-time observation, doubling the oracle attack surface.
- **Liquidity provision / market making** · Impact-market market-making is closer to options market-making than to binary market-making, raising the bar for capable LPs.

## Sources

- [Prediction Markets' Next Frontier: Impact and Decision Markets](https://www.galaxy.com/insights/research/prediction-markets-impact-markets-decision-markets-futarchy) — Zack Pokorny, Galaxy Research · Jan 12, 2026

