---
title: "Hidden bets — commit-reveal and batches"
description: "A hidden bet: first the commit (escrow without revealing the outcome/amount), then the reveal inside a window. Privacy until the reveal protects you from copying and sniping. Fail to reveal and part of the escrow is forfeited into the forfeit_pool. Requires a market with allow_batch."
---

# Hidden bets: commit-reveal and batches

Sometimes it matters that your bet is **not visible in advance** — so that it cannot be copied or played against. That is what the two-phase hidden bet is for: first you "seal" it (commit), then you reveal it (reveal). Let's go through why this exists and what happens if you do not reveal.

## The gist in two paragraphs

An ordinary bet is visible on chain immediately — outcome, amount, time. A hidden bet conceals that until the reveal: you put up **escrow** and commit the bet (`pm_commit_bet`) without showing which outcome and how much. Later, inside the **reveal window**, you reveal it (`pm_reveal_bet`) and it enters the pool like any other bet. Until the reveal nobody knows your position — that is protection against copying, front-running and sniping a large bet.

Privacy comes with responsibility: if you **fail to reveal** the bet inside the window, a **forfeit** kicks in — part of the escrow (the penalty) goes into the market's `forfeit_pool` (to the winners), the rest is refunded. Hidden bets only work on markets where batch mode is enabled (`allow_batch`) and require a minimum escrow (`pm_min_batch_bet`).

## How it works step by step

**Commit.** `pm_commit_bet`: you put up escrow (≥ `pm_min_batch_bet`, on the order of 1 VIZ) and commit the bet in sealed form. The chain shows that you staked something, but not the outcome or the amount. The escrow is debited.

**The reveal window.** You get a limited window (the epoch + `pm_reveal_window_blocks`, on the order of minutes). You must reveal within it.

**Reveal.** `pm_reveal_bet`: you disclose the outcome and the amount, the bet enters the pool at the current price — from there on it is an ordinary bet. To reveal, the client needs the `commit_id` of your commit (`get_account_commits`).

**No reveal → forfeit.** Miss the window and the forfeit is automatic: the penalty (a fraction of the escrow, set by `no_reveal_fee_percent`) goes into the market's `forfeit_pool`, the remainder is refunded. That is the price for taking a slot and not completing the bet.

## Why this exists

- **Privacy of intent.** A large player does not want their bet copied or played against before it enters the pool.
- **Anti-sniping.** The hidden phase makes it harder to peek at and front-run other people's bets.
- **Batches.** Commits are collected and revealed in batches — a mode for a fairer and more private round of betting.

## What you need to understand

- **Two phases, two actions.** Commit and reveal are different operations, with a window in between. Forget to reveal and you lose the penalty.
- **Revealing is mandatory.** The forfeit is not a bug but an incentive to finish what you started; the penalty goes to the winners through the forfeit_pool.
- **Only on allow_batch markets.** Not every market supports hidden bets.
- **You need the commit_id.** To reveal, the client fetches your open commits (`get_account_commits`) — without it there is nothing to reveal.
- **Kill switch.** The whole of commit-reveal is a subsystem with a median-voted toggle (`pm_commit_reveal_enabled`); validators can disable it without a hard fork.

## Related

- [Bettor](./bettor) — the ordinary (visible) bet, for comparison.
- [Why a pool and not odds](./why-pool-not-odds) — where a revealed bet lands.
- [Specification](../specification) — reveal windows, `no_reveal_fee_percent`, batch mechanics.
