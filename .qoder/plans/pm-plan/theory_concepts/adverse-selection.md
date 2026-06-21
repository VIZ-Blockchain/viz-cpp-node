# Adverse Selection

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Adverse Selection](https://www.pmatlas.xyz/concepts/adverse-selection)

---

## Definition

**Quick definition.** The risk that counterparties trade against you because they hold superior information, causing systematic losses for the liquidity provider. In prediction markets adverse selection is unusually severe because some counterparties hold *near-perfect* information about outcomes that resolve cleanly to $0 or $1.

## Key Insights

- semaji.eth's framing: "Conditional on someone trading with you, you should be less confident your trade was good." Adverse selection is the single biggest reason classical market-making models fail in prediction markets.
- semaji.eth ranks: Indian options easy → crypto medium → prediction markets *legendary*. The reason is gap risk: an informed counterparty can lift an entire order book on a tweet because they know the resolution.
- sybilpm's "Sniper's Tax": sniping geopolitical strike markets at 10c. In traditional markets sniping costs basis points; in PMs it costs **80 cents on the dollar** when 0.10 → 0.99 on a single tweet. The concrete case: on January 3, 2026 a trader "dudukos" cleared the entire order book of "Will Israel strike Gaza on January 3, 2026?" in a single trade, buying from $0.10 all the way up to $0.80 · then repeated the pattern on Jan 10, 11, 12 across dozens of Israel-strike markets. He didn't know more than the MMs; he was just slightly faster to react.
- sybilpm's MM expected-profit math: **E[π] = (s/2 · V(s)) − P_news · L_snipe**. When P_news is high (geopolitical, breaking-news markets) the sniping term dominates and MMs only have two responses · widen s or pull quotes. "This isn't a failure of will. It's the only rational response."
- sybilpm on the "unhedgeable problem": if you're an MM holding too much Apple, you sell Nasdaq futures. If you're holding too much "Will Israel strike Lebanon today?" · there's no correlated instrument. You're just exposed.
- Mitts & Ofir screened **93,000 Polymarket markets across 50,000 wallets, Feb 2024–Feb 2026** and flagged 210,718 wallet-market pairs with a **69.9% win rate** · more than 60 standard deviations above chance under a permutation test. Aggregate anomalous profit ≈ **$143M**, a conservative lower bound (buy-side only, excludes positions <$500, can't detect deliberately small trades). Their 5-signal composite score combined cross-sectional bet size, within-trader bet size, profitability, pre-event timing, and directional concentration.
- Deleep et al. ("How Wise Is the Crowd?"): whales are not the sharpest participants · heavily capitalized traders systematically bleed expected value to small-order traders, likely driven by ideological conviction rather than informational edge. The classic favorite-longshot bias may be a statistical artifact masking a pervasive YES bias.
- functionSPACE: "noisy traders fund the probability space" · they are not exit liquidity but the necessary fuel that makes adverse-selection-tolerant market making possible.
- 0xnagu's LOX (log-odds excess lateness) metric: decomposes late volume into *hazard* (info arrives late) vs. *toxicity* (early entry is punished by adverse selection); boxing markets cluster with news markets behavior-wise.
- Dougie's "Discovery vs Betrayal" framework: in distributed-truth markets (elections) informed traders sharpen the signal because no one holds the full answer; in concentrated-truth markets (earnings, planned events) insiders simply monetize a sealed result.
- Nic Carter: insider trading is **inescapable**; platforms face a calibration problem · too permissive and noise traders flee perceiving rigging; too strict and informed flow disappears and prices decay into sentiment.
- Hariharan proposes three layers of defense: platform-level detection + position limits scaled to account size; market-design mechanisms (dynamic spread widening, MM insurance pools); legal frameworks (compliance, CFTC guidance). His concrete example of catastrophic adverse selection: AlphaRaccoon predicted 22 of 23 Google Year-in-Search rankings correctly for >$1M PnL, and earlier $150k on the Gemini 3.0 release date.
- Roan ("Why Prediction Markets Aren't Gambling? (The Math)"): the actionable diagnostic for adverse selection is **fill quality** · measure your realized fill vs. mid; if you're systematically getting filled worse than mid you're being adversely selected.
- Human Invariant: FCFS order matching creates a latency arms race and forces defensive spread widening. "FCFS creates wider spreads and other negative externalities" · colocation arms race + defensive quote widening. Priority batch auctions (cancels → makers → takers) reduce this by letting makers update their quotes before snipers can fill stale ones.
- Polymarket order book shows surface symmetry at top-of-book but systematic **ask-side skew at deeper levels** · MMs are pricing in asymmetric adverse selection (@allquantor).
- Becker's category gradient maps adverse-selection intensity: Finance has a 0.17pp taker-maker gap (efficient); World Events is 7.32pp; Media 7.28pp; Entertainment 4.79pp. "Financial questions attract traders who think in probabilities and expected values rather than fans betting on their favorite team."
- Lebron warning: large-scale prediction markets lack heterogeneous risk preferences for efficiency; they rely on continuous retail losses, with reflexivity creating incentives toward "negative, sensational outcomes."
- Ruzicka on leverage: gap risk + adverse selection together explain why every team trying 10x or 20x leverage converges to 1x–1.5x.
- Alan Wu: information markets are public goods; their liquidity costs fall on a narrow trader base, creating cross-subsidization opportunities (profitable markets fund socially valuable ones).
- 23 Reasons (Lin): adverse selection sits alongside capital efficiency, oracle governance, and regulatory fragmentation as one of the four structural pillars of why prediction markets are stuck.
- Nic Carter's core tension: "The social value of prediction markets derives from financially incentivizing insiders to divulge confidential information, but this collapses noise trader confidence in the market over time." Robin Hanson explicitly endorses the insider trade-off; Kalshi bans it in ToS; Polymarket has a catch-all law-violation clause but does not explicitly ban insider trading.

## Notable Quotes

> "Conditional on someone trading with you, you should be less confident your trade was good."  
> — *semaji.eth*

> "In traditional markets, sniping costs basis points; in prediction markets, it costs 80 cents on the dollar."  
> — *sybilpm, "The Sniper's Tax"*

> "Insider trading in prediction markets is an inescapable problem... platforms face a calibration problem: too permissive and noise traders flee perceiving rigging, too strict and informed flow disappears."  
> — *Nic Carter*

> "The social value of prediction markets derives from financially incentivizing insiders to divulge confidential information, but this collapses noise trader confidence in the market over time."  
> — *Nic Carter*

> "Conditional on someone trading with you, you should be less confident your trade was good. You weren't providing liquidity to a forecaster. You were exit liquidity for a bot."  
> — *sybilpm*

> "Flagged traders achieved a 69.9% win rate"  
> — *a result that exceeds the null distribution of random chance by more than 60 standard deviations." · Mitts & Ofir, Harvard Corporate Governance forum*

## Where It Matters

Adverse selection is the master concept that explains nearly every other pathology: why spreads are wide, why AMMs lose money, why CLOBs concentrate liquidity in 23 pro market makers, why leverage is bounded at 1×, and why long-tail markets stay empty. Any platform serious about scaling beyond politics and sports must engineer specifically *for* asymmetric information · through batch auctions, dynamic fees, insurance pools, position limits, surveillance, and contract design that limits how much edge any one trader can extract.

## Related Concepts

- **Market making** · adverse selection is what MMs are paid (via the spread) to bear.
- **Toxic flow** · adverse selection in motion: orders that systematically move against the LP.
- **Insider trading** · the legal/ethical frame of one source of adverse selection.
- **Gap risk** · the binary-specific amplifier.
- **Information asymmetry** · the upstream cause.
- **Batched auctions** · a leading mitigation.
- **Kelly criterion** · sizing under edge; the inverse problem from the LP perspective.
- **Retail flow** · the "good" flow that subsidizes losses from informed flow.
- **Bid-ask spread** · partially a tax on adverse selection.

## Sources

- [How Prediction Markets Can Ascend](https://x.com/alanwu/article/2036438579824496906) — Alan Wu · Mar 25, 2026
- [Discovery and Betrayal: Insiders in Prediction Markets](https://x.com/DougieDeLuca/article/2033910178341413105) — Dougie · Mar 18, 2026
- [Noisy Traders Are Not Dumb Money](https://x.com/functionspaceHQ/article/2032397043579462028) — functionSPACE · Mar 13, 2026
- [Polymarket Doesn't Have a Money Problem. It Has a Plumbing Problem.](https://x.com/ZEITFinance/article/2031500989576949770) — @allquantor · Mar 11, 2026
- [The Sniper's Tax](https://sybilpm.substack.com/p/the-snipers-tax) — sybilpm · Mar 8, 2026
- [How Wise Is the Crowd? Bias and Edge in Prediction Markets](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=6322678) — Deleep, Lee, Bai, Suresh, Dhawan · Feb 28, 2026
- [23 Reasons Prediction Markets Are Broken Today](https://x.com/linfluence/status/2026757563518431696) — Alexander Lin · Feb 26, 2026
- [Prediction Markets Are Not Good Markets (Yet)](https://murmurationstwo.substack.com/p/prediction-markets-are-not-good-markets) — Nic Carter · Feb 21, 2026
- [How to Solve Insider Trading in Prediction Markets](https://www.dopaminemarkets.com/p/how-to-solve-insider-trading-in-prediction) — Shreyas Hariharan · Feb 10, 2026
- [Why Prediction Markets Aren't Gambling? (The Math)](https://x.com/RohOnChain/article/2020565633453412751) — Roan · Feb 9, 2026
- [Thoughts on the Law of Insider Trading and Prediction Markets](https://x.com/dbarabander/article/2019769802735178236) — Daniel Barabander · Feb 6, 2026
- [The Option Value of Waiting in Prediction Markets](https://x.com/0xnagu/article/2016620973391564951) — 0xnagu · Jan 28, 2026
- [Everyone's Promising 20x Leverage on Prediction Markets. Here's Why It's Hard.](https://medium.com/@nick.c.ruzicka/everyones-promising-20x-leverage-on-prediction-markets-here-s-why-it-s-hard-8684d9a77e11) — Nick Ruzicka · Jan 27, 2026
- [On Prediction Markets](https://x.com/outpxce/status/2013397772074905950) — outpxce · Jan 20, 2026
- [The Case For Alternative Ordering Mechanisms in Prediction Markets](https://www.humaninvariant.com/blog/pm-ordering) — Human Invariant · Nov 12, 2025
- [The Liquidity Problem in Prediction Markets, Part II: Adverse Selection in Prediction Markets](https://x.com/semajeth/status/1975211193418563697) — semaji.eth · Oct 6, 2025
- [The Liquidity Problem in Prediction Markets, Part I: Adverse Selection and Market Making](https://x.com/semajeth/status/1967598604006134024) — semaji.eth · Sep 15, 2025
- [The Liquidity Problem in Prediction Markets: Part 0](https://x.com/semajeth/status/1966144230826414225) — semaji.eth · Sep 11, 2025

