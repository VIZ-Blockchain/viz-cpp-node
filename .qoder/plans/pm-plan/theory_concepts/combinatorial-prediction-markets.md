# Combinatorial Prediction Markets

**Category:** Mechanism Design  
**Source:** [PM Atlas — Combinatorial Prediction Markets](https://www.pmatlas.xyz/concepts/combinatorial-prediction-markets)

---

## Definition

Prediction markets that allow participants to place bets on *conditional events* and *Boolean combinations* of base events, not just individual outcomes. By enabling richer betting vocabularies, they elicit joint probability distributions over related events rather than only marginal probabilities.

## Key Insights

- **The canonical reference:** Powell, Hanson, Laskey & Twardy (2013), *Combinatorial Prediction Markets: An Experimental Study* · the only article cited on the on-page corpus for this concept, but it is dense and foundational.
- The paper describes the **DAGGRE** combinatorial prediction market · an actual platform built for the IARPA forecasting tournament, not just a theoretical proposal. The experiment used a Bayesian network to generate the "ground truth" scenario and provide "gold standard" probabilities to compare against.
- The experimental setup: participants were "challenged to solve a 'whodunit' murder mystery by using a prediction market to arrive at group consensus probabilities for characteristics of the murderer, and to update these consensus probabilities as clues were revealed." This is a controlled environment where joint probabilities can be compared against the Bayes-net ground truth.
- Direct from the abstract: "Economic theory suggests that the greater expressivity of combinatorial prediction markets should improve accuracy by capturing dependencies among related questions" · and the paper provides empirical support comparing combinatorial vs flat market structures.
- **Co-authored by Robin Hanson**, whose LMSR underpins most automated prediction markets · LMSR is what makes combinatorial markets tractable because the log cost function decomposes naturally over a combinatorial outcome space.
- The combinatorial state space problem: with N base events, there are 2^N possible Boolean combinations, so a naive combinatorial market becomes intractable beyond ~20–30 base events. Tractable subclasses include tournament structures, ordering questions, and conditional-only markets.
- Combinatorial markets are particularly useful for **conditional decision markets**: "What is P(GDP growth | tariff policy)?" requires a joint distribution, which a flat market on each policy separately cannot deliver.
- Conditional prediction markets are the formal grounding of **futarchy** · Hanson's governance proposal that policy decisions be made based on which option prediction markets forecast will produce the best outcomes (covered separately under Governance and Decisions cluster, but the math lives here).
- For *decision-relevant* questions, combinatorial markets are strictly more informative than flat markets · you can answer "is X causal for Y?" by comparing P(Y|X) to P(Y|not-X) directly.
- Practical implementations are rare: MIT's "Yoopick," several SAIC / Consensus Point experiments, the IARPA ACE program (which DAGGRE itself was built for). Most modern prediction-market platforms (Polymarket, Kalshi) do not support combinatorial bets · they list each outcome as an independent binary.
- The on-page corpus is thin for this concept (only 1 article), but the related-concepts graph hints that combinatorial is positioned as a frontier mechanism: connected to market scoring rules, LMSR, proper scoring rules, and forecasting accuracy.

## Notable Quotes

> "Prediction markets produce crowdsourced probabilistic forecasts through a market mechanism in which forecasters buy and sell securities that pay off when events occur. Prices in a prediction market can be interpreted as consensus probabilities for the corresponding events."  
> — *Powell, Hanson, Laskey & Twardy*

> "Combinatorial prediction markets allow forecasts not only on base events, but also on conditional events (e.g., 'A if B') and/or Boolean combinations of events. Economic theory suggests that the greater expressivity of combinatorial prediction markets should improve accuracy by capturing dependencies among related questions."  
> — **ibid.**

> "The experiment challenged participants to solve a 'whodunit' murder mystery by using a prediction market to arrive at group consensus probabilities for characteristics of the murderer, and to update these consensus probabilities as clues were revealed. A Bayesian network was used to generate the 'ground truth' scenario and to provide 'gold standard' [probabilities]."  
> — **ibid.**

## Where It Matters

Combinatorial prediction markets are the formal foundation for *decision-relevant* forecasting · the mechanism that lets markets answer "if we do X, what happens to Y?" rather than just "what will Y be?" This is the missing piece for futarchy and for any prediction-market application where the question is causal/conditional rather than purely descriptive. The category is structurally underbuilt: most modern platforms list flat binaries because combinatorial UI and pricing are hard. For Dekant, the question is whether continuous-outcome markets can express the *conditional* dimension natively · that's an open design problem.

## Related Concepts

- LMSR · makes combinatorial markets computationally tractable via cost-function decomposition
- Market scoring rules · the AMM class combinatorial markets sit inside
- Proper scoring rules · combinatorial markets inherit IC from log-rule decomposability
- Forecasting accuracy · combinatorial markets empirically improve calibration
- Decision markets / futarchy · combinatorial conditional markets are the mathematical foundation
- Multi-outcome markets · combinatorial extends multi-outcome with conditional and Boolean structure
- Information aggregation · combinatorial markets aggregate joint, not just marginal, information

## Sources

- [Combinatorial Prediction Markets: An Experimental Study](https://link.springer.com/chapter/10.1007/978-3-642-40381-1_22) — Powell, Hanson, Laskey & Twardy · Springer

