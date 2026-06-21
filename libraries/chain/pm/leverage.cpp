#include <graphene/chain/pm/leverage.hpp>

namespace graphene { namespace chain { namespace pm { namespace leverage {

    namespace {
        // k / x as int64 (k is 128-bit, x>0 → result fits for reachable reserves).
        inline int64_t kdiv(fc::uint128_t k, int64_t x) {
            if (x <= 0) return 0;
            return (int64_t)(k / fc::uint128_t((uint64_t)x)).lo;
        }
        inline int64_t mul_pct(int64_t v, uint16_t pct) { // floor(v × (100+pct)/100)
            return (int64_t)(fc::uint128_t((uint64_t)v) * fc::uint128_t(100u + pct) / fc::uint128_t(100u)).lo;
        }
    }

    cpmm_fill cpmm_buy(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                       int64_t amount, int outcome) {
        cpmm_fill r;
        if (outcome == 0) {
            r.new_reserve_a = reserve_a + amount;
            r.new_reserve_b = kdiv(k, r.new_reserve_a);
            r.tokens        = reserve_b - r.new_reserve_b;
        } else {
            r.new_reserve_b = reserve_b + amount;
            r.new_reserve_a = kdiv(k, r.new_reserve_b);
            r.tokens        = reserve_a - r.new_reserve_a;
        }
        return r;
    }

    int64_t cancel_value(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                         int64_t tokens, int outcome) {
        int64_t cv;
        if (outcome == 0) {            // A-tokens sell back into reserve_b
            int64_t new_rb = reserve_b + tokens;
            cv = reserve_a - kdiv(k, new_rb);
        } else {                       // B-tokens sell back into reserve_a
            int64_t new_ra = reserve_a + tokens;
            cv = reserve_b - kdiv(k, new_ra);
        }
        return cv > 0 ? cv : 0;
    }

    int64_t cancel_value_after_opposing(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                                        int64_t tokens, int outcome, int64_t m) {
        int64_t cv;
        if (outcome == 0) {            // position on A, opposing bet on B
            int64_t ra_m = kdiv(k, reserve_b + m);          // reserves after opposing bet
            int64_t ra_mx = kdiv(k, (reserve_b + m) + tokens); // after also selling X A-tokens
            cv = ra_m - ra_mx;
        } else {                       // position on B, opposing bet on A
            int64_t rb_m = kdiv(k, reserve_a + m);
            int64_t rb_mx = kdiv(k, (reserve_a + m) + tokens);
            cv = rb_m - rb_mx;
        }
        return cv > 0 ? cv : 0;
    }

    int64_t worst_opposing_bet(int64_t reserve_a, int64_t reserve_b,
                               uint16_t sl_percent, uint16_t m_factor_percent) {
        int64_t minr = reserve_a < reserve_b ? reserve_a : reserve_b;
        if (minr <= 0) return 0;
        // min(ra,rb) × sl% /100 × m_factor% /100
        return (int64_t)(fc::uint128_t((uint64_t)minr)
                * fc::uint128_t(sl_percent) * fc::uint128_t(m_factor_percent)
                / fc::uint128_t(10000u)).lo;
    }

    int64_t liquidation_threshold(int64_t loan, uint16_t r_percent) {
        return mul_pct(loan, r_percent);
    }

    int64_t max_leverage_loan(int64_t reserve_a, int64_t reserve_b, fc::uint128_t k,
                              int64_t collateral, int outcome, int64_t hi_loan,
                              uint16_t r_percent, uint16_t s_percent,
                              uint16_t sl_percent, uint16_t m_factor_percent) {
        int64_t lo = 0, hi = hi_loan, best = 0;
        for (int i = 0; i < 50; ++i) {
            int64_t mid = (lo + hi) / 2;
            if (mid <= lo) break;
            cpmm_fill f = cpmm_buy(reserve_a, reserve_b, k, collateral + mid, outcome);
            int64_t m = worst_opposing_bet(f.new_reserve_a, f.new_reserve_b, sl_percent, m_factor_percent);
            int64_t cvw = cancel_value_after_opposing(f.new_reserve_a, f.new_reserve_b, k,
                                                      f.tokens, outcome, m);
            int64_t thr_safe = mul_pct(liquidation_threshold(mid, r_percent), s_percent);
            if (cvw >= thr_safe) { best = mid; lo = mid; }
            else                 { hi = mid; }
        }
        return best;
    }

}}}} // graphene::chain::pm::leverage
