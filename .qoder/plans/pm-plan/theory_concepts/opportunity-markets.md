# Opportunity Markets

**Category:** Governance & Decisions  
**Source:** [PM Atlas — Opportunity Markets](https://www.pmatlas.xyz/concepts/opportunity-markets)

---

## Definition

**Quick definition.** Private prediction markets where prices are visible *only to the sponsor* during an "opportunity window." Designed by Dave White and Matt Liston (Paradigm, August 2025) so that institutions can crowdsource scouting (artists, researchers, founders, athletes) without giving competitors a free read on the signal. People with information get paid by people with resources; the prediction-market public-goods leak is sealed.

## Key Insights

- **The problem they solve**: in a regular prediction market, sponsors who provide liquidity for "Will we sign Artist X in 2025?" subsidize the same information for all their competitors. The public-goods nature of price discovery destroys the institutional incentive to fund scouting markets. Opportunity markets fix this by keeping prices private from everyone except the sponsor for a window of time.
- **The other side of the problem**: scouts (fans, researchers, local experts) have information that institutions don't, but there's no clean way to monetize that insight short of becoming a paid scout. Opportunity markets create a permissionless scouting channel where anyone can put skin in the game.
- **Mechanism by example**: a music label creates a family of markets "Will we sign Artist X in 2025?" for any artist. The label acts as market maker, posting up to $25,000 of "dumb money" liquidity per artist. Scouts who recognize an artist early can buy YES shares at low prices; as the price rises, the sponsor sees this private signal and investigates the artist. If signed, scouts win the dumb-money pool. Effectively, the label has crowdsourced a decentralized scout program with explicit financial incentives.
- **Privacy is the load-bearing innovation**: only the sponsor sees the current market price. If traders could see fills in real time, they could reconstruct prices by trading and probing. To preserve privacy while still giving traders eventual feedback, opportunity markets use an **opportunity window** · perhaps two weeks · after which traders learn whether their orders filled. This window lets the sponsor investigate before competitors can free-ride on the signal.
- **Privacy enforcement options**: (1) trusted sponsor (simplest, weakest), (2) AMM running in a Trusted Execution Environment (TEE) that proves the sponsor cannot peek at trader positions beyond the protocol's reveal schedule, (3) cryptographic constructions (zk-rollup-like) that could in principle preserve sponsor-only views with stronger guarantees.
- **Liquidity provisioning is bounded by useful range**: a sponsor might post liquidity starting around 1% probability (below which the signal is informationally useless) and stopping around 30% probability (above which the signal is redundant · the sponsor already knows). This concentrates liquidity in the band where information has marginal value.
- **Unlimited markets vs. First-N collateralization**: For opportunity types with a hard cap on actions (a label can only sign so many artists), the sponsor can simply promise payouts on an unlimited number of markets ("Will we sign Artist X?") and ensure their total exposure stays within their action budget. For permissionless setups, fully collateralizing each market is too capital-intensive; instead, structure markets as "Will XYZ be among the first 10 artists we sign in 2025?" · which requires only 10× max liquidity total, not unlimited.
- **Exploitation risk is real and largely unsolved**: sponsors have private market-state information AND private knowledge of their own pipeline. They could feign interest in artist X to push prices up while aggressively selling, or do the inverse. The paper acknowledges this is hard to address mechanism-design-wise; reputation and trust are doing the heavy lifting in v1 designs.
- **Post-window reveal design choices**: after the opportunity window closes, sponsors choose what to reveal · all prices and positions publicly, positions only to individual traders, or differentiated rules for large vs. small orders. Each choice trades off information leakage vs. market accountability.
- **Sophisticated extensions**: limit orders that can be canceled before reveal, trading agents that operate on policy without revealing current positions, and time-decaying privacy where late information eventually becomes public (preserving the public-goods information value while protecting first-mover sponsorship).
- **Use-case domain** the paper emphasizes: opportunities that take significant resources to evaluate AND to act on, and have a competitive time-limited nature. Music label signings, research lab commercialization, VC sourcing, athletic recruitment, employer talent scouting, retail trend spotting. These are domains where knowing first confers real institutional advantage.
- **Why scout programs are not enough**: traditional scout programs are limited by trust requirements (the institution must vet each scout) and evaluation costs (each scout's recommendation must be triaged). Scaling beyond a small bench is hard. Opportunity markets are *permissionless* scout programs · anyone can put their judgment to work, and the institution only invests in following up on the strongest market signals.
- **Why public prediction markets are not enough**: even if institutions subsidized public PMs, competitors free-ride on the price signal. Subsidizing public price discovery is subsidizing your competitors. Privacy is what makes the funding economics work for the sponsor.
- **The Paradigm framing** aligns this with their broader prediction-market thesis: opportunity markets are one of several "fully-asymmetric-information" mechanisms (alongside Distribution Markets, which Dekant implements) that extend prediction-market mechanics into spaces where standard public markets fail.
- **Implementation surface**: TEEs are central · running the AMM logic inside a Trusted Execution Environment (Intel SGX, AWS Nitro, etc.) lets sponsors prove they aren't peeking. This is one of the more concrete commercial use cases for TEEs in DeFi.
- **The "decentralized scout program" tagline** captures the value prop crisply: a music label that today employs 5 scouts each running on personal networks can become a label that has 5,000 informal scouts each putting $50 of capital behind their judgment, with the label only paying out to the scouts who were right.

## Notable Quotes

> "Music labels, research labs, and VCs all want to find the next big thing before the competition. But the people who first spot opportunities often have no institutional connections. Historically, there hasn't been a clean way for these parties to find each other and transact."  
> — *Dave White, Matt Liston, *Opportunity Markets**

> "Even if institutions subsidized liquidity on these markets to benefit from the information, prediction markets as they are usually deployed today offer their information as a public good. Competitors could free-ride on the same signals, eliminating the advantage. This is the core leak opportunity markets seek to address."  
> — *White, Liston*

> "Opportunity markets address this problem by keeping market prices private from everyone but their sponsor… It's like a decentralized scout program where anyone in the world can get skin in the game."  
> — *White, Liston*

> "For traders to know their positions eventually [is necessary]. The solution is an opportunity window"  
> — *perhaps two weeks · after which traders learn whether their orders filled. This gives sponsors time to investigate promising opportunities before the information becomes public." · White, Liston*

> "[Sponsors] have both special information about the market state at any given time, and special knowledge about their own process, which opens the risk of exploitative behavior such as hinting they will take advantage of opportunity X while aggressively selling into that market."  
> — *White, Liston*

## Where It Matters

Opportunity markets are the cleanest worked example of "what does it look like when you fix the public-goods leak in prediction markets?" · a question that hangs over every institutional use case. The privacy-first design unlocks talent scouting, commercialization scouting, deal sourcing, and other domains where public price signals destroy the sponsor's competitive advantage. They are also a useful complement to Dekant's continuous-outcome design: where Dekant prices distributions over outcome variables in public markets, opportunity markets are private discrete markets where the institutional sponsor pays for early information. Both are answers to "what does prediction-market infrastructure unlock beyond elections?" · but they answer to very different buyers.

## Related Concepts

- **Decision markets** · Opportunity markets are decision-adjacent: the sponsor's decision to act on a signal is informed by the private market price.
- **Distribution markets** · Both are Paradigm-published designs (Distribution Markets · White, 2024; Opportunity Markets · White & Liston, 2025) that extend prediction-market mechanics beyond binary public outcomes.
- **Liquidity provision** · The sponsor is the dedicated LP; opportunity markets are explicit about LP exposure being bounded by the value of the information signal.
- **Hedging** · Less relevant; opportunity markets are about scouting, not hedging.
- **Insider trading** · Sponsors operate with privileged information by design; the design space explicitly accepts this and constrains it via reputation and TEE-style cryptography rather than disclosure law.
- **Oracle design** · Settlement requires verifying whether the sponsor actually acted (signed the artist, hired the candidate); oracle design here is about provable institutional action rather than external event observation.
- **Market manipulation** · Sponsors can exploit private price views; the open problem in opportunity-market design.
- **Network effects** · Sponsors compete to attract the best scout network; a label with a strong reputation for honoring market signals attracts more scouts.

## Sources

- [Opportunity Markets](https://www.paradigm.xyz/2025/08/opportunity-markets) — Dave White, Matt Liston, Paradigm · Aug 18, 2025 ·

