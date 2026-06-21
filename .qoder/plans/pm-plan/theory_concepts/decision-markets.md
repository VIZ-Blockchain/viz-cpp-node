# Decision Markets

**Category:** Governance & Decisions  
**Source:** [PM Atlas — Decision Markets](https://www.pmatlas.xyz/concepts/decision-markets)

---

## Definition

**Quick definition.** Prediction markets explicitly designed to inform specific decisions by forecasting outcomes of each option. The mechanism: agree on a metric of success, then run conditional markets to determine which policy or action is most likely to improve that metric.

## Key Insights

- The intellectual lineage runs back to Robin Hanson's futarchy proposal (Hanson, 2000) · decision markets are the operational mechanism that makes "vote on values, bet on beliefs" tractable. They extend prediction markets from forecasting events to selecting actions.
- The current state-of-the-art implementations are MetaDAO (futarchy for startups, using token price as the KPI) and Combinator (decision-market infrastructure protocol). Both use a conditional-futures architecture: "what will the KPI be if we take action A?" vs. "what will the KPI be if we don't?".
- Alex Janiak's diagnosis: current implementations of decision markets fail outside a few narrow market types because of two structural problems · (1) thin markets / lack of informed traders, and (2) overly complicated conditional-futures architecture.
- The trader problem maps onto Minimum Viable Liquidity (MVL): markets only price accurately above a capital threshold that makes it worth informed traders' time to participate. Polymarket and Kalshi clear MVL for major events because audiences are huge; decision markets typically can't because the pool of informed traders is tiny (employees, advisors, a few competitors).
- Decision markets are hardest to use exactly where they would be most valuable: at the company or individual level, where decisions are idiosyncratic and the relevant context (financials, competitive landscape, team capabilities) is private. Markets need traders to be "open book," which is impractical.
- Two cases work in practice despite the theoretical limits: (1) when you want **aggregated opinions** (which landing page converts better, which feature to prioritize) · i.e., a beauty-contest task rather than informational price discovery, and (2) when you want to **distribute trust** rather than optimize a decision (commitment device: a decision can only execute if the market endorses it).
- The architecture problem with token-price-as-KPI: a token's price reflects everything the market believes about the protocol · macro sentiment, unrelated news, general market inefficiency. You can correctly predict that a decision will improve fundamentals and still lose money because the token moves for unrelated reasons. This heavily disincentivizes rational informed trading, exactly what the mechanism depends on.
- Subtler structural issue: if you price tokens against a KPI in a conditional vault and the KPI improves, token holders redeem at higher value · the protocol is effectively shorting its own success metric.
- Galaxy's framing (Pokorny): Decision Markets extend Impact Markets from information revelation to governance automation. Rather than merely surfacing conditional valuations to inform individual decision-making, they directly and bindingly determine whether an organization takes an action based on which outcome the market prices higher.
- For Decision Markets to produce meaningful signals, the traded asset must be **causally connected** to the outcomes the decision aims to optimize; ideally, holders have material ownership claims over the economic value generated. Most crypto governance tokens fail this test · a chain's governance token cannot effectively guide ecosystem grant allocations when its value is structurally detached from the growth of applications built on the chain.
- Retrofitting decision markets onto existing DAOs is hard because established governance, social norms, informal power structures, and token/ownership structures may not align token value with organizational outcomes. New organizations purpose-built for decision-market governance face fewer such constraints · they can architect token economics from inception so success necessarily flows to token value.
- Decision markets are **reductive by design** · they collapse complex tradeoffs into a single dimension (economic value). This is a feature for capital allocation, resource deployment, and economically dominant strategic choices, but a limitation for decisions where alignment, community capital, or other qualitative factors matter.
- LessWrong "Prediction Markets Are Mediocre" critique: conditional prediction markets that ask "Will X lead to consequence Y?" don't actually convert probability assessments into useful policy guidance. The Polymarket "Trump imposes tariffs in first year" market sat at 56% on election day · neither low enough to dissuade Trump-leaning voters nor high enough to function as a deal-breaker. Adding more liquidity only marginally reduces fluctuation; layering meta-markets on meta-markets adds indirection without adding information.
- Hanson's "distilled human judgement" idea (via Vitalik): you have a trusted but expensive judgement mechanism (e.g., a court, expert panel). Set up a prediction market on what that mechanism would decide. 99.99% of the time, you never actually invoke the costly mechanism · you just use the market's average price. 0.01% of the time you invoke it, and compensate participants based on the verdict. This gives you a fast, cheap, credibly neutral "distilled copy" of the expensive process · and is a natural fit for DAOs where most votes shouldn't actually happen.
- Two genuinely promising directions from Janiak: (1) use AI forecasters as automated market makers to drop the MVL floor · synthesizing information and providing initial price discovery so the minimum threshold to attract informed humans drops; (2) abandon conditional futures for noisy KPIs in favor of combinatorial markets directly on the question ("Will this KPI increase if we take this decision?") · less information-dense but legible and free of the architecture problems.
- A subtle but important critique from commenter Elias Kunnas: futarchy outputs a **price instead of a model**. Traders model privately, but the institution doesn't see causal assumptions, hidden-variable audits, or any falsifiable claim to revise later. This is a separate failure mode from thin liquidity and KPI noise · and arguably explains why the working cases (aggregated opinions, commitment devices) are exactly the ones where model legibility doesn't matter.
- Mohamed Elrashid's argument extends this: forecasting accuracy has outpaced product design. The CTO of Cultivate Labs, after a decade running prediction markets inside the US intelligence community, says he "can't remember a single time that someone told us they had issues with the forecasts not being accurate enough." The bottleneck for decision markets isn't accuracy · it's that probability outputs don't slot into decision-makers' workflows. Simulation startups (Aaru, Simile) raised nine-figure rounds in 15 months because they delivered artifacts shaped like the consulting deliverables institutions already buy.

## Notable Quotes

> "Decision markets are hardest to use exactly where they'd be most valuable: at the company or individual level, where decisions are idiosyncratic and private."  
> — *alexjaniak, *Where Are All the Decision Markets?**

> "A token's price reflects everything the market believes about the protocol: macro sentiment, unrelated news, and general market inefficiency. You might correctly predict that a specific decision will improve fundamentals and still lose money because the token moves against you for reasons entirely unrelated to that decision."  
> — *alexjaniak*

> "In 10+ years, I can't remember a single time that someone told us they had issues with the forecasts not being accurate enough."  
> — *Cultivate Labs CTO (quoted in Mohamed Elrashid, *Good Forecasts, Bad Products*)*

> "Even past your two implementation problems, futarchy outputs a price instead of a model. Traders model privately but the institution doesn't see causal assumptions, hidden-variable audits, or any falsifiable claim to revise later."  
> — *Elias Kunnas (comment on Janiak)*

> "Decision Markets extend the Impact Markets mechanism from information revelation to governance automation… directly and bindingly determine whether an organization takes an action based on which outcome the market prices higher."  
> — *Zack Pokorny, Galaxy Research*

> "Decision Markets are powerful precisely because they're reductive: they collapse complex tradeoffs into a single dimension of economic value."  
> — *Zack Pokorny, Galaxy Research*

## Where It Matters

Decision markets are the bridge between prediction markets as "news at the chart level" and prediction markets as governance infrastructure. They are the canonical answer to "what is this category good for beyond elections and crypto prices?" · and the answer so far is murkier than the theory suggests. The current generation of implementations clarifies that information aggregation is necessary but not sufficient: decision markets also need a causally-connected asset, legible interfaces for traders, and ideally AI-driven market makers to drop the MVL floor for idiosyncratic decisions.

## Related Concepts

- **Futarchy** · Decision markets are the operational mechanism inside Hanson's futarchy proposal.
- **Conditional tokens** · The canonical implementation pattern: pABC/fABC (pass/fail) conditional tokens settle in opposite states depending on whether the decision is taken.
- **Impact markets** · Decision markets are Impact markets with binding governance hooks; the same conditional pricing machinery, but the highest-priced state determines organizational action.
- **Minimum viable liquidity** · The MVL framework explains why decision markets struggle: idiosyncratic decisions can't recruit enough informed traders to clear the liquidity threshold.
- **Reflexivity** · Decision markets are a controlled reflexive system: the market's price doesn't just predict the outcome, it determines the action that produces the outcome.
- **Info finance** · Vitalik's broader framing places decision markets as one application within a "correct-by-construction" info-finance discipline.
- **Hyperstition markets** · Decision markets are the rationalist cousin of hyperstition markets; both involve markets choosing outcomes, but hyperstition markets are explicit about reflexivity as a feature.
- **Wisdom of crowds / forecasting accuracy** · Decision markets inherit the wisdom-of-crowds advantage but face a deployment gap: accurate forecasts aren't decisions.

## Sources

- [Where Are All the Decision Markets?](https://www.lesswrong.com/posts/bDxqMn3GJEhPeqZey/where-are-all-the-decision-markets) — alexjaniak, LessWrong · May 12, 2026 ·
- [Good Forecasts, Bad Products](https://mo-el.xyz/blog/good-forecasts-bad-products/) — Mohamed Elrashid · Apr 24, 2026 ·
- [Polls Are Dead. Long Live Prediction Markets.](https://x.com/CalBlockchain/status/2047460674532790499) — Blockchain at Berkeley · Apr 23, 2026
- [On War Markets](https://x.com/probaaron/article/2027765452689014822) — aaron · Feb 28, 2026 ·
- [The Shape of Prediction Markets to Come](https://www.galaxy.com/insights/research/prediction-markets-leverage-ai-agents-defi) — Will Owens, Galaxy · Jan 19, 2026
- [Prediction Markets' Next Frontier: Impact and Decision Markets](https://www.galaxy.com/insights/research/prediction-markets-impact-markets-decision-markets-futarchy) — Zack Pokorny, Galaxy · Jan 12, 2026 ·
- [Stop Predicting. Start Manipulating.](https://x.com/hyperstiti0ns/article/1994422160790536299) — HYPERSTITIONS · Nov 28, 2025 ·
- [A Small Prediction Market Design Taxonomy](https://x.com/aaronjmars/article/1991975420669915328) — aaronjmars · Nov 22, 2025 ·
- [Opportunity Markets](https://www.paradigm.xyz/2025/08/opportunity-markets) — Dave White, Matt Liston, Paradigm · Aug 18, 2025 ·
- [Prediction Markets Are Mediocre](https://www.lesswrong.com/posts/opzJP7QHgyHzMth5o/prediction-markets-are-mediocre) — Ape in the coat, LessWrong · Apr 5, 2025 ·
- [10 Predictions About Prediction Markets](https://x.com/michael_lwy/status/1884570344184578081) — michaellwy · Jan 29, 2025 ·
- [From Prediction Markets to Info Finance](https://vitalik.eth.limo/general/2024/11/09/infofinance.html) — Vitalik Buterin · Nov 9, 2024 ·
- [Futarchy as Trustless Joint Ownership](https://www.umbraresearch.xyz/writings/futarchy) — Kevin Heavey · Oct 28, 2024 ·

