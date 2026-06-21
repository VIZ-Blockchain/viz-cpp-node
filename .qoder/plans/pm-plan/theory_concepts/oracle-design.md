# Oracle Design

**Category:** Oracle & Resolution  
**Source:** [PM Atlas — Oracle Design](https://www.pmatlas.xyz/concepts/oracle-design)

---

## Definition

**Quick definition.** Oracle design is the set of choices a prediction market makes about *how an off-chain outcome becomes an on-chain settlement* · who reports it, who can dispute it, what data sources are canonical, and what economic bonds back honesty. It is the most-cited concept on onprediction.xyz (referenced in 32 articles), because every other property of a prediction market · accuracy, manipulation-resistance, regulatory posture, liquidity · bottlenecks here.

## Key Insights

- **Resolution, not pricing, is the hardest problem.** Andy Hall (a16z): "The hardest problem in prediction markets isn't pricing the future. It's deciding what actually happened." Pricing is solved by liquidity; resolution is solved by mechanism design we haven't shipped yet.
- **Four properties any resolution mechanism must satisfy at once** (Hall): manipulation-resistance, reasonable accuracy, ex ante transparency (rules visible before the bet), and credible neutrality. Human committees fail at neutrality and scale; token voting fails at manipulation-resistance; centralized operators fail at transparency. Nothing on the market today satisfies all four.
- **The "Oracle Problem" is misnamed · it's a *definition* problem.** Aradtski catalogued 12+ Polymarket controversies and argues every single one was about ambiguous event wording, not oracle untrustworthiness. The Hayekian price-discovery argument breaks down when the question itself is semantically contested.
- **There is a resolver trilemma between efficiency, decentralization, and security** (XO Labs). Optimistic oracles with token voting trade security for efficiency; centralized operators trade decentralization for both; AI judges trade some accuracy for transparency. No single mechanism dominates · different markets need different rungs of the ladder.
- **The corruption-cost vs. value-at-stake gap is the central oracle failure mode.** Repeated case studies (government shutdown, Zelensky suit, Ukraine map, Venezuela invasion, Cardi B halftime, TikTok ban) all share the same shape: the *value at stake on the market exceeded the cost of corrupting the oracle*. Until that ratio inverts, oracle-driven settlement is exploitable in principle on every large market.
- **Optimistic oracles (UMA) have a known capture surface.** Token-holders who can both vote on resolutions *and* trade the underlying market are structurally conflicted. Frank Muci's Venezuela post-mortem documents UMA holders coordinating to overrule Polymarket's own stated rules. Lou Kerner's 2024 conspiracy walks through how a Trump-favoring UMA whale could profit twice · once on the trade, once on the vote.
- **Semantic ambiguity is the real adversary.** Matt Levine's framing: "Prediction markets need linguists and philosophers as much as traders." If the contract reads "Will Zelensky wear a suit?", *what is a suit?* is now a $240M question with no objective answer. The market becomes a referendum on definitions, not facts.
- **Source-of-truth attacks are the next attack surface.** When the resolution criterion points at a single URL (an OPM website, a niche map app, an unguarded thermometer near CDG airport), adversaries attack the data source, not the oracle. Gunitsky's "hair-dryer problem": $14,000 won by warming a single thermometer; the oracle was honest, the underlying signal was not.
- **AI/LLM judges with on-chain commitment are the leading proposed fix.** Hall's a16z proposal: at market creation, commit the *exact* LLM model version and prompt to chain. Resolution is then deterministic, transparent, and bribery-resistant (you cannot bribe model weights). The model doesn't have to be perfect · *it has to be predictable.* Traders can then price in its biases the way they price in any other contract parameter.
- **AI judges trade one set of vulnerabilities for another that may be more tractable.** Limitations: hallucination, prompt-engineering failures, source-poisoning attacks against the inputs the model reads, training-data poisoning over long horizons. But none of these involve real-time bribery of a $240M vote.
- **There are five MEV-style edges around oracles** (st1ne): (1) oracle latency arbitrage · trading on news before UMA updates; (2) resolution arbitrage · front-running settlement; (3) dispute sniping · gaming the UMA dispute liveness window; (4) order-book imbalance exploitation around expiry; (5) conditional probability arbitrage across correlated markets. Polymarket is "a hidden MEV playground" · sophisticated actors extract value from oracle structure, not informational edge.
- **Decentralization matters most where the data must be transported on-chain.** Sonal/Tabarrok/Kominers (a16z podcast): if the resolution data already lives on-chain (price feeds, smart-contract outputs), you don't need a decentralized oracle for that question. The decentralization budget should be spent on the *off-chain-to-on-chain transmission*, where MNPI extraction and manipulation cluster.
- **Layered architectures route each market to the security tier it needs** (XO Labs). Low-stakes meme markets can be admin-resolved or AI-resolved cheaply; high-stakes geopolitical markets need expensive decentralized adjudication. Treating resolution as a single mechanism is what produced the $500M tail of resolution failures.
- **Cross-platform divergence is now its own oracle failure mode.** The Cardi B halftime market resolved YES on Polymarket and NO on Kalshi · same event, two answers, two settlement systems, no reconciliation. michaellwy: prediction markets are *fundamentally non-fungible* because each market is "an event + a referee system," and the referees disagree.
- **Resolution disputes can themselves become a venue for state-actor manipulation.** Gunitsky: regimes with information-warfare budgets ($billions/year) can manipulate either trading prices *or* the disputed-resolution mechanism for propaganda value. A million-dollar loss on a propaganda operation is rounding error.
- **Some emerging designs sidestep oracles entirely.** Self-resolving markets (SKC, Monad explainer) use the crowd's own final consensus as the reference outcome. Parimutuel-with-AI-resolver designs (Turf/sdk.markets) let any URL serve as ground truth via an LLM oracle. These are aimed at long-tail/subjective markets that on-chain oracles fundamentally cannot serve.
- **Proprietary centralized oracles with platform skin-in-the-game may outperform decentralized mercenary capital** (Aradtski). The argument: a centralized operator whose brand dies if it cheats is more aligned than a swing-vote of mercenary token-holders whose downside is bounded by token forfeiture.
- **Insurance/cover markets are a stress test for oracle design.** Eigenmann (HIP-4): when binary event contracts share margin with the underlying exposure they hedge, oracle quality directly determines whether the contract acts as parametric cover or as a casino. The bar is much higher than for entertainment markets.
- **The 7 axes of competition include oracle reliability as a first-class differentiator** (Nyquist). New entrants can compete on oracle design itself · credibility tokens, layered resolvers, AI judges · as much as on UX or liquidity.

## Notable Quotes

> "The hardest problem in prediction markets isn't pricing the future. It's deciding what actually happened."  
> — *Andy Hall, *What to Do When Prediction Markets Fail**

> "The model doesn't have to be perfect; it has to be predictable."  
> — *Andy Hall, on LLM-as-judge*

> "Polymarket has descended into sheer arbitrariness. Words are redefined at will, detached from any recognized meaning, and facts are simply ignored."  
> — *Polymarket user, quoted in *Semantics for $10 Million**

> "We've seen that UMA is not a decentralized truth machine, it's just a bunch of people who vote in their own best interests."  
> — *Lou Kerner, *My Polymarket Conspiracy Theory**

> "In our markets, you are absolutely entitled to trade on MNPI that you rightfully own."  
> — *David Miller, CFTC Enforcement Director, March 31 2026 (quoted in michaellwy CFTC comment)*

> "Truth machines go to war."  
> — *Matt Levine framing for Bloomberg Opinion, on Kalshi mentions markets*

## Where It Matters

Oracle design is the load-bearing primitive: it determines whether a prediction market is a price-discovery institution or a venue for extracting value through ambiguity exploitation. Every other technical problem (liquidity, capital efficiency, UX) is downstream · a market with broken resolution attracts toxic flow and bleeds informed traders. The frontier is moving from optimistic-oracle-with-token-voting toward layered architectures (LLM judges for cheap markets, decentralized adjudication for high-stakes, parametric on-chain feeds where possible) and toward defining-the-event-precisely-enough-that-oracle-choice-doesn't-matter.

## Related Concepts

- **dispute resolution** · what happens when the oracle's answer is contested; UMA's DVM voting and similar mechanisms.
- **resolution criteria** · the contract wording itself; "definition problem" is one tier above oracle choice.
- **UMA protocol** · the dominant optimistic-oracle implementation behind Polymarket; the most-stressed instance of oracle design.
- **corruption value multiple** · the quantitative diagnostic: does the value-at-stake exceed the cost of corrupting the oracle?
- **self-resolving markets** · designs that try to avoid oracles entirely.
- **information aggregation** · the Hayekian function oracles are supposed to preserve.
- **incentive compatibility** · whether the oracle's voters/judges are paid for honesty.

## Sources

- [Resolving Prediction Markets: An AI-Driven Layered Approach](https://x.com/xolabs_/status/2055266974259961917) — XO Labs · May 15 2026
- [Orthogonal Precision in Trepa: A Tunable Second-Order Oracle for High-Frequency Forecasting](https://github.com/TrepaOrg/trepa-research/blob/main/Trepa_Orthogonal_Precision_20260513_Blanco%26Chung%26Meka.pdf) — Blanco, Chung, Meka · May 13 2026
- [Outcome Markets as a Cover Venue: HIP-4 and Its Traditional Comparables](https://x.com/DeanEigenmann/status/2052734335971713347) — Dean Eigenmann · May 8 2026
- [The Hidden Risk Of Prediction Markets: 14 Resolution Failures That Cost $500M](https://x.com/xomarket/status/2046924302952640934) — XO Market · Apr 22 2026
- [Every Opinion Deserves A Market](https://x.com/turfsports_/status/2047030930699854334) — Turf · Apr 22 2026
- [Truth Machines Go to War](https://www.bloomberg.com/opinion/newsletters/2026-04-06/truth-machines-go-to-war) — Matt Levine · Apr 6 2026
- [The Economy of Truth: Inside the Decentralized Courtroom of Polymarket & UMA](https://x.com/smaaaaliy/article/2033242239498076190) — Smaliy · Mar 16 2026
- [Polymarket Is a Hidden MEV Playground. Most Traders Have No Idea.](https://x.com/SolSt1ne/article/2032008002094366899) — st1ne · Mar 13 2026
- [On War Markets](https://x.com/probaaron/article/2027765452689014822) — aaron · Feb 28 2026
- [23 Reasons Prediction Markets Are Broken Today](https://x.com/linfluence/status/2026757563518431696) — Alexander Lin · Feb 26 2026
- [Prediction Markets are the Agentic Bazaar](https://x.com/benfielding/article/2023130119708242317) — Ben Fielding · Feb 16 2026
- [Prediction Markets: The Path to $1 Trillion and What Needs to Happen Next](https://x.com/a1research__/article/2022340314770375024) — Sakshi Mishra · Feb 14 2026
- [The 7 Axes of Prediction Markets](https://x.com/Jake_Nyquist/article/2021222519991124343) — Jake Nyquist · Feb 10 2026
- [What to Do When Prediction Markets Fail](https://a16zcrypto.substack.com/p/how-ai-judges-can-scale-prediction) — Andy Hall · Jan 24 2026 ·
- [Semantics for $10 Million: When Is an Invasion an Invasion?](https://www.oddchain.com/p/semantics-for-10-million-when-is-an-invasion-an-invasion-polymarket) — OddChain · Jan 23 2026 ·
- [Assassination Semantics: Why Every Market Carries the Risk of Violence](https://x.com/BetBreakNews/article/2008587654158340126) — Guillory & Zimmermann · Jan 7 2026
- [Why Prediction Markets Are Where They Are](https://tim0x.substack.com/p/why-prediction-markets-are-where) — Tim.0x · Jan 5 2026 ·
- [Prediction Markets · Everything You Need to Know](https://a16zcrypto.com/posts/podcast/prediction-markets-explained/) — Chokshi, Tabarrok, Kominers · Sep 25 2025 ·
- [A Technical Typology of Prediction Markets: Mechanics and Tradeoffs](https://x.com/Baheet_/status/1967186769356488828) — Baheet · Sep 14 2025
- [Meta Pool: A Unified Infrastructure for Liquidity and Resolution Trust in Prediction Markets](https://x.com/jeff__opinion/article/1957611486991487486) — Jeff Opinion · Aug 19 2025
- [Next Gen Prediction Markets](https://x.com/socialgraphvc/status/1952808508396552534) — Social Graph Ventures · Aug 6 2025
- [Prediction Markets: The 'Oracle Problem' Is Not the Problem](https://x.com/aradtski/status/1949977296523145695) — Aradtski · Jul 29 2025
- [The Risk Behind Arbitrage in Prediction Markets](https://x.com/michael_lwy/status/1925890768654254571) — michaellwy · May 23 2025
- [10 Predictions About Prediction Markets](https://x.com/michael_lwy/status/1884570344184578081) — michaellwy · Jan 29 2025
- [The Definitive Guide to Prediction Markets](https://docsend.com/view/atcc6k258s4umq6s) — Four Pillars · Jan 15 2025
- [Why Prediction Markets Are Broken (And How to Fix Them)](https://x.com/michael_lwy/status/1877211555496079683) — michaellwy · Jan 9 2025
- [Prediction Markets (II): Spoiling the Election Love Story](https://dirtroads.substack.com/p/64-prediction-markets-ii-spoiling) — Luca Prosperi · Nov 2 2024 ·
- [My Polymarket Conspiracy Theory](https://medium.com/quantum-economics/my-polymarket-conspiracy-theory) — Lou Kerner · Oct 20 2024
- [Crypto Prediction Markets](https://dirtroads.substack.com/p/63-crypto-prediction-markets) — Luca Prosperi · Oct 11 2024 ·
- [Polymarket and the Proliferation of Prediction Markets](https://www.shoal.gg/p/prediction-markets) — Alex Nardi · Oct 8 2024 ·
- [Mechanisms for Prediction Markets](https://paragraph.com/@mechanism.institute/prediction-markets) — Hadi, Cossar, Shimony · Aug 22 2024 ·
- [Polymarket Settles Bet Against Its Own Rules](https://frankmuci.substack.com/p/polymarket-settles-bet-against-its) — Frank Muci · Aug 8 2024 ·

