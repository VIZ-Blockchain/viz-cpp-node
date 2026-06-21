# Information Aggregation

**Category:** Information Theory  
**Source:** [PM Atlas — Information Aggregation](https://www.pmatlas.xyz/concepts/information-aggregation)

---

## Definition

The process by which markets combine dispersed private knowledge into a single consensus price signal. The Hayekian core idea: a market price is a low-bandwidth summary of every trader's private information, weighted by how much capital they're willing to stake on that information.

## Key Insights

- Roughly 3% of accounts drive most price discovery on prediction markets · their trades anticipate future prices, respond to news immediately, and improve calibration across a market's lifecycle. The other 97% contribute volume but minimal information, and their losses fund the informed minority (Gomez-Cram et al., SSRN 2026 · [FULL_READ] via Cloudflare; editorial summary preserved).
- This finding *reframes* the standard wisdom-of-crowds narrative: PM accuracy is not the average opinion of many · it's an informed minority that retail flow subsidizes. Has direct implications for platform design, surveillance, and how platforms market "accuracy."
- Insider trading is structurally a feature of information aggregation, not a bug: insider flow is what makes prices accurate (Nic Carter, citing Mansour, Coplan, Tenev, Hanson). Platforms face a calibration problem: too permissive and noise traders flee perceiving rigging; too strict and informed flow disappears, prices decay into sentiment.
- Carter spells out the contradiction directly: "The social value of prediction markets derives from financially incentivizing insiders to divulge confidential information, but this collapses noise trader confidence in the market over time." This is the central regulatory paradox the industry has not resolved.
- Hanson (quoted by Decrypt and cited by Carter): "If the point of [prediction] markets is to get accurate information on the prices, then you definitely want to allow insiders to trade, even if that discourages other people from betting because that makes the prices more accurate. And that's the priority."
- Prediction markets are the next stage in the history of expression (print → radio → social media → markets) · only markets demand that speakers bear consequence for being wrong. Frames staked speech as out-trusting cheap talk in an AI-saturated information environment (Abhitej, Bento.fun).
- Polymarket data shows extreme concentration: 70% of 1.7M addresses lost money; the top 0.04% captured >70% of $3.7B realized profits. The structure funnels retail into informed counterparties, including platform-operated MM desks at Kalshi and Crypto.com (Momin).
- Play-money prediction markets are accurate only in a low-manipulation regime; this accuracy is self-undermining · the more important they become, the more valuable they are to manipulate (alan).
- 2025 prediction market volume = ~$63.5B with $200B+ 2026 run rate. Structural tension: sports drive current revenue (83% of Kalshi volume) but valuations price in an information infrastructure future that hasn't arrived (Kaviish).
- "Information Vectors" thesis: binary contracts fragment liquidity and flatten beliefs into 1-bit structures; achieving 8-bit resolution requires 256 separate markets. Proposal: treat beliefs as vectors over probability distributions on a shared liquidity surface; reward variance compression (entropy reduction), not just final outcome correctness (functionSPACE).
- Probability layers thesis: prediction markets are a proof-of-concept for a broader shift. Three layers beyond trading: attention markets (price content virality), credibility markets (trust as continuously updated score), demand markets (consumer intent before production) (Aggie).
- TAM should include the supply side: as the cost of producing real-time probability estimates collapses, the addressable market extends beyond trading volume to every decision that benefits from better forecasts. Liquidity formation runs entertainment → information → institutional (functionSPACE).
- LLM-as-updater framing: more tractable than LLM-as-predictor. Distinguish cold prediction (no prior context) from updating (revising existing estimates as new info arrives) · implies AI tools deployed *alongside* human traders rather than replacing them (OddChain).
- OddChain detail (the MIT/Berkeley/Seoul/Kalshi "Market-Conditioned Prompting" paper): 856 Kalshi earnings-call mention market contracts spanning 50 companies and 70 earnings events; LLM (GPT 5.1) given up to 100 news articles + prior transcripts. *Without* MCP framing, the LLM underperformed the market baseline. MixMCP (70% market, 30% MCP) improved Brier from 0.1402 → 0.1392 and accuracy from 79.8% → 80.3% · statistically tiny (~4 extra correct predictions out of 856). The 50–60% probability band is where MCP wins most often (17/30 cases; 5/8 in 60–70% band).
- "Discovery vs Betrayal" framework for insider trading: in distributed-truth markets like elections, insiders sharpen the signal because no one holds the full answer; in concentrated-truth markets like earnings, insiders monetize sealed results rather than synthesize public fragments. The real question is what kind of asymmetry a market can absorb (Dougie).
- Tetlock's Superforecasting framework was operationalized at scale by Polymarket · converting crowd forecasting into a liquid financial market (Ahnianchykau).
- Prediction markets sometimes *underperform* simple baselines: Polymarket forecasts for weekly influenza hospitalizations were "dominated by the FluSight ensemble," and monthly measles forecasts were "outperformed by simple statistical baselines." Diagnosed failure modes: "placement of probability mass on impossible outcomes (e.g., decreasing values in cumulative forecasts)" and low trading volume. The best ensemble combination "puts zero weight on the markets" (Dudley & Magdaleno, May 2026).
- Game-theoretic foundation: truth-telling is dominant strategy through incentive compatibility; LMSR works as a proper scoring rule (Baheet).
- Self-resolving prediction markets can work for unverifiable outcomes · Srinivasan/Karger/Chen 2023 prove it is a "perfect Bayesian equilibrium (PBE) for all agents to report truthfully" when payoff is negative cross-entropy vs a reference agent who observes the full market history. Critically, this design works for verifiable AND unverifiable outcomes.
- Nielsen-moment thesis: coordination value > accuracy. Avci's full argument: Nielsen's authority didn't come from methodological correctness · diary-based sampling was known to be flawed · but from being the shared reference point. Opting out became "professional exile." Billboard's pre-1991 charts missed entire genres (hip-hop, country) because suburban record stores dominated the sample. SoundScan revealed albums debuting at #1 that never cracked the top 40. Implication: once Polymarket/Golden Globes and Kalshi/CNN partnerships lock in, displacement is nearly impossible regardless of methodological superiority.
- Combinatorial prediction markets · Powell, Hanson, Laskey, Twardy (SUM 2013) · extend the standard model to conditional events ("A if B") and Boolean combinations. Their DAGGRE experimental study used a murder-mystery scenario with a Bayesian network providing gold-standard probabilities. Theory: "the greater expressivity of combinatorial prediction markets should improve accuracy by capturing dependencies among related questions."
- Trepa's orthogonal precision multiplier rewards forecasts decorrelated from consensus, addressing Keynesian-beauty-contest equilibrium where private information gets underweighted (Blanco, Chung, Meka).
- Vitalik's umbrella framing: info finance is "correct by construction" · start from a fact you want to know, then deliberately design a market to optimally elicit it. Prediction markets are a three-sided market: bettors predict, readers consume, market outputs predictions as a public good. AI is the unlock: "we could potentially get reasonably high-quality info elicited even on markets with $10 of volume."
- Hanson's "distilled human judgement" mechanism: subsidize prediction markets that predict what an expensive trusted human process *would* say if invoked; only invoke that process 0.01% of the time. The market becomes a credibly neutral fast/cheap "distilled version" of the costly mechanism.

## Notable Quotes

> "Roughly 3% of accounts drive most price discovery: their trades anticipate future prices, respond to news immediately, and improve calibration across a market's lifecycle."  
> — *Gomez-Cram, Guo, Jensen, Kung, "Prediction Market Accuracy: Crowd Wisdom Or Informed Minority?"*

> "If the point of [prediction] markets is to get accurate information on the prices, then you definitely want to allow insiders to trade, even if that discourages other people from betting because that makes the prices more accurate. And that's the priority."  
> — *Robin Hanson (in Decrypt, cited by Nic Carter)*

> "The social value of prediction markets derives from financially incentivizing insiders to divulge confidential information, but this collapses noise trader confidence in the market over time."  
> — *Nic Carter, *Prediction Markets Are Not Good Markets (Yet)**

> "Insiders sharpen the signal because no one holds the full answer; in concentrated-truth markets like earnings, insiders monetize sealed results rather than synthesize public fragments."  
> — *Dougie, "Discovery and Betrayal"*

> "Achieving 8-bit resolution requires 256 separate markets."  
> — *functionSPACE, "Information Vectors"*

> "Info finance is that, but correct by construction. Similar to the concept of correct-by-construction in software engineering, info finance is a discipline where you (i) start from a fact that you want to know, and then (ii) deliberately design a market to optimally elicit that information from market participants."  
> — *Vitalik Buterin, *From Prediction Markets to Info Finance**

> "Nielsen provided coordination rather than truth, and coordination is harder to displace than accuracy because coordination compounds"  
> — *the more people use a system, the more costly it becomes to use something else." · Mehmet Avci, *The Nielsen Moment for Prediction Markets**

## Where It Matters

Information aggregation is the *core thesis* every PM platform sells · but the Gomez-Cram paper recasts the story: PMs work because of a sharp informed minority, not crowds. That changes platform design (do you cultivate sharps or retail? both, but for different reasons), surveillance (insider flow is constitutive, not deviant), and product framing (don't sell "wisdom of crowds" · sell "informed pricing under skin-in-the-game"). It also reshapes the manipulation conversation: real-money markets resist manipulation precisely because informed traders profit from correcting it. For Dekant's distribution-market thesis, the implication is that you want the curve-drawing primitive to *reward* the informed minority with a richer surface than a binary, so their information actually transmits at higher resolution.

## Related Concepts

- **Price discovery** · the surface; information aggregation is the underlying process
- **Wisdom of crowds** · the folk theory; PM data partly debunks it (informed minority hypothesis)
- **Forecasting accuracy** · the testable consequence
- **Adverse selection / Insider trading** · the dark side of informed flow that platforms must manage
- **Distribution markets** · argued by Tide/functionSPACE as the higher-resolution form of aggregation
- **Info finance** · Vitalik's framing of PMs as one app inside a wider information-pricing stack
- **Probability infrastructure** · Aggie's framing of the embedded endgame
- **Keynesian beauty contest** · the failure mode where private info gets underweighted

## Sources

- [Orthogonal Precision in Trepa: A Tunable Second-Order Oracle for High-Frequency Forecasting](https://github.com/TrepaOrg/trepa-research/blob/main/Trepa_Orthogonal_Precision_20260513_Blanco%26Chung%26Meka.pdf) — Ilich Blanco, Jong-Chan Chung, Leon Meka · May 13, 2026 [FULL_READ]
- [Prediction Markets Underperform Simple Baselines For Infectious Disease Forecasting](https://arxiv.org/abs/2605.11220) — Carson Dudley, Reiden Magdaleno · May 11, 2026 [FULL_READ · abstract]
- [Prediction Markets Have An Inescapable Insider Trading Problem](https://x.com/nic_carter/status/2048123008200724599) — Nic Carter · Apr 26, 2026 [FULL_READ]
- [Predictions Are The New Expression](https://x.com/abhitejxyz/status/2047353485700825546) — Abhitej · Apr 24, 2026 [FULL_READ]
- [Polls Are Dead. Long Live Prediction Markets.](https://x.com/CalBlockchain/status/2047460674532790499) — Blockchain at Berkeley · Apr 23, 2026 [FULL_READ]
- [The Prediction Market Epidemic: Who's Actually Winning](https://x.com/mominsaqib/status/2046551020231385529) — Momin · Apr 21, 2026 [FULL_READ]
- [When Prediction Markets Need Stake](https://x.com/alanwu/status/2044049214393524393) — alan · Apr 14, 2026 [FULL_READ]
- [The Financialization of Uncertainty](https://x.com/kaviish/status/2041214800731033700) — Kaviish · Apr 6, 2026 [FULL_READ]
- [Prediction Market Accuracy: Crowd Wisdom Or Informed Minority?](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6617059) — Gomez-Cram, Guo, Jensen, Kung · Apr 1, 2026 [FULL_READ]
- [How to Use Prediction Markets as a High Quality Info Source](https://uncover.substack.com/p/how-to-use-prediction-markets-as) — Isar Bhattacharjee · Mar 30, 2026 [FULL_READ]
- [The Probability Layers Are Coming](https://x.com/BlondiePredicts/status/2038595927225622569) — Aggie · Mar 30, 2026 [FULL_READ]
- [Information as Supply](https://x.com/functionspaceHQ/article/2035959494728176075) — functionSPACE · Mar 23, 2026 [FULL_READ]
- [Can LLMs Beat the Market?](https://www.oddchain.com/p/can-llms-beat-the-market) — OddChain · Mar 19, 2026 [FULL_READ]
- [Discovery and Betrayal: Insiders in Prediction Markets](https://x.com/DougieDeLuca/article/2033910178341413105) — Dougie · Mar 18, 2026 [FULL_READ]
- [The Book That Predicted Polymarket](https://x.com/mikita_crypto/article/2029939210350645729) — Mikita Ahnianchykau · Mar 6, 2026 [FULL_READ]
- [Ahead of the Headlines: Prediction Markets and the Collective Mind](https://jprz1321.substack.com/p/ahead-of-the-headlines-prediction) — JP · Feb 25, 2026 [FULL_READ]
- [Polymarket Is Not a Casino. Why Prediction Markets Are Finance, Not Gambling](https://x.com/13_niakris/article/2025614474263093627) — Niakris · Feb 23, 2026 [FULL_READ]
- [The Truth Machine Era Is Here](https://x.com/dgt10011/status/2024228182178595247) — Jeff Park · Feb 19, 2026 [FULL_READ]
- [Prediction Markets are the Agentic Bazaar](https://x.com/benfielding/article/2023130119708242317) — Ben Fielding · Feb 16, 2026 [FULL_READ]
- [Thoughts on the Law of Insider Trading and Prediction Markets](https://x.com/dbarabander/article/2019769802735178236) — Daniel Barabander · Feb 6, 2026 [FULL_READ]
- [Prediction Markets Don't Bend Reality](https://fiftycentdollars.substack.com/p/prediction-markets-dont-bend-reality) — Adhi Rajaprabhakaran · Feb 3, 2026 [FULL_READ]
- [What If We're Capturing the Wrong Signal?](https://x.com/TideMarkets/article/2016912289941827801) — Jo · Jan 29, 2026 [FULL_READ]
- [Prediction Markets as an Asset Class](https://x.com/akshayraj_v0/article/2016909929920233802) — Akshay · Jan 29, 2026 [FULL_READ]
- [The Option Value of Waiting in Prediction Markets](https://x.com/0xnagu/article/2016620973391564951) — 0xnagu · Jan 28, 2026 [FULL_READ]
- [Information Vectors: An Intro to Composable Beliefs](https://x.com/functionspaceHQ/article/2014809461647671570) — functionSPACE · Jan 24, 2026 [FULL_READ]
- [The Nielsen Moment for Prediction Markets](https://reachavci.substack.com/p/the-nielsen-moment-for-prediction) — Mehmet Avci · Jan 12, 2026 [FULL_READ]
- [Manifesto: Make Precision Pay](https://x.com/TideMarkets/article/2008508916616098086) — Tide · Jan 6, 2026 [FULL_READ]
- [Prediction Markets · Everything You Need to Know](https://a16zcrypto.com/posts/podcast/prediction-markets-explained/) — Sonal Chokshi, Alex Tabarrok, Scott Kominers · Sep 25, 2025 [FULL_READ · podcast notes]
- [The Game Theory Behind Prediction Markets](https://x.com/Baheet_/status/1965758390430208066) — Baheet · Sep 10, 2025 [FULL_READ]
- [How Manipulable Are Prediction Markets?](https://arxiv.org/abs/2503.03312) — Itzhak Rasooly, Roberto Rozzi · Mar 5, 2025 [FULL_READ · abstract]
- [The Definitive Guide to Prediction Markets](https://docsend.com/view/atcc6k258s4umq6s) — Four Pillars · Jan 15, 2025 [FULL_READ]
- [Designing Markets for Prediction](https://bpb-us-e1.wpmucdn.com/sites.harvard.edu/dist/b/845/files/2025/01/aim10.pdf) — Yiling Chen, David M. Pennock · Jan 14, 2025 [FULL_READ · PDF]
- [Prediction Markets and Beyond](https://a16zcrypto.com/posts/podcast/prediction-markets-information-aggregation-mechanisms/) — Tabarrok, Kominers, Chokshi · Nov 21, 2024 [FULL_READ · podcast notes]
- [From Prediction Markets to Info Finance](https://vitalik.eth.limo/general/2024/11/09/infofinance.html) — Vitalik Buterin · Nov 9, 2024 [FULL_READ]
- [Crypto Prediction Markets](https://dirtroads.substack.com/p/63-crypto-prediction-markets) — Luca Prosperi · Oct 11, 2024 [FULL_READ]
- [The Art of Forecasting](https://paragraph.com/@filarm/the-art-of-forecasting) — fil · Sep 30, 2024 [FULL_READ]
- [Deep Dive #8 | Decentralized Prediction Markets](https://ampbura.substack.com/p/deep-dive-8-decentralized-prediction) — Amp Burapachaisri · Feb 23, 2024 [FULL_READ]
- [Self-Resolving Prediction Markets for Unverifiable Outcomes](https://arxiv.org/abs/2306.04305) — Srinivasan, Karger, Chen · Jun 7, 2023 [FULL_READ · abstract]
- [Should Prediction Markets Be Charities?](https://www.overcomingbias.com/p/should_predictihtml) — Peter McCluskey · Dec 11, 2006 [FULL_READ]

