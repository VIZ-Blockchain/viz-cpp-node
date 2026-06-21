# Batched Auctions

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Batched Auctions](https://www.pmatlas.xyz/concepts/batched-auctions)

---

## Definition

**Quick definition.** A trading mechanism that collects orders over a time window and clears them simultaneously at a single uniform price, instead of matching continuously as orders arrive. In prediction markets, batched auctions are the leading proposed alternative to continuous limit order books · they neutralize speed-based sniping, reduce defensive spread widening, and shift trader competition from latency to price accuracy.

## Key Insights

- sybilpm ("The Sniper's Tax") opens with a named case study: on **January 3, 2026**, trader "**dudukos**" swept the entire order book of the "Will Israel strike Gaza on January 3, 2026?" market in a single trade · buying from **$0.10 all the way up to $0.80** · then repeated the pattern on Jan 10, 11, 12 and dozens of other Israel-strike markets. The MMs had quoted 10c reflecting a ~10% probability; once a strike happened the fair price jumped to ~100% and dudukos cleaned out the stale offers before they could be cancelled.
- sybilpm formalizes the MM math: `E[π] = (s/2 · V(s)) − P_news · L_snipe`. In equities `L_snipe` is a few ticks; in PMs it's "**80 cents on the dollar**" when `P_news` is high. MMs respond by widening `s` or pulling quotes entirely · both degrade the market for everyone else, and rationally so.
- sybilpm names the structural pathology a "**liquidity mirage**": at 3am Tuesday the order book *looks* deep, but that liquidity vanishes the instant news arrives · and once it's gone, the price can be shoved around with "hundreds of dollars".
- sybilpm's distributional finding: Polymarket and Kalshi LP-rebate programs "burn millions of dollars a year" but the rebates were *never* designed to cover 80-cent sniping losses · so they amount to "a quiet subsidy flowing from retail liquidity providers to sophisticated snipers". The retail LP "thinks they're earning yield. They're actually exit liquidity, again."
- sybilpm cites Budish-Cramton-Shim 2015 directly, and concludes batching "shifts the competitive frontier from network hardware to predictive intelligence".
- Human Invariant adds a hard datum from Nasdaq research: on FCFS exchanges, market makers update their quotes in time **only ~13% of the time** · meaning 87% of the time they get picked off by takers. Stable in equities (continuous asset, fee schedule), catastrophic in PMs (binary asset, 1%→100% jumps on a single news item).
- Human Invariant's priority-batch ordering rule is explicit and ordered: **(1) cancel orders → (2) maker orders → (3) taker orders**. Suggested batch intervals are tailored to the market type: **10 sec for live sports**, **1 min for news/government-shutdown markets**, **1 hr for election markets with 3+ years to resolution**. Even more granular: dynamic batches at each *dead ball* in basketball or *changeover* in tennis.
- Human Invariant's incumbent prediction: Polymarket, Kalshi, Opinion, and Limitless are unlikely to migrate because (a) they benefit from raw transaction counts and headline volume "including wash trading, to juice their vanity metrics", (b) technical risk, (c) anchored trader base, (d) airdrops/rebates reward "sheer activity rather than quality liquidity". The opportunity is for **new entrants** to launch batched-only.
- Will Howard's mechanism is sourced from real exchanges: NYSE's pre-open and pre-close **call auctions** already use single-price uniform clearing · collect orders, broadcast an "Indicative Match Price" each second, clear at the price that maximises matched volume. Howard's design proposal is to extend this to a "**Frequent Batch Auction**" running at every fixed interval with no continuous trading between.
- Howard's social-utility argument is concrete: he claims there is **no non-zero-sum use case** where it's valuable to know Maxwell-cut-a-deal or room-temp-superconductor news in 100ms vs 1s vs 1hr. The fast-reaction effort is pure rent-extraction. He cites the 2024 Biden drop-out market on Polymarket as the canonical case · the price moved "literally the moment" Biden's tweet went out, "too fast to read the post and buy through the web UI".
- Howard surfaces a UX angle the other two miss: batched auctions also have better **ergonomics** for retail browsers · "a fair price will be negotiated between a group of buyers and sellers" instead of "should you take the midpoint of the bid-ask spread, or the last executed trade, or some average?".
- Underlying theme across all three: in PMs, batching is a defense against gap risk and toxic flow simultaneously · the same mechanism that protects MMs from snipers also disincentivizes informed traders from racing to be first, because the batch clear price already reflects the new information.

## Notable Quotes

> "On January 3, 2026, a trader named 'dudukos' cleared the entire order book of the 'Will Israel strike Gaza on January 3, 2026?' market in a single trade, buying from $0.10 all the way up to $0.80."  
> — *sybilpm*

> "In traditional markets, being slightly slower than the fastest trader costs you a few basis points. In prediction markets, it costs you the entire value of your position."  
> — *sybilpm*

> "Polymarket and Kalshi burn millions of dollars a year on liquidity incentive programs"  
> — *basically paying people to post orders. The pitch is: earn rewards for providing liquidity. The reality is: you're being paid to stand in front of the train." · sybilpm*

> "On Nasdaq, market makers are only able to update their quotes in time approximately 13% of the time. This means that 87% of the time they are picked off by takers."  
> — *Human Invariant*

> "Reasonable starting points for prediction markets might include: 10 sec for live sports such as basketball and baseball, 1 min for news-based events such as the government shutdown ending and mention markets, 1 hr for election markets with three years or more until resolution."  
> — *Human Invariant*

> "Existing prediction market players benefit from maximizing raw transaction counts and headline volume, including wash trading, to juice their vanity metrics."  
> — *Human Invariant*

> "For almost all markets, there is no social utility to having a reaction time faster than about 1 second, and for many markets the 'socially useful' reaction time could be a day or more."  
> — *Will Howard*

> "Rather than waiting for one person to show up and take up your order at the limit price, you can walk away from the market knowing a fair price will be negotiated between a group of buyers and sellers."  
> — *Will Howard*

## Where It Matters

Batched auctions are the most consensus-friendly post-CLOB redesign in the literature. Unlike LMSR (capital-destructive) or parimutuel (timing-rigid), batching keeps the order-book mental model traders already understand while eliminating most of the pathologies that make passive market making unprofitable. The open question is whether incumbents (Polymarket, Kalshi) move to batching voluntarily or whether new entrants build batched-only venues to compete on spread quality.

## Related Concepts

- **Continuous double auction** · what batched auctions replace.
- **Order book** · batched auctions still use one, but clear it periodically.
- **Market making / adverse selection / toxic flow / gap risk** · all the problems batching is designed to mitigate.
- **Bid-ask spread** · what batching tightens.
- **Execution quality** · improves under batching (less sniping, less defensive widening).
- **Insider trading / market manipulation** · batching reduces the value of speed-based MNPI exploitation.

## Sources

- [The Sniper's Tax](https://sybilpm.substack.com/p/the-snipers-tax) — sybilpm · Mar 8, 2026
- [The Case For Alternative Ordering Mechanisms in Prediction Markets](https://www.humaninvariant.com/blog/pm-ordering) — Human Invariant · Nov 12, 2025
- [Many Prediction Markets Would Be Better Off as Batched Auctions](https://antidiluvian.substack.com/p/many-prediction-markets-would-be) — Will Howard · Aug 2, 2025

