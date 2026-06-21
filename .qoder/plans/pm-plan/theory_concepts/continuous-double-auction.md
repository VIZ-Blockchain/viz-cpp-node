# Continuous Double Auction

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Continuous Double Auction](https://www.pmatlas.xyz/concepts/continuous-double-auction)

---

## Definition

**Quick definition.** A trading mechanism where buy and sell orders are matched immediately as they arrive, with price-time priority (FCFS) determining execution order. The CDA is the standard matching rule for CLOBs on Polymarket and Kalshi · and the principal target of every "alternative ordering mechanism" proposal in the PM literature.

## Key Insights

- Human Invariant inventories the 2025 CDA incumbents explicitly: **Polymarket, Kalshi, Opinion, and Limitless** all run offchain CDA matching with price-time priority. "Onchain orderbooks still remain unviable."
- Trust assumption: FCFS in PMs is "very likely" but not enforceable · "the public must trust that the companies running the infrastructure are operating honestly and not re-ordering transactions". The regulatory backstops that ensure FCFS in equities don't yet exist for PMs.
- Failure mode 1 · **latency wars**: FCFS creates a large incentive to co-locate with the matching engine. This is the same dynamic seen in equities but amplified because PM contracts can jump 1%→100% on a single news item.
- Failure mode 2 · **defensive spread widening**: MMs widen quotes when they can't trust they'll cancel in time. Human Invariant's anchor stat: on Nasdaq, MMs successfully update quotes only **~13% of the time** (87% of the time they get picked off). Stable in equities; catastrophic in PMs.
- Failure mode 3 · **taker dominance**: Human Invariant: "the most profitable traders all run taker strategies, where they scan various markets to pick off mispriced quotes from market makers". The large LP-incentive programs from Polymarket and Kalshi exist *because* of this · they're paying MMs to keep quoting despite the structural disadvantage.
- Human Invariant's live-sports case study: spreads visibly widen during in-game play, with the "$0.04 spread" misleading because the resting liquidity is only **$3,000** · negligible. Compare with NBA regular-season pregame markets, which maintain $0.01 spreads with substantially deeper liquidity.
- The proposed replacement is **priority batch auctions** that process cancellations → maker orders → taker orders inside each batch. This rule structure lets MMs cancel stale quotes safely before takers can sweep them.
- Human Invariant's bear case on migration: "Existing prediction market players benefit from maximizing raw transaction counts and headline volume, including wash trading, to juice their vanity metrics." Migration is costly, the trader base is anchored, and airdrops/rebates already reward "sheer activity rather than quality liquidity". The likely route is new entrants, not incumbent reform.
- Implicit comparison set: sybilpm (continuous order books "structurally broken for binary assets") and Will Howard ("no practical social benefit from sub-second reaction times") reach the same conclusion from different angles.
- The CDA's saving grace is that it's the matching mechanism every trader already understands · it inherits the equity-market mental model. The cost is that everything that makes CDAs ugly in equities (HFT arms races, latency rents, last-look games) is amplified in PMs by gap risk and binary payoffs.
- The PM-specific failure mode: a CDA matches the first-arriving order at the prevailing price; when prices can jump 0.10 → 0.99 between two orders, the CDA's price-time priority *encodes the sniper's advantage* into the matching rule.

## Notable Quotes

> "First-Come-First-Served order matching in prediction markets creates perverse incentives: latency wars between traders and defensive spread widening by market makers."  
> — *Human Invariant*

> "On Nasdaq, market makers are only able to update their quotes in time approximately 13% of the time. This means that 87% of the time they are picked off by takers."  
> — *Human Invariant*

> "A single second can provide new information to cause the price to jump from 1% to 100% (pope announcement, game winning shot, etc.). As a market maker, you co-locate as close to the matching engine as possible but you are still very likely to be picked off in a FCFS environment."  
> — *Human Invariant*

> "The prediction market that creates the best market structure will host the most liquid markets with the best prices for users."  
> — *Human Invariant*

## Where It Matters

The CDA is the dominant microstructure for prediction markets in 2026, and it's also the most-critiqued. Every meaningful alternative proposal (batched auctions, parimutuel, LMSR, market scoring rules, covariance markets) defines itself against the CDA's pathologies. Whether incumbents migrate or new entrants arrive built around batching is the open product question of the year.

## Related Concepts

- **Batched auctions** · the leading alternative.
- **Order book / market making / bid-ask spread** · the CDA's operating environment.
- **Adverse selection / toxic flow / gap risk** · the failure modes the CDA amplifies.
- **Execution quality** · what CDAs are graded on.
- **Time arbitrage** · what FCFS rewards.

## Sources

- [The Case For Alternative Ordering Mechanisms in Prediction Markets](https://www.humaninvariant.com/blog/pm-ordering) — Human Invariant · Nov 12, 2025

