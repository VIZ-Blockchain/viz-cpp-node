# Minimum Viable Liquidity

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Minimum Viable Liquidity](https://www.pmatlas.xyz/concepts/minimum-viable-liquidity)

---

## Definition

**Quick definition.** The threshold of trading volume a prediction market needs to attract informed traders who can correct mispricing. Adhi Rajaprabhakaran formalizes it as **MVL = Cost of Expertise / Price Gap**. Beyond this threshold, additional liquidity does not improve forecast accuracy · implying platforms should optimize for breadth, not depth.

## Key Insights

- Rajaprabhakaran's empirical anchor: **149 quality-filtered Kalshi CPI events from June 2021 through January 2026** (177 total, 28 dropped for insufficient strike density). Trading volume explains **<1% of variance in forecast accuracy** (slope = −0.063, p=0.28, R²=0.008, 95% CI [-0.176, 0.05]). Volume ranged from 7 contracts to 11.4M contracts · a **1.6 million-fold spread**. Median absolute forecast error: 0.093pp. Mean: 0.103pp. 86% of outcomes fell within two standard deviations (vs. theoretical 95% · slight underconfidence).
- Rajaprabhakaran's MVL formula: **MVL = Cost of Expertise / Price Gap**. Below this floor, the bounty isn't large enough to attract anyone with real information and the price is just noise; above it, additional liquidity doesn't improve accuracy · it just raises the cost the next informed trader has to pay to correct a mispricing.
- CPI volume timeline: 2022 peak 250k-480k contracts/event when CPI hit 9.1% inflation; "dog days" mid-2024 fell as low as 12,000 contracts/event when CPI fell out of the news cycle; April 2025 Robinhood-integration spike hit 11.4M contracts (816× the dog-days trough). Across the entire range, MAE actually *improved* (0.131pp in 2022 → 0.074pp post-Robinhood) · a **learning curve, not a liquidity curve**.
- Rajaprabhakaran's metaphor: casual traders' losses were "venture-capital seed funding" for forecasters to build CPI pipelines. Once the sharp traders had built infrastructure (BLS methodology, component data, seasonal adjustment models), marginal cost of expertise collapsed and MVL collapsed with it. A few thousand dollars per event sustained world-class forecasts. The author was personally one of the BLS "super-users" who received the leaked inflation email.
- Strategic implication: platforms should run **many thin markets** rather than concentrating volume in a few. "100,000 thin markets covering 100,000 questions produces more social and informational value than one deep market on the presidential election or the Super Bowl. The winning platform won't necessarily be the one with the deepest order books. It will be the one that asks the most questions."
- Subsidy implication: "Subsidies, where they're needed, may only need to be temporary" · venture-capital seed funding for information production, not an ongoing operating expense. CPI needed startup liquidity in 2022 but now self-sustains.
- Hall & Paschal echo at scale: **98.7% of political prediction markets are "ghost towns"** with wide spreads and no counterparty, yet overall calibration still clusters near the line · confirming MVL's prediction that accuracy and depth decouple above a low threshold.
- functionSPACE V2: split Polymarket's 18,863 multi-market events into **continuous (price brackets, weather, margin %)** vs. **categorical (teams, candidates)** and re-ran pathology tests. Both concentrate 90% of volume in top 5–6 markets, but **ghost markets are largely a categorical phenomenon**. Continuous events distribute volume more evenly and survive the liquidity cliff longer at high N. Continuous events overtook categorical by event count in **2026 Q1** · making the case for continuous-distribution primitives more urgent.
- Melee: Kalshi's 23 active MMs with top three providing 70% of election-contract liquidity means MVL in practice is **the threshold those specific firms decide to quote into**. Any market they ignore is dead on arrival.
- Vaidik Mandloi: Polymarket's headline Brier score of 0.047 masks category failures · **sports markets score 0.325 (worse than coin flip)**, and 99% of volume is in the final hours. PMs only "work" on **roughly 2% of listed contracts**. When CNN/WSJ broadcast illiquid market odds, whale trades on thin books get laundered through credible newsrooms.
- alexjaniak ("Where Are All the Decision Markets?"): decision/futarchy markets fail precisely because they can't reach MVL · they're idiosyncratic, and token-price-as-KPI is too noisy to incentivize rational trading. Two structural problems: (1) informed-trader scarcity · at private companies almost all relevant context is private, would require "open book" disclosure to be tractable; (2) the conditional-futures architecture means traders predicting a decision's *causal* effect can still lose money if the token moves for unrelated reasons. "The protocol is shorting its own success metric" · if KPI improves, token holders redeem at a higher value, conflict with the goal.
- alexjaniak's proposed MVL fixes: (1) AI forecasters as AMMs to drop the MVL floor; (2) return to combinatorial markets on the question directly ("Will this KPI increase if we take this decision?") rather than conditional futures on a noisy KPI.
- functionSPACE ("Information as Supply"): the cost of *producing* real-time probability estimates is collapsing, so PM TAM should be measured by supply (decisions that benefit from forecasts) not demand (trading volume). Scaling to $1T requires **massive breadth in long-tail markets**, not concentrated depth.

## Notable Quotes

> "Trading volume explains less than 1% of variance in forecast accuracy."  
> — *Rajaprabhakaran*

> "Platforms should prioritize breadth over depth, running many thin markets rather than concentrating volume in few contracts."  
> — *Rajaprabhakaran*

> "Prediction markets only work on roughly 2% of listed contracts."  
> — *Vaidik Mandloi*

> "Prediction markets are the cheapest mechanism humanity has ever built for buying information about the future. A few thousand dollars can produce a forecast that outperforms million-dollar polling operations."  
> — *Rajaprabhakaran*

> "The information doesn't require deep liquidity. It requires the right people, the right questions, and just enough money to make it worth their while."  
> — *Rajaprabhakaran*

> "98.7% qualify as ghost towns: wide spreads, almost no one on the other side of the trade. Yet the overall calibration of these markets still clusters near the line."  
> — *Hall & Paschal, cited by Rajaprabhakaran*

## Where It Matters

MVL is the most important strategic-product concept for prediction-market platforms in 2026 because it inverts the dominant assumption. If accuracy plateaus at a relatively low liquidity threshold, the right product strategy is to launch *many small markets* with just-enough MM subsidy to clear MVL, rather than concentrating capital into a handful of high-volume contracts that don't get any more accurate from the marginal dollar. This is the structural argument for permissionless market creation, long-tail markets, AI-generated forecasts, and cross-subsidization across categories.

## Related Concepts

- **Liquidity provision** · MVL is the binding threshold below which liquidity is wasted.
- **Long-tail markets** · the geometry that makes breadth strategically valuable.
- **Liquidity fragmentation** · what makes hitting MVL harder when many binaries share a question.
- **Forecasting accuracy / calibration / Brier score** · the dependent variable MVL governs.
- **Decision markets / futarchy** · failure mode when MVL is unreachable.
- **Market making** · supplies the MVL threshold.

## Sources

- [Where Are All the Decision Markets?](https://www.lesswrong.com/posts/bDxqMn3GJEhPeqZey/where-are-all-the-decision-markets) — alexjaniak · May 12, 2026
- [Binary Events V2: Does Liquidity Trade The Tails?](https://x.com/functionspaceHQ/status/2048536153662636173) — functionSPACE · Apr 27, 2026
- [The Problem With CLOBs](https://x.com/meleemarkets/status/2046318159225897289) — Melee · Apr 21, 2026
- [Polymarket Is Not a Truth Machine](https://www.thetokendispatch.com/p/polymarket-is-not-a-truth-machine) — Vaidik Mandloi · Apr 11, 2026
- [Information as Supply](https://x.com/functionspaceHQ/article/2035959494728176075) — functionSPACE · Mar 23, 2026
- [Minimum Viable Liquidity](https://fiftycentdollars.substack.com/p/minimum-viable-liquidity) — Adhi Rajaprabhakaran · Feb 24, 2026

