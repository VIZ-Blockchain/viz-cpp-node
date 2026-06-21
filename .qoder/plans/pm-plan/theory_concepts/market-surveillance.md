# Market surveillance

**Category:** Business & Platforms  
**Source:** [PM Atlas — Market surveillance](https://www.pmatlas.xyz/concepts/market-surveillance)

---

## Definition

**Quick definition.** Systematic monitoring of trading by exchanges/regulators to detect insider trading, manipulation, and abuse. In PMs, surveillance is complicated by pseudonymous blockchain wallets and the absence of issuer-based disclosure obligations.

## Key Insights

- Carter frames insider trading as a structural feature, not a bug. Walks through the DOJ case against Master Sergeant Gannon Ken Van Dyke (made $400k on Polymarket trading the Maduro raid) and prior Israeli reservist arrests, then quotes Mansour/Coplan/Tenev/Hanson on how insider flow is what makes prices accurate. Platforms face a calibration problem: too permissive and noise traders flee perceiving rigging; too strict and informed flow disappears and prices decay into sentiment.
- Carter predicts Polymarket fully drops pseudonymous trading and ramps surveillance over the next year as enforcement closes in.
- Mitts and Ofir (Columbia/Haifa, via Harvard CGI): document the Feb 28 2026 US-Israeli Iran strike case · six newly created Polymarket wallets earned ~$1.2M buying 'Yes' shares as low as $0.10; one account ("Magamyman") placed its first trade 71 minutes before the news broke, when the market implied only a 17% probability. Their paper proposes platform-level registration, contract-level restrictions on high-risk categories, and extended misappropriation doctrine to close legal gaps (broader screening figures appear in the underlying SSRN paper, not in our fetched digest body).
- Jay Sykes (Congressional Research Service): walks through SEC Rule 10b-5, CFTC Rule 180.1, the STOCK Act, and Title 18 criminal statutes. Core gap: existing law requires breach of a duty, but many PM insiders (e.g., a political candidate betting on his own race) may not owe one. Four pending bills in the 119th Congress would close this gap differently.
- The CFTC's February 2026 advisory references two Kalshi enforcement actions · the first explicit signal that the agency intends to apply insider-trading-like rules to event contracts.
- Sethi (Trading on Violence): documents recent pattern from $1.2M profits on the timing of US strikes on Iran to trades linked to classified intelligence. Compares Kalshi's KYC-based surveillance and Polymarket's pseudonymous blockchain as fundamentally different enforcement surfaces. Argues platforms should reconsider contract offerings before regulators force the issue.
- Kalshi has conducted 200+ insider trading investigations, with a dozen cases currently active (Sethi).
- Sethi's wash-trading paper (Sirolly, Ma, Kanoria, Sethi): uses network analysis to distinguish wash trading from legitimate MM activity on Polymarket. Wash traders exhibit homophily (trading only within their collusive clique); MMs exhibit heterophily (trading indiscriminately with diverse counterparties). The algorithm identified a 200-wallet cluster generating $113M in volume with just $57.86 in aggregate losses.
- The detection asymmetry: MMs and wash traders both rapidly open/close positions and avoid large directional exposure. Looking only at counterparties is insufficient · you have to look at the counterparties of those counterparties.
- Surveillance challenge: Kalshi's KYC produces enforceable subjects for state law but is bypassable via offshore proxies; Polymarket's pseudonymity creates surveillance-resistant trades but limits regulatory reach. The two enforcement regimes are mirror-image.
- The Polymarket Venezuela election dispute is the surveillance failure-mode case study: oracle disputes plus pseudonymous trades plus political stakes produced an outcome that none of the parties · platform, traders, regulators · could fully validate.

## Notable Quotes

> "Six newly created Polymarket wallets collectively earned approximately $1.2 million by purchasing 'Yes' shares … when markets implied only a 17% probability of a strike."  
> — *Joshua Mitts and Moran Ofir, *From Iran to Taylor Swift: Informed Trading in Prediction Markets**

> "One account, operating under the handle 'Magamyman,' placed its first trade seventy-one minutes before the news broke."  
> — *Joshua Mitts and Moran Ofir, *From Iran to Taylor Swift**

## Where It Matters

Surveillance defines the legitimacy gradient: how clean the venue looks to institutions, regulators, and ordinary users. Pseudonymity is a double-edged sword · it's what makes Polymarket compelling but also what triggers enforcement risk. For Dekant: continuous-curve markets generate richer trading signatures (a shape, not a binary leg) that make wash-trading homophily easier to detect statistically, even with pseudonymous wallets.

## Related Concepts

- Insider trading · the primary surveillance target
- Wash trading · algorithmically detectable via network homophily
- Adverse selection · informed flow is what surveillance attempts to police
- Information asymmetry · measurable through trade patterns
- Federal preemption · determines which agency does the surveilling
- Regulatory arbitrage · pseudonymous PMs are the arbitrage surveillance can't reach

## Sources

- [Prediction Markets Have An Inescapable Insider Trading Problem](https://x.com/nic_carter/status/2048123008200724599) — Nic Carter · Apr 26 2026 ·
- [From Iran to Taylor Swift: Informed Trading in Prediction Markets](https://corpgov.law.harvard.edu/2026/03/25/from-iran-to-taylor-swift-informed-trading-in-prediction-markets/) — Joshua Mitts, Moran Ofir · Mar 25 2026 ·
- [Prediction Markets and Insider Trading Law](https://www.congress.gov/crs-product/LSB11406) — Jay B. Sykes · Mar 18 2026 ·
- [Trading on Violence](https://rajivsethi.substack.com/p/trading-on-violence) — Rajiv Sethi · Mar 2 2026 ·
- [The Detection of Wash Trading](https://rajivsethi.substack.com/p/the-detection-of-wash-trading) — Rajiv Sethi · Nov 12 2025 ·

