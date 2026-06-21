# Hedging

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Hedging](https://www.pmatlas.xyz/concepts/hedging)

---

## Definition

**Quick definition.** Taking offsetting positions to reduce exposure to adverse price movements or uncertain outcomes. In prediction markets, hedging is both an end-user use case (corporate, institutional event-risk) and a market-maker primitive (offsetting inventory) · but binary structure and fragmented venues make most hedges imperfect.

## Key Insights

- Astaria: prediction-market perps are structurally different from crypto perps because event contracts lack a tradeable underlying. Most implementations become "liquidation arcades"; the winning path is **institutional hedging infrastructure for continuous event-risk management**.
- a16z (Immerman & Rodriguez): three-stage institutional adoption · using markets as data → integrating into compliance → actively hedging risk. Bottleneck for institutions is **full notional collateral requirements**, which Kalshi is addressing via margin trading licenses.
- 0xturbanurban's "Bane of Binaries": Minsky's **vega wedge** · binary hedgers using vanilla options pay ~4.8% overcharge for BTC binaries, **7–20% for gold**. PMs can undercut this tax in deep-volume categories. What still keeps institutions out: no shared Black-Scholes equivalent, missing risk infrastructure.
- Marinson ("Seeing Like a Market") empirical pipeline: 87 event contracts across 11 categories, 2.89M trade-row dataset, Jan 2024 to Feb 2026. Core framework: PMs win when **structural wedge (W) > execution cost (C)**. The wedge is paid every time an institution uses derivatives; execution cost compresses as PM liquidity arrives.
- Marinson category scorecard: BTC (median VRP 4.10%) = 12 PM wins / 2 threshold / 6 losses; Elections = 12/0/5; FOMC = 3/9/0 (zero structural losses, just liquidity-bound); Equity = 1/1/11+1; Gold = 1/0/2 (high VRP but illiquid PM markets); Silver = 0/0/3 (negative VRP, derivatives structurally cheaper). **Overall 30/12/45 · 42 of 87 contracts already favor PMs at $3M reference depth.**
- Marinson's FOMC convergence: **cost gap fell from 12pp (March 2024) to <2pp (Feb 2026) at $3M** · driven by liquidity compression, not vol expansion. The "apparatus" of dealer infrastructure is being unbundled; the cost differential is "apparatus rent."
- Marinson on BTC's January 2026 natural experiment: 5 BTC contracts at identical 4.08% VRP but different strikes/volumes · PM wins at $100k ($13.3M vol), $105k ($7.1M), $110k ($4.9M), $150k ($32.8M); PM loses only at $125k ($2.2M vol). **Liquidity is the only variable**.
- Marinson on the smile: PM-derived IV across BTC strike contracts shows the **same skew, fat tails, and regime dynamics** as options markets. "No dealers. No SABR. No apparatus. The smile emerged from aggregation alone."
- Marinson on Fed Funds options unobservability: Databento returns zero rows, CME omits them from liquidity reviews, CFTC excludes them from weekly data, pit trading closed in May 2021 · the supposed benchmark for FOMC hedging cannot even be priced from public data. Required a 7-step replication pipeline via SOFR options and Black-76.
- Smallwood case studies in detail · Logitech: raw Polymarket tariff probability 51%, mapped down to **35% effective probability** for Logitech's gross-margin exposure because the contract resolved on legal refund event vs. economic-relief reality; weighted on a status-quo / relief / escalation scenario tree rather than binary thinking. Eli Lilly: raw retatrutide approval probability 29.5%, haircut to **22.1% effective**, EV uplift $24.2B from pulling approval into 2026; probability-weighted EV uplift ≈ $5.4B = 0.61% of LLY's market cap. "Useful does not mean decisive."
- Jeff Park: "the flip side of speculation is always insurance." PM payoffs are precise (binary → clean basis risk to truth) with finite expiry · structurally different from other derivatives.
- Alan Wu: PMs serve risk transfer (hurricane, policy exposure) and information accountability functions *even when prices drift from pure probability*.
- Nic Carter detailed: corporate hedging is impractical due to **market fragmentation and basis risk**. "The more useful the market is to the hedger, the less liquid it is likely to be." A vaccine manufacturer wanting to hedge an FDA shutdown is forced into buying "TrumpWin" · high correlation, but with non-trivial basis risk. The natural counterparty problem: a biotech firm is naturally long approval, so the firm + employees + management + shareholders all want to be short "approval"; no one wants the long side.
- Carter on why PM insurance economics fail: "Insurance markets are written on losses that are objectively measurable and tightly specified." A homeowner insures a defined asset for a defined value against a defined risk; CDS references a specific bond with a legal credit event. A PM contract on a Supreme Court decision is "a coarse, binary contract written on a political event whose economic consequences are heterogeneous and path dependent."
- Andy Hall & Elliot Paschal: **98.7% of political prediction markets qualify as "ghost towns"** with wide spreads and no counterparty; the truth-machine vision requires turning political contracts into political-risk insurance. Hedgers are the "liquidity traders" of Glosten-Milgrom · their presence is what makes it profitable for informed traders to participate at all.
- Hall & Paschal blueprint for institutional hedging: (1) shared contract definitions across platforms (ISDA-equivalent for PMs), (2) cross-subsidy from sports markets, (3) AI agents to trade where humans won't, (4) listings driven by user/partner proposals · CNN with Kalshi, WSJ with Polymarket. The OPM-website resolution debacle for a Polymarket government-shutdown contract is cited as why ISDA-style standardization is needed.
- Mike's "How Prediction Markets Turn Into Risk Instruments": three hedging modes already live · crypto-price binaries; attention markets (Trendle) as sentiment hedges; cross-platform composability (Gondor lending against PM positions, DFlow tokenizing Kalshi contracts as SPL tokens).
- Dean Eigenmann (HIP-4 paper): when outcome contracts share margin with underlying exposure on the same execution layer, parametric cover unlocks a market two orders of magnitude larger than current DeFi insurance. Compares to CDS, cat bonds, reinsurance sidecars, weather derivatives.
- Niakris: hedging use cases are part of the case PMs are finance not gambling · peer-to-peer architecture, skin-in-the-game pricing.
- Lebron: PMs lack heterogeneous risk preferences for efficiency; rely instead on continuous retail losses · limits how robust they can be as hedging infrastructure.

## Notable Quotes

> "The flip side of speculation is always insurance."  
> — *Jeff Park*

> "Corporate hedging is impractical due to market fragmentation and basis risk."  
> — *Nic Carter*

> "Binary hedgers pay around a structural overcharge (magnitude varies by asset; specific figures unverified)."  
> — *0xturbanurban, citing Minsky's vega wedge*

> "The cost differential is not a quality discount. It is apparatus rent. Same distributional content. Different transmission cost."  
> — *Lauris Marinson*

> "The more useful the market is to the hedger, the less liquid it is likely to be."  
> — *Nic Carter*

> "Insurance markets are written on losses that are objectively measurable and tightly specified. A prediction market on a Supreme Court decision or regulatory change is a coarse, binary contract written on a political event whose economic consequences are heterogeneous and path dependent."  
> — *Nic Carter*

## Where It Matters

Hedging is the asset class's path to institutional scale · if a corporate treasurer can use a PM contract to hedge tariff risk, policy risk, climate risk, or supply-chain risk, the addressable market goes from "retail betting" to a slice of OTC derivatives. The data shows isolated categories (BTC binaries, FOMC) are already cheaper than the derivative alternative, but most categories still have basis risk, illiquidity, and resolution-criteria mismatches that prevent serious corporate use. Fixing those · through standardized contracts, better collateral, and parametric-cover infrastructure like HIP-4 · is the explicit thesis behind teams like Astaria, Daedalus, and the Hyperliquid event-contract program.

## Related Concepts

- **Liquidity provision** · hedging only works at sufficient depth.
- **Event contracts** · the unit being hedged with.
- **Cross-platform arbitrage** · fragmentation creates both basis risk and arbitrage.
- **Position collateralization** · capital efficiency unlocks more hedging volume.
- **Binary contracts / multi-outcome markets / distribution markets** · granularity changes what you can hedge.
- **Resolution criteria / oracle design** · basis risk lives here.
- **Adverse selection** · hedgers can be informed; that's a feature for them, a problem for MMs.

## Sources

- [Outcome Markets as a Cover Venue: HIP-4 and Its Traditional Comparables](https://x.com/DeanEigenmann/status/2052734335971713347) — Dean Eigenmann · May 8, 2026
- [Prediction Market Perps - the 1% Winning Product](https://x.com/AstariaTrade/status/2051363114881568852) — Astaria · May 4, 2026
- [What Most People Get Wrong About Prediction Markets](https://x.com/dgt10011/status/2045952603301851173) — Jeff Park · Apr 20, 2026
- [Prediction Markets: They Grow Up So Fast](https://open.substack.com/pub/a16z/p/prediction-markets-they-grow-up-so) — Alex Immerman, Santiago Rodriguez (a16z) · Apr 16, 2026
- [The Bane Of Binaries: What Prediction Markets Are Missing](https://x.com/0xturbanurban/status/2044433520806830101) — 0xturbanurban · Apr 15, 2026
- [Can Polymarket Make You a Better Equity Analyst?](https://theagenticanalyst.substack.com/p/can-polymarket-make-you-a-better) — Alistair Smallwood · Apr 9, 2026
- [How Prediction Markets Can Ascend](https://x.com/alanwu/article/2036438579824496906) — Alan Wu · Mar 25, 2026
- [Seeing Like a Market: Event Contracts and Market Topology](https://www.slampaper.xyz/) — Lauris Marinson · Mar 1, 2026
- [Polymarket Is Not a Casino. Why Prediction Markets Are Finance, Not Gambling](https://x.com/13_niakris/article/2025614474263093627) — Niakris · Feb 23, 2026
- [Prediction Markets Are Not Good Markets (Yet)](https://murmurationstwo.substack.com/p/prediction-markets-are-not-good-markets) — Nic Carter · Feb 21, 2026
- [Building the Truth Machine](https://freesystems.substack.com/p/building-the-truth-machine) — Andy Hall, Elliot Paschal · Feb 13, 2026
- [How Prediction Markets Turn Into Risk Instruments](https://x.com/mikhryc0x/article/2020802941091741972) — Mike · Feb 9, 2026
- [Predicting Our Own Demise](https://reducibleerrors.com/prediction-markets/) — Agustin Lebron · Aug 17, 2025
- [Why Prediction Markets Aren't Popular](https://worksinprogress.co/issue/why-prediction-markets-arent-popular/) — Nick Whitaker, J. Zachary Mazlish · May 17, 2024

