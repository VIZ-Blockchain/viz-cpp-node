# Self-Resolving Markets

**Category:** Oracle & Resolution  
**Source:** [PM Atlas — Self-Resolving Markets](https://www.pmatlas.xyz/concepts/self-resolving-markets)

---

## Definition

**Quick definition.** Self-resolving markets settle *without* an external oracle. Two distinct families exist: (1) markets where outcome data is intrinsically on-chain or automatically verifiable (asset prices, smart-contract outputs), and (2) markets for *unverifiable* outcomes that resolve to the crowd's own final consensus, using mechanisms like the Srinivasan-Karger-Chen (SKC) cross-entropy scoring rule. The first family avoids the oracle problem; the second redefines it.

## Key Insights

- **Two distinct meanings · both real.** Type 1 = "on-chain ground truth" (the resolution data lives in a smart contract or a verifiable on-chain price feed; no human or oracle needs to opine). Type 2 = "no ground truth exists at all" (the question is fundamentally subjective; the market resolves to its own crowd's terminal consensus). The first solves the oracle problem by eliminating the off-chain step; the second solves it by abandoning the idea of external truth.
- **SKC mechanism** (Srinivasan, Karger, Chen, arXiv 2306.04305, 2023, last revised Feb 2025): proposes a *negative-cross-entropy* scoring rule where every agent except the last few is paid based on how much closer their reported probability is to the *final reporter's* probability. The protocol terminates with probability α after each report, and the last k agents receive a flat fee. The key theorem: under standard rational-agent assumptions, *truthful reporting is a perfect Bayesian equilibrium.*
- **The SKC key insight.** "A reference agent with access to more information can serve as a reasonable proxy for the ground truth." The final reporter has seen the full history of forecasts, so their report is the most informed consensus available. Pay everyone earlier based on how they moved the market toward that consensus.
- **Why pay delta, not absolute closeness?** michaellwy explainer (Monad blog): if you just paid for being close to the final consensus, traders could free-ride · copy the converged number, collect a prize for "accuracy" without adding insight. The delta-based rule rewards *marginal contribution* (how much your move pushed the market toward where it settled), not endpoint proximity.
- **Random stopping prevents endgame manipulation.** Each agent doesn't know if they'll be the last. This breaks the incentive for late agents to "skew their report to influence T's report" · because you don't know if you're T-1. The α parameter is the lever for trading off market length vs. final-agent influence.
- **The flat-fee-for-last-k participants is the anti-gaming patch.** Without it, the agent before the final reporter could manipulate to swing the reference; with it, late agents have no financial stake in influencing the final, so the final report is honestly informed.
- **The Keynesian-beauty-contest fix.** michaellwy: traders in this mechanism don't earn from "matching what others will say" · they earn from *moving* the consensus. So sincere reporting (your true probability given all available information) is the dominant strategy. The mechanism resists the convergence-on-public-anchor pathology that plagues normal forecasting tournaments.
- **What this unlocks** (michaellwy/SKC): markets for *unverifiable* questions , 
- "How safe does new drug X seem right now given today's evidence?"
- "How likely is policy Y to reduce unemployment in the next year?"
- "Is this research claim credible yet?"
- Long-term policy impacts, controversial geopolitical claims, subjective sentiment/cultural trends.
- **The "no human oracle needed" framing oversells slightly.** Type 1 self-resolving (on-chain price feeds) still requires *trusted* on-chain data · and on-chain price oracles have their own well-documented MEV/manipulation attack surface (st1ne lists oracle latency arbitrage and oracle MEV). Type 2 (SKC) eliminates external truth but introduces other coordination/gaming concerns.
- **Turf's sdk.markets parimutuel-with-AI-oracle is a hybrid.** Three resolution modes: single admin, multi-admin consensus, and an AI oracle that resolves from arbitrary URLs. The AI-from-URL mode is functionally "the AI fetches the answer from a specified page" · a *partial* self-resolving design (no human review, but still trusting the model and the page).
- **Self-resolving markets can be reflexive in dangerous ways** (Gunitsky, *Priced to Kill*). When prediction-market prices influence the outcomes they predict · when a "consensus" price *causes* the consensus through media amplification · self-resolution becomes self-reference and the market becomes a propaganda device, not a truth-discovery one. The Paris-thermometer/hair-dryer case is the trivial version; political-narrative manipulation is the dangerous version.
- **Long-tail markets are the natural application.** Most subjective/policy questions can never attract enough liquidity for a traditional oracle to bother resolving. Self-resolving designs let these markets exist at all · the *informational value of price discovery* is decoupled from the cost-of-oracle question.
- **The trust regress.** Self-resolving markets require trust in the mechanism design itself (the math of SKC, the on-chain feed integrity, the AI judge prompt). They've moved the trust problem from "oracle voters" to "mechanism specification," but they haven't eliminated it.
- **SKC equilibrium proof is impressive but assumption-heavy.** The proof assumes rational, self-interested agents with access to private information and common knowledge of the mechanism. Real-world deviations (irrational traders, coordinated groups, partial-information settings) may break the Bayesian-equilibrium result. michaellwy is candid: "admittedly stretches beyond my full comprehension."
- **The "what about Sybil attacks?" question.** Type 2 self-resolving markets implicitly need agents to be costly-to-spawn (otherwise an attacker creates many fake agents and floods the consensus). SKC paper doesn't fully address this; production implementations would need staking, KYC, or fee thresholds.
- **The wide-open frontier.** Among the six Oracle & Resolution concepts, self-resolving markets is the one with the *least* current production deployment and the *most* theoretical potential. Whether mainstream platforms (Polymarket, Kalshi) adopt SKC-style mechanisms for long-tail markets is the open question for 2026-2027.

## Notable Quotes

> "How can we design a system where the act of truthfully sharing information becomes the most rational and rewarding strategy, even when no objective 'right answer' will ever exist?"  
> — *michaellwy, *Explainer on Self-Resolving Prediction Markets**

> "A reference agent with access to more information can serve as a reasonable proxy for the ground truth."  
> — *Srinivasan, Karger, Chen*

> "The mechanism pays you for information you add, not for how close you end up to the crowd once it's finished."  
> — *michaellwy*

> "We show that it is a perfect Bayesian equilibrium (PBE) for all agents to report truthfully in our mechanism and to believe that all other agents report truthfully."  
> — *SKC abstract*

> "When you build a financial instrument that pays based on a number produced by a heat sensor in a plastic box, someone will try to point a heat source at that box."  
> — *Seva Gunitsky (on the dual-edge of self-resolving designs that rely on a single data source)*

## Where It Matters

Self-resolving markets are the only mechanism in the corpus that addresses *the entire long tail* of questions that current oracle-driven markets cannot serve. If SKC-style mechanisms work in production, the addressable market for prediction-market platforms expands by orders of magnitude (every policy debate, every research claim, every cultural sentiment is suddenly a tradable question). The catch: the design is theoretically elegant but production-untested at scale, and the assumptions about rational truth-telling have not been stress-tested against actual adversaries.

## Related Concepts

- **proper scoring rules** · SKC is built on negative cross-entropy, a proper scoring rule.
- **incentive compatibility** · SKC's central theoretical claim.
- **peer prediction** · the broader family of mechanisms SKC sits inside.
- **information aggregation** · what self-resolving markets are *for*.
- **oracle design** · what they try to eliminate.
- **reflexivity** · the risk: markets that resolve to crowd consensus may *cause* the consensus.
- **Keynesian Beauty Contest** · the failure mode SKC's payoff structure is designed to prevent.

## Sources

- [Priced to Kill](https://hegemon.substack.com/p/polymarket-is-making-wars-more-dangerous) — Seva Gunitsky · May 13 2026 ·
- [Every Opinion Deserves A Market](https://x.com/turfsports_/status/2047030930699854334) — Turf · Apr 22 2026 ·
- [Explainer on Self-Resolving Prediction Markets](https://blog.monad.xyz/blog/self-resolving-prediction-markets) — michaellwy · Nov 11 2025 ·
- [Self-Resolving Prediction Markets for Unverifiable Outcomes](https://arxiv.org/abs/2306.04305) — Srinivasan, Karger, Chen · Jun 7 2023 (rev. Feb 18 2025)

