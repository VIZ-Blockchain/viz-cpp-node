# Liquidity Provision

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Liquidity Provision](https://www.pmatlas.xyz/concepts/liquidity-provision)

---

## Definition

**Quick definition.** Supplying capital so traders can buy and sell prediction-market positions without excessive price impact or delay. In prediction markets this is structurally harder than in other asset classes because outcomes resolve to binary values and counterparties often hold near-perfect information.

## Key Insights

- Polymarket's late-2022 migration from an LMSR AMM to a central limit order book was the moment the industry recognized that prediction-market liquidity needs a different mechanism than token swaps. In a binary market that resolves to 0 or 1, impermanent loss becomes *permanent*: the pool inevitably holds worthless shares on the losing side, and trading fees can never offset a guaranteed structural loss (Melee, "Why AMMs Failed Prediction Markets").
- CLOBs fixed AMMs' capital destruction but introduced a new pathology: passive participation is essentially impossible and only professional market makers can quote. Kalshi reportedly has ~23 active market makers with the top three providing **70% of election-contract liquidity** · any market those firms ignore is dead on arrival (Melee, "The Problem With CLOBs").
- Polymarket's liquidity is **episodic and attention-driven**: p95 peak hour shows hundreds of millions in open interest while the p50 median is thin. Order book analysis shows surface symmetry at top-of-book but systematic ask-side skew at deeper levels (@allquantor / ZEIT Finance).
- Trapped/locked collateral is the binding constraint, not money. Dollars get reserved multiple times against mutually exclusive outcomes; better netting and capital efficiency, not more capital, is the fix (@allquantor).
- Trading **volume explains <1% of variance in forecast accuracy** across 149 Kalshi CPI markets (Rajaprabhakaran, MVL): the regression of log-MAE on log-volume yielded a slope of −0.063 (p=0.28, R²=0.008) across a 1.6 million-fold volume range. Volume ran from 7 contracts to 11.4M contracts (April 2025 Robinhood-driven spike, 816× the dog-days trough) · and median absolute forecast error stayed near 0.093 percentage points throughout. Volume from casual traders functioned as **venture-capital seed funding** for forecasters to build CPI pipelines; once the pipelines existed, MVL collapsed and the market self-sustained at <10k contracts/event.
- Rajaprabhakaran's MVL formula: **MVL = Cost of Expertise / Price Gap**. Below this floor, the bounty isn't large enough to attract anyone with real information and the price is just noise; above it, additional liquidity doesn't improve accuracy, it just raises the cost the next informed trader has to pay to correct a mispricing. CPI MAE actually *improved* (from 0.131 pp in 2022 to 0.074 pp in 2025) as volume collapsed 95% · the trend tracks a learning curve, not a liquidity curve.
- ARK Invest sizes the prediction-market opportunity at **up to ~$5 trillion medium-term**, framing the real potential as prediction markets becoming "a foundational layer of financial services" rather than disrupting sports betting (Grous, Prasanna, Hadi, ARK Invest, Apr 2026).
- Andy Hall & Elliot Paschal: **98.7% of political prediction markets qualify as "ghost towns"** · wide spreads, almost no one on the other side · yet overall calibration still clusters near the line. Only **70 of 131 resolved US elections (53%)** appeared on both Kalshi and Polymarket; among 1,826 active electoral events, overlap drops to **18%**. They explicitly call this a "public goods problem" solvable via cross-subsidy from sports markets (which generate billions in volume) plus philanthropic funders like Open Philanthropy, Schmidt Futures, or IARPA.
- Hall & Paschal's "Building the Truth Machine" blueprint: (1) stock the shelves with policy questions, (2) fund the floor with paid market-makers, (3) recruit AI traders where humans won't show up, (4) standardize resolution rules across platforms (ISDA-equivalent for PMs) to stop fragmentation. The OPM-website resolution debacle on a Polymarket government-shutdown contract is cited as why standardization matters.
- Alan Wu reframes prediction-market prices as **public goods**: benefits are non-excludable but liquidity costs fall on a narrow trader base. Cross-subsidization (profitable markets fund socially valuable ones) is the proposed growth mechanism, like newspaper ads funding investigative journalism.
- Parimutuel pools solve the cold-start problem because they bootstrap liquidity without market makers; the friction points are locked positions, timing constraints, and price readability (Melee).
- Jeff Opinion's Meta Pool proposes resolution-aware meta-pools that unify liquidity across fragmented platforms with different oracles, estimating **$3.4–8.5M annual efficiency losses** from current fragmentation.
- Functionspace's Information Vectors thesis: binary event contracts fragment liquidity and flatten beliefs into 1-bit structures · achieving 8-bit resolution requires **256 separate markets**. Continuous probability surfaces concentrate liquidity instead.
- Polymarket fast crypto markets (5m, 15m) generate ~16% of platform volume but ~40% of fees; bots control 55–62% of fast-market volume and Bitcoin drives 77% of turnover (Dune, "Anatomy of Polymarket's Fastest Markets").
- Limitless achieves **50–400 bps spreads** on price-prediction markets (better than onchain options at 1000+ bps) · these short-expiry contracts behave like a new asset class enabling synthetic covered calls and structured hedging (Ranger Global).
- Astaria argues prediction-market perpetuals are structurally different from crypto perps because event contracts lack a tradeable underlying; most implementations become "liquidation arcades" and the winning path is institutional event-risk hedging infrastructure.
- Sid argues prediction markets are the **future of advertising** · sponsors seed liquidity in markets traders then research at length, contrasted with seconds of passive display-ad exposure (specific seed-size and per-hour figures from the underlying X article; not independently verifiable from the fetched stub).
- Inception Capital (Hiroki Kotabe, "The Prediction Market Primitive"): the path to billion-user prediction markets runs through an **AI quartet** · content creators, event recommenders, liquidity allocators, and information aggregators · enabling "Tinder-like" microscopic-scale markets embedded in everyday digital life. Vitalik called prediction markets the "holy grail of epistemics technology."
- Polymarket: 4-account cluster controlled ~23% of open interest during 2024 election, ~41% of volume looked like wash trading · current platforms lack the structural conditions for reliable forecasting (Luca Prosperi).
- LessWrong "Will Jesus Christ Return": ~$100k wagered, market stable at 3% · but to fade to 1% requires locking ~$1M for a year to earn 1% (worse than T-bills). The "Time Value of Money" hypothesis explains why "Yes" trades elevated: holders are speculating that No-side counterparties will need cash later in the year to bet on other markets (elections, China invading Taiwan, conclave) and will sell their No positions at a premium. Historical proof: late-October 2024, Kamala in Kentucky and Trump in Massachusetts both traded at 0.3% and spiked to 1.5% on election day as No-holders desperately needed cash · a 5x return. Implication: in low-liquidity PM markets, prices reflect the **time-value of platform cash**, not literal probabilities, and Polymarket loan/liquidity-reward design distorts this further.
- Whitaker & Mazlish: PMs are caught between three participant types that don't match · gamblers want quick resolutions (42% of 2020 election volume traded in the final week), long-term investors prefer compounding in stocks, and MMs need consistent retail flow that only exists for elections/sports. None of the three finds PMF in the broader question space.
- Nardi (Shoal): Polymarket grew TVL to ~$130M and cumulative volume to ~$1B as of late 2024, but its categorical, episodic structure means liquidity is built around 1-2 dominant questions per cycle rather than a generalized base layer.

## Notable Quotes

> "In a binary market that resolves to 0 or 1, impermanent loss becomes permanent: the pool inevitably holds worthless shares on the losing side, and trading fees cannot offset a guaranteed structural loss."  
> — *Melee, "Why AMMs Failed Prediction Markets"*

> "Kalshi reportedly has 23 active market makers with the top three providing 70% of election-contract liquidity, meaning any market those firms ignore is dead on arrival."  
> — *Melee, "The Problem With CLOBs"*

> "Trading volume explains less than 1% of variance in forecast accuracy."  
> — *Adhi Rajaprabhakaran, "Minimum Viable Liquidity"*

> "Polymarket doesn't have a money problem. It has a plumbing problem."  
> — *@allquantor*

> "The information doesn't require deep liquidity. It requires the right people, the right questions, and just enough money to make it worth their while."  
> — *Rajaprabhakaran, on MVL*

> "The primary product of prediction markets isn't the ability to trade. It's the information that trading produces. The price is the product."  
> — *Rajaprabhakaran, cited in Hall & Paschal*

> "Right now, there's not much interesting stuff happening on Polymarket... at some point in 2025, other markets will get a lot of attention. The New York mayoral election... a new pope... and if it changes, some of the people betting No on Christ's return will want to unlock that money."  
> — *Eric Neyman, LessWrong, on Time-Value-of-PM-Cash*

## Where It Matters

Liquidity provision is the bottleneck on every other property of the asset class: thin markets cannot resist manipulation, cannot attract informed traders, cannot offer leverage safely, and cannot serve as hedging instruments. Because binary outcomes make traditional market-making unprofitable without subsidies, every successful platform must either subsidize liquidity (LMSR/incentives), concentrate it (CLOB with pro MMs), or pool it (parimutuel/AMM with adjusted mechanics) · and most of the design space in 2026 is occupied by attempts to escape the trade-offs each of these creates.

## Related Concepts

- **Market making** · the supply side of liquidity provision; rests on adverse-selection economics.
- **Adverse selection** · explains why binary markets are structurally hostile to passive liquidity.
- **Minimum viable liquidity** · argues there is a *threshold* of liquidity that matters, not unbounded depth.
- **Order book / continuous double auction** · the dominant trading mechanism that replaced AMMs.
- **Parimutuel markets** · the bootstrap alternative when no market maker exists.
- **LMSR / market scoring rules** · the original automated subsidy mechanism, now mostly abandoned for binaries.
- **Liquidity fragmentation** · what happens when many binary markets divide a question; meta-pool / covariance-market solutions try to undo this.
- **Position collateralization** · frees locked capital, the operational definition of "plumbing problem."
- **Long-tail markets** · the unsolved liquidity-provision frontier.

## Sources

- [Sportsbooks vs Prediction Markets - Market Structure and Its Effects](https://x.com/0xtaetaehoho/status/2054215076471873575) — taetaehoho · May 12, 2026
- [A Game of Volatility](https://x.com/Kunallegendd/status/2054191944944038084) — Kunal Doshi · May 12, 2026
- [Prediction Market Perps - the 1% Winning Product](https://x.com/AstariaTrade/status/2051363114881568852) — Astaria · May 4, 2026
- [Prediction Markets: The Potential Multi-Trillion Dollar Asset Class Hiding In Plain Sight](https://www.ark-invest.com/articles/analyst-research/prediction-markets-potential-multi-trillion-dollar-asset-class) — Nicholas Grous, Varshika Prasanna, Raye Hadi (ARK Invest) · Apr 22, 2026
- [The Problem With CLOBs](https://x.com/meleemarkets/status/2046318159225897289) — Melee · Apr 21, 2026
- [What Happens When Institutional Liquidity Enters Prediction Markets](https://x.com/DaedalusRsch/status/2046219948452979151) — Daedalus Research · Apr 20, 2026
- [Faster, Shorter, More Automated: Anatomy of Polymarket's Fastest Markets](https://x.com/Dune/status/2044045952730726863) — Dune · Apr 14, 2026
- [Why AMMs Failed Prediction Markets](https://x.com/meleemarkets/status/2043766417342755259) — Melee · Apr 13, 2026
- [Parimutuel Prediction Markets](https://x.com/meleemarkets/status/2041244791716110409) — Melee · Apr 6, 2026
- [How Prediction Markets Can Ascend](https://x.com/alanwu/article/2036438579824496906) — Alan Wu · Mar 25, 2026
- [The World's Biggest Risk Event Just Exposed Prediction Markets' Biggest Gap](https://x.com/jolimmmm/article/2036119259420807216) — Jo Lim · Mar 24, 2026
- [Noisy Traders Are Not Dumb Money](https://x.com/functionspaceHQ/article/2032397043579462028) — functionSPACE · Mar 13, 2026
- [Polymarket Doesn't Have a Money Problem. It Has a Plumbing Problem.](https://x.com/ZEITFinance/article/2031500989576949770) — @allquantor · Mar 11, 2026
- [Turning Probability into Assets: A Look Ahead at Prediction Market Agents](https://x.com/0xjacobzhao/article/2029229969914904694) — Jacob Zhao · Mar 5, 2026
- [Prediction Markets Are the Future of Advertising](https://x.com/sidbharth/article/2027740100214603934) — Sid · Feb 28, 2026
- [23 Reasons Prediction Markets Are Broken Today](https://x.com/linfluence/status/2026757563518431696) — Alexander Lin · Feb 26, 2026
- [Minimum Viable Liquidity](https://fiftycentdollars.substack.com/p/minimum-viable-liquidity) — Adhi Rajaprabhakaran · Feb 24, 2026
- [Prediction Markets Are Not Good Markets (Yet)](https://murmurationstwo.substack.com/p/prediction-markets-are-not-good-markets) — Nic Carter · Feb 21, 2026
- [Prediction Markets: The Path to $1 Trillion and What Needs to Happen Next](https://x.com/a1research__/article/2022340314770375024) — Sakshi Mishra · Feb 14, 2026
- [Leverage Fixes Prediction Markets: The Case for Why 10x Is Safer Than 1x](https://x.com/njokuScript/article/2022351299547689325) — njokuScript · Feb 14, 2026
- [Building the Truth Machine](https://freesystems.substack.com/p/building-the-truth-machine) — Andy Hall, Elliot Paschal · Feb 13, 2026
- [The 7 Axes of Prediction Markets](https://x.com/Jake_Nyquist/article/2021222519991124343) — Jake Nyquist · Feb 10, 2026
- [How to Solve Insider Trading in Prediction Markets](https://www.dopaminemarkets.com/p/how-to-solve-insider-trading-in-prediction) — Shreyas Hariharan · Feb 10, 2026
- [How Prediction Market Traders Reinvented the Bond](https://www.ft.com/content/3382195d-b417-4fc8-9c2e-17f38027a635) — Stephanie Stacey (FT) · Feb 6, 2026
- [The Super Bowl of Prediction Markets: Kalshi and Polymarket's Battle for Price vs Liquidity](https://panteraresearchlab.xyz/research/the-super-bowl-of-prediction-markets-kalshi-and-polymarkets-battle-for-price-vs-liquidity/) — Ally Zach, Danning Sui · Feb 5, 2026
- [The Option Value of Waiting in Prediction Markets](https://x.com/0xnagu/article/2016620973391564951) — 0xnagu · Jan 28, 2026
- [Everyone's Promising 20x Leverage on Prediction Markets. Here's Why It's Hard.](https://medium.com/@nick.c.ruzicka/everyones-promising-20x-leverage-on-prediction-markets-here-s-why-it-s-hard-8684d9a77e11) — Nick Ruzicka · Jan 27, 2026
- [Information Vectors: An Intro to Composable Beliefs](https://x.com/functionspaceHQ/article/2014809461647671570) — functionSPACE · Jan 24, 2026
- [The Shape of Prediction Markets to Come](https://www.galaxy.com/insights/research/prediction-markets-leverage-ai-agents-defi) — Will Owens (Galaxy) · Jan 19, 2026
- [Prediction Markets: A New Kind of Asset Class](https://x.com/njokuScript/article/2010080717729046613) — njokuScript · Jan 11, 2026
- [Why Prediction Markets Are Where They Are](https://tim0x.substack.com/p/why-prediction-markets-are-where) — Tim.0x · Jan 5, 2026
- [Liquidity in Prediction Markets and the Rise of a New Asset Class](https://x.com/Ranger_Global/status/2008177990971122003) — Ranger Global · Jan 5, 2026
- [Prediction Markets: The Next Level](https://x.com/0xkeshav_/article/1970163909224169626) — keshav · Sep 23, 2025
- [The Liquidity Problem in Prediction Markets: Part 0](https://x.com/semajeth/status/1966144230826414225) — semaji.eth · Sep 11, 2025
- [Meta Pool: A Unified Infrastructure for Liquidity and Resolution Trust in Prediction Markets](https://x.com/jeff__opinion/article/1957611486991487486) — Jeff Opinion · Aug 19, 2025
- [Opportunity Markets](https://www.paradigm.xyz/2025/08/opportunity-markets) — Dave White, Matt Liston (Paradigm) · Aug 18, 2025
- [How to Offer Prediction Market Parlays Without Fragmenting Liquidity](https://x.com/voliti_co/status/1946232456459255869) — Chicken · Jul 18, 2025
- [Will Jesus Christ Return in an Election Year?](https://www.lesswrong.com/posts/LBC2TnHK8cZAimdWF/will-jesus-christ-return-in-an-election-year) — LessWrong · Mar 25, 2025
- [How Manipulable Are Prediction Markets?](https://arxiv.org/abs/2503.03312) — Itzhak Rasooly, Roberto Rozzi · Mar 5, 2025
- [The Definitive Guide to Prediction Markets](https://docsend.com/view/atcc6k258s4umq6s) — Four Pillars · Jan 15, 2025
- [No, You Can't Bet on Everything and That's Okay](https://rnikhil.com/2024/12/18/prediction-market-crypto) — Nikhil R · Dec 18, 2024
- [Prediction Markets (II): Spoiling the Election Love Story](https://dirtroads.substack.com/p/64-prediction-markets-ii-spoiling) — Luca Prosperi · Nov 2, 2024
- [Crypto Prediction Markets](https://dirtroads.substack.com/p/63-crypto-prediction-markets) — Luca Prosperi · Oct 11, 2024
- [Polymarket and the Proliferation of Prediction Markets](https://www.shoal.gg/p/prediction-markets) — Alex Nardi · Oct 8, 2024
- [Mechanisms for Prediction Markets](https://paragraph.com/@mechanism.institute/prediction-markets) — Raye Hadi, Sofia Cossar, Ori Shimony · Aug 22, 2024
- [Why Prediction Markets Aren't Popular](https://worksinprogress.co/issue/why-prediction-markets-arent-popular/) — Nick Whitaker, J. Zachary Mazlish · May 17, 2024
- [The Prediction Market Primitive](https://medium.com/inception-capital/the-prediction-market-primitive-e48a055676bf) — Hiroki Kotabe · Apr 4, 2024

