# Toxic Flow

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Toxic Flow](https://www.pmatlas.xyz/concepts/toxic-flow)

---

## Definition

**Quick definition.** Order flow from informed traders that systematically moves against the market maker's positions · adverse selection in motion. In prediction markets the "toxicity" can be extreme because counterparties may hold near-perfect information about a discrete resolution event.

## Key Insights

- semaji.eth: market makers profit from retail flow *while avoiding toxic informed counterparties*. The Jane Street coffee question is the canonical illustration: if someone wants to trade with you, you should be *less* confident.
- semaji.eth Part II: in PMs, toxic flow is "worse than any other asset class" because informed counterparties can have **near-perfect information and take out entire order books** in a single tweet-driven move.
- sybilpm: snipers paying 10 cents on geopolitical markets that resolve to 0.99 are pure toxic flow. Costs the MM **80 cents on the dollar** per fill · proposes batched auctions to neutralize. Specific case: "dudukos" cleared the entire order book of "Will Israel strike Gaza on January 3, 2026?" from $0.10 to $0.80 in a single trade, then repeated the pattern Jan 10/11/12 across dozens of Israel-strike markets. Pure toxic flow with no informational edge · just faster reaction time.
- sybilpm's "liquidity mirage": passive LPs on Polymarket and Kalshi "behave more like underwriters of terminal risk than classical market makers" · the platforms burn millions on LP incentives, but rewards never sized to cover 80-cents-per-contract gap risk. The retail LP earns yield in normal periods and gets wiped out by toxic flow on the day news breaks.
- Mitts & Ofir's $143M anomalous-profit estimate at 69.9% win rate (60σ above chance) is the empirical signature of toxic flow on Polymarket · even with a conservative buy-side-only, $500+ filter, the magnitude of informed-trader extraction is enormous.
- Dune analysis: bots control 55–62% of fast-market volume on Polymarket. Without identification, hard to know how much of bot flow is toxic vs. arbitrage vs. liquidity-providing.
- taetaehoho: sportsbooks compensate for toxic flow via **counterparty-aware pricing** (price-discriminate against known winners). PMs compensate via maker competition and orderbook transparency. PM prices are 100–300 bps better on liquid markets but **10–50% spreads on long-tail** are the toxic-flow tax.
- Momin: the structural funnel sends **70% of 1.7M Polymarket addresses to losses** because retail flow is by definition the *non-toxic* side that informed flow needs to extract from.
- Daedalus Research: the academic frame is shifting from "accuracy" to **trader welfare** · which is largely a function of how toxic flow is distributed.
- Becker provides the category-level distribution of toxic vs. retail flow: Finance gap 0.17pp (efficient · informed/uninformed mix is balanced); Sports 2.23pp; World Events 7.32pp; Media 7.28pp; Entertainment 4.79pp. Higher gap = more toxic-flow exposure to takers.
- Becker on the temporal arrival of toxic flow on Kalshi: before October 2024 (CFTC court decision + election volume surge), takers actually outperformed makers (+2.0% vs -2.0%). Toxic flow as we know it today emerged when sophisticated algorithmic MMs arrived to absorb the new retail volume · pre-existing markets had been a different equilibrium.

## Notable Quotes

> "Market makers profit from retail flow while avoiding toxic informed counterparties."  
> — *semaji.eth*

> "Informed counterparties can have near-perfect information and take out entire order books."  
> — *semaji.eth*

> "Passive LPs on these venues behave more like underwriters of terminal risk than classical market makers."  
> — *Lotus*

## Where It Matters

Toxic flow is the asset class's central pricing input · its presence dictates spread width, its absence determines whether a market exists at all, and its mitigation defines every successful platform's microstructure. The empirical record is that long-tail markets fail not because no one wants to trade them but because no MM will quote into a venue where the modal counterparty knows the answer.

## Related Concepts

- **Adverse selection** · the conceptual parent.
- **Market making** · what bears the cost.
- **Retail flow** · the desirable counter-flow that subsidizes toxic flow losses.
- **Gap risk** · toxic flow's binary-specific amplifier.
- **Batched auctions** · leading mitigation.
- **Long-tail markets** · where toxic flow dominates and MMs disappear.
- **Insider trading** · one well-defined source of toxic flow.

## Sources

- [Sportsbooks vs Prediction Markets - Market Structure and Its Effects](https://x.com/0xtaetaehoho/status/2054215076471873575) — taetaehoho · May 12, 2026
- [The Prediction Market Epidemic: Who's Actually Winning](https://x.com/mominsaqib/status/2046551020231385529) — Momin · Apr 21, 2026
- [What Happens When Institutional Liquidity Enters Prediction Markets](https://x.com/DaedalusRsch/status/2046219948452979151) — Daedalus Research · Apr 20, 2026
- [Faster, Shorter, More Automated: Anatomy of Polymarket's Fastest Markets](https://x.com/Dune/status/2044045952730726863) — Dune · Apr 14, 2026
- [The Sniper's Tax](https://sybilpm.substack.com/p/the-snipers-tax) — sybilpm · Mar 8, 2026
- [The Liquidity Problem in Prediction Markets, Part II: Adverse Selection in Prediction Markets](https://x.com/semajeth/status/1975211193418563697) — semaji.eth · Oct 6, 2025
- [The Liquidity Problem in Prediction Markets, Part I: Adverse Selection and Market Making](https://x.com/semajeth/status/1967598604006134024) — semaji.eth · Sep 15, 2025

