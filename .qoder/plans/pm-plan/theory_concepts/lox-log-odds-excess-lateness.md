# LOX (Log-Odds Excess Lateness)

**Category:** Mechanism Design  
**Source:** [PM Atlas — LOX (Log-Odds Excess Lateness)](https://www.pmatlas.xyz/concepts/lox-log-odds-excess-lateness)

---

## Definition

A metric measuring how much a forecaster's probability updates *lag behind* optimal Bayesian updating. Specifically, the gap between when information *should* have been priced in (under a hazard model of information arrival) and when it actually was. A diagnostic for whether late market volume reflects late *information* or late *participation* (driven by adverse selection).

## Key Insights

- **Origin:** 0xnagu (Jan 2026), *The Option Value of Waiting in Prediction Markets*. The only article on the on-page corpus, but it introduces a substantively new metric.
- **The question LOX answers:** When prediction markets see clustered volume near resolution, is it because (a) information arrived late (a *hazard* effect · there was nothing to trade on earlier), or (b) early entry was punished by adverse selection (a *toxicity* effect · informed traders wait too)?
- This is a structural disambiguation: hazard and toxicity look identical in volume time-series but have *opposite* implications for market design. Hazard is fine. Toxicity means the market maker is being adversely selected and is bleeding capital.
- **LOX is computed from on-chain trades** · it measures whether new entrants *hesitate* more than volume alone would predict under a pure hazard model. A high LOX means traders are hanging back even when they have information, which signals adverse selection.
- The decomposition framework is built around Bayesian updating: an optimal Bayesian forecaster updates their log-odds as new evidence arrives. If actual updates lag, the gap is "excess lateness" · log-odds excess lateness.
- **Empirical finding (the boxing-markets puzzle):** boxing markets cluster with *news* markets, not with other *sports* markets, despite being categorized as sports. This is because boxing has information-driven late-resolving structure (pre-fight news, weigh-ins, last-minute fitness reports) more like a political event than a baseball game.
- The framework provides a diagnostic for **adverse selection severity**: when LOX is high, market makers are losing money to informed traders, and the market needs design intervention (wider spreads, fee changes, batching).
- The "option value of waiting" is the toxicity side: an informed trader is better off waiting for the spread to compress or for additional information to arrive before showing their hand. This option has value, and the value is observable through LOX.
- LOX is a microstructure metric that fits the broader category of *adverse-selection diagnostics* (cf. Kyle's lambda, VPIN in equities) but is specifically constructed for the probability-space setting of prediction markets.
- The metric is relevant for **market making mechanism design**: Avellaneda-Stoikov-style market making frameworks (XO Labs' work, covered under market-scoring-rules) need a measure of toxicity to set inventory-aware spreads · LOX is one candidate.

## Notable Quotes

> "Builds a formal framework to decompose why prediction markets have late volume: is it because information arrives late (hazard), or because early entry is punished by adverse selection (toxicity)?"  
> — *0xnagu, *The Option Value of Waiting in Prediction Markets**

> "LOX, a metric computed from on-chain trades that measures whether new entrants hesitate more than volume alone would predict."  
> — **ibid.**

> "Boxing markets cluster with news markets despite being categorized as sports."  
> — **ibid.**

## Where It Matters

LOX is part of the emerging quantitative-microstructure toolkit for prediction markets · the discipline of measuring *why* a market is behaving as it does, not just *what* it's doing. As prediction markets professionalize (DWF Ventures' "financial derivatives asset class" framing), metrics like LOX become the foundation for adaptive market-making, leverage pricing, and platform-level surveillance. For Dekant, the continuous-distribution setting opens an even richer LOX-equivalent space · lateness can be measured locally or against the entire belief curve.

## Related Concepts

- Adverse selection · LOX is a quantitative measure of adverse-selection severity
- Market making · high LOX requires wider inventory-aware spreads
- Liquidity provision · LPs use LOX as a toxicity signal
- Information aggregation · LOX measures *how fast* aggregation happens
- Insider trading · informed traders create high LOX
- Price discovery · LOX measures the lag in discovery
- Temporal arbitrage · LOX is conceptually adjacent (both measure timing-driven edge)

## Sources

- [The Option Value of Waiting in Prediction Markets](https://x.com/0xnagu/article/2016620973391564951) — 0xnagu · X

