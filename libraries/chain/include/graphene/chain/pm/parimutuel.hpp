#pragma once

#include <vector>
#include <cstdint>

// HF14 Prediction Markets — pure parimutuel settlement math (spec
// parimutuel-settlement.md §2). Kept free of database/chainbase types so it can
// be unit-tested in isolation; settle_market() in pm_evaluator.cpp is a thin
// wrapper that feeds chain state in and applies the result via adjust_balance.
//
// Strictly ZERO-SUM (no token emission):
//   Σ winner_payout + oracle_take + creator_take + lp_bonus + uncovered
//     == Σ winner.amount + losers_sum + forfeit_pool
// LP principal is returned separately by the caller; when winners_pool would go negative the
// shortfall is reported as `uncovered` and the caller charges it to LP principal (F1), so the
// identity above holds unconditionally rather than emitting the shortfall.

namespace graphene { namespace chain { namespace pm {

    // One winning position fed into settlement (curve weight is the claim device).
    struct winner_in {
        int64_t  amount = 0;        ///< staked principal (mVIZ)
        int64_t  weight = 0;        ///< curve weight (CPMM/LMSR tokens)
        uint32_t time_penalty = 0;  ///< 1e6-scaled late penalty applied to PROFIT only
    };

    struct settle_params {
        int64_t  losers_sum = 0;             ///< Σ amount of losing bets
        int64_t  forfeit_pool = 0;           ///< extra pot folded into winners_pool
        int64_t  oracle_fixed_fee = 0;       ///< funded from the pool, capped ≥0
        uint16_t oracle_fee_percent = 0;
        uint16_t creator_fee_percent = 0;
        uint16_t liquidity_fee_percent = 0;
    };

    struct settle_result {
        std::vector<int64_t> winner_payout;  ///< amount + profit − penalty, parallel to winners
        int64_t oracle_take = 0;             ///< oracle_fee + oracle_fixed_paid
        int64_t creator_take = 0;
        int64_t lp_bonus = 0;                ///< liq_fee + Σpenalty + rounding dust + undistributed pool
        int64_t uncovered = 0;               ///< F1: |negative winners_pool| the pot couldn't cover.
                                             ///< Flooring winners_pool at 0 returns winners their principal
                                             ///< but leaves this shortfall uncharged; the caller MUST net it
                                             ///< off LP principal (the leverage counterparty) pro-rata so the
                                             ///< zero-sum invariant above holds unconditionally. (PR #124 F1.)
    };

    /// Compute the parimutuel payout split. Winners are paid by weight out of the
    /// losers' stakes; the remainder/penalties accrue to the LP bonus.
    settle_result compute_settlement(const settle_params& p,
                                     const std::vector<winner_in>& winners);

    // One LP position for the time-weighted fee split.
    struct lp_in {
        int64_t principal = 0;       ///< staked liquidity (mVIZ)
        int64_t seconds_active = 0;  ///< settle_time − deposit_time (clamped ≥0)
    };

    /// Split `bonus` across LPs weighted by principal × time-in-market (minute
    /// granularity, +1 floor so simultaneous deposits fall back to principal-pro-rata,
    /// and the product stays within 128 bits). Returns each LP's bonus share (parallel
    /// to `lps`); the last LP absorbs the rounding remainder so Σ shares == bonus.
    std::vector<int64_t> distribute_lp(const std::vector<lp_in>& lps, int64_t bonus);

}}} // graphene::chain::pm
