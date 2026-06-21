# Noise Decomposition

**Category:** Information Theory  
**Source:** [PM Atlas — Noise Decomposition](https://www.pmatlas.xyz/concepts/noise-decomposition)

---

## Definition

Separating observed price movements into components attributable to genuine new information versus random microstructure noise · used to assess how much of a market's short-term variance reflects real signal. Closely tied to market-microstructure theory (Kyle, Glosten-Milgrom, Roll variance ratios) but specialized for PMs by the binary-payoff and short-horizon structure.

## Key Insights

- "Noisy Traders Are Not Dumb Money" · challenges the smart-vs-dumb dichotomy. Synthesizes Snowberg/Wolfers/Zitzewitz, INSEAD's BIN (Bias-Information-Noise) model, and Wharton's cognitive-search framework. Key argument: noisy traders *fund the probability space* rather than serve as exit liquidity. Compares how binary CLOBs vs continuous probability markets decompose and harness noise *differently* · in a binary market, noise gets absorbed as bid-ask spread costs to liquidity providers; in a distribution market, noise spreads across the surface and can be re-aggregated as a smoothing effect (functionSPACE).
- "Decomposing Crowd Wisdom" · Bayesian hierarchical model on 292M trades across 327K contracts on Kalshi and Polymarket. Decomposes calibration into four structured components: (1) a universal horizon effect, (2) domain-specific biases, (3) domain-by-horizon interactions, and (4) a trade-size scale effect. Together explains 87.3% of calibration variance on Kalshi. The "dominant pattern is persistent underconfidence in political markets, where prices are chronically compressed toward 50%" (generalises across both exchanges). Trade-size scale effect: Δ = 0.53 [0.29, 0.75] on Kalshi politics; Δ = 0.11 [-0.15, 0.39] on Polymarket · platform-specific microstructure. Bayesian model achieves 96.3% posterior predictive coverage (Nam Anh Le).

## Notable Quotes

> "Noisy traders fund the probability space rather than serve as exit liquidity."  
> — *functionSPACE*

> "Together explain 87.3% of variance on Kalshi."  
> — *Nam Anh Le*

> "Persistent underconfidence in political markets where prices compress toward 50%."  
> — *Nam Anh Le*

## Where It Matters

Noise decomposition is the diagnostic that lets a builder say *honestly* what their market is good at. Aggregate Brier scores treat all variance as forecast quality; decomposition separates "the market correctly updated on news" from "the market thrashed because a 5¢ tick read as a 1pp probability change" (semantic tick size). For PM platforms, this matters in three ways: (1) it lets you exclude microstructure noise from accuracy claims, (2) it identifies categories where noise dominates and informed flow is absent (signaling minimum-viable-liquidity failures), (3) it surfaces the platform-specific microstructure effects (Kalshi's large trades amplify underconfidence; Polymarket's don't) that determine whether a contract behaves like a real probability or a token price. For Dekant, the distribution-market surface should naturally decompose noise across the distribution, with traders rewarded for variance compression (entropy reduction) rather than for being on the right side of a binary threshold.

## Related Concepts

- **Calibration** · what noise decomposition cleans up
- **Brier score** · the aggregate metric that noise decomposition explodes
- **Adverse selection** · informed flow is the signal portion
- **Liquidity provision** · noise is what LPs are paid to absorb
- **Market making** · bid-ask spread is one mechanism for pricing noise
- **Semantic tick size** · a specific microstructure noise pattern
- **Longshot bias** · domain-specific bias surfaced by decomposition
- **Distribution markets** · proposed mechanism for noise re-aggregation across a surface

## Sources

- [Noisy Traders Are Not Dumb Money](https://x.com/functionspaceHQ/article/2032397043579462028) — functionSPACE · Mar 13, 2026 [FULL_READ]
- [Decomposing Crowd Wisdom: Domain-Specific Calibration Dynamics in Prediction Markets](https://arxiv.org/abs/2602.19520) — Nam Anh Le · Feb 23, 2026 [FULL_READ · abstract; Bayesian hierarchical decomposition with 96.3% posterior predictive coverage; trade-size scale effect Δ=0.53 on Kalshi politics vs Δ=0.11 on Polymarket]

