# Covariance Markets

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Covariance Markets](https://www.pmatlas.xyz/concepts/covariance-markets)

---

## Definition

**Quick definition.** Markets that trade on the *correlation* between two events rather than each event individually. The original proposal (Chicken / Voliti) solves the parlay-fragmentation problem: instead of creating separate markets for every AND/OR combination, a single covariance market between two base markets enables all 8 joint combinations while maintaining concentrated liquidity.

## Key Insights

- Chicken's design proposal: parlays normally fragment liquidity because every AND/OR/conditional combination needs its own market. A **covariance market between two base markets** unlocks all 8 joint combinations on one shared liquidity pool.
- The mechanism: trade correlation directly. If two events A and B trade at probabilities p_A and p_B, the covariance market expresses the difference between actual joint probability and p_A × p_B. Going long covariance = betting the events move together more than independence implies.
- This is the PM-native version of variance/correlation swaps in traditional derivatives.
- Compatible with the **distribution-markets / continuous markets** thesis: instead of fragmenting a question across many binaries, a covariance overlay enables compositional bets without multiplying contracts.
- Connection to Jon Turek's implied-correlation work: the *reason* covariance markets would exist is to absorb the relative-value trades that arise from inconsistent cross-market correlation pricing.
- Daedalus Research's "Black-Scholes for PMs" paper proposes correlation swaps as one of three core derivative instruments (alongside variance swaps and first-passage notes) · covariance markets are the spot product underneath those derivatives.

## Notable Quotes

> "A single covariance market between two base markets enables all 8 joint combinations while maintaining concentrated liquidity."  
> — *Chicken*

## Where It Matters

Covariance markets are one of the more elegant design proposals for solving liquidity fragmentation without abandoning the binary-contract substrate. Rather than splitting questions into dozens of correlated binaries, traders express the joint structure once. The catch is that they require traders to think in covariances, which is a much bigger conceptual lift than yes/no · meaning these are infrastructure for AI agents and quant desks more than for retail.

## Related Concepts

- **Parlays** · what covariance markets are designed to replace.
- **Liquidity provision** · covariance markets concentrate it.
- **Liquidity fragmentation** · what covariance markets undo.
- **Implied correlation / relative value trading** · the trading strategies that consume covariance pricing.
- **Distribution markets / multi-outcome markets** · sibling proposals for non-binary primitives.

## Sources

- [How to Offer Prediction Market Parlays Without Fragmenting Liquidity](https://x.com/voliti_co/status/1946232456459255869) — Chicken · Jul 18, 2025

