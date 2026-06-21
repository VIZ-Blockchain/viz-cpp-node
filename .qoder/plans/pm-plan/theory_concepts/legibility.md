# Legibility

**Category:** Information Theory  
**Source:** [PM Atlas — Legibility](https://www.pmatlas.xyz/concepts/legibility)

---

## Definition

The degree to which market prices reveal specific underlying information. Prediction markets have *high* legibility because contracts reference explicit events, making the information behind a price move visible to all participants · unlike stocks where a price spike could mean anything (earnings, M&A, macro, flow). The clean basis-to-truth that makes PM prices uniquely interpretable.

## Key Insights

- Single source · Andrew Courtney's "Legibility" Substack essay (Apr 27, 2026).
- Core argument: prediction markets make information legible in a way stocks and options do not. When someone bets big on an attack on Maduro, *everyone immediately knows what the bet is about* · there's no ambiguity, no need to construct a thesis. The contract specification is the interpretation.
- Contrast with equity markets: a stock spike could mean earnings beat, takeover rumor, macro news, index rebalancing, factor flow, or pure noise. The signal in equity prices is *high-bandwidth but uninterpretable* without significant context. PM prices are *narrower bandwidth but instantly interpretable* because the contract names the event being priced.
- Legibility is a *feature*, even when it surfaces uncomfortable national-security implications. The Maduro-attack example matters because it forces a societal question: do we want a public, real-time market in geopolitical risk events? Courtney argues yes · the legibility is what makes PMs an honest information source rather than another opaque derivative.
- Implicit consequence: legibility *amplifies* the insider-trading problem (because insider trades are easier to detect and embarrass), but also makes platforms harder to manipulate covertly (because every price move has a public interpretation). Compare to the Mitts/Ofir Harvard paper on Iran/Taylor Swift trading · that paper exists because PM legibility surfaced the asymmetric trades.
- Courtney (FULL_READ) explicit hierarchy: **Stock prices = low legibility** ("more buyers than sellers" is the classic Wall Street non-explanation). **Options = medium legibility** (timing and probability distribution but requires pricing expertise to read). **Prediction markets = high legibility** (every wallet's bet has a single public meaning).
- Courtney's basis-risk framing: prediction markets collapse the basis risk between information and the contract. "A corn farmer wants to hedge their specific crop, the CME corn futures contract is deliverable based on No. 2 yellow corn... there's almost always some basis risk." A hurricane parametric insurance pays on wind speed in Miami but the owner cares about *their roof*. A prediction market on "US attacks Iran" collapses the gap between the bettor's information and the contract being traded. "The specific contract collapses the basis between your information and what you're betting on."
- Courtney's regulatory implication: because the cost of insider trading on PMs can be much higher than counterparty losses (e.g., leaking US troop movements), "pseudonymous wallet-based market structures are a poor fit for the most sensitive contracts. You want KYC, market surveillance, and audit trails. Ideally, you want to identify likely insiders up front and prevent them from trading."
- Courtney's "tail wags the dog" attack vector · a novel manipulation pattern enabled by legibility: an adversary first builds a position in oil futures, then buys a huge amount of a thinly-traded geopolitical PM contract. Other traders see the PM signal and incorporate it into oil pricing. Oil spikes. The adversary sells oil futures, having "incinerated a few hundred thousand dollars to manipulate the prediction market, and made millions in the futures market. This is harder to catch, the 'insider' looks dumb on the prediction market, and the trading is much less legible on the more liquid exchange."
- Courtney on war markets: "While war markets are currently not allowed in the US under the CFTC, legibility does not respect jurisdictional boundaries. If the market exists offshore, the information it reveals is still visible everywhere. If the information is anywhere, it's everywhere." Also: "all markets are death and war markets to some extent" · oil traders already implicitly bet on war outcomes; the only difference with explicit war markets is legibility, not the existence of the trade.

## Notable Quotes

> "When someone bets big on an attack on Maduro, everyone immediately knows what the bet is about, unlike a stock price spike that could mean anything."  
> — *Andrew Courtney*

> "Andrew Courtney argues this legibility is a feature even when it surfaces uncomfortable national security implications."  
> — *onprediction summary*

> "Prediction markets have high legibility because contracts reference explicit events, making the information behind a price move visible to all participants unlike stocks where price moves have ambiguous causes."  
> — *onprediction.xyz definition*

## Where It Matters

Legibility is the structural property that determines whether PMs become *information infrastructure* or remain a niche speculative product. Three implications: 1. **Regulatory:** legibility makes insider trading more *visible*, which is partly why PM insider trading is now a real political issue (Mitts/Ofir, Sethi) while equity insider trading is largely background noise. Platforms have to decide whether the regulatory cost of legible information is worth the credibility benefit. 2. **Product:** legibility is what makes PM prices *embeddable* in other products · a Bloomberg terminal could show "Polymarket: 42% chance of recession by Q4" without needing a 200-word footnote, because the contract is self-explanatory. Equity prices don't ship with that property. 3. **For Dekant:** distribution-market contracts inherit legibility (the curve shape is a public, interpretable belief over a named quantity) and *increase* the resolution · a curve carries more legible information than a binary "YES/NO at X%". The risk: a more complex contract surface could be *less* legible if users can't read the curve. UX is critical.

## Related Concepts

- **Information asymmetry** · legibility surfaces asymmetry (insider trades are easier to spot)
- **Price discovery** · legibility makes discovery more interpretable to outsiders
- **Insider trading** · legibility is why PM insider trades become public news
- **Market surveillance** · legibility makes surveillance more tractable
- **Information aggregation** · legibility is what makes the aggregated signal usable
- **Probability infrastructure** · legibility is the property that makes embedding viable
- **Nowcasting** · legibility is why PM prices can substitute for econ indicators

## Sources

- [Legibility](https://whirligigbear.substack.com/p/legibility) — Andrew Courtney · Apr 27, 2026 [FULL_READ]

