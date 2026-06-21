# Peer Prediction

**Category:** Mechanism Design  
**Source:** [PM Atlas — Peer Prediction](https://www.pmatlas.xyz/concepts/peer-prediction)

---

## Definition

Methods for eliciting honest subjective reports by comparing respondents against each other statistically. When there is no ground truth to score against, peer-prediction mechanisms use the *correlation structure between reporters* to incentivize truth-telling.

## Key Insights

- Peer prediction is the mechanism-design answer to the *unverifiable outcome* problem: how do you reward honest reporting when there's nothing to verify the report against?
- The canonical formulation (Miller, Resnick, Zeckhauser 2005, not on-page but background): each reporter's payment depends on the reports of a reference peer, with a Bayesian-truth-serum-like scoring rule that makes truthful reporting a Bayesian-Nash equilibrium.
- Yiling Chen & David Pennock's Harvard survey (Jan 2025) is the on-page canonical reference, treating peer prediction systems as a distinct class alongside scoring rules and market scoring rules. Their framing: "Prediction markets elicit forecasts for events with a clear, objective outcome that can be reliably discerned after the fact ... Many information-aggregation tasks do not conform to this requirement, either because the outcome is subjective · the quality of a movie · or unmeasurable · the extinction of the human race. Peer prediction systems operate by evaluating each agent's prediction not against an objective reality but against the other agents' predictions."
- Chen & Pennock's key incentive claim: "Remarkably, under certain conditions such systems can induce truth telling in equilibrium, meaning that if others are playing honestly the best response is to play honestly as well, yielding aggregate assessments of subjective or unmeasurable outcomes."
- Srinivasan, Karger & Chen (SKC, 2023) · *Self-Resolving Prediction Markets for Unverifiable Outcomes* · extends peer-prediction logic into a *market* (vs. survey) setting. From the abstract: "We present a novel incentive-compatible prediction market mechanism to elicit and efficiently aggregate information from a pool of agents without observing the outcome, by paying agents the negative cross-entropy between their prediction and that of a carefully chosen reference agent."
- The SKC reference-agent insight: "A reference agent with access to more information can serve as a reasonable proxy for the ground truth ... The final agent is chosen as the reference agent since they observe the full history of market forecasts, and thus have more information by design." This is the bridge from peer prediction (any peer) to self-resolving markets (the *most informed* peer).
- Random termination is the SKC mechanism's anti-strategic-timing device. Monad's explainer: "this random termination scheme makes the length of the game unpredictable. No one can be sure they're the final trader or how many more will come after them, which, as discussed in the paper, helps keep everyone honest."
- Monad's reward structure summary: "All participants except the last k agents are rewarded based on how close their reported probability was to q(T). The specific rule is the negative cross-entropy market scoring rule." The last k agents receive a fixed reward R as a participation prize.
- The signature trade-off in peer-prediction mechanisms: they only work if reporters' beliefs are *correlated through the underlying signal*. If reporters can collude or coordinate on a non-truth-correlated equilibrium, the mechanism breaks.
- Peer prediction generalizes beyond prediction markets into peer review, content moderation, crowdsourced labeling, and DAO governance · anywhere subjective reports need incentive-compatible elicitation.
- The relationship to proper scoring rules: peer prediction *is* a proper scoring rule, but the "outcome" is another reporter's report rather than a ground-truth event. This shifts the equilibrium analysis from individual best-response to Bayesian-Nash among reporters.
- For prediction markets specifically, peer prediction is the foundation of every "self-resolving" market mechanism · the SKC paper is the bridge from peer-prediction theory to deployable on-chain markets.
- Open practical questions: minimum number of reporters required for incentive compatibility (typically n ≥ 3), robustness to Sybil attacks (a single attacker controlling multiple reporters breaks the mechanism), and how to handle uninformed reporters who report priors rather than signals.

## Notable Quotes

> "Peer prediction systems operate by evaluating each agent's prediction not against an objective reality but against the other agents' predictions. Remarkably, under certain conditions such systems can induce truth telling in equilibrium."  
> — *Chen & Pennock, *Designing Markets for Prediction**

> "We present a novel incentive-compatible prediction market mechanism to elicit and efficiently aggregate information from a pool of agents without observing the outcome, by paying agents the negative cross-entropy between their prediction and that of a carefully chosen reference agent."  
> — *Srinivasan, Karger & Chen, *Self-Resolving Prediction Markets for Unverifiable Outcomes**

> "A reference agent with access to more information can serve as a reasonable proxy for the ground truth."  
> — *SKC, *ibid.**

> "Markets resolve using crowd consensus as the outcome, with delta-based scoring rewarding participants for moving markets toward final consensus."  
> — *michaellwy, *Explainer on Self-Resolving Prediction Markets**

## Where It Matters

Peer prediction is the *only* mechanism class that lets prediction markets cover questions without ground truth · opinion markets, taste markets, subjective forecasts, "is this true?" markets about contested information. Polymarket and Kalshi avoid these entirely (they require verifiable resolution); the next wave of prediction-market design (self-resolving markets, on-chain truth markets) is being built on peer-prediction foundations. The SKC mechanism is the most likely on-chain instantiation.

## Related Concepts

- Self-resolving markets · the deployment surface for peer prediction
- Proper scoring rules · the formal foundation
- Incentive compatibility · the property peer prediction delivers
- LMSR · peer prediction is a sibling, not a subset
- Information aggregation · peer prediction is for aggregating *subjective* information

## Sources

- [Explainer on Self-Resolving Prediction Markets](https://blog.monad.xyz/blog/self-resolving-prediction-markets) — michaellwy · Monad Blog
- [Designing Markets for Prediction](https://bpb-us-e1.wpmucdn.com/sites.harvard.edu/dist/b/845/files/2025/01/aim10.pdf) — Yiling Chen, David M. Pennock · Harvard

