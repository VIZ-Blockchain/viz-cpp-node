# Relative Value Trading

**Category:** Liquidity & Trading  
**Source:** [PM Atlas — Relative Value Trading](https://www.pmatlas.xyz/concepts/relative-value-trading)

---

## Definition

**Quick definition.** A strategy that bets on the *relationship* between two correlated prediction-market prices rather than the direction of either one. Profits when a mispriced spread converges · equivalent to equity pair-trading or fixed-income spread trading, transposed into discrete-event space.

## Key Insights

- Jon Turek frames RV as a direct lift from macro trading: "relative value trading is a very big macro trading strategy that plays on the idea that the spread or correlation between two securities has some sort of fair value". Simply put: "betting on the relationship between two prices rather than the direction of either one individually".
- The reformulation Turek offers: "Instead of saying 'I think X will go up,' you're saying 'I think X is mispriced relative to Y, and that gap will close.'"
- Turek's leverage framing: RV is also "a way to add leverage to an existing view". If you think outcome X is interesting and you believe outcome Y is highly correlated to X, you can structure correlated bets that "juice your returns relative to the payout of just betting on your underlying view".
- Canonical example with full data: **30–35% Polymarket recession odds** (matches Goldman; baseline economist estimate is 15–20%); **65% chance unemployment rises to 5%, 35% chance to 5.5%**; yet Fed-cut market prices only **20% chance of more than two cuts**. The six historical episodes of unemployment rising +1.2% in a year (1974–75, 1980–82, 1990–91, 2001, 2007–09, 2020) averaged **seven Fed cuts each**.
- Concrete trade construction: long "YES" on Fed rate cuts + short "NO" on unemployment outcomes. "Could offer attractive payouts in either macroeconomic outcomes" because the legs hedge each other where economic logic says they should be correlated.
- Macro context Turek frames the trade with: ~92k jobs shed last month, ~6 months of weak payroll growth, falling JOLTs job openings, and energy shock from war in Iran · all factors that should push the unemployment-to-Fed-cuts correlation up, not down.
- Turek closes with the recurring meta-observation: "we have a lot of listed markets that are being priced idiosyncratically but many of them have an implied correlation to them. I think this is creating a lot of really compelling relative value trades."
- Unlike directional bets, RV positions are partially insulated from the binary-resolution gap risk that wrecks ordinary leveraged PM strategies: if both legs move with the spread, the directional risk cancels.
- The strategy benefits disproportionately from the structural fact that PM contracts are **priced idiosyncratically** with no built-in cross-market arbitrageur.
- Closely related to:
- **Implied correlation** (the underlying mispricing)
- **Covariance markets** (the proposed venue to express RV cleanly)
- **Cross-platform arbitrage** (an RV trade across venues rather than across events)
- **Conditional probability arbitrage** (st1ne's MEV-style edge: trading correlated PMs as a portfolio)
- The natural participants are macro-aware hedge funds and AI agents capable of scanning thousands of event pairs; retail is structurally locked out by the analytical lift.

## Notable Quotes

> "Relative value trading is betting on the relationship between two prices rather than the direction of either one individually. Instead of saying 'I think X will go up,' you're saying 'I think X is mispriced relative to Y, and that gap will close.'"  
> — *Jon Turek*

> "Correlation can create leverage or arbitrage, and I think both of these things are relevant in the space of prediction markets right now."  
> — *Jon Turek*

> "I think this presents a really attractive relative value trade between 'NO' on unemployment rate outcomes and 'YES' on Fed rate cuts."  
> — *Jon Turek*

> "Cross-market mispricings like this are common and create attractive relative value trades."  
> — *Jon Turek*

## Where It Matters

Relative-value trading is the leverage-tolerant strategy class for prediction markets · and possibly the largest untapped category for institutional capital. Because RV trades hedge most of the directional binary risk, they can carry size that pure directional positions can't, and they exploit a structural inefficiency (idiosyncratic pricing) that incumbents are not solving. The bottleneck is tooling: explicit covariance markets, cross-market scanners, and capital-efficient margining of paired positions.

## Related Concepts

- **Implied correlation** · the upstream concept.
- **Covariance markets** · the cleanest venue.
- **Arbitrage** · parent category.
- **AI agents** · scale enabler.
- **Position collateralization** · improves RV capital efficiency.
- **Gap risk** · mostly hedged out by paired positions.

## Sources

- [Prediction Markets and Implied Correlation](https://cheapconvexity.substack.com/p/prediction-markets-and-implied-correlation) — Jon Turek · Mar 24, 2026

