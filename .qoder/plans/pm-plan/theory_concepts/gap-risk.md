# Gap Risk

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Gap Risk](https://www.pmatlas.xyz/concepts/gap-risk)

---

## Definition

**Quick definition.** Exposure to sudden large price jumps in binary markets when new information arrives between trades. Gap risk is what makes prediction markets uniquely hostile to leverage, to passive market making, and to traditional risk-management toolkits · outcomes resolve instantly, skipping the intermediate prices that liquidation engines need to function.

## Key Insights

- semaji.eth: "Gap risk is effectively worse than any other asset class because informed counterparties can have near-perfect information and take out entire order books." Indian options easy → crypto medium → PMs *legendary*.
- Nick Ruzicka surveys leverage attempts and finds nearly everyone converges to **1×–1.5×** rather than the 10×–20× they advertise. **dYdX's TRUMPWIN perp on election night 2024** is the case study: sophisticated safeguards still broke under real conditions. Three camps: constrain, engineer around (dynamic fees, circuit breakers), or ship and iterate.
- Darren on the four leverage models · lending pools (Gondor / Morpho-style), prime brokers (Ultramarkets), synthetic desks (CFD counterparties), perpetual futures (dYdX-style) · all share **structural dependency on CLOB venue architecture that degrades during jump events**. Fee revenue opportunity sized at $15M base / $50.7M bull, **87% from financing on OI**.
- sybilpm's "Sniper's Tax": sniping geopolitical strike markets at 10c costs **80 cents on the dollar** when 0.10 → 0.99 on a single tweet. Concrete case: "dudukos" cleared the entire order book of "Will Israel strike Gaza on January 3, 2026?" in one trade, buying from $0.10 up to $0.80, then repeated the pattern Jan 10/11/12 across dozens of Israel-strike markets. Proposes frequent batch auctions (Budish, Cramton, Shim 2015) to neutralize gap risk by shifting competition from speed to accuracy.
- sybilpm on the unhedgeable problem: in equities an MM holding Apple can sell Nasdaq futures; in PMs there's no correlated instrument to a "Will Israel strike Lebanon today?" position · the MM is just exposed. Quote: "You weren't providing liquidity to a forecaster. You were exit liquidity for a bot."
- sybilpm MM profit equation: **E[π] = (s/2 · V(s)) − P_news · L_snipe**. When P_news is high (geopolitical, breaking-news markets) the sniping term dominates and MMs only have two options · widen s or pull quotes. Both make the market worse.
- sybilpm's "liquidity mirage": at 3am Tuesday a reasonable order book is visible, but it'll vanish the instant news drops; once real liquidity is gone, markets become trivially manipulable with a few thousand dollars. Polymarket/Kalshi LP-incentive programs route retail LPs into being "exit liquidity for snipers" · rewards never sized to cover 80-cents-per-contract gap risk.
- njokuScript ("Leverage Fixes PMs"): the gap-risk workaround is **temporal arbitrage** · leveraged markets close *before* event resolution, so liquidation engines never face the discontinuity. Vault-based yield layer captures trading-activity returns rather than directional outcome exposure.
- Sakshi Mishra: trade-expressiveness is one of five infrastructure pillars on the path to $1T · leverage faces a gap-risk constraint unique to binary markets.
- DWF Ventures: jump risk, binary valuation, and regulatory uncertainty are the three structural barriers to leverage and collateral lending. Emerging solutions: epoch-based fees, perp futures on outcomes, tokenized positions on Solana.
- Stephanie Stacey (FT) on bonding trades: ~$150k executable at one time on near-certain PMs; small yield, **catastrophic tail risk** if the near-impossible outcome occurs. Gap risk is the tail of bonding.
- njokuScript ("A New Kind of Asset Class"): PM positions have **time-bounded decay and binary convergence to truth** · properties that create temporal arbitrage but also make platform-native margin dangerous.
- Will Howard's adjacent framing: continuous-time markets *reward speed over accuracy*. In a batch auction Trader A and Trader B both land in the same window; "the winner is whoever valued the contract higher, not whoever had the shorter wire." The "rents flow toward accuracy instead of infrastructure" · Budish-Cramton-Shim showed this formally in 2015 for equities; the case in PMs is even stronger because prices can jump 80 points in a tick.

## Notable Quotes

> "In traditional markets, sniping costs basis points; in prediction markets, it costs 80 cents on the dollar."  
> — *sybilpm*

> "The resolution is binary and often instant. There's no gradual price discovery."  
> — *Nick Ruzicka*

> "Gap risk is effectively worse than any other asset class."  
> — *semaji.eth*

> "The markets with the highest social value and information content"  
> — *those with extreme or high news sensitivity · are precisely the markets in which passive liquidity is most difficult to sustain." · sybilpm*

> "Your cancel button was slower than their react button."  
> — *sybilpm*

## Where It Matters

Gap risk is the binary-specific amplifier on every other microstructure problem. It's why MMs widen spreads defensively, why leverage products keep converging to 1.5×, why dYdX's election perp was an instructive failure, and why "bonding" trades carry tail risk that wipes out years of small yield in a single resolution. Every serious design proposal in 2026 · batched auctions, epoch-based fees, temporal-arbitrage leverage windows, tokenized positions, dynamic fees scaled by P×(1-P) · is a reaction to gap risk specifically.

## Related Concepts

- **Adverse selection** · gap risk is adverse selection in extremis.
- **Toxic flow** · the realized form of gap risk.
- **Market making** · gap risk is the binding constraint on profitable MMing.
- **Batched auctions** · leading mitigation.
- **Position collateralization / temporal arbitrage** · capital-efficiency and leverage workarounds that depend on closing positions before resolution.
- **Binary contracts** · the structural cause.
- **Oracle design / resolution criteria** · control the moment when gap risk fires.

## Sources

- [A Second Identity: Prediction Markets as Financial Derivatives](https://x.com/i/article/2049414664946323456) — DWF Ventures · Apr 30, 2026
- [Leverage in Prediction Markets](https://x.com/0xMims/status/2041498106181627918) — Darren · Apr 7, 2026
- [The Sniper's Tax](https://sybilpm.substack.com/p/the-snipers-tax) — sybilpm · Mar 8, 2026
- [Prediction Markets: The Path to $1 Trillion and What Needs to Happen Next](https://x.com/a1research__/article/2022340314770375024) — Sakshi Mishra · Feb 14, 2026
- [Leverage Fixes Prediction Markets: The Case for Why 10x Is Safer Than 1x](https://x.com/njokuScript/article/2022351299547689325) — njokuScript · Feb 14, 2026
- [How Prediction Market Traders Reinvented the Bond](https://www.ft.com/content/3382195d-b417-4fc8-9c2e-17f38027a635) — Stephanie Stacey (FT) · Feb 6, 2026
- [Everyone's Promising 20x Leverage on Prediction Markets. Here's Why It's Hard.](https://medium.com/@nick.c.ruzicka/everyones-promising-20x-leverage-on-prediction-markets-here-s-why-it-s-hard-8684d9a77e11) — Nick Ruzicka · Jan 27, 2026
- [Prediction Markets: A New Kind of Asset Class](https://x.com/njokuScript/article/2010080717729046613) — njokuScript · Jan 11, 2026
- [The Liquidity Problem in Prediction Markets, Part II: Adverse Selection in Prediction Markets](https://x.com/semajeth/status/1975211193418563697) — semaji.eth · Oct 6, 2025

