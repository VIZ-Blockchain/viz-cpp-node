# Futarchy

**Category:** Governance & Decisions  
**Source:** [PM Atlas — Futarchy](https://www.pmatlas.xyz/concepts/futarchy)

---

## Definition

**Quick definition.** A governance system, proposed by Robin Hanson in 2000, where decisions are made by prediction markets: a community votes on what it values (the objective metric), and conditional markets determine which policy is most likely to maximize that metric. "Vote on values, bet on beliefs."

## Key Insights

- Origin: Robin Hanson's "Shall We Vote on Values, But Bet on Beliefs?" (2000), part of his broader information-aggregation research at George Mason University. Hanson had already argued markets aggregate dispersed information more efficiently than polls, forecasts, or expert committees; futarchy is the proposal to use that information for governance.
- Modern asset-futarchy variant (Heavey/Umbra): people own shares in a valuable asset (DAO, company, treasury), and proposals are decided by conditional markets that bet on the asset's value if the proposal passes vs. fails. The MetaDAO is the canonical implementation; Proposal 6 (Ben Hawkins) is the canonical raid-prevention case study.
- The deeper innovation Heavey identifies: futarchy doesn't just produce better decisions · it solves **trustless joint ownership**. Companies enforce minority shareholder rights through laws (oppression doctrines, fiduciary duty); DAOs historically have no equivalent. Asset futarchy makes majority raids economically irrational by construction, replacing legal enforcement with mechanism design.
- The economic argument: for Bob (majority) to drain Alice's (minority) treasury via a malicious proposal, the conditional-market arithmetic forces him to buy pABC above its true (post-raid) value AND sell fABC below the pre-proposal spot price. The expected outcome of "almost succeeding" is Bob buying Alice's tokens at a premium · the attack is self-defeating.
- This works regardless of Bob's stake · there is no special threshold at 50%. Asset futarchy decouples decision power from majority-token ownership and ties it to willingness-to-back-with-capital.
- **Coin price is the only acceptable objective function for asset futarchy** (Heavey's argument): it is the fairest, most elegant, and least manipulable. Vitalik's 2021 concern that "it's not just coin price that people want" is, on Heavey's reading, mostly wrong · alternative metrics introduce manipulation and ambiguity that destroy the mechanism.
- **The MetaDAO and Combinator** both run conditional-futures implementations of futarchy for crypto-native organizations. MetaDAO built futarchy.guide as a dedicated onboarding/explainer site because the conditional-futures interface is genuinely novel and confusing · a real friction point for trader recruitment.
- Galaxy Pokorny: Decision Markets (the active mechanism inside futarchy) are reductive by design · they collapse complex tradeoffs into a single dimension (economic value). This is exactly what makes them powerful for capital allocation and resource deployment, and exactly what makes them poorly suited for governance whose stated goals include alignment, community capital, or social legitimacy.
- **Newly-formed organizations vs. retrofits**: Galaxy argues retrofitting futarchy onto existing DAOs is hard because token economics, governance norms, and ownership structures may not align token value with organizational outcomes. New "market-native" orgs purpose-built around futarchy face fewer constraints.
- Janiak's critique applies: the conditional-futures architecture is genuinely confusing, MVL is hard to clear for idiosyncratic decisions, and token price is a noisy KPI. Even where futarchy theoretically works, in practice it has worked for two narrow use cases · aggregated opinions ("which option is most popular?") and commitment devices ("we will only execute if the market endorses").
- Soft rug pulls: futarchy doesn't prevent a founder from raising funds, taking a salary, and then walking away. The Parrot DAO incident saw the team distribute treasury pro-rata across token holders · but because the team minted themselves most of the tokens, the founders effectively pocketed ~$47m of investor funds while the mechanism behaved "fairly" on paper.
- **Settlement price calculation is non-trivial**: MetaDAO's TWAP implementation requires constant trader monitoring; if a trader doesn't engage on the final day, they can find the TWAP diverges from their fair-value view too late to react. Auction-style closing settlements are an active design frontier.
- **Insider trading regulation** is a real obstacle: in a regulated company context, executives may be restricted from participating in decision markets on company assets even when they don't have material non-public information about the specific proposal. The same lockup rules that prevent stock manipulation also constrain the people best positioned to price decision markets.
- **Custodial-asset futarchy fails**: if a DAO controls $100m of user deposits but has only $10m of fair-value market cap, an attacker can rationally buy DAO tokens above fair value to pass a deposit-draining proposal. Futarchy works as joint-ownership protection only when the DAO's controlled assets approximate its own fair value.
- Hanson's "distilled human judgment" framing (via Vitalik): futarchy can be used as a cheap proxy for expensive trusted mechanisms (courts, expert panels). Set up a prediction market on what the expensive mechanism would decide; only invoke the expensive mechanism rarely. This dramatically lowers cost while preserving legitimacy.
- Abhitej's framing: futarchy is part of the broader argument that **markets are the next stage in expression**. From print to radio to social media, each medium widened who could speak. Markets are unique in demanding that speakers bear consequences for being wrong · Hayek's price-as-coordination, Taleb's skin-in-the-game, Hanson's futarchy. In an AI-saturated information environment, "staked speech" may out-trust "cheap talk."
- a16z crypto podcast (Tabarrok / Kominers): prediction markets and futarchy are part of a broader category of information-aggregation mechanisms (alongside peer prediction, etc.). Applications span corporate decision-making, scientific reproducibility, DeSci, and governance · but each application needs custom design, and the most promising near-term use cases may be corporate rather than democratic.

## Notable Quotes

> "Since its inception, futarchy has been presented as a way to make smarter decisions by harnessing the predictive power of markets. This is accurate, but exposure to the lawless wasteland of blockchain governance has revealed a more fundamental innovation: futarchy solves the problem of trustless joint ownership."  
> — *Kevin Heavey, *Futarchy as Trustless Joint Ownership**

> "Joint ownership without minority protection is an illusion. And since majoritarian DAOs typically do not operate as actual companies (and if they did it would be difficult to describe them as decentralized), any semblance of minority ownership they project only lasts as long as the majority wants it to."  
> — *Kevin Heavey*

> "Attempting to manipulate the market created a scenario where the potential gains from the proposal's passage were outweighed by the sheer cost of acquiring the necessary META."  
> — *Ben Hawkins, attempting Proposal 6 raid on MetaDAO (quoted by Heavey)*

> "Pure futarchy has proven difficult to introduce, because in practice objective functions are very difficult to define (it's not just coin price that people want!)"  
> — *Vitalik Buterin (quoted by Heavey, who pushes back: coin price *is* the right objective for asset futarchy)*

> "MetaDAO even had to build a dedicated site, futarchy.guide, just to walk people through the basics. Another hurdle for attracting traders."  
> — *alexjaniak*

> "Each medium widened who could speak, but only markets demand that speakers bear consequence for being wrong."  
> — *Abhitej, *Predictions Are The New Expression**

## Where It Matters

Futarchy is the most ambitious governance application of prediction markets and the longest-standing "killer use case" in the rationalist canon. It is also the most contested: the elegance of the theory has not been matched by deployment success outside MetaDAO. The two open frontiers are (1) **whether new-form market-native organizations can avoid the retrofitting problem** and (2) whether **AI-driven market making** can lower MVL enough to make idiosyncratic futarchic decisions tractable. Practical futarchy is the most direct competitor to traditional DAO governance and a clarifying lens on what prediction markets are *for* beyond "betting on news."

## Related Concepts

- **Decision markets** · Decision markets are the operational mechanism inside futarchy.
- **Conditional tokens** · pABC/fABC + pUSD/fUSD is the canonical onchain implementation pattern.
- **Information aggregation / price discovery** · Futarchy is the application that proves (or disproves) that price discovery can be redirected into governance.
- **Impact markets** · Asset-futarchy is a special case of impact markets where the asset-conditional price is bound to a governance trigger.
- **Hyperstition markets** · Hyperstition is futarchy with execution: HYPERSTITIONS frames it as "futarchy with execution built in · betting YES means coordinating action toward manifestation."
- **No-loss prediction markets** · A potential lower-stakes onramp to futarchic governance for organizations that can't justify principal-at-risk markets.
- **Reflexivity** · Futarchy is a controlled reflexive system: the price determines the action that determines the outcome.
- **Minimum viable liquidity** · MVL is the dominant practical obstacle to futarchic governance for non-major decisions.
- **Wisdom of crowds** · Futarchy is the most-discussed wisdom-of-crowds-applied-to-governance mechanism; both share the assumption that distributed information is better than centralized expertise.

## Sources

- [Where Are All the Decision Markets?](https://www.lesswrong.com/posts/bDxqMn3GJEhPeqZey/where-are-all-the-decision-markets) — alexjaniak, LessWrong · May 12, 2026 ·
- [Predictions Are The New Expression](https://x.com/abhitejxyz/status/2047353485700825546) — Abhitej · Apr 24, 2026
- [Prediction Markets' Next Frontier: Impact and Decision Markets](https://www.galaxy.com/insights/research/prediction-markets-impact-markets-decision-markets-futarchy) — Zack Pokorny, Galaxy · Jan 12, 2026 ·
- [Stop Predicting. Start Manipulating.](https://x.com/hyperstiti0ns/article/1994422160790536299) — HYPERSTITIONS · Nov 28, 2025 ·
- [A Small Prediction Market Design Taxonomy](https://x.com/aaronjmars/article/1991975420669915328) — aaronjmars · Nov 22, 2025 ·
- [Next Gen Prediction Markets](https://x.com/socialgraphvc/status/1952808508396552534) — Social Graph Ventures · Aug 6, 2025 ·
- [Prediction Markets and Beyond](https://a16zcrypto.com/posts/podcast/prediction-markets-information-aggregation-mechanisms/) — Tabarrok, Kominers, Chokshi (a16z crypto) · Nov 21, 2024 ·
- [From Prediction Markets to Info Finance](https://vitalik.eth.limo/general/2024/11/09/infofinance.html) — Vitalik Buterin · Nov 9, 2024 ·
- [Futarchy as Trustless Joint Ownership](https://www.umbraresearch.xyz/writings/futarchy) — Kevin Heavey · Oct 28, 2024 ·

