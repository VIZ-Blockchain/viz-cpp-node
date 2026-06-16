<?php
/**
 * module/lmsr_fixed.php — Deterministic fixed-point LMSR.
 *
 * Reference implementation of docs/lmsr-fixed-point-spec.md. Bit-exact with
 * the JS BigInt port in market_math.js and the future C++ VIZ DLT plugin.
 *
 * No `float`, `double`, `log()`, `exp()`, `pow()`, `sqrt()` calls — pure GMP.
 * Replaces module/lmsr_math.php (which is float-based and non-deterministic).
 *
 * Public API matches lmsr_math.php so module/api.php migrates by `require_once`
 * and unchanged call signatures. All amounts are int64 in milli-VIZ.
 *
 * @see docs/lmsr-fixed-point-spec.md
 */

// =====================================================================
// Constants — frozen by the spec. Any change is consensus-breaking.
// =====================================================================

if (!extension_loaded('gmp')) {
    throw new Exception('module/lmsr_fixed.php requires the GMP extension');
}

// Public precisions (unchanged from lmsr_math.php so legacy call sites match)
if (!defined('LMSR_PRECISION')) {
    define('LMSR_PRECISION', 1000);          // 1 VIZ = 1000 milli-VIZ
}
if (!defined('LMSR_PRICE_PRECISION')) {
    define('LMSR_PRICE_PRECISION', 1000000); // price ×10^6, 1e6 = 100%
}

// Internal Q96 anchors. Stored as GMP resources, lazily initialised below.
function _lmsr_q96_const() {
    static $c = null;
    if ($c !== null) return $c;
    $one  = gmp_pow(2, 96);                                     // 2^96
    $half = gmp_pow(2, 95);                                     // 2^95
    // ⌊ln(2) · 2^96⌋ = 0xB17217F7D1CF79ABC9E3B398
    $ln2  = gmp_init('B17217F7D1CF79ABC9E3B398', 16);
    $c = [
        'ONE'   => $one,
        'HALF'  => $half,
        'LN2'   => $ln2,
        'NEG_LN2' => gmp_neg($ln2),
        'EXP_DOMAIN_LIMIT' => gmp_mul($one, gmp_init(200)),     // 200 · ONE
        'MAX_B' => gmp_pow(2, 53),                              // §1.4 domain caps
        'MAX_Q' => gmp_pow(2, 53),
        'ZERO'  => gmp_init(0),
        'ONE_INT'   => gmp_init(1),
        'TWO_INT'   => gmp_init(2),
        'THOUSAND'  => gmp_init(1000),
        'PRICE_PRECISION' => gmp_init(1000000),
    ];
    return $c;
}

// =====================================================================
// Q96 primitives (§2 of spec)
// =====================================================================

/**
 * Truncated integer division (round toward zero). GMP's gmp_div_q with
 * GMP_ROUND_ZERO is exactly this; we wrap for clarity.
 */
function _q_trunc_div($a, $b) {
    return gmp_div_q($a, $b, GMP_ROUND_ZERO);
}

/**
 * Arithmetic right shift by `bits`. Floor division by 2^bits, NOT truncation.
 * For non-negative values ASR == truncation; for negative values ASR rounds
 * toward -∞ (this is the spec rule for `mul_q`, §3.5).
 */
function _q_asr($n, $bits) {
    return gmp_div_q($n, gmp_pow(2, $bits), GMP_ROUND_MINUSINF);
}

/**
 * mul_q(a, b) = (a · b) >> 96 with arithmetic right shift (§3.5).
 * Wide accumulator: GMP integers are unbounded, so a 256-bit intermediate
 * is naturally available.
 */
function _mul_q($a, $b) {
    return _q_asr(gmp_mul($a, $b), 96);
}

/**
 * div_q(a, b) = (a << 96) / b, truncated toward zero (§3.5).
 * Result is Q96.
 */
function _div_q($a, $b) {
    return _q_trunc_div(gmp_mul($a, gmp_pow(2, 96)), $b);
}

/**
 * MSB position (0-indexed) of a positive GMP integer. Returns -1 for 0.
 * Used by ln_q range reduction (§3.2). No FP calls.
 */
function _msb($x) {
    if (gmp_cmp($x, 0) <= 0) return -1;
    return strlen(gmp_strval($x, 2)) - 1;
}

/**
 * Test whether a GMP integer is odd (works for negatives too).
 * Used by exp_q's banker's rounding (§3.5).
 */
function _is_odd($q) {
    // gmp_mod for negatives: gmp_mod(-3, 2) returns 1 (PHP GMP follows sign-of-divisor).
    // Either way the result is 0 or non-zero; we only need parity.
    return gmp_cmp(gmp_abs(gmp_mod($q, 2)), 0) !== 0;
}

// =====================================================================
// Range reduction for exp_q — round-to-nearest-even (banker's), §3.5
// =====================================================================

/**
 * Round x/LN2 to nearest integer, ties to even. The single non-truncating
 * rounding step in the entire pipeline.
 */
function _round_div_ln2($x) {
    $C = _lmsr_q96_const();
    $LN2 = $C['LN2'];
    $NEG = $C['NEG_LN2'];

    $q = _q_trunc_div($x, $LN2);             // truncated toward zero
    $r = gmp_sub($x, gmp_mul($q, $LN2));     // |r| < LN2 (sign matches truncation residual)
    $two_r = gmp_mul($r, 2);

    // Positive-side: 2r > LN2  → bump up. 2r == LN2 and q odd → bump up (ties to even).
    $cmp_pos = gmp_cmp($two_r, $LN2);
    if ($cmp_pos > 0 || ($cmp_pos === 0 && _is_odd($q))) {
        $q = gmp_add($q, 1);
    }
    // Negative-side: 2r < -LN2 → bump down. 2r == -LN2 and q odd → bump down.
    // Note: at most one of (cmp_pos>=0) and (cmp_neg<=0) can be strict for any single r.
    $cmp_neg = gmp_cmp($two_r, $NEG);
    if ($cmp_neg < 0 || ($cmp_neg === 0 && _is_odd($q))) {
        $q = gmp_sub($q, 1);
    }
    return $q;
}

// =====================================================================
// exp_q — Taylor with range reduction (§3.1)
// =====================================================================

function _exp_q($x) {
    $C = _lmsr_q96_const();
    // Domain check: |x| ≤ 130 · ONE
    $abs_x = gmp_abs($x);
    $limit = gmp_mul($C['ONE'], gmp_init(130));
    if (gmp_cmp($abs_x, $limit) > 0) {
        throw new Exception('LMSR_OVERFLOW: exp_q domain exceeded');
    }

    // 1. Range reduction
    $k = _round_div_ln2($x);                          // signed integer
    $r = gmp_sub($x, gmp_mul($k, $C['LN2']));         // |r| ≤ LN2/2

    // 2. Polynomial — exactly 30 iterations
    $acc  = $C['ONE'];                                 // term[0] = 1
    $term = $C['ONE'];
    for ($n = 1; $n <= 30; $n++) {
        // term = mul_q(term, r) / n   (mul_q first, then truncated div by integer n)
        $term = _q_trunc_div(_mul_q($term, $r), gmp_init($n));
        $acc  = gmp_add($acc, $term);
    }

    // 3. Scale by 2^k (signed). k is small (|k| ≤ ⌈130/ln(2)⌉ ≈ 188).
    $k_int = gmp_intval($k);
    if ($k_int >= 0) {
        return gmp_mul($acc, gmp_pow(2, $k_int));     // exact left shift
    } else {
        // Arithmetic right shift; for non-negative `acc` (true here since exp > 0)
        // ASR == truncation, but we use ASR by spec for consistency.
        return _q_asr($acc, -$k_int);
    }
}

// =====================================================================
// ln_q — atanh series with range reduction (§3.2)
// =====================================================================

function _ln_q($x) {
    $C = _lmsr_q96_const();
    if (gmp_cmp($x, 0) <= 0) {
        throw new Exception('LMSR_DOMAIN_LN: ln_q called with x ≤ 0');
    }

    // 1. p = msb(x) - 96  ; m = x scaled into [ONE, 2·ONE)
    $p = _msb($x) - 96;
    if ($p >= 0) {
        // m = x >> p, ASR (x is positive, == truncation)
        $m = _q_asr($x, $p);
    } else {
        // m = x << (-p), exact left shift
        $m = gmp_mul($x, gmp_pow(2, -$p));
    }

    // 2. z = (m - ONE) / (m + ONE) in Q96, |z| < 1/3
    $num = gmp_sub($m, $C['ONE']);
    $den = gmp_add($m, $C['ONE']);
    $z   = _div_q($num, $den);

    // 3. atanh series, 50 iterations
    $z2   = _mul_q($z, $z);
    $acc  = $z;       // first term = z (k=0 → 2k+1 = 1)
    $term = $z;
    for ($k = 1; $k <= 50; $k++) {
        $term = _mul_q($term, $z2);                              // term · z²
        $denom = gmp_init(2 * $k + 1);                           // 3, 5, 7, ...
        $acc   = gmp_add($acc, _q_trunc_div($term, $denom));
    }

    // 4. ln(m) = 2 · acc
    $ln_m = gmp_mul($acc, 2);

    // 5. ln(x) = p · LN2 + ln(m)
    $p_gmp = gmp_init($p);
    return gmp_add(gmp_mul($p_gmp, $C['LN2']), $ln_m);
}

// =====================================================================
// log-sum-exp (§3.3)
// =====================================================================

function _lse_q(array $ratios) {
    $C = _lmsr_q96_const();
    if (empty($ratios)) return $C['ZERO'];

    // Find max — straightforward, deterministic
    $max = $ratios[0];
    foreach ($ratios as $r) {
        if (gmp_cmp($r, $max) > 0) $max = $r;
    }

    $acc = $C['ZERO'];
    $cutoff = gmp_neg($C['EXP_DOMAIN_LIMIT']);    // -200 · ONE
    foreach ($ratios as $r) {                      // input order — DO NOT sort
        $d = gmp_sub($r, $max);                    // d ≤ 0
        if (gmp_cmp($d, $cutoff) < 0) continue;    // strict < (per spec §3.3)
        $acc = gmp_add($acc, _exp_q($d));
    }
    if (gmp_cmp($acc, 0) <= 0) {
        // Defensive — should be unreachable: at least the d=0 term contributes ONE.
        return $max;
    }
    return gmp_add($max, _ln_q($acc));
}

// =====================================================================
// Domain validation (§1.4)
// =====================================================================

function _validate_domain(array $q, $b) {
    $C = _lmsr_q96_const();
    if (count($q) < 2 || count($q) > 16) {
        throw new Exception('LMSR_INVALID_N: outcomes must be 2..16');
    }
    if (gmp_cmp(gmp_init((string)$b), 0) <= 0) {
        throw new Exception('LMSR_INVALID_B: b must be > 0');
    }
    if (gmp_cmp(gmp_init((string)$b), $C['MAX_B']) > 0) {
        throw new Exception('LMSR_INVALID_B: b exceeds MAX_B (2^53)');
    }
    foreach ($q as $qi) {
        $g = gmp_init((string)$qi);
        if (gmp_cmp($g, 0) < 0) {
            throw new Exception('LMSR_INVALID_Q: q[i] < 0');
        }
        if (gmp_cmp($g, $C['MAX_Q']) > 0) {
            throw new Exception('LMSR_INVALID_Q: q[i] exceeds MAX_Q (2^53)');
        }
    }
}

/**
 * Convert int64 milli-VIZ q[]/b into Q96 ratios = q[i] · ONE / b (truncated).
 * Direct one-step form per spec §3.4 — avoids double-truncation.
 */
function _ratios_q96(array $q, $b) {
    $C = _lmsr_q96_const();
    $b_gmp = gmp_init((string)$b);
    $ratios = [];
    foreach ($q as $qi) {
        $num = gmp_mul(gmp_init((string)$qi), $C['ONE']);
        $ratios[] = _q_trunc_div($num, $b_gmp);
    }
    return $ratios;
}

// =====================================================================
// Public API (§4) — same signatures as module/lmsr_math.php
// =====================================================================

/**
 * LMSR cost C(q) = b · ln(Σ exp(q_j / b))
 * @return int milli-VIZ, truncated toward zero
 */
function lmsr_cost(array $q, int $b): int {
    if ($b <= 0) return 0;
    _validate_domain($q, $b);
    $C = _lmsr_q96_const();
    $ratios = _ratios_q96($q, $b);
    $lse    = _lse_q($ratios);                                   // Q96
    // cost_milli = (b · lse) / ONE  (truncated toward zero — see §3.4)
    $product = gmp_mul(gmp_init((string)$b), $lse);
    $cost    = _q_trunc_div($product, $C['ONE']);
    return intval(gmp_strval($cost));
}

/**
 * Probability of outcome i, returned as integer ×10^6 (1e6 = 100%).
 */
function lmsr_price(array $q, int $b, int $i): int {
    if ($b <= 0 || !isset($q[$i])) return 0;
    _validate_domain($q, $b);
    $prices = lmsr_prices($q, $b);
    return $prices[$i] ?? 0;
}

/**
 * All N prices in one pass. Sum of returned prices is in [LMSR_PRICE_PRECISION-N, LMSR_PRICE_PRECISION]
 * (truncation residual ≤ N). Caller MUST tolerate this — it is consensus-fixed.
 */
function lmsr_prices(array $q, int $b): array {
    $n = count($q);
    if ($b <= 0) return array_fill(0, $n, 0);
    _validate_domain($q, $b);
    $C = _lmsr_q96_const();

    $ratios = _ratios_q96($q, $b);
    // Find max
    $max = $ratios[0];
    foreach ($ratios as $r) if (gmp_cmp($r, $max) > 0) $max = $r;

    $cutoff = gmp_neg($C['EXP_DOMAIN_LIMIT']);
    $exps = [];
    $sum  = $C['ZERO'];
    foreach ($ratios as $r) {
        $d = gmp_sub($r, $max);
        if (gmp_cmp($d, $cutoff) < 0) {
            $exps[] = $C['ZERO'];
        } else {
            $e = _exp_q($d);
            $exps[] = $e;
            $sum = gmp_add($sum, $e);
        }
    }
    if (gmp_cmp($sum, 0) <= 0) {
        return array_fill(0, $n, 0);
    }
    // price_i_q = exp_i / sum   (Q96 division ↦ Q96)
    // price_i_int = (price_i_q * PRICE_PRECISION) / ONE   truncated
    $out = [];
    foreach ($exps as $e) {
        $pq    = _div_q($e, $sum);                                // Q96
        $scaled= gmp_mul($pq, $C['PRICE_PRECISION']);
        $int   = _q_trunc_div($scaled, $C['ONE']);
        $out[] = intval(gmp_strval($int));
    }
    return $out;
}

/**
 * buy_cost = max(0, C(q + Δ·e_i) − C(q))
 */
function lmsr_buy_cost(array $q, int $b, int $i, int $delta): int {
    if ($b <= 0 || $delta <= 0 || !isset($q[$i])) return 0;
    $q_after = $q;
    $q_after[$i] += $delta;
    return max(0, lmsr_cost($q_after, $b) - lmsr_cost($q, $b));
}

/**
 * sell_return = max(0, C(q) − C(q − Δ·e_i))
 */
function lmsr_sell_return(array $q, int $b, int $i, int $delta): int {
    if ($b <= 0 || $delta <= 0 || !isset($q[$i])) return 0;
    if ($q[$i] - $delta < 0) return 0;       // can't sell more than the outcome holds
    $q_after = $q;
    $q_after[$i] -= $delta;
    return max(0, lmsr_cost($q, $b) - lmsr_cost($q_after, $b));
}

/**
 * Largest Δ such that buy_cost(Δ) ≤ amount. Deterministic binary search,
 * fixed 100-iteration bound (§4.1).
 */
function lmsr_tokens_for_amount(array $q, int $b, int $i, int $amount): int {
    if ($b <= 0 || $amount <= 0 || !isset($q[$i])) return 0;
    _validate_domain($q, $b);

    $lo = 0;
    // Upper bound: amount × 10, clamped to MAX_Q. Mirrors the legacy heuristic
    // and guarantees a feasible search interval (cost ≥ amount at hi).
    $hi = $amount * 10;
    $C  = _lmsr_q96_const();
    $max_q = intval(gmp_strval($C['MAX_Q']));
    if ($hi > $max_q) $hi = $max_q;
    $best = 0;

    for ($iter = 0; $iter < 100; $iter++) {
        if ($hi - $lo <= 1) break;
        $mid = intdiv($lo + $hi, 2);
        $cost = lmsr_buy_cost($q, $b, $i, $mid);
        if ($cost <= $amount) {
            $best = $mid;
            $lo = $mid;
        } else {
            $hi = $mid;
        }
    }
    return $best;
}

/**
 * b = liquidity / ln(N), in milli-VIZ. Computed once per market at creation.
 * Output truncated toward zero.
 */
function lmsr_b_from_liquidity(int $liquidity, int $n): int {
    if ($liquidity <= 0 || $n <= 1) return 0;
    if ($n > 16) throw new Exception('LMSR_INVALID_N');
    $C = _lmsr_q96_const();
    // ln(n) in Q96 — n is small int, scale to Q96 then ln_q
    $n_q96  = gmp_mul(gmp_init($n), $C['ONE']);
    $ln_n   = _ln_q($n_q96);                                     // Q96
    // b_milli = floor(liquidity · ONE / ln_n)
    $num    = gmp_mul(gmp_init((string)$liquidity), $C['ONE']);
    $b_q    = _q_trunc_div($num, $ln_n);
    return intval(gmp_strval($b_q));
}

/**
 * Maximum theoretical LMSR loss. Reference only — not used in settlement.
 */
function lmsr_max_loss(int $b, int $n): int {
    if ($b <= 0 || $n <= 1) return 0;
    $C = _lmsr_q96_const();
    $n_q96 = gmp_mul(gmp_init($n), $C['ONE']);
    $ln_n  = _ln_q($n_q96);
    $product = gmp_mul(gmp_init((string)$b), $ln_n);
    $loss = _q_trunc_div($product, $C['ONE']);
    return intval(gmp_strval($loss));
}

// =====================================================================
// Settlement and leverage helpers — pass-through from lmsr_math.php
// (these don't call exp/ln, so they're already deterministic; we copy the
// exact functions to make this file a single drop-in replacement)
// =====================================================================

/**
 * Onix Multi parimutuel settlement. Identical to lmsr_math.php — kept here
 * so callers only need to require_once one file.
 */
function lmsr_settlement(array $bets, int $winning_outcome, int $oracle_fee_permille, int $creator_fee_permille, int $liquidity_fee_permille): array {
    $losers_sum = 0;
    $winning_bets = [];
    $total_winning_tokens = 0;
    foreach ($bets as $bet) {
        if (intval($bet['outcome_index']) === $winning_outcome) {
            $winning_bets[] = $bet;
            $total_winning_tokens += intval($bet['weight']);
        } else {
            $losers_sum += intval($bet['amount']);
        }
    }
    $oracle_fee    = intval($losers_sum * $oracle_fee_permille    / 1000);
    $creator_fee   = intval($losers_sum * $creator_fee_permille   / 1000);
    $liquidity_fee = intval($losers_sum * $liquidity_fee_permille / 1000);
    $winners_pool  = $losers_sum - $oracle_fee - $creator_fee - $liquidity_fee;
    if ($winners_pool < 0) $winners_pool = 0;
    $payouts = [];
    $total_distributed = 0;
    foreach ($winning_bets as $bet) {
        $bet_amount = intval($bet['amount']);
        $tokens     = intval($bet['weight']);
        $profit_share = ($total_winning_tokens > 0) ? intval($winners_pool * $tokens / $total_winning_tokens) : 0;
        $time_penalty_ratio = intval($bet['time_penalty']);    // ×10^6
        $penalty_deduction  = intval($profit_share * $time_penalty_ratio / 1000000);
        $net_profit         = $profit_share - $penalty_deduction;
        $payout             = $bet_amount + $net_profit;
        $total_distributed += $payout;
        $payouts[] = [
            'user'         => $bet['user'],
            'bet_id'       => $bet['id'] ?? 0,
            'amount'       => $bet_amount,
            'tokens'       => $tokens,
            'profit_share' => $profit_share,
            'penalty'      => $penalty_deduction,
            'payout'       => $payout,
        ];
    }
    $undistributed = max(0, $winners_pool - ($total_distributed - array_sum(array_map(function($b){ return intval($b['amount']); }, $winning_bets))));
    return [
        'losers_sum'           => $losers_sum,
        'oracle_fee'           => $oracle_fee,
        'creator_fee'          => $creator_fee,
        'liquidity_fee'        => $liquidity_fee,
        'winners_pool'         => $winners_pool,
        'total_winning_tokens' => $total_winning_tokens,
        'payouts'              => $payouts,
        'undistributed'        => $undistributed,
        'total_penalty_pool'   => array_sum(array_column($payouts, 'penalty')),
    ];
}

/**
 * Leverage helper: max bet amount that keeps slippage within `slippage_pct`.
 * Uses the deterministic lmsr_price / lmsr_tokens_for_amount internally,
 * so its output is now also bit-deterministic.
 */
function lmsr_max_bet_amount(array $q, int $b, float $slippage_pct): int {
    if ($b <= 0 || $slippage_pct <= 0 || empty($q)) return 0;
    $n = count($q);
    if ($n < 2) return 0;
    $C = _lmsr_q96_const();
    $max_amount = 0;
    // slippage_pct → integer ×1e6 of fraction (e.g. 10% → 100_000 of 1_000_000 = 0.1)
    // We compare price-change fraction (price_after - price_before)/price_before * 100 ≤ slippage_pct
    // Implemented in integer ×10^6 price space: |dp| · 100 / p_before ≤ slippage_pct
    // → |dp| · 100 · 1e6 ≤ slippage_pct · 1e6 · p_before
    $sl_scaled = (int) round($slippage_pct * 1000000);   // slippage in ppm of percent — note: this float
                                                          // is non-consensus (only an off-chain risk gate)
    foreach ($q as $i => $qi) {
        $lo = 0;
        $hi = $b * 10;
        if ($hi > intval(gmp_strval($C['MAX_Q']))) $hi = intval(gmp_strval($C['MAX_Q']));
        $best = 0;
        for ($iter = 0; $iter < 60; $iter++) {
            if ($hi - $lo <= LMSR_PRECISION) break;
            $mid = intdiv($lo + $hi, 2);
            if ($mid <= 0) break;
            $price_before = lmsr_price($q, $b, $i);
            if ($price_before <= 0) { $hi = $mid; continue; }
            $tokens = lmsr_tokens_for_amount($q, $b, $i, $mid);
            if ($tokens <= 0) { $hi = $mid; continue; }
            $q_after = $q;
            $q_after[$i] += $tokens;
            $price_after = lmsr_price($q_after, $b, $i);
            $dp = abs($price_after - $price_before);
            // |dp|·100·1e6 ≤ slippage_pct·1e6·price_before
            // (multiply explicitly in int64-safe range — prices are ≤ 1e6, dp ≤ 1e6)
            $lhs = $dp * 100;                                   // ≤ 1e8
            $rhs_per_million = $sl_scaled;                       // = slippage_pct · 1e6
            // lhs · 1e6 / price_before ≤ rhs_per_million
            $threshold = intdiv($rhs_per_million * $price_before, 1000000);
            if ($lhs <= $threshold) {
                $best = $mid; $lo = $mid;
            } else {
                $hi = $mid;
            }
        }
        if ($best > $max_amount) $max_amount = $best;
    }
    return $max_amount;
}
