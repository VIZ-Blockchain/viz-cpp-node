# Resolution Criteria

**Category:** Oracle & Resolution  
**Source:** [PM Atlas — Resolution Criteria](https://www.pmatlas.xyz/concepts/resolution-criteria)

---

## Definition

**Quick definition.** Resolution criteria are the *pre-defined, natural-language rules* specifying exactly what counts as YES, NO, or VOID for a given prediction market. They are the contract · every other layer (oracle, dispute system, AI judge) is just enforcing this text. Aradtski's claim that "the Oracle Problem isn't the problem · definition is" is the central thesis of this concept: most cataloged failures trace to the criteria, not the resolver.

## Key Insights

- **"The Oracle Problem is actually a definition problem."** Aradtski catalogued 12+ Polymarket controversies and argued each one was about ambiguous event wording, not oracle untrustworthiness. The oracle did exactly what the rules said; the rules were the bug.
- **Bet on the rules, not on reality.** OddChain (the most-cited resolution-criteria essay) frames the trader's job: *"You're not necessarily taking a position based on the outcome of the question; you're taking a position based on the rules of resolution for that market."* The Ginsburg-on-hot-dogs analogy applies · give me the definition first, then I'll tell you the answer.
- **Vague terms are the dominant failure pattern.** XO Market's 14-resolution-failure catalog groups them into four buckets · and **vague criteria** is the top category by dollar volume. Specifically:
- "Suit" (Zelensky $240M)
- "Invasion" (Venezuela $10M+)
- "Ban" (TikTok $120M)
- "Performed" (Cardi B halftime $47M)
- "Word" (Kalshi mentions markets)
- **Even legal and academic sources don't agree on these terms.** OddChain's invasion case study: UN Resolution 3314, Rome Statute, Geneva Conventions, US Constitution, and Black's Law Dictionary all use the word "invasion" but none defines it precisely. The market wrote an underdefined word into a $10M contract and was surprised to have a $10M dispute.
- **The "consensus of credible sources" clause is the most-abused resolution criterion.** It pushes the definitional fight from the event itself onto *which sources count* and *what counts as consensus*. Polymarket's Zelensky-suit market had 40+ outlets calling it a suit; UMA still found no "consensus of credible reporting." OddChain on Venezuela: US wire services said "raid," foreign governments said "invasion," and the discrepancy *itself* was the resolution.
- **Polymarket's mid-game "clarifications" are functionally rule changes.** OddChain on Venezuela: a clarification was issued Jan 4 · after the Jan 3 raid and after Trump's "we will run the country" statement · narrowing what counted. From a trader's view, this is changing the rules after positions are locked. Hall (a16z) calls this "violating the basic compact between platform and participant."
- **Resolution-source pinning is its own attack surface.** When the criterion says "resolves based on X website" or "X media outlet," the *X* becomes the attack target. The OPM-website shutdown failure is the cleanest case: the website was a day late and the market resolved against reality. The Polymarket Paris-temperature/hair-dryer (Gunitsky) is the same pattern with a different sensor.
- **The "hair dryer problem" is the criteria-as-attack-surface principle in extremis.** Gunitsky: when resolution criteria point at an unguarded thermometer, $20,000 in winnings goes to the guy with a hair dryer. The fix isn't a better oracle · it's better criteria (multi-source resolution, integrity checks, statistical outlier filters).
- **Criteria can also encode moral hazard.** michaellwy CFTC comment: a market on whether a CEO says a specific phrase on an earnings call gives the CEO an incentive to say (or not say) the phrase. Brian Armstrong literally admitted this on a Coinbase earnings call in October 2025. The market's *existence* changed the speaker's behavior · and the criteria made it economical to do so.
- **Markets on individual humans implicitly encode assassination risk** (Guillory & Zimmermann). Any market with a "void if subject dies" rule incentivizes losing bettors to harm the subject; any market *without* a void-on-death rule prices in the assassination probability. There's no neutral wording · the criteria themselves are politically loaded.
- **Resolution-source biased oracles** (Lou Kerner). The 2024 Polymarket presidential market named AP, Fox News, and NBC as resolution sources. Kerner argues choosing Fox News was structurally biased · Fox would never be the first to call against Trump · meaning a Trump-loss outcome could *never* resolve cleanly through the criteria as written. The "neutrality" of the criteria masks the encoded asymmetry.
- **michaellwy's "1c trick" exposes a notional-volume criterion problem.** Resolution criteria are tied to "market volume" for some Polymarket features; that volume can be inflated by trading low-probability shares at 1¢. takers account for ~41-47% of YES volume *within the 1-10¢ longshot bucket* (Becker) - not 41% of lifetime volume. The criterion was honest; the input was gameable.
- **The Time "Person of the Year · Other" failure illustrates open-set criteria.** Tim.0x: "Person of the Year 2025" was given to "Architects of AI." Polymarket had no "Architects of AI" outcome listed, so the market resolved to "Other." Traders who correctly predicted AI-related winners lost on a criterion technicality. Synthetic data / generative outcomes are being proposed to handle this class of failures.
- **Defensive criteria-reading is now a trader strategy.** OddChain's playbook: (1) Read the criteria, not the title. (2) Identify the hinge words. (3) Understand what "consensus of credible sources" means in this jurisdiction. (4) Avoid markets with ambiguity. (5) Model the resolver, not reality.
- **The "regulated event contract" pathway forces precision.** Kalshi's CFTC-regulated contracts have to define everything precisely up-front, and the CFTC reviews them. michaellwy CFTC comment: this is the strongest argument for federal preemption · at least the rules can't be changed mid-game on regulated venues.
- **Insurance use cases require parametric criteria, not narrative ones.** Eigenmann HIP-4: when an outcome contract acts as parametric cover for a DeFi exploit (like the $292M Kelp DAO rsETH bridge case), the criteria *must* be machine-readable triggers (on-chain TVL drops, oracle-fed thresholds), not "consensus of credible reporting." The narrative-resolution mode doesn't scale to parametric insurance.
- **The criteria-precision tradeoff with social engagement.** Tighter criteria = fewer disputes but also fewer surprising/popular markets. The most viral Polymarket markets (Zelensky suit, Venezuela invasion) are *precisely* the loosely-worded ones. The platform faces a structural choice: precision-or-engagement.

## Notable Quotes

> "A tricky aspect of prediction markets is that you're not necessarily taking a position based on the outcome of the question; you're taking a position based on the rules of resolution for that market."  
> — *OddChain*

> "If you tell me what an invasion is, I'll tell you if what happened in Venezuela was one."  
> — *OddChain (paraphrasing the Ginsburg "you tell me what a sandwich is" framing)*

> "When resolution criteria are clear, prediction markets can surface the truth. But when the contract relies on vague or contested terminology, we're no longer predicting reality, we're predicting whether reality aligns with someone else's definition."  
> — *OddChain*

> "Read the resolution criteria, not the title… Model the resolver, not reality. The question isn't 'What do I think happened?' but 'What would people applying these specific rules to these specific sources conclude?'"  
> — *OddChain*

> "[The Oracle Problem] is actually a definition problem, not a trustlessness problem."  
> — *Aradtski*

## Where It Matters

Resolution criteria are the layer where the prediction market's truth-claim either holds or collapses. Sloppy criteria attract volume (the ambiguity is fun) and produce disputes (the ambiguity is exploitable). Every serious oracle-design proposal ultimately routes back to "write better criteria" · AI judges can interpret consistently, layered oracles can route by criteria type, parametric markets eliminate criteria interpretation entirely. The platforms that scale will be the ones that solve criteria precision without killing market diversity.

## Related Concepts

- **oracle design** · the layer that *enforces* the criteria.
- **dispute resolution** · the layer that *adjudicates ambiguous* criteria.
- **event contracts** · the legal/regulatory wrapper around the criteria.
- **UMA protocol** · the resolver that has stressed criteria precision most.
- **self-resolving markets** · designs that route around criteria by using crowd consensus.
- **AI agents** · proposed criteria-interpretation substrate.

## Sources

- [Resolving Prediction Markets: An AI-Driven Layered Approach](https://x.com/xolabs_/status/2055266974259961917) — XO Labs · May 15 2026 ·
- [Outcome Markets as a Cover Venue: HIP-4 and Its Traditional Comparables](https://x.com/DeanEigenmann/status/2052734335971713347) — Dean Eigenmann · May 8 2026 ·
- [Why Prediction Markets Are Hard to Regulate](https://michaellwy.substack.com/p/my-comment-to-cftcs-prediction-markets) — Michael Li (michaellwy) · Apr 30 2026 ·
- [The Hidden Risk Of Prediction Markets: 14 Resolution Failures That Cost $500M](https://x.com/xomarket/status/2046924302952640934) — XO Market · Apr 22 2026 ·
- [Every Opinion Deserves A Market](https://x.com/turfsports_/status/2047030930699854334) — Turf · Apr 22 2026 ·
- [Truth Machines Go to War](https://www.bloomberg.com/opinion/newsletters/2026-04-06/truth-machines-go-to-war) — Matt Levine · Apr 6 2026 ·
- [The Economy of Truth: Inside the Decentralized Courtroom of Polymarket & UMA](https://x.com/smaaaaliy/article/2033242239498076190) — Smaliy · Mar 16 2026 ·
- [All Types of Arbitrage on Prediction Markets](https://x.com/Nekt_0/article/2019107985079816395) — Nekt0 · Feb 5 2026 ·
- [What to Do When Prediction Markets Fail](https://a16zcrypto.substack.com/p/how-ai-judges-can-scale-prediction) — Andy Hall · Jan 24 2026 ·
- [Semantics for $10 Million: When Is an Invasion an Invasion?](https://www.oddchain.com/p/semantics-for-10-million-when-is-an-invasion-an-invasion-polymarket) — OddChain · Jan 23 2026 ·
- [The Measles Market on Kalshi Is One of the Dumbest and Most Tragic Markets We've Seen This Year](https://www.oddchain.com/p/the-measles-market-on-kalshi-for-2025-is-one-of-the-dumbest-and-most-tragic-markets-we-ve-seen-this) — OddChain · Dec 30 2025 ·
- [A Technical Typology of Prediction Markets: Mechanics and Tradeoffs](https://x.com/Baheet_/status/1967186769356488828) — Baheet · Sep 14 2025 ·
- [Prediction Markets: The 'Oracle Problem' Is Not the Problem](https://x.com/aradtski/status/1949977296523145695) — Aradtski · Jul 29 2025 ·

