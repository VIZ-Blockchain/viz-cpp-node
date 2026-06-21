# Insider Trading

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Insider Trading](https://www.pmatlas.xyz/concepts/insider-trading)

---

## Definition

**Quick definition.** Trading on material non-public information (MNPI) obtained through breach of a confidentiality duty or implied promise. In prediction markets, it arises when participants trade on privileged access to information about upcoming events, creating adverse selection for everyone else and exposing the platform to legal and reputational risk.

## Key Insights

- Mitts & Ofir (Harvard Corporate Governance forum): screened **93,000 Polymarket markets across 50,000 unique wallets, Feb 2024–Feb 2026**. Flagged 210,718 wallet-market pairs with a **69.9% win rate** · more than 60 standard deviations above chance under a permutation test. Aggregate anomalous profit ≈ **$143M**, an explicit lower bound (buy-side only, excludes positions <$500, can't catch traders who deliberately stay small). Composite score combines five signals: cross-sectional bet size, within-trader bet size, profitability, pre-event timing, directional concentration.
- Mitts & Ofir concrete cases: Feb 28, 2026 US-Israeli strike on Iran · six newly-created wallets earned ≈$1.2M, "Magamyman" placed first trade **71 minutes before** the news at 17% implied probability and resolved with $553k. The Maduro capture: "Burdensome-Mix" earned ≈$485k from a $38,500 investment, biggest trades placed hours before public announcement.
- Mitts & Ofir legal analysis: three reasons existing law fails · (1) Section 10(b)/Rule 10b-5 classical and misappropriation theories both require *securities*, and most PM contracts are commodities; (2) CFTC Rule 180.1, the analog to 10b-5, doesn't clearly carry a Cady-Roberts disclose-or-abstain duty, and trading on "lawfully obtained" commercial information without deception remains legal; (3) federal wire fraud (United States v. Chastain, 2025) requires that the misappropriated information have commercial value to the original source · military and personal information often has none.
- Mitts & Ofir's proposed three-layer regulatory response: (1) platform-level registration/surveillance for any operator offering contracts to US persons, leveraging CFTC extraterritorial jurisdiction; (2) contract-level targeted rules for high-risk categories (government data releases, military/diplomatic operations, corporate material events); (3) liability theory directed at informed traders themselves on decentralized platforms · extending Rule 180.1 misappropriation doctrine through CFTC rulemaking to cover non-securities duties of confidentiality owed by soldiers, government employees, and corporate insiders.
- Mitts & Ofir on H.R. 7004 (Public Integrity in Financial Prediction Markets Act of 2026, Rep. Torres): wouldn't have deterred any of the documented cases · Maduro/Iran trades involved military personnel covered by separate statutes, the IDF case involved a non-US Israeli reservist, the OpenAI browser case involved corporate trade secrets, the Taylor Swift case involved personal social knowledge.
- Rajiv Sethi: wallets profited **$1.2M** on the timing of US strikes on Iran; pattern likely involves classified intelligence. KYC-based surveillance (Kalshi) vs. pseudonymous blockchain (Polymarket) creates fundamentally different enforcement surfaces. Kalshi has conducted **>200 investigations into insider trading, with a dozen cases currently active**.
- "Information Contagion" (Sethi): on March 24, 2026 at 6:50am ET, an unusual spike in oil/stock futures trading volume preceded Trump's 7:05am ET social-media announcement about Iran negotiations by 15 minutes. "Whoever entered large short positions in oil and long positions in the stock index made millions in minutes." Sethi's point: insider trades on Polymarket leaked into regulated KYC venues · quant funds extracted the signal from pseudonymous crypto trades and acted on it in regulated derivatives markets, without (technically) breaking any law.
- DOJ case against MSgt Gannon Ken Van Dyke: $400k on Polymarket on the Maduro raid. Israeli reservist arrests preceded similar pattern.
- Courtney's **legibility** thesis: a US soldier traded ~$33k for $400k+ profit on Polymarket on the Maduro plans · tiny in financial-markets terms, but legibility makes the information leak immediate and public. Unlike a stock spike that the public can't read, a Polymarket bet on "US strikes Maduro" tells the world exactly what someone knows. This makes PMs uniquely dangerous as intelligence-leak vectors.
- Nic Carter: insider trading is **structural, not a bug**. Robin Hanson explicitly said in a 2024 Decrypt interview: "If the point of [prediction] markets is to get accurate information on the prices, then you definitely want to allow insiders to trade, even if that discourages other people from betting because that makes the prices more accurate. And that's the priority." Kalshi bans it in ToS; Polymarket has only a catch-all law-violation clause.
- Carter's "rogues' gallery" of suspected insider trading: 2025 Nobel Peace Prize, Google's 2025 Year in Search Rankings, Maduro's capture, Israel's 2024 strikes on Iran, OpenAI GPT-5.2 release, UFC fight results, Taylor Swift engagement. Polymarket's pseudonymity + on-chain visibility is "the worst combination": you can sort of cover your tracks while every transaction is publicly auditable.
- Daniel Barabander (legal analysis): no distinct "insider trading statute" applies · it's governed by **existing fraud law**, requiring a deceptive breach of an implied promise. Many PM insiders (e.g., a political candidate betting on his own race) may owe no duty at all.
- Jay Sykes (Congressional Research Service): walks through SEC Rule 10b-5, CFTC Rule 180.1, the STOCK Act, Title 18; surveys four pending bills in the 119th Congress that would close gaps. Notes the CFTC's February 2026 advisory on two Kalshi enforcement actions.
- Michael Li's CFTC comment: four-dimensional framework · (1) **information structure** (legibility, concentration of decision-makers, institutional insulation); (2) **manipulation economics** (whether the contract creates incentives to *cause* the outcome and at what cost); (3) **social utility** of the price signal (policy/economic > informational > entertainment > hyper-specific); (4) **repugnance as a market-design constraint** (Roth's category · markets whose existence is intolerable regardless of efficiency). Li separates "insider trading" into three patterns: information asymmetry, duty-breach trading, outcome influence · calibrated remedies require holding them apart.
- Li on Coinbase's "mention market": on October 30, 2025, Brian Armstrong told investors he'd been "a little distracted" tracking a Polymarket contract on what he'd say, then deliberately rattled off the words bettors had wagered on. The person who controlled the outcome publicly acknowledged responding to the price · pure manipulation economics.
- Li on the P2P.me case: a Coinbase-Ventures-backed firm used Polymarket to bet on its own fundraising round, netting <$15k. "The same actor controlled the outcome of the underlying fundraise and traded on a market resolving on that outcome."
- Hariharan's three-layer solution: platform detection + position limits scaled to account size; market design (dynamic spread widening, MM insurance pools); legal frameworks (compliance, CFTC guidance). AlphaRaccoon predicted 22/23 Google Year-in-Search rankings (>$1M) and earlier $150k on Gemini 3.0 release date.
- Dougie's discovery-vs-betrayal framework: in distributed-truth markets like elections, informed traders *sharpen* the signal; in concentrated-truth markets (earnings, planned events) insiders monetize a sealed result.
- Seva Gunitsky ("Priced to Kill"): prediction markets create three dangers · **military insider trading at scale, manipulation of outcome resolution, propaganda advantages for autocratic regimes**. Concrete cases: the Israeli Air Force major told investigators "the entire squadron is on Polymarket. The entire air force is betting." A war journalist (Fabian) was bribed and threatened by "No" bettors trying to get him to change his reporting on whether an Iranian missile struck Israeli soil. A "hair dryer" attack on a Paris weather sensor near Charles de Gaulle airport let a trader collect $14k on April 6 and $20k on April 15 betting on abnormal temperatures · France's meteo agency filed a criminal complaint, Polymarket switched data sources but did not refund the payouts.
- Gunitsky on regime-power consolidation: "Prediction market accuracy relies not on the crowd but on a small minority of informed traders. If only three percent of accounts drive price discovery, you don't need to outbid millions of independent signals, only to co-opt or suppress some fraction of those three percent." Donald Trump Jr. is a strategic adviser to Kalshi and an investor in Polymarket; "the only person indicted so far is a sergeant who used his personal email."
- aaron ("On War Markets"): proposed guardrails include delayed settlement and conflict-of-interest screens. Kalshi avoids conflict markets; Polymarket lists them freely.
- Robin Hanson defends a light-touch regime: prediction markets deserve the same regulatory treatment as other information institutions (journalism, academia), approved by default and restricted only on clear evidence of specific harm. All info institutions · gossip, academia, journalism · can induce people to reveal secrets, waste time, make misleading contributions, change the world for favorable treatment; PMs should be treated similarly, not specially.
- Noah Litvin: every standard objection (gambling, insider trading, manipulation, slot-machine durations) has a larger-scale analog already accepted in traditional finance · the $950M oil ceasefire trades on CME, LIBOR, accredited-investor rules.
- Jeff Park: insider-trading fears are overblown for obscure markets because liquidity will be negligible in asymmetric questions.

## Notable Quotes

> "Prediction markets have an inescapable insider trading problem."  
> — *Nic Carter*

> "Information flows freely across platforms even when regulatory frameworks differ."  
> — *Rajiv Sethi, "Information Contagion"*

> "Insider trading in prediction markets is governed by existing fraud law rather than a distinct insider trading statute."  
> — *Daniel Barabander*

> "The real question is not whether insiders should be allowed but what kind of informational asymmetry a market can absorb without losing the participation and trust that make the signal useful."  
> — *Dougie*

> "The entire squadron is on Polymarket. The entire air force is betting."  
> — *Israeli Air Force major to investigators, quoted in Gunitsky*

> "These profits represent transfers from uninformed retail participants to those with access to material non-public information"  
> — *a regressive outcome that undermines the democratic appeal of prediction markets as venues where ordinary forecasters can profit from accurate beliefs." · Mitts & Ofir*

> "If the point of [prediction] markets is to get accurate information on the prices, then you definitely want to allow insiders to trade, even if that discourages other people from betting because that makes the prices more accurate. And that's the priority."  
> — *Robin Hanson, 2024 Decrypt interview, quoted by Nic Carter*

> "The entire prediction market edifice, with its blockchain infrastructure, smart contracts, and information-aggregation theory was defeated by a man with a hair dryer."  
> — *Seva Gunitsky*

## Where It Matters

Insider trading is simultaneously the largest legal/regulatory risk facing the asset class and a feature without which prices wouldn't be accurate. Every platform must take a position on the spectrum from Polymarket (pseudonymous, permissive) to Kalshi (KYC, restrictive). The unsolved question is whether jurisdictions converge on contract-level restrictions (no military, no earnings, no personal info), platform-level surveillance, or legal-doctrine extensions (misappropriation theory) · and how those interact with cross-platform information contagion into KYC venues.

## Related Concepts

- **Adverse selection** · the economic frame insiders create for other traders.
- **Information asymmetry** · the upstream cause.
- **Market manipulation** · the active-trading flip side.
- **Market surveillance** · detection mechanism.
- **Regulatory classification** · determines which insider-trading regime applies.
- **Federal preemption** · determines whether state or CFTC rules govern.
- **Reflexivity / endogeneity** · when insider trades themselves influence outcomes (war markets).
- **Legibility** · why insider trades are obvious in PMs and ambiguous in stocks.
- **Resolution criteria / oracle design** · bad resolution is one channel of insider abuse.

## Sources

- [Priced to Kill](https://hegemon.substack.com/p/polymarket-is-making-wars-more-dangerous) — Seva Gunitsky · May 13, 2026
- [Why Prediction Markets Are Hard to Regulate](https://michaellwy.substack.com/p/my-comment-to-cftcs-prediction-markets) — Michael Li · Apr 30, 2026
- [On Prediction Market Regulation](https://www.overcomingbias.com/p/on-prediction-market-regulation) — Robin Hanson · Apr 29, 2026
- [You Don't Hate Prediction Markets. You Hate Capitalism.](https://x.com/noahlitvin/status/2048527516587966660) — Noah Litvin · Apr 27, 2026
- [Legibility](https://whirligigbear.substack.com/p/legibility) — Andrew Courtney · Apr 27, 2026
- [Prediction Markets Have An Inescapable Insider Trading Problem](https://x.com/nic_carter/status/2048123008200724599) — Nic Carter · Apr 26, 2026
- [What Most People Get Wrong About Prediction Markets](https://x.com/dgt10011/status/2045952603301851173) — Jeff Park · Apr 20, 2026
- [Are We on the Brink of a Prediction Market Supercycle?](https://x.com/mphrediction/status/2043419029378048164) — mph · Apr 12, 2026
- [How Prediction Markets Are Blurring the Line Between Trading and Betting](https://www.bloomberg.com/news/articles/2026-04-09/how-prediction-markets-are-blurring-the-line-between-trading-and-betting) — Christopher Beam (Bloomberg) · Apr 9, 2026
- [Information Contagion](https://rajivsethi.substack.com/p/information-contagion) — Rajiv Sethi · Mar 31, 2026
- [How to Use Prediction Markets as a High Quality Info Source](https://uncover.substack.com/p/how-to-use-prediction-markets-as) — Isar Bhattacharjee · Mar 30, 2026
- [From Iran to Taylor Swift: Informed Trading in Prediction Markets](https://corpgov.law.harvard.edu/2026/03/25/from-iran-to-taylor-swift-informed-trading-in-prediction-markets/) — Joshua Mitts, Moran Ofir · Mar 25, 2026
- [Discovery and Betrayal: Insiders in Prediction Markets](https://x.com/DougieDeLuca/article/2033910178341413105) — Dougie · Mar 18, 2026
- [Prediction Markets and Insider Trading Law](https://www.congress.gov/crs-product/LSB11406) — Jay B. Sykes (CRS) · Mar 18, 2026
- [Are Prediction Markets Decaying or Evolving?](https://x.com/_drNeyo/article/2029205512391172364) — Eniola · Mar 4, 2026
- [Trading on Violence](https://rajivsethi.substack.com/p/trading-on-violence) — Rajiv Sethi · Mar 2, 2026
- [On War Markets](https://x.com/probaaron/article/2027765452689014822) — aaron · Feb 28, 2026
- [How to Solve Insider Trading in Prediction Markets](https://www.dopaminemarkets.com/p/how-to-solve-insider-trading-in-prediction) — Shreyas Hariharan · Feb 10, 2026
- [Thoughts on the Law of Insider Trading and Prediction Markets](https://x.com/dbarabander/article/2019769802735178236) — Daniel Barabander · Feb 6, 2026

