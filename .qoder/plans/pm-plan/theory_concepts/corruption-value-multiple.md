# Corruption Value Multiple (CVM)

**Category:** Oracle & Resolution  
**Source:** [PM Atlas — Corruption Value Multiple (CVM)](https://www.pmatlas.xyz/concepts/corruption-value-multiple)

---

## Definition

**Quick definition.** The Corruption Value Multiple is a quantitative diagnostic: the ratio of *value-at-stake outside a market* (Polymarket open interest, derivatives downstream, social/political leverage from a flipped resolution) to the *cost of corrupting the oracle* (half of the oracle token float + bonds + slashing risk). A CVM > 1 means honest resolution is economically irrational for a marginal voter. The phrase was coined by Luca Prosperi (Dirt Roads #64) and is now standard vocabulary on the topic.

## Key Insights

- **The original Prosperi formula** (Nov 2024): for Polymarket-on-UMA, CVM = (Open Interest / 2) / (UMA float / 2). At the time of writing, OI ≈ $300M, UMA market cap ≈ $220M, majority-stake capture ≈ $110M. Resulting CVM = **$150M / $110M ≈ 1.36x.**
- **The 1.36x figure is a *lower bound*.** It assumes: (a) all UMA tokens always vote (in reality ~20% do), (b) resolutions are unambiguous (in reality ambiguous ones are easier to swing), (c) attacker has no leverage from outside-the-market value (in reality propaganda, derivative positions, and political outcomes amplify the prize). True CVM is significantly higher.
- **A CVM above 1.0 means oracle honesty is economically irrational.** The whole optimistic-oracle design rests on the assumption that voters lose more from slashing than they could gain from corruption. When CVM crosses 1, that assumption inverts and the protocol becomes a transparent attack target.
- **CVM is the unifying lens for every documented Polymarket failure case:**
- Venezuela 2024 election: market open interest ≈ $6M+, UMA capture cost vastly higher · but external-value (regime-legitimacy narrative, derivative bets) is what drove the lobbying intensity.
- Government shutdown 2025: technical OPM-website resolution, low CVM, but the optical/political value of a Polymarket-says-shutdown-ended on a specific day created soft pressure.
- Zelensky suit $240M / TikTok ban $120M / Cardi B halftime $47M: each had value-at-stake comparable to UMA float, making them direct CVM stress tests.
- **The CVM denominator is wrong if you only count tokens.** Real corruption costs also include:
- Price-impact of accumulating tokens (Prosperi: thin float, slippage on a $110M buy)
- Reputation/value-destruction of the token itself (perceived manipulation tanks UMA price)
- Coordination overhead of organizing voters
- Risk of dissenting voters exposing the attack
- Derivative positions downstream
- Information-arbitrage opportunities elsewhere
- Propaganda value (Gunitsky: "headline value is extracted immediately")
- State-actor information-warfare budgets
- **Polymarket scaling produces an unbounded CVM increase.** Polymarket's volume grew ~35x from May to Sep 2024 (Nardi). UMA's market cap is bounded by token demand. The asymmetry is structural: market value scales with adoption, oracle security scales with token speculation.
- **The CVM diagnostic generalizes beyond UMA.** Any token-voted oracle, AI-judge mechanism with corrupt-the-input attacks, or human-committee oracle has a CVM. michaellwy applies it to AI judges: corrupting the model is harder than corrupting tokens but not impossible (training-data poisoning, prompt injection in inputs).
- **The fix is not bigger bonds · it's smaller markets per oracle.** XO Labs layered approach: route each market to the resolution tier where its CVM is < 1. Low-stakes meme markets get cheap AI resolution; high-stakes geopolitical markets get expensive deeply-decentralized adjudication; on-chain price feeds get parametric resolvers with CVM ≈ 0.
- **Sortition/recusal mechanisms compress CVM.** If voters cannot also trade the market, the numerator collapses to "external value only" · usually much smaller than full open interest. UMA does not currently enforce this.
- **The CVM is a *worst-case bound*, not an average-case prediction.** Most markets resolve honestly because most votes are routine and the attack-effort is unwarranted. CVM matters most for *contested edge cases* where someone bothers to organize. Empirically, those are exactly the cases that have failed.
- **CVM-aware market design suggests asymmetric bonds.** Make dispute bonds scale with market value, not be flat. Polymarket's flat $100k bond is rounding error on $240M markets.
- **The OPM-website class of failures has CVM ≈ 0 but still fails.** Because no token-corruption is involved · the website just got out of sync with reality, and the UMA voters honestly reported what they saw. This is a *different failure class* from CVM-driven capture; it's a data-source failure. (Self-resolving markets and AI-with-multi-source resolution are the proposed mitigations.)
- **The CVM ratio explains why Polymarket-style markets struggle to scale further.** Each additional dollar of open interest *increases* CVM linearly; UMA market cap does not scale linearly with adoption. The "wall" is the moment large counterparties (sportsbooks, hedgers, institutional traders) refuse to enter because the oracle isn't economically secure for their position size.
- **The corollary: AI judges have a different CVM topology.** Corruption cost is roughly constant (cost of training-data manipulation or prompt injection) regardless of market size · so AI-judge CVM *decreases* per-dollar as market size grows. This is the strongest pro-AI-judge argument in the corpus.
- **Repeated UMA capture in small ways may have already happened.** Prosperi flags that the perceived integrity of UMA is part of its market cap. If observable failures accelerate, UMA price falls, CVM rises further, and the protocol enters a death spiral.

## Notable Quotes

> "The Corruption Value Multiple (CVM) is a metric that compares the cost of manipulating a market to the potential gains from such manipulation. In this context, with Polymarket's open interest at c. $300m and UMA's market capitalisation around c. $220m, the CVM would be ($300 / 2) / $110 = 1.36x, indicating that for every $1 invested in manipulation there's a potential gain of $1.36."  
> — *Luca Prosperi*

> "The 1.36x number might well understate CVM since it assumes that (a) every $UMA always participate in voting, while that number seems to be actually c. 20%, and (b) resolutions are trivial, in reality ambiguous wording, delays in result verification, and the potential for social manipulation make things more complicated. My gut feeling says that the $$$ required to control the oracle are much, much less."  
> — *Luca Prosperi*

> "[The UMA system has] a corruption cost lower than the value at stake."  
> — *michaellwy (paraphrasing Polymarket shutdown post-mortem)*

## Where It Matters

CVM is the most useful single number for evaluating an oracle. It's the prediction-market equivalent of a casino's house-edge · except inverted: when CVM > 1, the *house* is the trader being extracted from, and the attacker is anyone with capital. Every new oracle proposal (XO Oracle layered, AI judges committed on-chain, Meta Pool credibility tokens) can be benchmarked by what its CVM looks like across the market-size distribution.

## Related Concepts

- **oracle design** · CVM is the most cited oracle-quality metric in the corpus.
- **UMA protocol** · the empirical case study where CVM first became binding.
- **dispute resolution** · the layer CVM measures.
- **market manipulation** · when CVM > 1, manipulation is the rational strategy.
- **incentive compatibility** · CVM < 1 is approximately the condition for incentive-compatible truth-telling.
- **election markets** · the highest-stakes markets and therefore the highest-CVM tests.

## Sources

- [Resolving Prediction Markets: An AI-Driven Layered Approach](https://x.com/xolabs_/status/2055266974259961917) — XO Labs · May 15 2026 ·
- [Semantics for $10 Million: When Is an Invasion an Invasion?](https://www.oddchain.com/p/semantics-for-10-million-when-is-an-invasion-an-invasion-polymarket) — OddChain · Jan 23 2026 ·
- [Why Prediction Markets Are Broken (And How to Fix Them)](https://x.com/michael_lwy/status/1877211555496079683) — michaellwy · Jan 9 2025 ·
- [Prediction Markets (II): Spoiling the Election Love Story](https://dirtroads.substack.com/p/64-prediction-markets-ii-spoiling) — Luca Prosperi · Nov 2 2024
- [My Polymarket Conspiracy Theory](https://medium.com/quantum-economics/my-polymarket-conspiracy-theory) — Lou Kerner · Oct 20 2024
- [Polymarket Settles Bet Against Its Own Rules](https://frankmuci.substack.com/p/polymarket-settles-bet-against-its) — Frank Muci · Aug 8 2024 ·

