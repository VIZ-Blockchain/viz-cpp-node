# Endogeneity

**Category:** Information Theory  
**Source:** [PM Atlas — Endogeneity](https://www.pmatlas.xyz/concepts/endogeneity)

---

## Definition

When the existence or visibility of a prediction market influences the very outcome it aims to forecast, undermining its reliability as a neutral signal. Particularly relevant in political and social markets where public price movements can shift the behavior of voters, candidates, or media. The "thermometer that becomes a thermostat" problem.

## Key Insights

- Single-source concept page citing one Bhattacharjee article. The framing is about *which PM categories are reliable enough to use for nowcasting* · and endogeneity is the core reason political/social markets are *lower-tier* than financialized economic indicators. The Bhattacharjee tiered framework explicitly ranks: 1. **Financialized economic indicators** (CPI, employment, Fed action) · *highest reliability*, because the underlying outcome is set by independent institutional processes and shouldn't react to PM prices. 2. **Verifiable hard outcomes** (sports, scheduled events) · moderate, low endogeneity risk. 3. **Speculative prop bets and political/social predictions** · *lowest tier*, because PM prices can leak into candidate behavior, media coverage, donor allocation, and voter perception.
- Cited evidence: Federal Reserve paper validating Kalshi data quality (specifically on financial-econ contracts, where endogeneity is minimal). The implication is that endogeneity is *category-specific*: a CPI market is a clean thermometer because the BLS doesn't read PMs; a presidential election market is a thermostat because campaigns, media, and donors do.
- Three practical use cases the article proposes, ranked by endogeneity risk: triangulating against polls (low risk · uses PMs as one of many signals), nowcasting delayed econ data (very low risk · the underlying process is exogenous), hedging event risk (depends on the event · exogenous events like CPI are fine, endogenous events like elections are dangerous).
- Bhattacharjee (FULL_READ) defines endogeneity directly: "Endogeneity is when variables that are supposed to be independent actually influence each other. It's the bane of econometricians (stats nerds who focus on economics) because it starts to break down the assumptions of our models. If the price of a prediction market (or even its existence) impacts the outcome of the prediction, it starts to lead to funny results."
- Bhattacharjee's canonical example: Brian Armstrong, CEO of Coinbase, was on an earnings call. He became aware Polymarket was running a contract on whether he would mention specific phrases. "He modified the words he was going to use. Here, the market was supposed to be predicting what he would say. Instead, his knowledge of the market's bets changed what he said." This is endogeneity at the contract level.
- Bhattacharjee on the joint failure mode: "Ultimately both insider trading and endogeneity risks prevent sophisticated participants from using these markets, which in turn reduces their usefulness. These risks are both clearest in category 3 and 4 markets [sports, prop bets, mention markets] and frankly that's why I think they will never mature into efficient, useful sources of information like categories 1 and 2 [econ data, political/macro outcomes]."

## Notable Quotes

> "Public price movements can shift behavior."  
> — *onprediction.xyz definition*

> "Particularly relevant in political and social markets where public price movements can shift behavior."  
> — *onprediction.xyz definition*

> "Financialized economic indicators rank highest, speculative prop bets lowest."  
> — *Isar Bhattacharjee*

## Where It Matters

Endogeneity is the most under-discussed structural risk in the PM bull case. Every "PMs as truth machines" narrative implicitly assumes the market price doesn't *change* the outcome it predicts · but for elections, social movements, AI safety announcements, M&A rumors, and product launches, that assumption is wrong. The Kyla Scanlon NYT op-ed (responded to by Adhi Rajaprabhakaran in the price-discovery cluster) argued exactly this for PMs broadly; Rajaprabhakaran's pushback was that PMs lack the *causal mechanism* stocks have, which makes endogeneity smaller · but not zero. For builders, the regulatory and ethical implication is: don't list contracts where the PM is itself a campaign tool (e.g., "Will X be assassinated by Y date?" creates a literal hit-market). For category strategy, lean into financialized-econ markets (low endogeneity, Fed-cited credibility) and away from political markets when accuracy claims matter.

## Related Concepts

- **Nowcasting** · endogeneity determines which categories can be nowcast reliably
- **Reflexivity** · the broader concept that PM endogeneity is one form of
- **Information aggregation** · endogeneity is what corrupts aggregation
- **Wisdom of crowds** · endogeneity is one failure mode that breaks the WoC claim
- **Market manipulation** · deliberate exploitation of endogeneity
- **Insider trading** · different but related failure mode of price signal
- **Decision markets** · by design *use* endogeneity (the market should influence the decision)
- **Election markets** · the canonical endogenous category

## Sources

- [How to Use Prediction Markets as a High Quality Info Source](https://uncover.substack.com/p/how-to-use-prediction-markets-as) — Isar Bhattacharjee · Mar 30, 2026 [FULL_READ]

