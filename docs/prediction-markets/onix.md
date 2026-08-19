---
title: Onix — AMM-Priced Parimutuel Prediction Markets
description: Onix puts continuous AMM price discovery on top of parimutuel settlement, so prices move like an AMM while liquidity carries the risk profile of a tote — the market maker can never be bankrupted.
---

# Onix — AMM-Priced Parimutuel Prediction Markets

> Continuous **AMM price discovery** on top of **parimutuel settlement** — the price moves like an
> AMM, while liquidity carries the risk profile of a tote: **the market maker can never be
> bankrupted.**

::: info Onix & Forecaster
**Onix** is the on-chain protocol. **Forecaster** is the thin client to it on VIZ Ledger — the headless,
platform-independent access layer that lets people anywhere in the world participate in the on-chain
prediction market by signing `pm_*` operations directly against public VIZ nodes. See the
[section overview & map](./) for the full documentation tree.
:::

## The one idea

Onix **decouples price from payout**:

- **Price (discovery)** — a CPMM curve (binary) or LMSR-softmax (multi) updates a live probability on
  every bet and assigns each bet a **weight** (its claim ticket).
- **Payout (settlement)** — winners are paid **only** from the losers' forfeited stakes, split by
  weight: pure **parimutuel**, strictly zero-sum (the protocol never mints a token).

Everything distinctive about Onix follows from this split.

## Why it matters — three things

::: tip 1 · Liquidity that cannot be drained
Because winners are paid from losers and never from LP principal, the liquidity provider **cannot be
bankrupted** — no impermanent loss, no inventory risk, no death-by-sniper. Guaranteed *by construction*
(AM–GM for CPMM, conservation for LMSR), not by insurance.
:::

::: tip 2 · Passive yield without IL — the Lazy Pool
One deposit auto-spreads as silent liquidity across many markets and funds opt-in leverage, with
MasterChef-style reward accounting. Earn prediction-market liquidity yield **without** picking markets
or bearing impermanent loss.
:::

::: tip 3 · Native to the chain, zero-sum
Markets are first-class consensus operations (`pm_*`), not smart contracts: censorship-resistant,
composable, ~3-second blocks, no oracle bridge. The protocol never emits tokens — it only redistributes.
:::

## How a bet works

1. **You bet** `X` on an outcome. `X` enters the curve; the curve returns your **weight** — more weight
   if you bet earlier, before the price moves.
2. **The board updates.** The live coefficient for a side is
   `1 + opposing_pool × (1 − commission) / own_pool`, with the commission (oracle + creator + LP) already
   baked in.
3. **At resolution**, the losers' stakes (minus commission) are split among the winners by weight. Your
   payout = your stake back **+** your share of the losing pool. LP principal is returned untouched.

## How it compares

| | CLOB / AMM (Polymarket, Kalshi) | Plain parimutuel (tote) | **Onix** |
|---|---|---|---|
| Live price | yes | no (pool ratio only) | **yes (CPMM / LMSR)** |
| Odds locked at bet time | yes | no | no (honest parimutuel) |
| LP / maker can be bankrupted | **yes** (IL, snipers, gap risk) | n/a | **no (structural)** |
| Yield-bearing liquidity layer | fragile | none | **Lazy Pool, no IL** |
| Lives in | contracts / backend | backend | **consensus (`pm_*`)** |
| Token emission | sometimes | no | **no (zero-sum)** |

## The honest tradeoff

::: warning Odds are parimutuel — they drift until close
Onix does **not** lock your coefficient at bet time. The board moves as money flows, and the final
coefficient is known only at close — exactly like a tote. This is not a flaw to patch: the *only* way to
lock odds is to have a counterparty bear the risk (a bookmaker, or an AMM LP that can lose). Onix's drift
is the direct price of its LP guarantee — risk lives **between bettors**, so no one's liquidity can burn.
:::

## What's novel

- **AMM weighting + parimutuel settlement** in one integrated engine — continuous price discovery
  *without* maker inventory risk.
- **Structural, provable LP safety** instead of insured or subsidized liquidity.
- **A mutualized, yield-bearing liquidity layer** (the Lazy Pool) that also funds opt-in leverage —
  liquidations run against pre-bet reserves so the pool is always made whole.
- **Opt-in anti-MEV** (batch / commit-reveal betting) and **transparent governance** (bonded oracles,
  public-hearing disputes with revisable votes) — all layered on the safe base without ever touching the
  LP guarantee.

## Learn more

- Protocol operations — [Prediction Markets](../protocol/operations/prediction-markets)
- Plugin API — [Prediction Market API](../plugins/prediction-market-api)
