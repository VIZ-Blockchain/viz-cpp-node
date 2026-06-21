# Attention Markets

**Category:** Governance & Decisions  
**Source:** [PM Atlas — Attention Markets](https://www.pmatlas.xyz/concepts/attention-markets)

---

## Definition

**Quick definition.** Markets that trade on what topics, events, or narratives will capture public attention. Most onchain implementations (Trendle is the prototype) treat attention as a normalized engagement index aggregated across social platforms, and let users long/short the index perpetual-futures-style.

## Key Insights

- The thesis (Trendle/Monad blog): in the digital age, **attention is the only scarce resource** · information, content, and tools for creation are essentially infinite, but the human cognitive bandwidth that "ratifies" content is fixed. Every actor in the attention economy competes for the same fixed pool, which makes attention a credible underlying for a derivative.
- Kyla Scanlon's "attention supply chain" is the framework: outrage, novelty, and identity are raw inputs → memes, clips, short-form packaging turn them into digestible units → feeds and news cycles push them into circulation → people invest emotionally or financially in where the narrative goes. If attention has a supply chain it can be quantified, priced, and traded.
- **Attention indexes are the underlying** (not a binary event). Trendle aggregates engagement signals from X, Reddit, and YouTube (with Google Search planned) and normalizes them onto a 0–1 scale per topic. The platform layers a perpetual-style market on top.
- Anti-gaming is the central design challenge · Goodhart's Law explicitly: when a measure becomes a target, it stops being a good measure. Naive metrics like "post count" can be pumped with bots; Trendle's defenses are:
- **Source diversification**: multi-platform ingestion (X, Reddit, YouTube + Google Search planned) raises the cost of coordinated manipulation.
- **Normalization & smoothing**: 0–1 scaling, outlier trimming, exponential decay.
- **Deseasonalization**: a sports team's pre-game attention spike doesn't count as breakout if it follows the historical pattern.
- **Quantile clipping**: extreme values are bounded rather than allowed to dominate the index.
- The signals ingested per platform are deliberately heterogeneous: Reddit (posts, votes, comments, hot rank, velocity), YouTube (views, view-weighted likes/comments), Twitter (retweets, bookmarks, impressions, quotes, replies). The bet is that manipulating *all of these consistently* is dramatically harder than manipulating one.
- **Trendle is structurally a perpetual, not a binary**: the two-layer architecture separates (1) the **index layer** (attention index computed independently of positioning) from (2) the **market layer** (perpetual-style derivative built around it). Up to 5x leverage; users with leverage can be liquidated.
- **Funding-rate purpose differs from crypto perps**: in conventional perpetuals, funding rates exist to keep the perp price near an external oracle spot. Trendle has no tradable spot attention asset · there's nothing to arbitrage against. Instead funding rates act as a **crowding tax**: penalize the crowded side, reward the minority. "The most obvious trade becomes more expensive to hold."
- This is a meaningful design choice: it converts "betting on attention going up" into a more sophisticated game of timing the *position-crowdedness* rather than just the direction. A crowded long doesn't mean the index won't rise · it means the cost of maintaining the long compounds, and the short side earns funding as compensation for taking the uncrowded view.
- **Hedging use case** (Mike's "How Prediction Markets Turn Into Risk Instruments"): attention markets can serve as a sentiment hedge against binary prediction-market positions. If you're long YES on a discrete event, you can short attention on the same topic · if the narrative dies before resolution, the binary may still resolve YES while the attention short pays out, and vice versa.
- Attention markets are positioned as a **second layer on prediction markets**, analogous to how derivatives form a second layer on stock exchanges. Mike groups them with cross-platform hedging (Gondor lending against PM positions, DFlow tokenizing Kalshi contracts) as the next phase of PM composability.
- **Liquidity fragmentation, execution risk, and UX** are the named adoption barriers. Attention markets compete with social-media engagement loops for the same user attention they trade · a structural irony.
- An attention index will **never be perfect**. Trendle explicitly acknowledges that if the financial reward for gaming is large enough, manipulators will find ways. The bet is that the index stays trustworthy enough relative to manipulation cost, evolving with more data sources (TikTok, news media) and statistical safeguards.
- Cultural framing: attention markets convert the social-media metric culture (likes, shares, follows) into a tradable instrument. The implicit claim is that price-as-attention is a more honest measure than engagement metrics gamed for advertiser audits.
- The conceptual parent is **prediction market reflexivity**: trading on attention *is* attention. The market participants generate part of the very signal they trade. Trendle dampens this with the funding-rate crowding tax, but the loop is structural.
- Adjacent design space includes Cookie.fun's mindshare attention measurement and Mindshare N1's attention scoring · they share the index-construction problem but most have not yet wrapped it in tradable perpetual instruments.

## Notable Quotes

> "Attention is the only scarce resource in the digital age as everything else keeps expanding towards abundance."  
> — *michaellwy, *Perpetual Attention Markets**

> "When a measure becomes a target, it stops being a good measure."  
> — *Goodhart's Law (quoted by michaellwy as the central design constraint)*

> "Manipulating one platform might already be costly. Manipulating several at once, in a coordinated way that produces a consistent attention signal, is much harder. The wide range of inputs raises the cost of manipulation."  
> — *michaellwy*

> "Trendle rewards the trader who understands the difference between 'this narrative is rising' and 'this narrative is already fully priced in by positioning.'"  
> — *michaellwy*

> "Since there is no external spot price as reference, the funding rates on Trendle serve as a 'crowding tax' to penalize the crowded side and reward the minority side."  
> — *michaellwy*

## Where It Matters

Attention markets are the most concrete answer to the question "what else can prediction-market infrastructure trade?" beyond binary events and asset prices. They turn the prediction-market category into a substrate for narrative finance · making "is X going viral?" a hedgeable, tradable view rather than a vibe. For Dekant in particular, attention markets are an interesting parallel: both are continuous distributions wrapped in a market-pricing mechanism, but Dekant's distribution is over discrete outcome values while attention markets' distribution is over a normalized engagement index.

## Related Concepts

- **Info finance** · Attention markets fit cleanly inside Vitalik's info-finance framing: a market deliberately designed to price a specific informational signal (here, attention).
- **Hedging** · Attention shorts as sentiment hedge against binary YES positions on the same topic.
- **Conditional tokens** · Trendle's mechanism is a perpetual rather than a conditional-token design, but cross-margining with conditional tokens is the implied next step.
- **Cross-platform arbitrage** · Trendle's multi-source index is itself a cross-platform aggregation; manipulation must clear cross-platform consistency.
- **Reflexivity** · Trading attention generates attention; the funding rate is the explicit attempt to dampen the loop.
- **Parlays** · Multi-topic attention combinations would be a natural parlay product (attention on A AND on B).
- **Toxic flow / market manipulation** · The defining adversarial concern; the design exists in dialogue with the bot/farm problem.

## Sources

- [How Prediction Markets Turn Into Risk Instruments](https://x.com/mikhryc0x/article/2020802941091741972) — Mike · Feb 9, 2026
- [Perpetual Attention Markets](https://blog.monad.xyz/blog/perpetual-attention-markets) — michaellwy · Dec 15, 2025 ·

