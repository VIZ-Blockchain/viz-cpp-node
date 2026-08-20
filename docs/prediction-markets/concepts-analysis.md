---
title: Concept analysis — Onix vs the 90 prediction-market concepts
description: How the live VIZ on-chain implementation (Onix protocol, Forecaster client) maps onto the 90 prediction-market theory concepts — what is solved, inherent, not needed, or roadmap.
---

# Concept analysis — Onix vs the 90 theory concepts

> **Forecaster** is the thin client to the VIZ on-chain prediction market — the access layer that lets
> people anywhere participate by signing `pm_*` operations (see the [section overview](./)).
> **Onix** is the protocol it talks to. This page maps each of the **90 PM-Atlas prediction-market
> concepts** onto **how the live VIZ on-chain implementation addresses it**, and **whether it is needed**
> for this architecture.
>
> Grounding docs: [whitepaper](./whitepaper), [specification](./specification),
> [workflows & disputes](./workflows).

## Legend

| Mark | Meaning |
|------|---------|
| ✅ **Resolved** | Onix design directly solves or handles it |
| ⚪ **Inherent** | A property Onix exhibits/inherits by construction (no extra work) |
| ➖ **Not needed** | Architecturally unnecessary under Onix |
| 🟡 **Partial / Roadmap** | Partly addressed today; rest is on the VIZ roadmap |
| 🏛 **Client-layer** | Handled by the jurisdictional client, not the protocol |
| 🔴 **Open / Risk** | Still a live concern; not fully solved |

The single biggest structural difference from every other platform: **LP principal is structurally guaranteed (winners paid only from losers' forfeited stakes), pricing is CPMM for binary and LMSR-softmax + parimutuel settlement for multi, and there is no order book.** Most "liquidity & trading" concepts that exist to manage market-maker inventory risk simply **do not apply** because Onix has no inventory-bearing maker.

> **On-chain actualization (HF14 / live).** This mapping was first written against the whitepaper/spec. Several items then marked *roadmap* are now **implemented as consensus operations** and verified in `consensus_sim`:
> - **Batch auctions + commit-reveal betting** — `pm_commit_bet` / `pm_reveal_bet` / `pm_batch_settle`, per-market `allow_batch` / `allow_instant_bet`, median kill-switch `pm_commit_reveal_enabled`. **Binary only** (multi forces `allow_instant_bet` — no LMSR batch yet).
> - **Opt-in leverage subsystem** — `pm_leverage_open/close/convert`, lazy-pool-funded, kill-switch `pm_leverage_enabled` (default off). Directly answers **Position Collateralization (#30)**.
> - **The Lazy Pool itself** — singleton, auto-allocation, MasterChef accounting, leverage loans, graduated recall.
> - **`endogeneity_tier`** market field; on-chain **creator bans** (`pm_creator_ban_object`); richer settlement virtual ops (`pm_payout` per bettor, `pm_leverage_resolve`, `pm_market_accepted`, `pm_auto_payout`) + matching plugin API.
> - **New since the spec:** **lazy-pool stake counts as governance weight** in PM disputes *and* DAO committee-request voting (converted to vesting-shares, HF14-gated).
>
> **Deliberately NOT built:** commit-reveal *dispute* voting — committee disputes are **open public hearings by design** (votes stay public via `pm_dispute_vote`, and ballots are revisable until close). **Still roadmap:** automated exogenous data oracles. Rows and the mitigations table below are updated to this live state.

---

## 1. Information Theory

| # | Concept | Verdict | How Forecaster-on-VIZ handles it / is it needed |
|---|---------|---------|--------------------------------------------------|
| 1 | **Brier Score** | ⚪ Inherent | Not a protocol mechanism but the *metric* by which Onix markets are judged. On VIZ, every bet/resolution is a consensus-validated event, so per-market price histories and outcomes are fully on-chain → Brier scoring of the platform (and of oracles) is computable by anyone. Needed only as an analytics/reputation input, not core logic. |
| 2 | **Calibration** | ⚪ Inherent | Onix prices are genuine probabilities (CPMM `P(A)=reserve_b/(reserve_a+reserve_b)`, LMSR softmax sums to 1). Calibration is an emergent property to *measure*, improved indirectly by the time-penalty (discourages no-info last-second bets) and deep LP liquidity. Not something the protocol enforces. |
| 3 | **Credibility Markets** | 🟡 Partial | The bonded-oracle + 14-metric reputation + composite trust score is effectively a credibility market for *resolvers*. Staking credibility against an outcome is native. A general "stake reputation on claims" product is a possible client-layer build, not core. |
| 4 | **Distribution Markets** | 🟡 Roadmap | Onix Multi (3–10 discrete outcomes) approximates a distribution via bucketed outcomes. True continuous distribution markets (CDF/scalar) are **not** in scope today; would need a scalar-outcome operation. Listed-adjacent to the roadmap's "category-level AMMs." |
| 5 | **Endogeneity** | 🟡 Partial (mitigated) | The risk that the market changes the thing it predicts. Category-specific (econ-data clean, political/social risky). Mitigated by the **live `endogeneity_tier` market field** (oracle-tagged 1/2/3) and **opt-in commit-reveal/batch betting (now on-chain)** that stops the public price from "leaking" mid-window (the thermostat channel); exogenous resolution via automated data oracles is **still roadmap**. See [Mitigations §](#mitigations-for-the-reflexivity-family). |
| 6 | **Forecasting Accuracy** | ⚪ Inherent | The whole value proposition. Onix improves it indirectly: zero-risk LP → deeper books → less slippage → more informed participation → better prices. Accuracy is the output to measure, not a feature to build. |
| 7 | **Info Finance** | ⚪ Inherent | Onix *is* an info-finance instrument: consensus-level operations turn information into priced, settleable positions. VIZ migration makes the information layer censorship-resistant and composable. |
| 8 | **Information Aggregation** | ✅ Resolved | Core function. CPMM/LMSR pricing aggregates dispersed bets into a single probability; deep risk-free LP liquidity is precisely the lever Onix pulls to make aggregation work (the flywheel in §7.3 of the whitepaper). |
| 9 | **Information Asymmetry** | 🟡 Partial | Onix's parimutuel/CPMM design means informed traders extract from *other losing bettors*, not from the LP — so asymmetry doesn't bankrupt liquidity (unlike CLOB/LMSR makers). **Opt-in commit-reveal + batch betting is now live (binary):** committed bets settle at a uniform batch price, removing the mempool-direction leak; per-market `allow_batch` + median kill-switch keep it optional. |
| 10 | **Legibility** | ✅ Resolved | Every financial action is a consensus-validated VIZ operation with a `market_log` audit trail (before/after reserves). Fully legible/auditable by any node — strictly more legible than a centralized backend or opaque CLOB. New settlement virtual ops (**`pm_payout`** per bettor, **`pm_leverage_resolve`**, **`pm_market_accepted`**) + dedicated plugin API methods make per-bettor outcomes and leverage resolutions directly queryable. |
| 11 | **Longshot Bias** | 🟡 Partial | CPMM/LMSR pricing can still exhibit favorite-longshot bias from bettor behavior; Onix doesn't correct it directly. The time penalty and deep liquidity dampen distortion, but bias is a behavioral output, not eliminated. |
| 12 | **Noise Decomposition** | ⚪ Inherent | Analytical lens, not a protocol feature. On-chain price/volume series on VIZ make signal-vs-noise decomposition feasible for analysts. Not needed in core. |
| 13 | **Nowcasting** | ⚪ Inherent | Onix prices update continuously per bet (~3s VIZ blocks), giving real-time nowcast estimates. Inherent to any live AMM market; no extra mechanism. |
| 14 | **Price Discovery** | ✅ Resolved | CPMM and LMSR-softmax are continuous price-discovery engines; price coherence (`Σ price = 1`) holds *by construction* with no arbitrage/split-merge layer needed. |
| 15 | **Probability Infrastructure** | ✅ Resolved | This is essentially Onix's thesis on VIZ: prediction markets as **first-class consensus operations** (`pm_*`), not smart contracts — a base-layer probability primitive. Directly the migration goal. |
| 16 | **Superforecasting** | ⚪ Inherent | Individual-skill concept; Onix rewards accurate bettors via the losers→winners payout. Position transfers + reputation could support superforecaster identity, but it's a participant trait, not protocol logic. |
| 17 | **Wisdom of Crowds** | ✅ Resolved | The mechanism Onix monetizes. Risk-free LP lowers the barrier so more of the crowd participates, sharpening the aggregate. Core to the design rationale. |
| 18 | **Yes Bias** | 🟡 Partial | Behavioral tilt toward "Yes." Onix's symmetric CPMM and profit-only time penalty don't structurally favor Yes, but they don't correct human bias either. Mitigated by liquidity depth; a measurement concern. |

---

## 2. Mechanism Design

| # | Concept | Verdict | How Forecaster-on-VIZ handles it / is it needed |
|---|---------|---------|--------------------------------------------------|
| 19 | **Binary Contracts** | ✅ Resolved | Onix Binary = CPMM (`x·y=k`) on two outcomes. AM-GM proof guarantees `reserve_a+reserve_b ≥ L`, so LP principal is covered. This is the primary market type. |
| 20 | **Combinatorial Prediction Markets** | ➖ Not needed (today) | LMSR decomposes naturally over combinatorial spaces, but Onix Multi caps at 3–10 *independent* outcomes and deliberately omits CTF split/merge. Combinatorial/conditional bundles are explicitly out of scope; not required for the LP-guarantee model. |
| 21 | **Incentive Compatibility** | ✅ Resolved | LMSR inherits IC from the log scoring rule (truth-telling dominant). Onix adds incentive alignment via bonded oracles (insurance > manipulation profit), losers-fund-winners settlement, and time-weighted LP rewards. |
| 22 | **Keynesian Beauty Contest** | ✅ Resolved (market) / ⚪ (dispute layer — accepted by design) | KBC is a pathology of *relative/peer scoring*. The Onix **market** layer pays bettors against external ground truth (parimutuel), so it is structurally anti-KBC — you profit by *deviating* from the crowd price when it's wrong. The only KBC exposure is the **stake-weighted committee dispute vote** (a peer mechanism). **Decision: commit-reveal dispute voting will NOT be implemented** — a committee dispute is an **open public hearing**, and the DAO's credibility depends on resolving disputes as transparently as possible; hiding ballots would erode that trust. The residual KBC risk is accepted and structurally small: voters are **not paid** for matching the majority (no bandwagon bounty) and **ballots are revisable** until close (so honest updates on new evidence are expected, not suppressed). Pooled DAO members are also **enfranchised** (lazy-pool stake → vesting-shares, HF14). See [Mitigations §](#mitigations-for-the-reflexivity-family). |
| 23 | **LMSR** | ✅ Resolved (the key innovation) | The concept file notes LMSR *failed for binaries* (permanent loss on the 0/1 boundary). Onix's answer: **use CPMM for binary, and use LMSR only for multi where the maker is NOT the counterparty** — parimutuel settlement pays winners from losers, so the LMSR subsidy is never at risk (`LP max loss = 0` vs `b·ln(N)`). This is the central design move. |
| 24 | **LOX (Log-Odds Excess Lateness)** | ➖ Not needed | A specialized scoring/lateness metric. Onix instead uses a **quadratic time penalty on profit** to handle late-bet incentives — a simpler, settlement-time mechanism. LOX scoring is not part of the model. |
| 25 | **Market Manipulation** | 🟡 Partial | Bonded oracle (bond must exceed manipulation profit), DPoS-validated operations, and committee dispute arbitration raise manipulation cost. Price manipulation via large bets is bounded by depth and — on **batch/commit-reveal markets (now live)** — by uniform-price settlement that neutralises speed-based sniping (the "sniper's tax"); active surveillance is still roadmap. |
| 26 | **Market Scoring Rules** | ✅ Resolved | Onix Multi is a market scoring rule (LMSR) implementation, repurposed with parimutuel settlement. Directly used. |
| 27 | **Multi-Outcome Markets** | ✅ Resolved | Onix Multi handles N=3–10 via LMSR softmax pricing + parimutuel payout, with `b = S/ln(N)`. First-class market type. |
| 28 | **Parimutuel Markets** | ✅ Resolved (foundational) | Settlement in *both* market types is parimutuel: losers' forfeited stakes form `winners_pool`, distributed by token share. This is what makes the LP guarantee structural rather than insured. |
| 29 | **Peer Prediction** | ➖ Not needed | Truth-telling-without-ground-truth schemes. Onix relies on bonded oracles + committee dispute, not peer-prediction scoring. Could inform subjective-market resolution but not used. |
| 30 | **Position Collateralization** | ✅ Resolved (+ opt-in leverage, live) | By default every bet is fully prepaid (full amount enters reserves; no fees at bet time) — total collateralization by construction. Onix now **also** ships the concept's "next level": an **opt-in leverage subsystem** (`pm_leverage_open/close/convert`, kill-switch `pm_leverage_enabled`, default off). Margin is a **loan from the Lazy Pool** (no token emission — zero-sum preserved), so the position stays fully collateralized *from the system's view*. The binary "jump risk" that breaks CLOB liquidation engines (per this concept) is handled by **liquidating against pre-bet reserves**: opposing-bet / settlement force-close recovers `min(cancel_value, obligation) ≥ loan`, so the pool gets loan + interest back; the **only** bounded bad-debt path is a same-side `pm_cancel_bet` (Case B). The liquidation cascade is deliberately **not** gated by the kill-switch, so toggling leverage off never strips protection from open positions. |
| 31 | **Proper Scoring Rules** | ✅ Resolved | LMSR is the cost-function dual of the log proper scoring rule; Onix Multi inherits its truthful-elicitation property. |
| 32 | **Reflexivity** | 🟡 Partial (mitigated) | Parent of endogeneity. Mitigated by the same toolkit — **commit-reveal/batch betting + `endogeneity_tier` are now live**, exogenous resolution still roadmap — **plus on-chain creator bans** (`pm_creator_ban_object`) for harmful reflexivity (assassination/"hit" markets, propaganda markets that create a "constituency for the outcome"); the prohibited-category *list* itself stays client-layer. Deep risk-free LP depth also raises the cost of newsworthy price manipulation. See [Mitigations §](#mitigations-for-the-reflexivity-family). |

---

## 3. Liquidity & Trading

> **Headline:** Onix has **no order book and no inventory-bearing market maker**. A large class of these concepts exists specifically to manage CLOB/maker inventory risk and therefore **do not apply** to Onix.

| # | Concept | Verdict | How Forecaster-on-VIZ handles it / is it needed |
|---|---------|---------|--------------------------------------------------|
| 33 | **Adverse Selection** | ✅ Resolved (reframed) | The classic problem (informed flow bankrupts the maker) **cannot bankrupt the Onix LP**: winners are paid from losers, never from LP principal (AM-GM / parimutuel guarantee). Informed traders extract from other *bettors*, not the LP. Eliminates the core LP failure mode. |
| 34 | **Arbitrage** | ⚪ Inherent | Intra-market arbitrage is unnecessary: price coherence (`Σ price = 1`) holds by construction in both CPMM and LMSR-softmax. No split/merge arbitrage layer needed. |
| 35 | **Batched Auctions** | ✅ Resolved (opt-in, live) | Implemented as a **per-market uniform-price batch** (`mode=1` bets + commit-reveal → `pm_batch_settle` at each `pm_batch_epoch_blocks` epoch); only the **net residual** moves the AMM, so all same-side fills clear at one price and speed-based sniping (the "sniper's tax") is neutralised. Per-market `allow_batch` + median kill-switch `pm_commit_reveal_enabled`; **binary only** today (multi forces `allow_instant_bet`). The LP `Σreserve ≥ L` invariant is untouched — each batch is one valid CPMM transition. |
| 36 | **Bid-Ask Spread** | ➖ Not needed | No order book → no quoted spread. "Cost of trading" appears as CPMM/LMSR slippage, governed by liquidity depth, not maker spreads. Concept doesn't map. |
| 37 | **Bonding Trades** | ⚪ Inherent | Bets *are* bonded trades: capital is committed into reserves and only released at resolution (or via cancellation/transfer). Native behavior. |
| 38 | **Continuous Double Auction** | ➖ Not needed | CDA is the CLOB model Onix explicitly rejects in favor of AMM pricing. Not used. |
| 39 | **Covariance Markets** | ➖ Not needed | Trading correlation between events requires combinatorial/conditional structure Onix deliberately omits. Out of scope. |
| 40 | **Cross-Platform Arbitrage** | 🟡 Partial | Onix prices can diverge from Polymarket/Kalshi; arbitrage across platforms is possible but external to the protocol. VIZ's open API + headless client make price data accessible; no native cross-platform bridge. |
| 41 | **Execution Quality** | ✅ Resolved (reframed) | No partial fills/queue position. Execution quality = deterministic slippage + optional `min_tokens`/`min_return` slippage guards, validated at consensus. Predictable by construction. |
| 42 | **Gap Risk** | ✅ Resolved (for LP) | Gap risk (sudden jump to 0/1 wiping the maker) is the failure mode Onix's structural LP guarantee eliminates — the LP never holds the losing side's terminal risk. Bettors still bear their own outcome risk (as intended). |
| 43 | **Hedging** | 🟡 Partial | Bettors can hedge by taking offsetting positions, transferring positions (`pm_transfer_position`), bet cancellation (if allowed) via reverse CPMM, and now **opt-in leverage** (`pm_leverage_open/convert`) for capital-efficient offsetting. No native multi-leg derivatives; basic + leveraged hedging is possible. |
| 44 | **Implied Correlation** | ➖ Not needed | Requires multi-event/combinatorial markets Onix omits. Out of scope. |
| 45 | **Insider Trading** | 🟡 Partial / 🏛 Client | Protocol can't detect insider info; mitigated by time penalty (late-info bets earn less profit) and bonded-oracle resolution. KYC/surveillance to police insiders is a **client-layer** responsibility (regulated clients). |
| 46 | **Kelly Criterion** | ⚪ Inherent | A bettor staking strategy, not a protocol feature. Onix exposes clean probabilities and full collateralization so Kelly sizing is computable by participants; the new **opt-in leverage** lets a bettor act on a fractional-Kelly edge with margin (pool-funded, liquidation-bounded). No core involvement beyond exposing the primitives. |
| 47 | **Liquidity Fragmentation** | 🟡 Roadmap | Per-market pools fragment liquidity today. The whitepaper's top-priority roadmap item — **shared/category-level AMM pools** — is the architectural fix. Lazy Pool already mutualizes *deposits* across markets. |
| 48 | **Liquidity Provision** | ✅ Resolved (core differentiator) | Risk-free LP is the headline: principal structurally guaranteed, time-weighted fee rewards, Lazy Pool auto-allocation + MasterChef accounting. Solves the "LPs lose money" problem that motivates the whole protocol. |
| 49 | **Market Making** | ✅ Resolved (reframed) | No active maker needed — the AMM + LP pool *is* the maker, and it bears no inventory risk. "Market making" collapses into passive, risk-free liquidity provision. |
| 50 | **Minimum Viable Liquidity** | ✅ Resolved | Enforced floor: min initial liquidity 100 VIZ; Lazy Pool auto-seeds every new market with `free_balance × allocation_%`. MVL is structurally bootstrapped rather than left to chance. |
| 51 | **Order Book** | ➖ Not needed | Onix is AMM-based; no order book by design. |
| 52 | **Orderflow Arbitrage** | ➖ Not needed | No order book / no PFOF-style flow routing → not applicable. |
| 53 | **Relative Value Trading** | ➖ Not needed | Cross-instrument RV requires correlated/combinatorial markets Onix omits. Out of scope. |
| 54 | **Retail Flow** | ✅ Resolved (reframed) | In CLOB models retail flow subsidizes maker losses to toxic flow. In Onix there is no maker to protect — retail and informed bettors all pay into the same parimutuel pool; LP is indifferent. The "retail-vs-toxic" tension dissolves at the LP layer. |
| 55 | **Semantic Tick Size** | ➖ Not needed | Tick granularity is a CLOB concept. Onix prices are continuous AMM functions; precision is the fixed mVIZ unit (1/1000). No tick design needed. |
| 56 | **Temporal Arbitrage** | 🟡 Partial | Betting earlier vs later carries different risk; Onix's **time penalty on profit** is precisely the mechanism that prices in lateness, dampening "wait-for-certainty" temporal arbitrage. Not eliminated, but explicitly disincentivized. |
| 57 | **Time Arbitrage** | 🟡 Partial | Same family as #56 — exploiting information timing. Quadratic time penalty + ~3s block cadence reduce, but don't remove, the edge. Addressed by design intent. |
| 58 | **Toxic Flow** | ✅ Resolved (for LP) | The defining CLOB/LMSR problem (sniper clears the book at 10¢ on a 99¢ outcome, maker eats 80¢) **does not hit the Onix LP** — payouts come from losers' stakes, and the LP subsidy is returned unconditionally. Toxic flow simply means informed bettors win the parimutuel pool, as intended. Major structural win. |
| 59 | **Wash Trading** | 🟡 Partial / 🏛 Client | No fees at bet time removes one wash incentive, but volume-faking is still possible; transfers are pure reassignment (no fee farming there). Detection/surveillance is a client + roadmap surveillance concern. |

---

## 4. Oracle & Resolution

| # | Concept | Verdict | How Forecaster-on-VIZ handles it / is it needed |
|---|---------|---------|--------------------------------------------------|
| 60 | **Corruption Value Multiple (CVM)** | ✅ Resolved (by design principle) | The protocol's explicit security invariant: oracle **insurance bond must exceed potential manipulation profit**. Risk factor (insurance/bets ratio) feeds the composite trust score. CVM is directly the bonding rationale. |
| 61 | **Dispute Resolution** | ✅ Resolved | Full system: 12h grace, `dispute_fee`, mandatory oracle response, per-market resolver (`dispute_mode==0` committee stake-weighted vote / `==1` named resolver), insurance slashing, 3-outcome no-contest disputes, 14-day auto-close anti-freeze, on-chain creator/oracle bans. Among the most fully specified parts. **HF14 addition:** lazy-pool depositors keep their dispute vote weight (pool NAV → vesting-shares, added to `effective_vesting_shares`). |
| 62 | **Oracle Design** | ✅ Resolved | Bonded oracle model: registration fee, ≥5000 VIZ insurance, explicit acceptance, evidence-backed resolution, 14-metric reputation, freshness decay, ban mechanics. Core subsystem. |
| 63 | **Resolution Criteria** | 🟡 Partial / 🏛 Client | Market question/criteria live in `url`/description (custom_json, display-only). Protocol enforces *process* (who resolves, disputes) but not criterion *quality* — ambiguous criteria are a creator/client responsibility, policed retroactively via disputes + **on-chain creator bans** (`pm_creator_ban_object`, now live). |
| 64 | **Self-Resolving Markets** | ➖ Not needed (today) | Onix resolution is oracle-driven, not algorithmic self-resolution. Automated data oracles (Chainlink-style feeds) are a high-priority roadmap item for objective markets, which would approximate self-resolution. |
| 65 | **UMA Protocol** | ➖ Not needed (replaced) | UMA's optimistic oracle (used by Polymarket) is functionally replaced by Onix's bonded oracle + VIZ committee dispute model. Same problem, native VIZ solution — no external oracle dependency. |

---

## 5. Governance & Decisions

| # | Concept | Verdict | How Forecaster-on-VIZ handles it / is it needed |
|---|---------|---------|--------------------------------------------------|
| 66 | **Attention Markets** | ➖ Not needed | Trading attention/virality is a distinct product; Onix is event-resolution focused. Could be a client-layer market category, not core. |
| 67 | **Conditional Tokens** | ➖ Not needed (explicit) | Whitepaper §7.2 argues CTF split/merge is **architecturally unnecessary** — price coherence is mathematical, not enforced by tokens. The *one* useful CTF feature (transferable positions) is reimplemented natively as `pm_transfer_position` with encrypted memos. Deliberately omitted. |
| 68 | **Decision Markets** | 🟡 Possible | Onix Multi could express decision markets, but conditional "if-policy-then-metric" structure isn't native (no conditional tokens). Buildable at client layer; not a core primitive. |
| 69 | **Futarchy** | 🟡 Possible (client) | Concept file: futarchy = decision markets on conditional futures. Onix lacks native conditional markets, so full futarchy isn't supported in core. VIZ's stake-weighted committee already governs *parameters*; governance-by-market would be a client/roadmap construction. |
| 70 | **Hyperstition Markets** | ➖ Not needed (deliberate) | Reflexivity-as-a-feature (coordinate, don't forecast). This is a *design choice, not a bug to fix*: Onix's requirement that an outcome be **externally verifiable by a bonded oracle** structurally excludes hyperstition markets by default. Could exist as a separate client-layer "coordination market" product with milestone resolution, but is not a core target. See [Mitigations §](#mitigations-for-the-reflexivity-family). |
| 71 | **Impact Markets** | ➖ Not needed | Retrospective-funding/impact-certificate markets are a separate domain. Possible client-layer category; not core. |
| 72 | **No-Loss Prediction Markets** | 🟡 Adjacent | Onix isn't no-loss for *bettors* (losers forfeit stakes — that funds winners). But it **is** "no-loss" for *LPs* (principal guaranteed). The yield-funded no-loss variant (stake yield, principal returned) is a different model; LP-side no-loss is already delivered. |
| 73 | **Opportunity Markets** | ⚪ Inherent (adjacent) | The Lazy Pool's **opportunity-cost protection** (graduated recall, active-market penalty, fault stamps) addresses capital-opportunity-cost directly — though "opportunity markets" as a product category is out of scope. |

---

## 6. Business & Platforms

| # | Concept | Verdict | How Forecaster-on-VIZ handles it / is it needed |
|---|---------|---------|--------------------------------------------------|
| 74 | **AI agents** | 🟡 Roadmap | Headless client + open VIZ operations make programmatic agents (bettors, LPs, automated oracles) straightforward. AI-driven liquidity/resolution is a natural extension, not yet specified. |
| 75 | **Cross-subsidization** | ⚪ Inherent | The Lazy Pool cross-subsidizes liquidity across many markets from one deposit; MasterChef `reward_per_share` shares fee yield. Cross-subsidization is built into the pool economics. |
| 76 | **Demand markets** | ➖ Not needed | Markets that gauge/aggregate demand are a product category; not a core Onix primitive. Client-layer. |
| 77 | **Distribution moat** | 🟡 Strategy | Onix's moat is risk-free LP yield + VIZ-native infrastructure (the flywheel). Distribution (Telegram WebApp today → headless web client) is a go-to-market concern, partly addressed by platform-independence post-migration. |
| 78 | **Election markets** | 🏛 Client | Supported as ordinary binary/multi markets; their *legality* is a jurisdictional-client matter (whitelisted oracles, category filters). Protocol-neutral. |
| 79 | **Event contracts** | ⚪ Inherent | Every Onix market *is* an event contract. The regulatory classification of these contracts is a client/legal question, not protocol logic. |
| 80 | **Federal preemption** | 🏛 Client (N/A to protocol) | Concept file: turns on whether US event contracts are "swaps." VIZ DLT is **infrastructure, not an operator** (whitepaper §6.2) — like Bitcoin is a ledger. Legal obligations attach to clients, not consensus. Not a protocol concern. |
| 81 | **Long-tail markets** | ✅ Resolved | The exact niche LMSR's bounded-loss enables — and Onix makes it *risk-free* to seed via Lazy Pool auto-allocation + min-liquidity floor. Long-tail viability is a core selling point. |
| 82 | **Market structure** | ✅ Resolved (defined) | Onix defines a clear structure: AMM pricing, parimutuel settlement, bonded oracles, DPoS-governed parameters, consensus-level ops. A coherent, novel market structure vs CLOB platforms. |
| 83 | **Market surveillance** | 🟡 Roadmap / 🏛 Client | Full on-chain audit trail (`market_log`, every op consensus-validated) makes surveillance *possible* by anyone. Active surveillance/enforcement is a client + roadmap concern. |
| 84 | **Network effects** | 🟡 Strategy | The flywheel (risk-free LP → depth → bettors → fees → more LP) is the intended network effect. Shared liquidity pools (roadmap) strengthen it. Go-to-market, not protocol mechanics. |
| 85 | **Parlays** | ➖ Not needed | Multi-leg combined bets need conditional/combinatorial structure Onix omits. Out of scope (could be a client construction over independent markets). |
| 86 | **Platform competition** | 🟡 Strategy | Competes on the unique "passive yield without impermanent loss" angle vs Polymarket/Kalshi (whitepaper §7.1 comparison table). Strategic positioning, not protocol logic. |
| 87 | **Polymarket** | ⚪ Reference | The primary benchmark. Onix differs on every axis: CPMM/LMSR vs CLOB, zero LP risk vs inventory risk, native ops vs Polygon contracts, bonded oracle vs UMA, no CTF. Used as comparison, not adopted. |
| 88 | **Regulatory arbitrage** | 🏛 Client | Jurisdictional-client model means each region builds its compliant (or permissionless) client on neutral VIZ rails. Regulatory positioning lives entirely at the client layer. |
| 89 | **Regulatory classification** | 🏛 Client | Whether markets are swaps/gaming/securities is decided per-jurisdiction at the client layer; the protocol is classification-neutral (same `pm_*` ops for permissionless and regulated clients). Not a protocol concern. |

---

## Summary — what the Onix-on-VIZ design actually changes

**Solved structurally (the core wins):**
- LP-side **adverse selection, toxic flow, gap risk, impermanent loss, market-maker inventory risk** → all eliminated because winners are paid only from losers' forfeited stakes and LP principal is returned unconditionally (CPMM AM-GM proof; LMSR parimutuel settlement).
- **LMSR's binary-market failure** → sidestepped by using CPMM for binary and confining LMSR to multi-outcome markets where the maker is not the counterparty.
- **Liquidity provision, minimum viable liquidity, long-tail viability** → risk-free LP + Lazy Pool auto-allocation.
- **Oracle design, dispute resolution, CVM** → bonded oracle + 14-metric reputation + stake-weighted committee disputes.
- **Price discovery, arbitrage, conditional tokens** → price coherence is mathematical (`Σ price = 1`), so no order book, no split/merge, no internal arbitrage layer needed.

**Not needed / deliberately omitted:** order book, CDA, bid-ask spread, semantic tick size, orderflow arbitrage, CTF split/merge, combinatorial/covariance/correlation/relative-value/parlay markets, UMA, peer prediction, LOX.

**Pushed to the jurisdictional client layer:** federal preemption, regulatory classification/arbitrage, election-market legality, KYC/insider-trading enforcement, surveillance.

**Newly implemented since the spec-era mapping (now live on-chain, HF14):** opt-in batch auctions + commit-reveal betting (binary), the opt-in leverage subsystem (position collateralization), the Lazy Pool, `endogeneity_tier`, on-chain creator bans, per-bettor/leverage settlement vops + plugin API, and lazy-pool governance weight in PM disputes + DAO committee-request voting.

**On the VIZ roadmap (partial today):** shared/category liquidity pools (fixes fragmentation), automated data oracles (→ self-resolving objective markets), distribution markets, AI agents. *(Note: commit-reveal **dispute** voting is **not** on this list — it is deliberately rejected; dispute hearings stay public. Commit-reveal **betting** is already live.)*

**Still open / behavioral (mitigated, not eliminated):** longshot/yes bias, market manipulation via depth, cross-platform arbitrage. The reflexivity family (endogeneity, reflexivity, KBC, hyperstition) has a concrete mitigation plan — see below.

---

## Mitigations for the reflexivity family

Endogeneity, reflexivity, the Keynesian beauty contest (KBC), and hyperstition are **one root problem at different layers**: the market/price influences the outcome it measures. One small set of primitives addresses all four.

### Root-cause map

| Layer | Concept | Channel |
|-------|---------|---------|
| Forecaster-level | **Keynesian Beauty Contest** | herding to visible consensus in *relative/peer scoring* |
| Market-level | **Endogeneity** | market *existence/visibility* changes behavior (category-specific) |
| Market-level | **Reflexivity** | general price↔reality feedback; manipulation-as-propaganda |
| By-design | **Hyperstition** | reflexivity used *intentionally* to coordinate an outcome |

### Mitigation primitives

| Primitive | Status | Fixes | Notes |
|-----------|--------|-------|-------|
| **Commit-reveal dispute voting** | **rejected (will NOT be built)** | KBC | A committee dispute is an **open public hearing**: `pm_dispute_vote` is a public ballot and **stays that way by design** — DAO credibility depends on transparent adjudication. Ballots are **revisable** until close (re-vote overwrites) so voters update honestly on new evidence; KBC residual is accepted (voters aren't paid for matching the majority). |
| **Commit-reveal betting (batched)** | **live (opt-in, binary)** | endogeneity, reflexivity, info-asymmetry | `pm_commit_bet`/`pm_reveal_bet`/`pm_batch_settle` hide in-flight order direction/size so the public price doesn't "leak" during the betting window (kills the thermostat channel). Settled as a uniform-price **batch** (see below). |
| **`endogeneity_tier` market field** | **live field** | endogeneity | Oracle tags tier 1 (econ data — clean), 2 (sports/scheduled), 3 (political/social — risky); UI surfaces the reflexive-risk level; clients can restrict tier-3. |
| **Exogenous resolution (automated data oracles)** | roadmap (high) | endogeneity, reflexivity | Resolution bound to an external feed (BLS/Fed/sports API) the market can't influence → clean thermometer. |
| **Prohibited-category list + creator ban** | **creator ban live on-chain**; list client-layer | harmful reflexivity, hyperstition | Block markets where YES creates a "constituency for the outcome" (assassination/"hit"/terror markets, propaganda markets). Enforced via on-chain creator ban (`pm_creator_ban_object`) + client category filter. |
| **Deep risk-free LP liquidity** | core today | manipulation-driven reflexivity | The flywheel makes the book deep, so moving price for a "newsworthy" manipulated headline is expensive. |

### KBC: why the market layer is already safe

KBC is a pathology of **relative scoring** (pay for closeness to peers → herd to peers). Onix's market layer pays bettors against **external ground truth via parimutuel settlement** — you are rewarded for *deviating* from a wrong crowd price, not for matching it. So the bettor layer is structurally anti-KBC. The only relative/peer mechanism in the protocol is the **stake-weighted committee dispute vote**, and the residual KBC risk there is **accepted by design** — the dispute is kept an open public hearing (no commit-reveal) because DAO credibility depends on transparent adjudication; what bounds the risk instead is that voters aren't paid for matching the majority and ballots stay revisable as evidence comes in.

### Commit-reveal vs. the CPMM `a·b=k` invariant

CPMM is **path-dependent** (tokens depend on reserves at execution time), so commit-reveal *cannot* be done bet-by-bet against the live curve — reveal ordering would re-introduce MEV and leak price. The fix (and why §8.3 pairs commit-reveal with the batch-auction model): **stop updating the curve per-bet; update it once per epoch via a uniform-price batch settlement.**

1. **Commit:** submit `hash(side, amount, salt, min_tokens)` and escrow `amount`.
2. **Reveal:** reveal `(side, amount, salt)`; reveals are collected but **not applied** until epoch close (seeing others' reveals is useless — yours is already committed).
3. **Settle once:** opposing flow (`A_in` vs `B_in`) nets between bettors at a single clearing price `p*`; only the **net residual** moves the AMM, so `k` is recomputed **once**. All A-fills get `p*`, all B-fills get `1−p*` → no intra-batch ordering advantage.
4. **`min_tokens` floor:** since price is invisible at commit time, a per-bet token floor is mandatory; if `tokens < min_tokens` at settlement, the bet is rejected and refunded from escrow.
5. **Anti-griefing:** non-reveal forfeits a penalty from escrow to the LP-fee/DAO pool, killing the "commit optionality, reveal only winners" attack.

**The LP guarantee is untouched:** each batch settlement is still a valid CPMM transition, so AM-GM `reserve_a + reserve_b ≥ L` still holds. Only the *granularity* of curve updates changes (per-bet → per-epoch). Onix Multi is analogous via the aggregated LMSR cost function.

**Phasing:** (1) uniform-price batch auctions first — already kill ordering MEV/front-running cheaply; (2) commit-reveal hiding on top — adds in-flight confidentiality, enabled selectively for tier-3 (endogeneity-sensitive) markets. **Both (1) and (2) are now implemented on-chain for binary markets** (multi still forces instant betting — no LMSR batch yet).

### Net verdict change

| Concept | Before | After |
|---------|--------|-------|
| Keynesian Beauty Contest | 🔴 Open | ✅ market / ⚪ dispute — votes public **by design** (no commit-reveal; revisable ballots, unpaid voters, pooled voters enfranchised) |
| Endogeneity | 🔴 Open | 🟡 mitigated — `endogeneity_tier` + commit-reveal/batch **live**; exogenous oracles roadmap |
| Reflexivity | 🔴 Open | 🟡 mitigated — creator ban **live on-chain**; category list client-layer |
| Hyperstition | 🔴 Open | ➖ excluded by design (optional client product) |
