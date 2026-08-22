#pragma once

#include <cstdint>
#include <fc/uint128_t.hpp>

// HF14 Prediction Markets — pure leverage (margin) math for binary CPMM markets,
// frozen against leverage-risk-off-strategy.md §4. Deterministic integer math only;
// `k = reserve_a × reserve_b` is 128-bit. Free of database types so it is unit-tested
// in isolation; the evaluators / atomic-liquidation hook feed live reserves in.
//
// Reserve convention matches the chain's pm_place_bet (NOT the PHP prototype):
//   outcome 0 (A): bet adds to reserve_a; tokens = reserve_b − k/(reserve_a+amount)
//   outcome 1 (B): bet adds to reserve_b; tokens = reserve_a − k/(reserve_b+amount)
//
// Leverage ships behind the `pm_leverage_enabled` kill-switch and is CPMM-binary only
// in HF14 (LMSR leverage is future work).

namespace graphene { namespace chain { namespace pm { namespace leverage {

    struct cpmm_fill {
        int64_t tokens = 0;        ///< weight received
        int64_t new_reserve_a = 0;
        int64_t new_reserve_b = 0;
    };

    // §4.1 — place a bet of `amount` on `outcome`; returns tokens + post-bet reserves.
    cpmm_fill cpmm_buy(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                       int64_t amount, int outcome);

    // §4.2 — exit price: VIZ returned by selling `tokens` of `outcome` back to the curve
    // at current reserves (floored at 0).
    int64_t cancel_value(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                         int64_t tokens, int outcome);

    // §4.3 — cancel value of `tokens` on `outcome` AFTER an opposing bet of `m` on the
    // other side moves the price against the position (floored at 0). Monotonically
    // decreasing in `m`; the worst case uses m = worst_opposing_bet().
    int64_t cancel_value_after_opposing(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                                        int64_t tokens, int outcome, int64_t m);

    // §4.3 — worst-case opposing bet size: min(reserve_a,reserve_b) × sl% /100 × m_factor% /100.
    int64_t worst_opposing_bet(int64_t reserve_a, int64_t reserve_b,
                               uint16_t sl_percent, uint16_t m_factor_percent);

    // §4.4 — pool obligation / liquidation threshold = loan × (1 + r_percent/100).
    int64_t liquidation_threshold(int64_t loan, uint16_t r_percent);

    // §4.6 Constraint 2 (API preview) — max loan L (50-iter binary search over [0, hi])
    // such that, after placing (collateral+L) on `outcome`, the worst-case cancel value
    // ≥ liquidation_threshold(L) × (1 + s_percent/100). Reserves are the PRE-bet market
    // reserves. Returns the loan (0 if none qualifies).
    int64_t max_leverage_loan(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                              int64_t collateral, int outcome, int64_t hi_loan,
                              uint16_t r_percent, uint16_t s_percent,
                              uint16_t sl_percent, uint16_t m_factor_percent);

}}}} // graphene::chain::pm::leverage
