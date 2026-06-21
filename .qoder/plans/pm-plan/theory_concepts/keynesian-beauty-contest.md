# Keynesian Beauty Contest

**Category:** Mechanism Design  
**Source:** [PM Atlas — Keynesian Beauty Contest](https://www.pmatlas.xyz/concepts/keynesian-beauty-contest)

---

## Definition

A dynamic in forecasting contests where participants predict *what others will predict* rather than their true belief, causing private information to be underweighted and forecasts to converge on the public consensus. Named for Keynes's 1936 metaphor of newspaper beauty contests where readers vote not for the prettiest face but for the face they think others will vote for.

## Key Insights

- The Keynesian beauty contest is the failure mode of forecasting-contest design: if rewards depend heavily on *closeness to the consensus*, rational participants will herd to consensus rather than report private signals, and the contest aggregates *less* information than a market would.
- Blanco, Chung & Meka (May 2026) · *Orthogonal Precision in Trepa* · is the only article on the on-page corpus, but it is foundational. The paper's framing: "Under rank-order tournaments · where rewards depend on relative performance rather than absolute accuracy · the equilibrium can shift from fundamental information revelation to second-order expectations: agents forecast what others will forecast, a dynamic Keynes famously likened to a beauty contest."
- **Trepa as case study:** "Trepa, a one-minute numerical forecasting game on Solana, operates precisely such a contest. Participants pay a fixed entry fee (1 USDC), submit an estimate of the BTC price 30 seconds hence, and are rewarded based on how close their estimate is to the true outcome · but only if they beat the median error. The combination of a median cutoff, steep accuracy weights (γ = 6), and risk aversion pushes the equilibrium toward herding: optimal predictions anchor on the public consensus and deviate only timidly toward private signals."
- The protocol mechanics: prize distribution uses a water-filling allocation with accuracy weight `aᵢ = 1/(1+rᵢ)^γ` where `rᵢ = eᵢ/m` and `γ = 6`. A per-winner profit cap C = 100 F (100 USDC gain on 1 USDC entry) prevents extreme concentration. Trepa is "backed by Colosseum and Balaji Srinivasan."
- **Diagnosis:** Theorem 3.1 in Trepa proves: "For λ = 0 and ρ > 0, the unique symmetric equilibrium satisfies w = 0 (pure herding) whenever the risk-aversion coefficient ρ exceeds a finite threshold. For lower risk aversion, w is positive but small, bounded above by a value that vanishes as the curvature γ of the accuracy weight increases."
- **Their fix · the orthogonal precision multiplier:** rewards forecasts that are both accurate *and decorrelated from the consensus*. The augmented utility: `Uᵢ = A(eᵢ)·(1 + λDᵢ)` where `Dᵢ = |xᵢ - E[x̄_-i | Iᵢ]| = w|sᵢ - E[Y|sᵢ]|`. "This distance rewards a report only if it deviates from what the rest of the crowd is expected to report, given the same information. It naturally vanishes under pure herding (w = 0) and increases linearly with the weight on private signal."
- **Phase transition (Theorem 4.1):** "For λ > λc, an interior equilibrium emerges with w∗ > 0 increasing in λ, bounded above by wmax = E[|sᵢ−θ|]/(E[|sᵢ−θ|]+noise) < 1." The protocol "does not need to reward diversity at all times; a minimal incentive is required to overcome the herding barrier created by risk aversion and the median cutoff."
- **Information gain via mutual information:** Signal Extraction Rate `SER = I(Y_{t+Δ}; x̃_t) - I(Y_{t+Δ}; Y_t)` quantifies what Trepa's consensus reveals about future prices beyond the spot price. Theorem 5.1: "for any λ > λc (i.e., w∗ > 0), SER > 0. That is, the orthogonal precision mechanism extracts predictive information that complements the spot oracle."
- **Anti-collusion design:** the modified diversity multiplier uses conditional information contribution `∆Iᵢ` thresholded at small τ. Proposition 6.1: "Any coalition of M agents with perfectly correlated reports receives the same total orthogonality bonus as a single independent agent. The marginal benefit of adding another correlated member is zero."
- **Instability of perfect herding:** "Because ties lose, perfect herding is not an absorbing state. Any agent has an incentive to deviate infinitesimally to break the tie, creating a structural tension that reinforces the need for orthogonal precision as a stabilizing mechanism."
- The paper proves equilibrium existence via potential game theory: `Φ(x) = -Σᵢϕ(eᵢ) + λΣᵢψ(|xᵢ-x̄_-i|) - (κ/2)Σᵢ(xᵢ-θ)²` is continuously differentiable, concave, with compact strategy set · guaranteeing a pure Nash equilibrium.
- The orthogonal precision concept generalizes: any forecasting mechanism that uses inter-participant comparison (peer prediction, self-resolving markets, prediction-contest scoring) is susceptible to Keynesian-beauty-contest equilibria and needs a second-order correction to elicit private signal.
- The connection to **reflexivity** is direct: when forecasters anticipate consensus, the consensus becomes self-fulfilling · a *forecaster-level* reflexivity that compounds with any market-level reflexivity.
- The connection to **proper scoring rules**: standard proper scoring rules elicit truth-telling *against ground truth*, but in contests where the scoring is *relative to peers* (median error, rank order), properness against ground truth doesn't imply properness against the contest. Hence the need for orthogonal-style corrections.
- For **prediction-market design**: the beauty-contest dynamic is one reason markets · where each trader takes capital risk against the consensus · can outperform contests as information aggregators. Market participants are paid to *deviate* from consensus when they think consensus is wrong.
- Open question: how to apply orthogonal-precision corrections to *market* mechanisms (vs. contest mechanisms), where the structure already partially incentivizes deviation.

## Notable Quotes

> "Forecasting contests with rank-order payouts risk collapsing into pure Keynesian beauty contests (KBC): participants report the crowd's expectation, extinguishing independent signals."  
> — *Blanco, Chung & Meka, *Orthogonal Precision in Trepa**

> "The combination of a median cutoff, steep accuracy weights (γ = 6), and risk aversion pushes the equilibrium toward herding: optimal predictions anchor on the public consensus and deviate only timidly toward private signals."  
> — *Blanco, Chung & Meka, *ibid.**

> "For λ = 0 and ρ > 0, the unique symmetric equilibrium satisfies w = 0 (pure herding) whenever the risk-aversion coefficient ρ exceeds a finite threshold."  
> — **ibid.* (Theorem 3.1)*

> "This tunability is the core operational insight."  
> — *Blanco, Chung & Meka on the phase transition at λc*

> "Any coalition of M agents with perfectly correlated reports receives the same total orthogonality bonus as a single independent agent. The marginal benefit of adding another correlated member is zero."  
> — **ibid.* (Proposition 6.1)*

## Where It Matters

The Keynesian beauty contest is the design hazard underneath every relative-scoring mechanism: forecasting contests, prediction tournaments, peer prediction, self-resolving markets, even contest-style oracles. As crypto-native prediction infrastructure expands beyond pure CLOB markets into contest-based oracles (Trepa) and self-resolving mechanisms (SKC), the beauty-contest pathology becomes the central design obstacle. The Trepa orthogonal-precision construction is the most direct on-page solution and likely a primitive that other mechanisms will reuse.

## Related Concepts

- Reflexivity · Keynesian beauty contest is the forecaster-level analog
- Proper scoring rules · standard rules elicit truth against ground truth; beauty contests break this in contest settings
- Information aggregation · beauty-contest equilibria *reduce* aggregation efficiency
- Oracle design · contest-based oracles need orthogonality corrections
- Self-resolving markets · peer-prediction-style mechanisms susceptible to herd equilibria
- Peer prediction · directly vulnerable to beauty-contest dynamics
- LOX (log-odds excess lateness) · measures one form of forecaster lag that connects to herding

## Sources

- [Orthogonal Precision in Trepa: A Tunable Second-Order Oracle for High-Frequency Forecasting](https://github.com/TrepaOrg/trepa-research/blob/main/Trepa_Orthogonal_Precision_20260513_Blanco%26Chung%26Meka.pdf) — Ilich Blanco, Jong-Chan Chung, Leon Meka · GitHub

