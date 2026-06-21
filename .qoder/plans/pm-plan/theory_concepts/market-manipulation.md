# Market Manipulation

**Category:** Mechanism Design  
**Source:** [PM Atlas — Market Manipulation](https://www.pmatlas.xyz/concepts/market-manipulation)

---

## Definition

Deliberately trading to distort prices away from true probabilities for strategic or financial gain. In prediction markets, manipulation has unusually high stakes because prices double as public information signals, so distorted prices contaminate journalism, policy, and downstream financial decisions.

## Key Insights

- Rasooly & Rozzi's field experiment (arXiv 2503.03312) "involves randomly shocking prices across 817 separate markets; we then collect hourly price data to examine whether the effects of these shocks persist over time" · manipulation effects "are visible even 60 days after they have occurred" but "as predicted by our model, the effects of the manipulations somewhat fade over time." Markets with more traders, greater volume, and external probability estimates are harder to manipulate.
- Rajiv Sethi's wash-trading paper (Sirolly, Ma, Kanoria, Sethi) introduces a modular detection algorithm with two stages: (1) initialization · each wallet receives a score based on "propensity to open and close positions repeatedly"; (2) iterative network-based redistribution · "we update each wallet's score based on the scores of its volume-weighted counterparties" until convergence. Wash traders exhibit *homophily* · they trade only within their collusive clique; market makers exhibit *heterophily* · "market makers neither know nor care who their counterparties are."
- Concrete wash-trading cluster found: "MAY20, MAY175, and MAY176 ... part of a cluster of 200 with names all starting with MAY, trading almost exclusively with each other. These wallets collectively traded over 116 million shares and generated more than 113 million in dollar volume, but ended up with an aggregate loss of just $57.86." Wallet MAY117 alone "bought and sold over a million shares across 33 markets over several months and ended up with profits of precisely zero."
- Sethi's example trade pattern: a trader buys, then sells at "a price that is a tenth of a penny lower, resulting in a guaranteed loss" · accepting modest losses in exchange for boosting recorded volume. Such activity is "prohibited by law on regulated exchanges in the United States, and can be subject to significant sanctions."
- Wash-trading detection is robust to parameter changes but vulnerable to strategic adaptation · Sethi flags that any published algorithm "is likely to result in strategic responses by wash traders that better allow them to evade detection."
- Sethi *Trading on Violence*: six Polymarket wallets opened shortly before US strikes on Iran, bought large positions, "banked $1.2 million in profits in a matter of hours." Kalshi has run "more than 200 investigations into insider trading, with a dozen cases currently active." KYC at Kalshi is "primarily designed to address money laundering and terrorism financing, but also facilitates the detection of insider trading."
- The Polymarket / Kalshi structural asymmetry on insider trading: Polymarket trades in USDC with on-chain transparency but pseudonymous wallets · "Insider trading under these conditions becomes easier to notice but harder to prevent."
- Same-named contracts resolve differently: Polymarket's "Khamenei out as Supreme Leader" resolved YES on his death, while the corresponding Kalshi contract "resolved to the price at last trade prior to his death." Kalshi CEO position: "as a federally regulated prediction market, we are required and feel it is important not to enable direct profiting from war, assassination, terrorism, or other violent outcomes."
- Hanson (Overcoming Bias) frames prediction-market regulation around six info-institution failure modes · (A) reveal info better kept secret, (B) reveal secrets, (C) waste time/money, (D) misleading contributions, (E) change the world to get favorable treatment, (F) reward participants unequally · and argues all info institutions (journalism, academia, prediction markets) should face the same scrutiny.
- Hanson on insider trading: "for stocks ... at public firms announcements, half of the price change happens beforehand, and half of that is from insider trading. We shouldn't expect prediction market rules to succeed much better, or to result in much worse harms."
- Hanson on manipulation resistance: "speculative markets are actually far more resistant to manipulation than other info institutions. When traders expect more efforts to manipulate a price, they respond so that prices on average become MORE accurate. Also, in head-to-head comparisons with other info institutions, with the same question, time, participants, and resources, speculative markets have been consistently about as accurate or much more accurate."
- Hanson's life-insurance analogy on (E): "life insurance has big enough stakes and easy enough personal influence that we reasonably regulate it to prevent murder for money. But we see almost no cases of traders successfully sabotaging firms to profit from stock trades."
- Gunitsky (*Priced to Kill*) reports an IDF major's testimony: "The entire squadron is on Polymarket. The entire air force is betting." A "study of every transaction on Polymarket found only three percent of accounts are responsible for almost all price discovery. The remaining suckers generate volume but no information."
- Gunitsky on conflict-market mechanics: pair of Israeli traders "split $160,000 in winnings" on strike timing, then bet again on the 12-day war and a Yemen strike. A US Army special forces soldier was indicted for betting on Maduro's removal "before Trump announced the capture, which paid out over $400,000."
- The "hair dryer problem": Polymarket settled Paris temperature bets via "an unguarded sensor near the Charles de Gaulle airport." A bettor bought long-shot abnormally-high-temperature contracts; "the sensor saw a four-degree spike before dropping back to normal. No neighboring station registered anything similar. The contract resolved in the bettor's favor." The bettor collected $14,000 then $20,000 again on April 15. France's meteorological agency filed a criminal complaint. Polymarket switched data sources but did not refund.
- The "bribery escalation" case: Polymarket had $14M wagered on Iran strikes against Israel that day. Reporter Emanuel Fabian was pressured to recharacterize an impact as an interception. "'You have exactly half an hour to correct your attempt at influence,' said one message ... 'You will discover enemies who will be willing to pay anything to make your life miserable.'"
- John Sides (Good Authority, Dec 18 2025) summarizes Clinton & Huang's analysis of "more than 2,500 political prediction markets" across Iowa Electronic Markets, Kalshi, PredictIt, and Polymarket "during the final five weeks of the 2024 U.S. presidential campaign involving more than two billion dollars in transactions." Headline result: "While 93% of PredictIt markets correctly predicted outcomes better than chance, accuracy fell to 78% on Kalshi and 67% on Polymarket." Sides notes Kalshi's recently inked CNN and CNBC data-partnership deal as the reason caution about these accuracy gaps matters now.
- Clinton & Huang on (in)efficiency: "prices for identical contracts diverged across exchanges, daily price changes were weakly correlated or negatively autocorrelated, and arbitrage opportunities peaked in the final two weeks before Election Day." Persistent cross-platform arbitrage *increased* into resolution rather than decreasing as one would expect of an efficient market.
- Andy Hall's hypothetical CNN-CNBC-Kalshi scenario (cited by Sides): a market spike with no apparent news source becomes a self-validating news event, with the manipulation accusation itself becoming part of the narrative · illustrating how *media partnerships* multiply the political cost of even unprovable manipulation.
- Rajaprabhakaran (FiftyCent Dollars) makes the *opposite* case: "Me putting a few hundred dollars on a prediction market doesn't buy any ads, knock on any doors, or change a single vote." Critics of manipulation, in his account, conflate prediction markets with stocks (which *have* causal channels via cost of capital) and bond markets (which firing Liz Truss "actually undermines her argument" · bonds had a direct causal channel via gilts' yield).

## Notable Quotes

> "We find that prediction markets can be manipulated: the effects of our trades are visible even 60 days after they have occurred. However, as predicted by our model, the effects of the manipulations somewhat fade over time. Markets with more traders, greater trading volume, and an external source of probability estimates are harder to manipulate."  
> — *Rasooly & Rozzi, *How Manipulable Are Prediction Markets?**

> "Wash traders trade with other wash traders in their collusive clique, while market makers neither know nor care who their counterparties are. In fact, market makers seldom trade with other market makers."  
> — *Rajiv Sethi, *The Detection of Wash Trading**

> "These wallets collectively traded over 116 million shares and generated more than 113 million in dollar volume, but ended up with an aggregate loss of just $57.86."  
> — *Sethi, *ibid.**

> "Speculative markets are actually far more resistant to manipulation than other info institutions. When traders expect more efforts to manipulate a price, they respond so that prices on average become MORE accurate."  
> — *Robin Hanson, *On Prediction Market Regulation**

> "The entire squadron is on Polymarket. The entire air force is betting."  
> — *IDF Air Force major, quoted in Gunitsky, *Priced to Kill**

> "Polymarket settled daily temperature bets for Paris using an unguarded sensor near the Charles de Gaulle airport. On April 6, someone bought the long-shot contract for abnormally high temperatures. Later that evening, the sensor saw a four-degree spike before dropping back to normal. No neighboring station registered anything similar."  
> — *Gunitsky, *ibid.**

> "While 93% of PredictIt markets correctly predicted outcomes better than chance, accuracy fell to 78% on Kalshi and 67% on Polymarket."  
> — *Clinton & Huang, summarized by Sides, *The Perils of Election Prediction Markets**

> "Prices for identical contracts diverged across exchanges, daily price changes were weakly correlated or negatively autocorrelated, and arbitrage opportunities peaked in the final two weeks before Election Day."  
> — *Clinton & Huang, quoted in Sides, *ibid.**

> "Markets that resolve on whether someone stays in power, speaks publicly, shows up somewhere, or holds a certain job carry implicit incentives connected to their continued existence. Nearly every such market dealing with control, decision, or public appearance is technically an assassination market."  
> — *Guillory & Zimmermann, quoted in Sethi, *Trading on Violence**

## Where It Matters

Manipulation resistance is the gating constraint for prediction markets being treated as public-good information infrastructure rather than gambling. The same property that makes them useful (prices are read as truth) makes them attractive to attack, and most current architectures (binary order books, optimistic oracles, pseudonymous wallets) have weak structural defenses. The 2024-election data shows that high volume does *not* automatically buy accuracy · Polymarket's largest US-election market was less accurate than PredictIt's much smaller market, and arbitrage opportunities *grew* into the resolution window. For continuous markets like Dekant, the manipulation surface shifts from single-tick attacks to curve-shape distortion · relevant when designing how much capital is needed to move a kernel.

## Related Concepts

- Wash trading · direct subtype (synthetic volume without economic risk)
- Insider trading · adverse-selection cousin of manipulation
- Reflexivity · when manipulation actively changes the outcome being predicted
- Incentive compatibility · manipulation is the negative space of incentive compatibility
- Oracle design / dispute resolution · resolution manipulation is a distinct attack surface from price manipulation
- Self-resolving markets · proposed as a structural defense against oracle capture

## Sources

- [Priced to Kill](https://hegemon.substack.com/p/polymarket-is-making-wars-more-dangerous) — Seva Gunitsky · Hegemon Substack
- [On Prediction Market Regulation](https://www.overcomingbias.com/p/on-prediction-market-regulation) — Robin Hanson · Overcoming Bias
- [You Don't Hate Prediction Markets. You Hate Capitalism.](https://x.com/noahlitvin/status/2048527516587966660) — Noah Litvin · X
- [The Hidden Risk Of Prediction Markets: 14 Resolution Failures That Cost $500M](https://x.com/xomarket/status/2046924302952640934) — XO Market · X
- [When Prediction Markets Need Stake](https://x.com/alanwu/status/2044049214393524393) — alan · X
- [Are Prediction Markets Decaying or Evolving?](https://x.com/_drNeyo/article/2029205512391172364) — Eniola · X
- [Trading on Violence](https://rajivsethi.substack.com/p/trading-on-violence) — Rajiv Sethi · Substack
- [On War Markets](https://x.com/probaaron/article/2027765452689014822) — aaron · X
- [Polymarket Is Not a Casino. Why Prediction Markets Are Finance, Not Gambling](https://x.com/13_niakris/article/2025614474263093627) — Niakris · X
- [Leverage Fixes Prediction Markets: The Case for Why 10x Is Safer Than 1x](https://x.com/njokuScript/article/2022351299547689325) — njokuScript · X
- [Assassination Semantics: Why Every Market Carries the Risk of Violence](https://x.com/BetBreakNews/article/2008587654158340126) — Sean Guillory & Dan Zimmermann · X
- [The Perils of Election Prediction Markets](https://goodauthority.org/news/the-perils-of-election-prediction-markets/) — John Sides · Good Authority
- [The Detection of Wash Trading](https://rajivsethi.substack.com/p/the-detection-of-wash-trading) — Rajiv Sethi · Substack
- [How Manipulable Are Prediction Markets?](https://arxiv.org/abs/2503.03312) — Itzhak Rasooly, Roberto Rozzi · arXiv

