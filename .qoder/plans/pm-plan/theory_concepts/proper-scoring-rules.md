# Proper Scoring Rules

**Category:** Mechanism Design  
**Source:** [PM Atlas — Proper Scoring Rules](https://www.pmatlas.xyz/concepts/proper-scoring-rules)

---

## Definition

Incentive-compatible functions that reward forecasters most when they report their true beliefs honestly. The mathematical foundation underneath every market scoring rule, LMSR, and self-resolving market mechanism in the prediction-market literature.

## Key Insights

- A scoring rule `S(p, x)` is *proper* if a forecaster maximizing expected score is required to report her true subjective probability `p` · any deviation strictly reduces expected reward. This is the formal condition for incentive compatibility under truthful reporting.
- The **logarithmic** scoring rule `S(p, x) = log p_x` is the canonical proper scoring rule for prediction markets. It is the *only* proper scoring rule that is both local and additive · the property that makes LMSR's cost function tractable.
- Other classic proper scoring rules: quadratic (Brier), spherical, beta. Each makes different trade-offs between risk sensitivity, calibration weight, and tail behavior.
- Chen & Pennock's Harvard survey (Jan 2025) is the canonical academic reference and frames scoring rules as "the simplest prediction mechanism ... a payment to a single expert in return for her information. The payment amount depends on the expert's prediction and the actual outcome in a way that motivates the expert to be honest." Their objectives hierarchy: expressiveness, liquidity, incentive compatibility, computational tractability, individual rationality.
- The Srinivasan, Karger & Chen (2023) result extends proper scoring rules into the *unverifiable outcome* regime: rewards use the last reporter's prediction as a reference point with the *negative cross-entropy market scoring rule* between each agent's report and the final consensus `q(T)`. From the abstract: "Truthful reporting is a perfect Bayesian equilibrium ... Although primarily of interest for unverifiable outcomes, this design is also applicable for verifiable outcomes."
- Monad's explainer of SKC names the mechanism as "delta-based scoring": "the payoff is not based on your final absolute closeness to the reference number, it's based on your marginal improvement toward it." Formally: each agent's payment is the cross-entropy delta `S(reportᵢ, q(T)) - S(reportᵢ₋₁, q(T))` · exactly the proper-scoring-rule market mechanism shifted from ground truth to consensus.
- Powell, Hanson, Laskey & Twardy (2013) · the DAGGRE combinatorial-markets experiment · used LMSR (which inherits properness from the log scoring rule) to enable bets on conditional events ("A given B") and Boolean combinations. Result: greater expressivity from a proper-scoring-rule foundation improved both accuracy and calibration vs flat structures.
- Baheet's game-theory thread: builders need economists and game-theory experts. LMSR works *because* it's a proper scoring rule; truth-telling is the dominant strategy. This is not a UX problem.
- The Trepa paper (Blanco, Chung & Meka 2026) is a critique-and-extension of standard proper-scoring-rule *contests*. They prove formally that with γ=6 (steep accuracy weights) and a median-error cutoff, "the unique symmetric equilibrium satisfies w = 0 (pure herding) whenever the risk-aversion coefficient ρ exceeds a finite threshold." Their fix: an orthogonal precision multiplier `Uᵢ = A(eᵢ)·(1 + λDᵢ)` where `Dᵢ = |xᵢ - E[x̄_-i | Iᵢ]|` rewards forecasts that are both accurate *and* decorrelated from consensus.
- The Trepa paper's contribution explicitly distinguishes between *first-order* properness (against ground truth, classical) and *second-order* properness (against the contest itself): "Section 11 compares Trepa's mechanism with proper scoring rules." Their construction is a tunable second-order oracle that elicits private information while retaining the rank-based incentive structure.
- Trepa shows there's a phase transition: only when `λ > λc = κ/C` does an interior equilibrium with `w∗ > 0` emerge. Below the threshold, even a small orthogonality bonus is dominated by the herding pressure created by risk aversion + the median cutoff.
- The Trepa mechanism is significant for prediction-market design because it shows the standard proper scoring rule is not enough to elicit private information in *contest* (vs. market) settings, and that second-order corrections (rewarding orthogonality, not just accuracy) are needed.
- Proper scoring rules generalize naturally to continuous outcomes via the *continuous ranked probability score* (CRPS), which is what Dekant's continuous market settlement is structurally related to · though not explicitly cited in the on-page corpus.

## Notable Quotes

> "Truthful reporting is a perfect Bayesian equilibrium."  
> — *Srinivasan, Karger & Chen, *Self-Resolving Prediction Markets for Unverifiable Outcomes**

> "Markets resolve using crowd consensus as the outcome, with delta-based scoring rewarding participants for moving markets toward final consensus."  
> — *michaellwy, *Explainer on Self-Resolving Prediction Markets**

> "The simplest prediction mechanism is a scoring rule, or payment to a single expert in return for her information. The payment amount depends on the expert's prediction and the actual outcome in a way that motivates the expert to be honest."  
> — *Chen & Pennock, *Designing Markets for Prediction**

> "For λ = 0 and ρ > 0, the unique symmetric equilibrium satisfies w = 0 (pure herding) whenever the risk-aversion coefficient ρ exceeds a finite threshold."  
> — *Blanco, Chung & Meka, *Orthogonal Precision in Trepa* (Theorem 3.1, KBC Equilibrium)*

> "The orthogonal precision multiplier ... rewards accurate forecasts decorrelated from the consensus, transforming Trepa into a tunable second-order oracle."  
> — *Blanco, Chung & Meka, *ibid.**

## Where It Matters

Proper scoring rules are *the* mathematical primitive of mechanism design for prediction. Every market scoring rule (including LMSR), every self-resolving mechanism, and every forecasting-contest reward function is a proper scoring rule in some dress. The frontier work is in extending properness: into unverifiable outcomes (SKC), into contests where reporters can collude (Trepa orthogonal precision), and into continuous outcomes (CRPS, the unstated cousin behind Dekant's settlement math).

## Related Concepts

- LMSR · the cost-function dual of the log scoring rule
- Market scoring rules · the class LMSR belongs to
- Incentive compatibility · properness is the formal condition
- Self-resolving markets · built on proper scoring rules without an oracle
- Peer prediction · proper scoring rule for inter-respondent comparison
- Keynesian Beauty Contest · the equilibrium that breaks naive proper-scoring contests
- Information aggregation · proper scoring rules are the engine
- Combinatorial prediction markets · exploit log-rule decomposability
- Forecasting accuracy · what proper scoring rules optimize for
- Oracle design · proper scoring rules underpin many oracle proposals
- Reflexivity · properness can fail under reflexive settings

## Sources

- [Orthogonal Precision in Trepa: A Tunable Second-Order Oracle for High-Frequency Forecasting](https://github.com/TrepaOrg/trepa-research/blob/main/Trepa_Orthogonal_Precision_20260513_Blanco%26Chung%26Meka.pdf) — Ilich Blanco, Jong-Chan Chung, Leon Meka · GitHub
- [Explainer on Self-Resolving Prediction Markets](https://blog.monad.xyz/blog/self-resolving-prediction-markets) — michaellwy · Monad Blog
- [The Game Theory Behind Prediction Markets](https://x.com/Baheet_/status/1965758390430208066) — Baheet · X
- [Designing Markets for Prediction](https://bpb-us-e1.wpmucdn.com/sites.harvard.edu/dist/b/845/files/2025/01/aim10.pdf) — Yiling Chen, David M. Pennock · Harvard
- [Self-Resolving Prediction Markets for Unverifiable Outcomes](https://arxiv.org/abs/2306.04305) — Siddarth Srinivasan, Ezra Karger, Yiling Chen · arXiv
- [Combinatorial Prediction Markets: An Experimental Study](https://link.springer.com/chapter/10.1007/978-3-642-40381-1_22) — Powell, Hanson, Laskey & Twardy · Springer

