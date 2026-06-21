#include <graphene/chain/pm/lmsr_q96.hpp>
#include <fc/exception/exception.hpp>

// Deterministic fixed-point LMSR — C++ port of reference/lmsr_fixed.php (normative).
// Every numeric step mirrors the PHP/GMP reference so the C++ output is bit-identical
// to reference/lmsr_fixed_vectors.json. See lmsr-fixed-point-spec.md.

namespace graphene { namespace chain { namespace lmsr {

    using boost::multiprecision::int256_t;
    using boost::multiprecision::int512_t;

    namespace {

        // Q96 anchors (frozen).
        const q96 ONE      = q96(1) << 96;
        // 0xB17217F7D1CF79ABC9E3B398 = floor(ln(2) * 2^96)
        const q96 LN2      = q96("54916777467707473351141471128");
        const q96 NEG_LN2  = -LN2;
        const q96 EXP_DOMAIN_LIMIT = q96(200) * ONE;
        const q96 MAX_B    = q96(1) << 53;
        const q96 MAX_Q    = q96(1) << 53;

        // Floor division (round toward -inf). d is always > 0 here.
        template<typename T>
        inline T floor_div(const T& n, const T& d) {
            T q = n / d;            // boost: truncation toward zero
            T r = n - q * d;
            if (r != 0 && ((r < 0) != (d < 0))) q -= 1;
            return q;
        }

        // Arithmetic right shift by `bits` = floor(n / 2^bits). Spec rule for mul_q / scaling.
        inline q96 asr(const q96& n, unsigned bits) {
            return floor_div(n, q96(q96(1) << bits));
        }

        // mul_q(a,b) = (a*b) >> 96, arithmetic (sign-preserving). Wide 512-bit product.
        inline q96 mul_q_impl(const q96& a, const q96& b) {
            int512_t t = int512_t(a) * int512_t(b);
            int512_t d = int512_t(1) << 96;
            int512_t q = t / d;
            int512_t r = t - q * d;
            if (r != 0 && ((r < 0) != (d < 0))) q -= 1;   // floor
            return q96(q);
        }

        // div_q(a,b) = (a << 96) / b, truncated toward zero.
        inline q96 div_q_impl(const q96& a, const q96& b) {
            int512_t t = int512_t(a) << 96;
            return q96(t / int512_t(b));                   // boost: trunc toward zero
        }

        inline bool is_odd(const q96& q) {
            q96 r = q % 2;
            return r != 0;
        }

        // Round x/LN2 to nearest integer, ties to even (the single non-truncating step, §3.5).
        q96 round_div_ln2(const q96& x) {
            q96 q = x / LN2;                               // truncated toward zero
            q96 r = x - q * LN2;
            q96 two_r = r * 2;
            if (two_r > LN2 || (two_r == LN2 && is_odd(q))) q += 1;
            if (two_r < NEG_LN2 || (two_r == NEG_LN2 && is_odd(q))) q -= 1;
            return q;
        }

        q96 exp_q(const q96& x) {
            q96 abs_x = x < 0 ? q96(-x) : x;
            FC_ASSERT(abs_x <= q96(130) * ONE, "LMSR_OVERFLOW: exp_q domain exceeded");

            q96 k = round_div_ln2(x);
            q96 r = x - k * LN2;

            q96 acc = ONE;
            q96 term = ONE;
            for (int n = 1; n <= 30; ++n) {               // FIXED 30 iterations
                term = mul_q_impl(term, r) / q96(n);      // mul_q first, then trunc-div by n
                acc += term;
            }

            long long k_int = k.convert_to<long long>();
            if (k_int >= 0) {
                return acc * (q96(1) << (unsigned)k_int); // exact left shift
            } else {
                return asr(acc, (unsigned)(-k_int));       // ASR (acc>0 -> == trunc)
            }
        }

        q96 ln_q(const q96& x) {
            FC_ASSERT(x > 0, "LMSR_DOMAIN_LN: ln_q called with x <= 0");

            int p = (int)boost::multiprecision::msb(x) - 96;
            q96 m;
            if (p >= 0) m = asr(x, (unsigned)p);
            else        m = x * (q96(1) << (unsigned)(-p));

            q96 num = m - ONE;
            q96 den = m + ONE;
            q96 z = div_q_impl(num, den);

            q96 z2 = mul_q_impl(z, z);
            q96 acc = z;
            q96 term = z;
            for (int k = 1; k <= 50; ++k) {               // FIXED 50 iterations
                term = mul_q_impl(term, z2);
                q96 denom = q96(2 * k + 1);
                acc += term / denom;                       // trunc-div
            }
            q96 ln_m = acc * 2;
            return q96(p) * LN2 + ln_m;
        }

        void validate_domain(const std::vector<int64_t>& q, int64_t b) {
            FC_ASSERT(q.size() >= 2 && q.size() <= 16, "LMSR_INVALID_N: outcomes must be 2..16");
            FC_ASSERT(b > 0, "LMSR_INVALID_B: b must be > 0");
            FC_ASSERT(q96(b) <= MAX_B, "LMSR_INVALID_B: b exceeds MAX_B (2^53)");
            for (int64_t qi : q) {
                FC_ASSERT(qi >= 0, "LMSR_INVALID_Q: q[i] < 0");
                FC_ASSERT(q96(qi) <= MAX_Q, "LMSR_INVALID_Q: q[i] exceeds MAX_Q (2^53)");
            }
        }

        // ratios[i] = q[i] * ONE / b, truncated (one-step form, §3.4).
        std::vector<q96> ratios_q96(const std::vector<int64_t>& q, int64_t b) {
            std::vector<q96> out;
            out.reserve(q.size());
            q96 bb(b);
            for (int64_t qi : q) {
                q96 num = q96(qi) * ONE;
                out.push_back(num / bb);                   // trunc toward zero (qi,b >= 0)
            }
            return out;
        }

        q96 lse_q(const std::vector<q96>& ratios) {
            if (ratios.empty()) return q96(0);
            q96 mx = ratios[0];
            for (const auto& r : ratios) if (r > mx) mx = r;

            q96 acc(0);
            q96 cutoff = -EXP_DOMAIN_LIMIT;
            for (const auto& r : ratios) {                 // input order — DO NOT sort
                q96 d = r - mx;                            // d <= 0
                if (d < cutoff) continue;                  // strict <
                acc += exp_q(d);
            }
            if (acc <= 0) return mx;                        // defensive (unreachable)
            return mx + ln_q(acc);
        }

    } // anonymous namespace

    // ───────────────────────── public API ─────────────────────────

    int64_t lmsr_cost(const std::vector<int64_t>& q, int64_t b) {
        if (b <= 0) return 0;
        validate_domain(q, b);
        std::vector<q96> ratios = ratios_q96(q, b);
        q96 lse = lse_q(ratios);
        q96 product = q96(b) * lse;
        q96 cost = product / ONE;                          // trunc toward zero
        return cost.convert_to<int64_t>();
    }

    std::vector<int64_t> lmsr_prices(const std::vector<int64_t>& q, int64_t b) {
        size_t n = q.size();
        if (b <= 0) return std::vector<int64_t>(n, 0);
        validate_domain(q, b);

        std::vector<q96> ratios = ratios_q96(q, b);
        q96 mx = ratios[0];
        for (const auto& r : ratios) if (r > mx) mx = r;

        q96 cutoff = -EXP_DOMAIN_LIMIT;
        std::vector<q96> exps;
        exps.reserve(n);
        q96 sum(0);
        for (const auto& r : ratios) {
            q96 d = r - mx;
            if (d < cutoff) { exps.push_back(q96(0)); }
            else { q96 e = exp_q(d); exps.push_back(e); sum += e; }
        }
        if (sum <= 0) return std::vector<int64_t>(n, 0);

        std::vector<int64_t> out;
        out.reserve(n);
        for (const auto& e : exps) {
            q96 pq = div_q_impl(e, sum);                   // Q96
            q96 scaled = pq * q96(LMSR_PRICE_PRECISION);
            q96 v = scaled / ONE;                          // trunc
            out.push_back(v.convert_to<int64_t>());
        }
        return out;
    }

    int64_t lmsr_price(const std::vector<int64_t>& q, int64_t b, int i) {
        if (b <= 0 || i < 0 || (size_t)i >= q.size()) return 0;
        validate_domain(q, b);
        auto prices = lmsr_prices(q, b);
        return prices[i];
    }

    int64_t lmsr_buy_cost(const std::vector<int64_t>& q, int64_t b, int i, int64_t delta) {
        if (b <= 0 || delta <= 0 || i < 0 || (size_t)i >= q.size()) return 0;
        std::vector<int64_t> q_after = q;
        q_after[i] += delta;
        int64_t d = lmsr_cost(q_after, b) - lmsr_cost(q, b);
        return d > 0 ? d : 0;
    }

    int64_t lmsr_sell_return(const std::vector<int64_t>& q, int64_t b, int i, int64_t delta) {
        if (b <= 0 || delta <= 0 || i < 0 || (size_t)i >= q.size()) return 0;
        if (q[i] - delta < 0) return 0;
        std::vector<int64_t> q_after = q;
        q_after[i] -= delta;
        int64_t d = lmsr_cost(q, b) - lmsr_cost(q_after, b);
        return d > 0 ? d : 0;
    }

    int64_t lmsr_tokens_for_amount(const std::vector<int64_t>& q, int64_t b, int i, int64_t amount) {
        if (b <= 0 || amount <= 0 || i < 0 || (size_t)i >= q.size()) return 0;
        validate_domain(q, b);

        int64_t lo = 0;
        int64_t hi = amount * 10;
        int64_t max_q = (int64_t)1 << 53;
        if (hi > max_q) hi = max_q;
        int64_t best = 0;

        for (int iter = 0; iter < 100; ++iter) {           // FIXED 100 iterations
            if (hi - lo <= 1) break;
            int64_t mid = (lo + hi) / 2;
            int64_t cost = lmsr_buy_cost(q, b, i, mid);
            if (cost <= amount) { best = mid; lo = mid; }
            else { hi = mid; }
        }
        return best;
    }

    int64_t lmsr_b_from_liquidity(int64_t liquidity, int n) {
        if (liquidity <= 0 || n <= 1) return 0;
        FC_ASSERT(n <= 16, "LMSR_INVALID_N");
        q96 n_q96 = q96(n) * ONE;
        q96 ln_n = ln_q(n_q96);
        q96 num = q96(liquidity) * ONE;
        q96 b_q = num / ln_n;                              // trunc
        return b_q.convert_to<int64_t>();
    }

    namespace detail {
        q96 mul_q(const q96& a, const q96& b) { return mul_q_impl(a, b); }
        q96 div_q(const q96& a, const q96& b) { return div_q_impl(a, b); }
        q96 exp_q(const q96& x) { return graphene::chain::lmsr::exp_q(x); }
        q96 ln_q(const q96& x) { return graphene::chain::lmsr::ln_q(x); }
    }

} } } // graphene::chain::lmsr
