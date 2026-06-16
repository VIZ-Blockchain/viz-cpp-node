<?php
/**
 * Smoke test for module/lmsr_fixed.php — sanity-checks the deterministic
 * implementation against analytical reference values. NOT the canonical
 * test-vector file (that comes in Phase 4); this is just a quick gate so
 * we don't move to the JS port with a broken PHP impl.
 */
require_once __DIR__ . '/lmsr_fixed.php';

$failed = 0;
$passed = 0;

function check($label, $actual, $expected, $tol = 0) {
    global $failed, $passed;
    $diff = abs($actual - $expected);
    $ok = ($diff <= $tol);
    $tag = $ok ? '  OK' : 'FAIL';
    printf("[%s] %-50s actual=%-15s expected=%-15s diff=%s\n",
        $tag, $label, $actual, $expected, $diff);
    if ($ok) $passed++; else $failed++;
}

// ---------------------------------------------------------------------
// 1. Q96 primitives — constants
// ---------------------------------------------------------------------
$C = _lmsr_q96_const();
check('LN2 decimal',           gmp_strval($C['LN2']),  '54916777467707473351141471128');
check('ONE = 2^96',            gmp_strval($C['ONE']),  '79228162514264337593543950336');

// ---------------------------------------------------------------------
// 2. exp_q sanity: exp(0) = 1 (= ONE), exp(LN2) = 2 (= 2·ONE)
// ---------------------------------------------------------------------
$exp_0     = _exp_q($C['ZERO']);
$exp_ln2   = _exp_q($C['LN2']);
$exp_neg_ln2 = _exp_q($C['NEG_LN2']);
check('exp_q(0) = ONE',
    gmp_strval($exp_0),  gmp_strval($C['ONE']));
// exp(ln 2) should be 2 in real, i.e. 2·ONE in Q96. Allow ≤ 2 ULP rounding noise.
$two_one = gmp_strval(gmp_mul($C['ONE'], 2));
check('exp_q(LN2) ≈ 2·ONE',
    gmp_strval($exp_ln2), $two_one,
    /*tol*/ 0);   // demand bit-exact for 2 (range reduction makes k=1, r=0 exactly)
// exp(-LN2) = 0.5 = HALF
check('exp_q(-LN2) = HALF',
    gmp_strval($exp_neg_ln2), gmp_strval($C['HALF']),
    /*tol*/ 0);

// ---------------------------------------------------------------------
// 3. ln_q sanity: ln(ONE) = 0, ln(2·ONE) = LN2, ln(HALF) = -LN2
// ---------------------------------------------------------------------
$ln_one  = _ln_q($C['ONE']);
$ln_two  = _ln_q(gmp_mul($C['ONE'], 2));
$ln_half = _ln_q($C['HALF']);
check('ln_q(ONE) = 0',
    gmp_strval($ln_one), '0');
check('ln_q(2·ONE) = LN2',
    gmp_strval($ln_two), gmp_strval($C['LN2']),
    /*tol*/ 2);    // up to 2 ULPs in the atanh series rounding chain
check('ln_q(HALF) = -LN2',
    gmp_strval($ln_half), gmp_strval(gmp_neg($C['LN2'])),
    /*tol*/ 2);

// ---------------------------------------------------------------------
// 4. Round trip: ln(exp(x)) ≈ x, exp(ln(x)) ≈ x
// ---------------------------------------------------------------------
$x = gmp_init('12345678901234567890');           // small Q96 (≈ 1.56e-10 in real)
$rt1 = _ln_q(_exp_q($x));
check('ln(exp(0.00...)) ≈ x', gmp_strval($rt1), gmp_strval($x), /*tol*/ 4);

$x = gmp_mul($C['ONE'], 5);                      // 5.0 in real
$rt2 = _ln_q(_exp_q($x));
check('ln(exp(5)) ≈ 5·ONE', gmp_strval($rt2), gmp_strval($x), /*tol*/ 8);

// ---------------------------------------------------------------------
// 5. LMSR-level checks
// ---------------------------------------------------------------------

// Symmetric 2-outcome: q=[0,0], b=100000 milli-VIZ → cost = 0·b·... wait
// C(q) for q=[0,0], b=100000: ratios=[0,0], lse = ln(2) ≈ 0.693, cost = b · 0.693 = 69314 (in milli-VIZ, truncated)
$cost_zero_b = lmsr_cost([0, 0], 100000);
// Real expected: 100000 · ln(2) = 69314.7180...; truncated → 69314
check('lmsr_cost([0,0], b=100000) ≈ 69314', $cost_zero_b, 69314, /*tol*/ 1);

// 3 outcomes balanced: q=[0,0,0], b=100000 → cost = b · ln(3) = 100000 · 1.0986... = 109861
$cost_zero_b3 = lmsr_cost([0, 0, 0], 100000);
check('lmsr_cost([0,0,0], b=100000) ≈ 109861', $cost_zero_b3, 109861, /*tol*/ 1);

// Imbalanced: q=[10000,0,0], b=100000
// Σ exp(q_j/b) = exp(0.1)+exp(0)+exp(0) = 1.105171 + 2 = 3.105171
// ln(3.105171) = 1.1330676...
// cost = 100000 · 1.1330676 = 113306.76 → truncated 113306
$cost_imbal = lmsr_cost([10000, 0, 0], 100000);
check('lmsr_cost([10000,0,0], b=100000) ≈ 113306',
    $cost_imbal, 113306, /*tol*/ 1);

// ---------------------------------------------------------------------
// 6. Prices sum to LMSR_PRICE_PRECISION (within truncation residual)
// ---------------------------------------------------------------------
$p_balanced = lmsr_prices([0, 0, 0], 100000);
$sum_p = array_sum($p_balanced);
check('balanced 3-way prices sum ≈ 1e6',
    $sum_p, 1000000, /*tol*/ 3);   // ≤ N truncation residual
foreach ($p_balanced as $i => $pi) {
    check("balanced 3-way price[$i] ≈ 333333", $pi, 333333, /*tol*/ 1);
}

// Imbalanced: q=[10000,0,0]
$p_imb = lmsr_prices([10000, 0, 0], 100000);
$sum_p2 = array_sum($p_imb);
check('imbalanced prices sum ≈ 1e6', $sum_p2, 1000000, /*tol*/ 3);
// price[0] should be ≈ exp(0.1)/(exp(0.1)+2·exp(0)) = 1.1052/(1.1052+2) = 0.3559... → ≈ 355900
check('imbalanced price[0] ≈ 355900',
    $p_imb[0], 355900, /*tol*/ 100);   // small margin for rounding chain

// ---------------------------------------------------------------------
// 7. buy_cost monotone, tokens_for_amount ≤ amount in cost
// ---------------------------------------------------------------------
$bc1 = lmsr_buy_cost([1000, 1000], 100000, 0, 1000);
$bc2 = lmsr_buy_cost([1000, 1000], 100000, 0, 2000);
check('buy_cost monotone', ($bc2 > $bc1) ? 1 : 0, 1);

$tok = lmsr_tokens_for_amount([1000, 1000], 100000, 0, 5000);
$cost_back = lmsr_buy_cost([1000, 1000], 100000, 0, $tok);
check('tokens_for_amount(5000): cost ≤ amount',
    ($cost_back <= 5000) ? 1 : 0, 1);
check('tokens_for_amount(5000): cost(tok+1) > amount',
    (lmsr_buy_cost([1000,1000], 100000, 0, $tok+1) > 5000) ? 1 : 0, 1);

// ---------------------------------------------------------------------
// 8. lmsr_b_from_liquidity sanity
// ---------------------------------------------------------------------
// liquidity = 100000, N = 2 → b = 100000 / ln(2) = 100000 / 0.6931 = 144269 (truncated 144269)
$b_from_l = lmsr_b_from_liquidity(100000, 2);
check('b_from_liquidity(100000, 2) ≈ 144269',
    $b_from_l, 144269, /*tol*/ 1);

// liquidity = 100000, N = 4 → b = 100000 / ln(4) = 100000 / 1.3862... = 72134
$b_from_l4 = lmsr_b_from_liquidity(100000, 4);
check('b_from_liquidity(100000, 4) ≈ 72134',
    $b_from_l4, 72134, /*tol*/ 1);

// ---------------------------------------------------------------------
echo "\n---\n";
printf("PASSED: %d   FAILED: %d\n", $passed, $failed);
exit($failed === 0 ? 0 : 1);
