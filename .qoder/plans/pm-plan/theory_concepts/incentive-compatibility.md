# Incentive Compatibility

**Category:** Mechanism Design  
**Source:** [PM Atlas — Incentive Compatibility](https://www.pmatlas.xyz/concepts/incentive-compatibility)

---

## Definition

Designing mechanisms where truthful participation is each player's optimal strategy regardless of others' actions. In prediction markets, this is the mathematical condition that price-revelation is in every trader's interest · the formal underpinning for why markets aggregate information at all.

## Key Insights

- Truth-telling is the *dominant* strategy under proper scoring rules · this is what makes LMSR work. A forecaster maximizing expected reward must report her true probability estimate. This is the canonical incentive-compatibility result in the prediction-market literature (Baheet).
- Srinivasan, Karger & Chen (SKC, 2023) extended incentive compatibility to *unverifiable* outcomes. From the paper's abstract: "We present a novel incentive-compatible prediction market mechanism to elicit and efficiently aggregate information from a pool of agents without observing the outcome, by paying agents the negative cross-entropy between their prediction and that of a carefully chosen reference agent." The reference agent is "the final agent ... chosen as the reference agent since they observe the full history of market forecasts."
- SKC's formal result: "It is a perfect Bayesian equilibrium (PBE) for all agents to report truthfully in our mechanism and to believe that all other agents report truthfully." The mechanism applies to verifiable outcomes too, but is "primarily of interest for unverifiable outcomes."
- The SKC mechanism uses *random termination* of the market · Monad's explainer notes "no one can be sure they're the final trader or how many more will come after them, which, as discussed in the paper, helps keep everyone honest." The last k agents receive a fixed reward R as a participation prize; all others are paid the delta of cross-entropy against `q(T)`.
- Monad's payoff intuition: "An early trader who nudges the number from 40 → 50% (a 10-point improvement) gets more than a late trader who nudges 60 → 61%, even if the late trader's number is 'more accurate' in an absolute sense. This design choice prevents free-riding. Without it, someone could wait until the market has converged, copy that near-final number, and collect a prize for 'accuracy' without adding insight."
- Monad explicitly ties SKC's design to the Keynesian beauty contest problem: "In this mechanism you don't get paid for matching what you think others will say. Instead you get paid for how much your move pushes the market toward where it ultimately ends."
- Yiling Chen & David Pennock's Harvard survey (Jan 2025) names incentive compatibility, computational tractability, and individual rationality as the three common objectives prediction mechanisms share, alongside the unusual properties of *expressiveness* and *liquidity*. They note: "A pure prediction mechanism may reasonably operate at a loss; maximizing revenue or even balancing the budget may not be a concern. If the operator wants information, she may be perfectly happy to pay for it."
- Chen & Pennock formalize incentive compatibility as: "every agent's best strategy is to honestly report all of their information as soon as they have it, an important property that's difficult to achieve in general."
- Incentive compatibility is *not* automatic in prediction markets · it requires a specific scoring rule (logarithmic, quadratic, spherical) and breaks down under collusion, manipulation, and information asymmetry.
- aaron (On War Markets) argues incentive compatibility on conflict markets is structurally broken by moral hazard: the IDF insider trading case shows participants have incentives to *create* the outcome they bet on, not just predict it. The information value is real but the moral-hazard tax is underpriced.
- Guillory & Zimmermann's "Assassination Semantics" piece is essentially an incentive-compatibility critique: when a market embeds violence as a resolution path, blanket void-on-death rules can flip incentives toward causing harm to recover bets.
- JP's "Ahead of the Headlines" frames prediction markets as a real-time information layer that complements journalism · skin-in-the-game accountability is the incentive-compatibility argument at the platform level. JP's framing: "There's no reward for confidently being wrong. There's only cost." He acknowledges COVID-19 as a case where the mechanism failed ("Markets are not omniscient. They are human").
- Incentive compatibility connects to the *governance* of the market: who chooses resolution criteria, who runs the oracle, and whether platforms (Kalshi vs Polymarket) have different incentives to list manipulable contracts.
- Baheet's punchline: prediction market builders need economists and game-theory experts on their teams · incentive compatibility is not a UX problem.

## Notable Quotes

> "It is a perfect Bayesian equilibrium (PBE) for all agents to report truthfully in our mechanism and to believe that all other agents report truthfully."  
> — *Srinivasan, Karger & Chen, *Self-Resolving Prediction Markets for Unverifiable Outcomes**

> "Our key insight is that a reference agent with access to more information can serve as a reasonable proxy for the ground truth."  
> — *SKC, *ibid.**

> "An early trader who nudges the number from 40 → 50% (a 10-point improvement) gets more than a late trader who nudges 60 → 61%, even if the late trader's number is 'more accurate' in an absolute sense."  
> — *michaellwy, *Explainer on Self-Resolving Prediction Markets**

> "Every agent's best strategy is to honestly report all of their information as soon as they have it, an important property that's difficult to achieve in general."  
> — *Chen & Pennock, *Designing Markets for Prediction**

> "Truth-telling is the dominant strategy through incentive compatibility."  
> — *Baheet, *The Game Theory Behind Prediction Markets**

> "There's no reward for confidently being wrong. There's only cost."  
> — *JP, *Ahead of the Headlines**

## Where It Matters

Incentive compatibility is the load-bearing assumption behind every claim that prediction markets are good for information aggregation. If it fails · through manipulation, collusion, reflexivity, or moral hazard · the entire epistemic case collapses. Every new mechanism in the space (SKC for unverifiable outcomes, Trepa's orthogonal precision, hyperstition markets, futarchy) is, at root, a claim about restoring or extending incentive compatibility into a new regime. For continuous-outcome markets like Dekant, the question is whether the kernel-aggregation step preserves incentive-compatible reporting across the distribution.

## Related Concepts

- Proper scoring rules · the formal mechanism class that delivers incentive compatibility
- Self-resolving markets · incentive compatibility without an oracle
- Peer prediction · incentive compatibility via inter-respondent comparison
- LMSR · the most-deployed incentive-compatible market maker
- Market manipulation · the failure mode of incentive compatibility
- Information aggregation · the consequence when incentive compatibility holds
- Decision markets · incentive compatibility under endogenous outcomes

## Sources

- [On War Markets](https://x.com/probaaron/article/2027765452689014822) — aaron · X
- [Ahead of the Headlines: Prediction Markets and the Collective Mind](https://jprz1321.substack.com/p/ahead-of-the-headlines-prediction) — JP · Substack
- [Assassination Semantics: Why Every Market Carries the Risk of Violence](https://x.com/BetBreakNews/article/2008587654158340126) — Sean Guillory & Dan Zimmermann · X
- [Explainer on Self-Resolving Prediction Markets](https://blog.monad.xyz/blog/self-resolving-prediction-markets) — michaellwy · Monad Blog
- [The Game Theory Behind Prediction Markets](https://x.com/Baheet_/status/1965758390430208066) — Baheet · X
- [Self-Resolving Prediction Markets for Unverifiable Outcomes](https://arxiv.org/abs/2306.04305) — Siddarth Srinivasan, Ezra Karger, Yiling Chen · arXiv

