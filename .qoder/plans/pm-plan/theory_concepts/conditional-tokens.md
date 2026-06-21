# Conditional Tokens

**Category:** Governance & Decisions  
**Source:** [PM Atlas — Conditional Tokens](https://www.pmatlas.xyz/concepts/conditional-tokens)

---

## Definition

**Quick definition.** Tokens whose redemption value depends on a specific future outcome. They enable composable prediction-market positions: split collateral into YES/NO (or multi-outcome) tokens, trade each independently, then merge or redeem at resolution. Pioneered as a standard by Gnosis' Conditional Token Framework (CTF), which underpins Polymarket and most onchain prediction markets.

## Key Insights

- The Gnosis CTF is the canonical implementation: any user can **split** one unit of collateral (typically USDC) into one of each outcome token, or **merge** a complete set of outcome tokens back into one unit of collateral. In a correctly structured marketplace, outcome-token prices stay between 0 and 1 and sum to 1 (binary case).
- The split/merge invariant is what lets liquidity expand whenever matched counterparties exist: if Jack wants 10 YES at $0.35 and Jill wants 10 NO at $0.65, the protocol mints 10 full conditional sets from $10 of collateral and distributes them · no external maker needed. Ranger Global's microstructure series identifies this as the structural reason CLOBs beat constant-product AMMs for binary events.
- Conditional tokens compose into **multi-outcome markets** by stacking conditions: each candidate gets its own YES/NO market priced independently. The price sum is enforced only loosely (by arbitrage), so the "sum of YES prices ≠ 100%" gap creates a "soft link" arbitrage opportunity · buy all underpriced YES sides to lock in a small profit when at least one must resolve true.
- Polymarket alone hit roughly 35x weekly active user growth from May to September 2024 (Shoal), with 99.2% of trading volume concentrated in political markets and two-thirds of cumulative volume occurring in the last six months before the 2024 US election (ASXN). The conditional-token primitive is the operational substrate underneath this.
- **Hyperliquid's HIP-4** (Pink Brains) reframes outcome contracts as an onchain **options layer**: unified margin engine, fee structure, and token value capture targeted at credit default swaps, parametric insurance, and futarchy rather than just binary betting. Conditional tokens are the building block for the options stack.
- **Conditional decision tokens** (futarchy implementation): pABC/fABC and pUSD/fUSD ("p" = pass, "f" = fail). If the proposal passes, pABC redeems for regular ABC and fABC becomes worthless; the opposite if it fails. Two markets trade simultaneously (pABC/pUSD and fABC/fUSD); the proposal passes if pABC/pUSD's TWAP exceeds fABC/fUSD's. Used by MetaDAO.
- The conditional-tokens architecture is what makes **trustless joint ownership** possible (Heavey/Umbra): an attempt to raid a DAO treasury through governance becomes self-defeating because the attacker must buy worthless pABC above spot AND sell fABC below fair value. The mechanism makes the rational economic outcome of the attack a net loss for the attacker.
- Conditional tokens enable **composable hedging**: a BTC holder worried about an election can lock in a conditional BTC price for the specific scenario by trading directly on an Impact Market token (BTC | election outcome), rather than separately holding BTC and betting against probability · collapses basis risk between event view and asset exposure (Galaxy/Pokorny).
- **DeFi composability**: conditional tokens are SPL or ERC-20 tokens, so they can be used as collateral elsewhere. Gondor lets users lend against PM positions; DFlow tokenizes Kalshi contracts as SPL tokens for cross-chain DeFi use. Mike's "How Prediction Markets Turn Into Risk Instruments" frames this as PMs developing the same derivative second-layer that stocks did.
- **Resolution-aware Meta-Pools** (Jeff Opinion): proposes infrastructure for swapping semantically similar conditional tokens issued by different oracles. The architecture introduces CredibilityTokens (trading on oracle trustworthiness) and ConvergenceTokens (hedging the divergence risk between two oracle interpretations of "the same" event). Estimates $3.4–8.5M of annual efficiency loss from current fragmentation.
- **Cross-platform arbitrage on conditional tokens** is a major activity: identical events trade at different prices on Polymarket vs. Kalshi vs. Hyperliquid. Latency under 100ms captures 73% of arbitrage profits (Ranger Global). The conditional-token primitive makes positions transferable and hedgeable across venues.
- **Probability-scaled dynamic fees** (Ranger Global) are designed to address the structural challenge that bid-ask spread should shrink as price approaches 0 or 1 (where most of the mass concentrates). Conditional tokens at the extremes need fee curves that adjust to capital efficiency rather than being flat percentages.
- The Polymarket US government shutdown market (Jan 2025) is the canonical conditional-token resolution failure case (michaellwy): tokenholder-voters with retroactive rule changes and a corruption cost below the value at stake · the conditional token resolved YES on a shutdown that did not actually happen. The fragility of conditional tokens is downstream of oracle design, not token mechanics.
- **Why 99.2% Polymarket volume sits in politics**: conditional tokens need both schelling-point salience and credible resolution. Politics has both; obscure questions have neither, which is why Polymarket's category expansion has lagged its election cycle peaks.
- **The "no mechanical link" arbitrage** in multi-outcome markets is a feature, not a bug: it preserves the ability to trade each outcome as a continuous variable in a unified order book, while letting arbitrageurs enforce the sum-to-1 constraint through capital, not protocol logic. Inefficiencies live in spread, not in mispricing.

## Notable Quotes

> "All outcomes on Polymarket are tokenized and exchanged both non-custodially and atomically on the Polygon Network… Polymarket outcome shares are binary outcomes, Yes/No, and are represented using Gnosis' Conditional Token Framework."  
> — *Alex Nardi, Shoal*

> "The matching calculation for this example is simple, $3.50 will be transferred from Jack to Jill in exchange for her 10 shares… An amount of $3.50 from Jack and $6.50 from Jill ($10 in total) is used to mint 10 full conditional token sets (10 YES and 10 No), then distribute 10 Yes to Jack and 10 No to Jill."  
> — *ASXN, *Polymarket: An Election-Driven Success Story**

> "Impact markets transfer the burden of correlation estimation from every individual end user to the market's price discovery process… analogous to the difference between estimating implied volatility yourself versus reading it off an options chain."  
> — *Zack Pokorny, Galaxy Research*

> "For Bob's proposal to pass, pABC/pUSD needs to trade above fABC/fUSD… From both Alice and Bob's perspectives, 1 fABC is worth 1 ABC because the DAO carries on as normal if the proposal fails, and 1 pABC is worth 0 because as soon as the proposal passes, the DAO won't possess anything anymore."  
> — *Kevin Heavey, *Futarchy as Trustless Joint Ownership**

> "PM traders systematically underreact to spot moves by 10-20%, and latency under 100ms now captures 73% of arbitrage profits."  
> — *Ranger Global, *Anatomy Of A New Asset Class I**

## Where It Matters

Conditional tokens are the load-bearing primitive: every onchain prediction market of any size today uses them, every futarchy implementation uses them in their pass/fail form, every Impact Market design extends them with asset-denominated payouts, and every cross-platform arbitrage strategy is fundamentally trading them. The fragility points are oracle design (how outcomes get reported) and liquidity fragmentation across venues · both of which are solvable in principle and the active frontier.

## Related Concepts

- **Decision markets** · Pass/fail conditional token pairs are the standard implementation of decision markets.
- **Futarchy** · Conditional tokens make asset-futarchy decision markets work; the trustless-joint-ownership argument runs through conditional-token mechanics.
- **Impact markets** · Conditional tokens denominated in real assets (BTC, equity, etc.) rather than $0/$1 are the natural generalization.
- **Oracle design / UMA / dispute resolution** · Conditional-token redemption is downstream of oracle output; bad oracles produce bad redemptions regardless of how clean the token model is.
- **Cross-platform arbitrage** · Identical conditional tokens at different platforms create the most reliable arbitrage opportunities in the category.
- **Order book / market making / execution quality** · CLOBs work better than CFAMMs for binary events specifically because the split/merge invariant lets order books absorb directional flow without LP losses.
- **Attention markets** · Trendle is conceptually a conditional-token market where the underlying is a normalized attention index instead of a binary outcome.
- **Network effects / platform competition** · Conditional-token standards (Gnosis CTF, HIP-4) become a coordination point for liquidity; switching costs flow through whichever standard captures the largest split/merge pool.

## Sources

- [HIP-4 Is Not a Prediction Market · It's the Options Layer: A Full Guide](https://x.com/PinkBrains_io/status/2051312783074226606) — Pink Brains · May 4, 2026
- [Anatomy Of A New Asset Class I: How Markets Turn Capital Into Probability](https://x.com/Ranger_Global/status/2046547375649427572) — Ranger Global · Apr 21, 2026 ·
- [How Prediction Markets Turn Into Risk Instruments](https://x.com/mikhryc0x/article/2020802941091741972) — Mike · Feb 9, 2026 ·
- [Prediction Markets' Next Frontier: Impact and Decision Markets](https://www.galaxy.com/insights/research/prediction-markets-impact-markets-decision-markets-futarchy) — Zack Pokorny, Galaxy · Jan 12, 2026 ·
- [Meta Pool: A Unified Infrastructure for Liquidity and Resolution Trust in Prediction Markets](https://x.com/jeff__opinion/article/1957611486991487486) — Jeff Opinion · Aug 19, 2025 ·
- [10 Predictions About Prediction Markets](https://x.com/michael_lwy/status/1884570344184578081) — michaellwy · Jan 29, 2025 ·
- [Why Prediction Markets Are Broken (And How to Fix Them)](https://x.com/michael_lwy/status/1877211555496079683) — michaellwy · Jan 9, 2025 ·
- [Futarchy as Trustless Joint Ownership](https://www.umbraresearch.xyz/writings/futarchy) — Kevin Heavey · Oct 28, 2024 ·
- [Polymarket and the Proliferation of Prediction Markets](https://www.shoal.gg/p/prediction-markets) — Alex Nardi, Shoal · Oct 8, 2024 ·
- [Mechanisms for Prediction Markets](https://paragraph.com/@mechanism.institute/prediction-markets) — Raye Hadi, Sofia Cossar, Ori Shimony · Aug 22, 2024 ·
- [Polymarket: An Election-Driven Success Story](https://newsletter.asxn.xyz/p/polymarket-an-election-driven-success) — ASXN · Aug 10, 2024 ·

