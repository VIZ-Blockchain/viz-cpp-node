# AI agents

**Category:** Business & Platforms  
**Source:** [PM Atlas — AI agents](https://www.pmatlas.xyz/concepts/ai-agents)

---

## Definition

**Quick definition.** Autonomous software systems that trade on PMs · potentially improving liquidity and accuracy, but currently underperforming in live experiments.

## Key Insights

- Prediction Arena experiment (Arcada Labs): six AI models (Claude, GPT, Gemini, two Groks, GLM) trade real money on Kalshi with $10K each. After three weeks, five of six are underwater. Only Grok 4.20 ("Mystery Model Alpha") is up ~10%. The smartest LLMs are losing while occasionally placing "Blind Trades" after hitting data quota limits.
- The Avci diagnosis: successful PM traders profit from embodied, local knowledge (monitoring flights, calling embassies) rather than synthesizing public information · a domain where AI remains fundamentally constrained. Raw information processing isn't edge.
- OddChain's Market-Conditioned Prompting (MCP) research: LLMs perform better as updaters than as cold predictors. Giving GPT 5.1 the market-implied probability as a Bayesian prior and asking it to update with news + earnings transcripts outperforms naive prediction. The MCP method beats market baseline most when prices are uncertain (50-70% range).
- Zhao's four-layer agent architecture: data, analysis, execution, learning. Maps the ecosystem of existing agents, compares Kelly criterion vs fixed-fraction bet sizing, surveys cross-platform arbitrage, and outlines business models (agent-as-a-service, liquidity mining, data sales). Predicts AI agents become dominant participants within two years.
- Ben Fielding's agentic bazaar: PMs are the natural marketplace for sovereign AI agents to trade their core commodity · information. Decentralized PMs become the venue where agents monetize alpha through positions, market creators earn fees from surfacing unanswered questions, and reproducible computation enables incorruptible AI judges for dispute resolution.
- Andy Hall (a16z crypto): the hardest PM problem isn't pricing but deciding what actually happened. Proposes cryptographically-committed LLMs as resolution judges · trading human bias and conflicts of interest for more tractable technical vulnerabilities. Cites Polymarket disputes (Venezuela election, Ukraine map manipulation, government shutdown) as evidence current systems fail at scale.
- Hall & Paschal (Truth Machine): only 1.3% of political markets are liquid enough to be manipulation-resistant. Proposed fix includes deploying AI market makers where human interest is insufficient · using AI to subsidize long-tail political liquidity.
- Will Owens (Galaxy): AI is the interface layer for fragmented venues. Convergence toward derivatives · event contracts as hedges, collateral, and composable financial primitives · needs AI aggregators so retail can interact across rails.
- Hiroki Kotabe's AI quartet: content creators (generating markets), event recommenders (personalization), liquidity allocators, and information aggregators. Argues this enables PMs at microscopic scale · making them personally relevant.
- @allquantor's Polymarket plumbing analysis: AI flow is now categorizable as a third order-flow type alongside soft (retail) and hard (professional). Polymarket's order book has surface symmetry at top-of-book but systematic ask-side skew at deeper levels.
- The deeper plumbing problem (@allquantor): capital is trapped · dollars reserved multiple times against mutually exclusive outcomes. Better netting and capital efficiency, not more AI agents, is the actual fix.
- Sealaunch wallet data: crypto markets on Polymarket are dominated by algorithmic execution; politics by casual event-driven participants. Different categories have very different AI-vs-human compositions.
- The "AI as oracle judge" debate (Hall + Fielding): if LLMs commit to their reasoning cryptographically, they become a viable arbitration layer for ambiguous outcomes · but at the cost of new attack surfaces (prompt injection, model jailbreaking).

## Notable Quotes

> "An AI quartet of content creators, event recommenders, liquidity allocator, and information aggregators can catalyze massive new activity in this space."  
> — *Hiroki Kotabe, *The Prediction Market Primitive**

> "Integrating these AIs into the current prediction market framework can enable prediction markets at a microscopic scale, making them personally attractive and relevant."  
> — *Hiroki Kotabe, *The Prediction Market Primitive**

## Where It Matters

AI agents are the leverage layer for both liquidity (subsidized MMs in long-tail markets) and resolution (LLM judges for ambiguous outcomes). They're also the existential threat to today's retail trader edge · once agents become reliable, raw forecasting alpha disappears. For Dekant: an LLM-driven curve-drawing assistant would meaningfully lower the cognitive barrier to using continuous markets, where novice users currently can't translate a belief into a distribution shape.

## Related Concepts

- Liquidity provision · AI MMs as the subsidy mechanism for thin markets
- Oracle design · LLMs as judges in dispute resolution
- Forecasting accuracy · current AI agents underperform, but MCP improves
- Long-tail markets · where AI MMs can plausibly bootstrap liquidity
- Market manipulation · AI flow detection is a new surveillance challenge

## Sources

- [Can LLMs Beat the Market?](https://www.oddchain.com/p/can-llms-beat-the-market) — OddChain · Mar 19 2026 ·
- [Polymarket Doesn't Have a Money Problem. It Has a Plumbing Problem.](https://x.com/ZEITFinance/article/2031500989576949770) — @allquantor · Mar 11 2026 ·
- [Turning Probability into Assets: A Look Ahead at Prediction Market Agents](https://x.com/0xjacobzhao/article/2029229969914904694) — Jacob Zhao · Mar 5 2026 ·
- [Prediction Markets are the Agentic Bazaar](https://x.com/benfielding/article/2023130119708242317) — Ben Fielding · Feb 16 2026 ·
- [Building the Truth Machine](https://freesystems.substack.com/p/building-the-truth-machine) — Andy Hall, Elliot Paschal · Feb 13 2026 ·
- [Is AI Any Good at Predicting?](https://reachavci.substack.com/p/is-ai-any-good-at-predicting) — Mehmet Avci · Feb 2 2026 ·
- [What to Do When Prediction Markets Fail](https://a16zcrypto.substack.com/p/how-ai-judges-can-scale-prediction) — Andy Hall · Jan 24 2026 ·
- [The Shape of Prediction Markets to Come](https://www.galaxy.com/insights/research/prediction-markets-leverage-ai-agents-defi) — Will Owens · Jan 19 2026 ·
- [The Prediction Market Primitive](https://medium.com/inception-capital/the-prediction-market-primitive-e48a055676bf) — Hiroki Kotabe · Apr 4 2024 ·

