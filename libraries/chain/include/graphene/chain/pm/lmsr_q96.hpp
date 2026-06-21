#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <cstdint>

// HF14 — Deterministic fixed-point LMSR (Q96). C++ consensus port of the normative
// PHP/JS reference (.qoder/plans/pm-plan/reference/lmsr_fixed.php + market_math.js) and
// lmsr-fixed-point-spec.md. MUST reproduce reference/lmsr_fixed_vectors.json bit-for-bit.
//
// No <cmath> / exp / log / pow / sqrt — pure big-integer arithmetic. The translation unit
// is compiled with -fno-fast-math -ffp-contract=off and CI greps the object for FP symbols.
//
// Rounding rules (frozen, spec §3.5):
//   mul_q / shifts  : arithmetic right shift = floor toward -inf
//   div_q / term/n  : truncation toward zero (C99 integer division)
//   exp range-reduce: round to nearest, ties to even (the single non-truncating step)

namespace graphene { namespace chain { namespace lmsr {

    using q96 = boost::multiprecision::int256_t;

    // Public precisions (unchanged from the reference API).
    constexpr int64_t LMSR_PRECISION       = 1000;     // 1 VIZ = 1000 milli-VIZ
    constexpr int64_t LMSR_PRICE_PRECISION = 1000000;  // price x10^6

    // Public API — all int64 milli-VIZ in/out. On domain violation these FC_ASSERT (fail the op).
    int64_t lmsr_cost(const std::vector<int64_t>& q, int64_t b);
    std::vector<int64_t> lmsr_prices(const std::vector<int64_t>& q, int64_t b);
    int64_t lmsr_price(const std::vector<int64_t>& q, int64_t b, int i);
    int64_t lmsr_buy_cost(const std::vector<int64_t>& q, int64_t b, int i, int64_t delta);
    int64_t lmsr_sell_return(const std::vector<int64_t>& q, int64_t b, int i, int64_t delta);
    int64_t lmsr_tokens_for_amount(const std::vector<int64_t>& q, int64_t b, int i, int64_t amount);
    int64_t lmsr_b_from_liquidity(int64_t liquidity, int n);

    // Exposed for the vector-parity test (primitives + transcendentals).
    namespace detail {
        q96 mul_q(const q96& a, const q96& b);
        q96 div_q(const q96& a, const q96& b);
        q96 exp_q(const q96& x);
        q96 ln_q(const q96& x);
    }

} } } // graphene::chain::lmsr
