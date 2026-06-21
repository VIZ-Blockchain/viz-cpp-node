# Wash Trading

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Wash Trading](https://www.pmatlas.xyz/concepts/wash-trading)

---

## Definition

**Quick definition.** Executing offsetting buy and sell transactions to inflate apparent trading volume without taking real market risk. Federally prohibited under the Commodity Exchange Act. On prediction markets, wash trading boosts platform metrics, distorts liquidity signals, and laundered trader cohorts can pose as legitimate market makers.

## Key Insights

- Rajiv Sethi opens with a forensic vignette from **6pm on November 1 (~2 days before US polls opened)** on Polymarket: a single trader executed a string of paired buy-then-sell transactions where each purchase was followed quickly by a sale at "a price that is a tenth of a penny lower" · guaranteed micro-losses on every round-trip, but enormous volume inflation. The puzzle: are those losses real, or are the counterparties also in on it?
- The paper Sethi co-authors (lead author **Allen Sirolly**, with **Hongyao Ma** and **Yash Kanoria**) introduces a novel detection algorithm: **modular, with an initialization stage and an iterative network-based redistribution stage**. Initialization scores each wallet on its "propensity to open and close positions repeatedly". Redistribution updates each wallet's score from the volume-weighted scores of its counterparties, iterated to convergence. Wallets retained above a threshold are flagged.
- The theoretical core: in a counterparty network, **wash traders exhibit homophily** (trade only within their collusive clique) while **market makers exhibit heterophily** ("market makers seldom trade with other market makers" because each side of an MM trade is a liquidity provider vs taker, not two MMs).
- Empirical anchor (now expanded with article detail): the flagged cluster is a group of **200 wallets all starting with "MAY"** (e.g. MAY20, MAY175, MAY176) trading "almost exclusively with each other". Collectively they traded **over 116 million shares** generating **>$113M in dollar volume** for an aggregate loss of just **$57.86**. One wallet · **MAY117** · bought and sold over a million shares across **33 markets** over several months and ended up with **profits of precisely zero**.
- Algorithm convergence: Figure 5 in the paper shows many wallets red-flagged at initialization get dropped at successive redistribution iterations, "leaving wash traders identified with high confidence after three iterations".
- Sethi names two foundational adverse-selection references for MM behaviour: **Glosten and Milgrom** and **Kyle** · important because the legal/economic distinction between an MM and a wash trader hinges on adverse-selection exposure (real MMs face it; wash traders avoid it).
- Sethi's strategic note: any published detection algorithm "is likely to result in strategic responses by wash traders that better allow them to evade detection. Ideally one would want to design a system that is not vulnerable to such gaming, but whether or not this ideal is even attainable remains an open question."
- Sethi's framing of the contribution: the *enduring* contribution is "a transparent and modular detection algorithm · rather than the specific empirical claims about a particular exchange". Trading volume is the most commonly cited measure of market participation, and wash trading "makes it imprecise and less meaningful".
- Sethi reports the **initial reaction from at least one Polymarket insider has been encouraging** · suggesting cooperation rather than adversarial response from the venue.
- Luca Prosperi's separate finding (referenced in liquidity-provision file): during the 2024 election cycle, **~41% of Polymarket volume appeared to be wash trading** and four coordinated accounts controlled 23% of open interest.
- The implication is severe: **a significant share of headline PM volume may be wash trading**, and when CNN/WSJ broadcast probabilities backed by that volume, they're laundering manipulated signal through credible newsrooms (Vaidik Mandloi).
- The legal hook: wash trading is "prohibited by law on regulated exchanges in the United States, and can be subject to significant sanctions" under the **Commodity Exchange Act** · making Kalshi's CFTC-regulated structure structurally tougher for wash traders than Polymarket's pseudonymous blockchain.
- Detection is network-based; pseudonymous chains help and hurt simultaneously · wallet linkages are visible on-chain but real-world identity is not.

## Notable Quotes

> "If one were to represent traders as nodes and transactions between them as edges in a network, wash traders would exhibit homophily while market makers exhibit heterophily."  
> — *Rajiv Sethi*

> "These wallets collectively traded over 116 million shares and generated more than 113 million in dollar volume, but ended up with an aggregate loss of just $57.86."  
> — *Rajiv Sethi*

> "One of these was MAY117, an account that bought and sold over a million shares across 33 markets over several months and ended up with profits of precisely zero."  
> — *Rajiv Sethi*

> "Trading volume is among the most commonly used measures of market participation and conviction, and wash trading makes it imprecise and less meaningful."  
> — *Rajiv Sethi*

> "Any algorithm of this kind, once published and implemented, is likely to result in strategic responses by wash traders that better allow them to evade detection."  
> — *Rajiv Sethi*

## Where It Matters

Wash trading is one of the most legally exposed pathologies in the asset class · and one of the easiest for sophisticated detection algorithms to identify post-hoc. Every platform's headline volume figure should be discounted by the share that on-chain network analysis flags as wash trading. For regulated venues (Kalshi) this is enforced; for pseudonymous venues (Polymarket) it's a structural risk that can be detected but only after the fact. The fact that 23% of OI during the 2024 election was controlled by four wallets is the single most-cited data point that PMs are not yet ready to serve as authoritative information signals.

## Related Concepts

- **Market manipulation** · the broader category.
- **Market surveillance** · the detection regime.
- **Market making** · what wash traders pose as.
- **Insider trading** · frequent companion offense.
- **Liquidity provision** · wash trading inflates reported liquidity without providing real depth.
- **Regulatory classification** · determines who has authority to enforce.

## Sources

- [The Detection of Wash Trading](https://rajivsethi.substack.com/p/the-detection-of-wash-trading) — Rajiv Sethi · Nov 12, 2025

