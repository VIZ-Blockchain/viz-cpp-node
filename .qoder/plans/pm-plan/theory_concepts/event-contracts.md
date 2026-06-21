# Event contracts

**Category:** Business & Platforms  
**Source:** [PM Atlas — Event contracts](https://www.pmatlas.xyz/concepts/event-contracts)

---

## Definition

**Quick definition.** Standardized binary contracts on specific real-world events · the core trading unit of prediction markets. Their legal status (swap vs gambling) is the defining regulatory question of the era.

## Key Insights

- Event contracts are the regulatory and commercial unit of account. Hariharan (Dopamine) argues the industry's fate rests on whether sports event contracts count as "swaps" under the Commodity Exchange Act · if yes, federal preemption applies in all 50 states; if no, the path collapses into state-by-state gambling licenses.
- Third Circuit became the first appeals court to side with Kalshi, holding sports event contracts are swaps. Courts elsewhere are split (Kalshi won NJ appeals, TN district, N. Cal district; states won OH district, MA state, NV state). 19 federal lawsuits pending. CFTC is suing Arizona, Illinois, Connecticut on Kalshi's behalf. Supreme Court resolution likely within two years.
- Rob Schwartz (Morgan Lewis) argues "industry consensus is what matters" under the CEA: DCMs, FCMs, brokers, and DCOs all treat sports event contracts as swaps, so they are swaps and CFTC jurisdiction is exclusive. He counters the court characterization that the products "are sports wagers and everyone who sees them knows it" · the derivatives industry's own terminology disproves it.
- HIP-4 reframes event contracts. Dean Eigenmann positions them as parametric cover for crypto-native exposures (Kelp DAO rsETH bridge exploit, $292M, as motivating case): when outcome contracts share margin with underlying exposure on the same execution layer, the addressable market is two orders of magnitude larger than current DeFi insurance.
- Pink Brains: HIP-4 is the on-chain options layer, not "another PM." Unified margin engine + fee structure + HYPE token value capture differentiates it. Design space includes CDS, parametric insurance, futarchy.
- Astaria: prediction market perps are structurally different from crypto perps because event contracts lack a tradeable underlying. Most "perps on events" implementations devolve into liquidation arcades. The winning path is institutional hedging infrastructure for continuous event-risk management.
- DWF Ventures argues PMs are evolving into a financial derivatives asset class. Structural barriers: jump risk, binary valuation, regulatory uncertainty. Emerging solutions: epoch-based fee models, perpetual futures on outcomes, tokenized positions on Solana.
- 0xturbanurban introduces the "vega wedge" · the structural overcharge binary hedgers pay when they replicate via vanilla options. The argument: PMs can undercut this tax in deep-volume categories but still lack a Black-Scholes-equivalent shared pricing language and risk infrastructure. (Specific bp figures cited in the original tweet are not in our fetched digest body.)
- ARK Invest sizes the medium-term opportunity at up to ~$5 trillion, arguing prediction markets' "real potential will come from their instantiation as the future of financial infrastructure" · not sports betting disruption. They explicitly argue PMs will *not* disrupt sports betting but will become a foundational layer of financial services.
- Levine's "Truth Machines Go to War" (Bloomberg, paywalled in our fetch) argues Kalshi's "mentions markets" · paying off if a specific word is said · show how reality rarely provides clean binary boundaries, forcing platforms into interpretive disputes.
- Stefan von Imhof debunks the canonical misconception that market prices equal probabilities. Risk-free rates, opportunity costs, and bid-ask spreads create systematic price deviations from true beliefs · material when calibrating PM data for serious use.
- The Measles Market on Kalshi (OddChain): real-world ethical limits on the binary-everything frontier. Turning a public health crisis into a tradeable instrument is the cost of a category that converts any future into a contract.
- Lauris argues (via his variance-risk-premium framing) that high-VRP categories · Bitcoin and elections in particular · already cross the displacement threshold where event contracts beat options replication, with FOMC markets having compressed their cost gap dramatically since 2024 (specific figures cited in the original tweet are not in our fetched digest body).
- Michael Li's four-dimensional classification (information structure, manipulation economics, social utility, repugnance) makes the case that lumping every event contract under one regulatory regime is either too restrictive (suppressing election/macro forecasting) or too permissive (allowing perverse contracts with no countervailing benefit).
- Park's history: Iowa Electronic Markets' 1988 no-action letter -> Intrade's 2012 collapse -> Kalshi's court win establishing "gaming" doesn't cover financial contracts on uncertain outcomes -> Bitwise PredictionShares ETF launch as the mainstream tipping point.
- Cassar (Oxford): EU regulators face a parallel classification crisis · gambling vs MiFID II derivative vs something else. Proposes a Malta-style "Prediction Test" to systematically categorize through exclusion.

## Notable Quotes

> "All of these entities refer to and treat the contracts as 'swaps,' subject to regulation under the exclusive jurisdiction of the Commodity Futures Trading Commission … under the Commodity Exchange Act, industry consensus is what matters."  
> — *Rob Schwartz, *Federal Preemption in Sports Prediction Market Litigation**

> "[Sports event contracts] are sports wagers and everyone who sees them knows it."  
> — *court opinion quoted in Rob Schwartz, *Federal Preemption in Sports Prediction Market Litigation**

> "Prediction markets … real potential will come from their instantiation as the future of financial infrastructure, presenting a potential medium-term opportunity up to ~$5 trillion."  
> — *ARK Invest, *Prediction Markets: The Potential Multi-Trillion Dollar Asset Class**

## Where It Matters

Event contracts are the fundamental unit; everything else (oracles, liquidity, distribution) is downstream of whether you can offer them legally and how cleanly they map to user intent. For Dekant: continuous-curve markets are themselves a kind of unbundled event contract · instead of one binary, a partition-of-unity over the outcome space. The regulatory tailwind for swap-classified contracts under the CEA could be relevant if a continuous market is structured as a basket of "commonly known to the trade" swap legs.

## Related Concepts

- Regulatory classification · how the contract is legally categorized
- Federal preemption · whether CFTC's swap jurisdiction overrides state gaming law
- Binary contracts · the dominant subtype
- Distribution moat · multi-asset wrappers cross-sell event contracts
- Market structure · event contracts as the lego brick across rails/wrappers

## Sources

- [Outcome Markets as a Cover Venue: HIP-4 and Its Traditional Comparables](https://x.com/DeanEigenmann/status/2052734335971713347) — Dean Eigenmann · May 8 2026 ·
- [Benchmarks Are Key to Scale Prediction Markets Institutionally. Question: Which Ones Can Deliver?](https://x.com/lzminsky/status/2051741765682508073) — Lauris · May 5 2026 ·
- [Prediction Market Perps - the 1% Winning Product](https://x.com/AstariaTrade/status/2051363114881568852) — Astaria · May 4 2026 ·
- [HIP-4 Is Not a Prediction Market - It's the Options Layer: A Full Guide](https://x.com/PinkBrains_io/status/2051312783074226606) — Pink Brains · May 4 2026 ·
- [A Second Identity: Prediction Markets as Financial Derivatives](https://x.com/i/article/2049414664946323456) — DWF Ventures · Apr 30 2026 ·
- [Why Prediction Markets Are Hard to Regulate](https://michaellwy.substack.com/p/my-comment-to-cftcs-prediction-markets) — Michael Li · Apr 30 2026 ·
- [You Don't Hate Prediction Markets. You Hate Capitalism.](https://x.com/noahlitvin/status/2048527516587966660) — Noah Litvin · Apr 27 2026 ·
- [Prediction Markets: The Potential Multi-Trillion Dollar Asset Class Hiding In Plain Sight](https://www.ark-invest.com/articles/analyst-research/prediction-markets-potential-multi-trillion-dollar-asset-class) — Nicholas Grous, Varshika Prasanna, Raye Hadi (ARK) · Apr 22 2026 ·
- [The Bane Of Binaries: What Prediction Markets Are Missing](https://x.com/0xturbanurban/status/2044433520806830101) — 0xturbanurban · Apr 15 2026 ·
- [States vs. Prediction Markets: The Fight Over the Meaning of 'Swap'](https://www.dopaminemarkets.com/p/prediction-markets-vs-states-the) — Shreyas Hariharan · Apr 6 2026 ·
- [Truth Machines Go to War](https://www.bloomberg.com/opinion/newsletters/2026-04-06/truth-machines-go-to-war) — Matt Levine · Apr 6 2026 ·
- [The State of Prediction Markets](https://stateofpredictionmarkets.blocmates.com/) — blocmates · Apr 1 2026 ·
- [Regulating Prediction Markets in Europe Requires a 'Prediction Test'](https://blogs.law.ox.ac.uk/oblb/blog-post/2026/03/regulating-prediction-markets-europe-requires-prediction-test) — Terence Cassar · Mar 31 2026
- [Prediction Markets and Insider Trading Law](https://www.congress.gov/crs-product/LSB11406) — Jay B. Sykes (CRS) · Mar 18 2026 ·
- [Federal Preemption in Sports Prediction Market Litigation: This Shouldn't Be a Jump Ball](https://www.morganlewis.com/-/media/files/publication/outside-publication/article/2026/federal-preemption-in-sports-prediction-market-litigation-this-shouldnt-be-a-jump-ball-futures-and-derivatives-law-report.pdf) — Rob Schwartz · Mar 1 2026 ·
- [Seeing Like a Market: Event Contracts and Market Topology](https://www.slampaper.xyz/) — Lauris Marinson · Mar 1 2026 ·
- [The Truth Machine Era Is Here](https://x.com/dgt10011/status/2024228182178595247) — Jeff Park · Feb 19 2026 ·
- [The Measles Market on Kalshi Is One of the Dumbest and Most Tragic Markets We've Seen This Year](https://www.oddchain.com/p/the-measles-market-on-kalshi-for-2025-is-one-of-the-dumbest-and-most-tragic-markets-we-ve-seen-this) — OddChain · Dec 30 2025 ·
- [Prediction Markets Explained](https://alternativeassets.substack.com/p/prediction-markets-explained) — Stefan von Imhof · Feb 4 2024 ·

