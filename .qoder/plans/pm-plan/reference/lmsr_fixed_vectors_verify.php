<?php
/**
 * tests/lmsr_fixed_vectors_verify.php — asserts that the current PHP
 * implementation reproduces every value in tests/lmsr_fixed_vectors.json
 * with strict equality. Re-run after any edit to module/lmsr_fixed.php.
 *
 * Usage:  php tests/lmsr_fixed_vectors_verify.php
 * Exit:   0 = all pass, 1 = at least one mismatch
 */
require_once __DIR__ . '/lmsr_fixed.php';

$path = __DIR__ . '/lmsr_fixed_vectors.json';
if (!is_file($path)) {
    fwrite(STDERR, "[FAIL] missing $path — run `php tests/lmsr_vectors_generate.php` first\n");
    exit(1);
}
$V = json_decode(file_get_contents($path), true);
if (!is_array($V)) {
    fwrite(STDERR, "[FAIL] cannot parse $path\n");
    exit(1);
}

$pass = 0; $fail = 0;
function check($label, $expected, $actual) {
    global $pass, $fail;
    if ($expected === $actual) {
        $pass++;
        return;
    }
    $fail++;
    $exp = is_array($expected) ? json_encode($expected) : (string)$expected;
    $act = is_array($actual)   ? json_encode($actual)   : (string)$actual;
    echo "  FAIL  $label\n    expected: $exp\n    actual:   $act\n";
}

// ---- constants ----
$C = _lmsr_q96_const();
check('const ONE',  $V['constants']['ONE'],  gmp_strval($C['ONE']));
check('const HALF', $V['constants']['HALF'], gmp_strval($C['HALF']));
check('const LN2',  $V['constants']['LN2'],  gmp_strval($C['LN2']));
check('const EXP_DOMAIN_LIMIT', $V['constants']['EXP_DOMAIN_LIMIT'], gmp_strval($C['EXP_DOMAIN_LIMIT']));
check('const MAX_B', $V['constants']['MAX_B'], (string)$C['MAX_B']);
check('const MAX_Q', $V['constants']['MAX_Q'], (string)$C['MAX_Q']);

// ---- primitives ----
foreach ($V['mul_q'] as $i => $t) {
    $r = _mul_q(gmp_init($t['a']), gmp_init($t['b']));
    check("mul_q[$i] $t[a]·$t[b]", $t['expected'], gmp_strval($r));
}
foreach ($V['div_q'] as $i => $t) {
    $r = _div_q(gmp_init($t['a']), gmp_init($t['b']));
    check("div_q[$i] $t[a]/$t[b]", $t['expected'], gmp_strval($r));
}
foreach ($V['exp_q'] as $i => $t) {
    $r = _exp_q(gmp_init($t['x']));
    check("exp_q[$i] x=$t[x]", $t['expected'], gmp_strval($r));
}
foreach ($V['ln_q'] as $i => $t) {
    $r = _ln_q(gmp_init($t['x']));
    check("ln_q[$i] x=$t[x]", $t['expected'], gmp_strval($r));
}

// ---- public API ----
foreach ($V['lmsr_cost'] as $i => $t) {
    check("lmsr_cost[$i]", (int)$t['expected'], lmsr_cost($t['q'], (int)$t['b']));
}
foreach ($V['lmsr_prices'] as $i => $t) {
    check("lmsr_prices[$i]", array_map('intval', $t['expected']),
          lmsr_prices($t['q'], (int)$t['b']));
}
foreach ($V['lmsr_buy_cost'] as $i => $t) {
    check("lmsr_buy_cost[$i]", (int)$t['expected'],
          lmsr_buy_cost($t['q'], (int)$t['b'], (int)$t['i'], (int)$t['delta']));
}
foreach ($V['lmsr_sell_return'] as $i => $t) {
    check("lmsr_sell_return[$i]", (int)$t['expected'],
          lmsr_sell_return($t['q'], (int)$t['b'], (int)$t['i'], (int)$t['delta']));
}
foreach ($V['lmsr_tokens_for_amount'] as $i => $t) {
    check("lmsr_tokens_for_amount[$i]", (int)$t['expected'],
          lmsr_tokens_for_amount($t['q'], (int)$t['b'], (int)$t['i'], (int)$t['amount']));
}
foreach ($V['lmsr_b_from_liquidity'] as $i => $t) {
    check("lmsr_b_from_liquidity[$i]", (int)$t['expected'],
          lmsr_b_from_liquidity((int)$t['liquidity'], (int)$t['n']));
}

$total = $pass + $fail;
echo "\nPHP verifier: $pass / $total passed";
if ($fail) { echo "  ($fail FAILED)\n"; exit(1); }
echo "\n";
exit(0);
