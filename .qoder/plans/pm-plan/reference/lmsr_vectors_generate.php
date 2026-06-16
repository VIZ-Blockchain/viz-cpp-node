<?php
/**
 * tests/lmsr_vectors_generate.php — generates the canonical test-vector
 * file `tests/lmsr_fixed_vectors.json`. The PHP reference is the SOURCE
 * OF TRUTH (per docs/lmsr-fixed-point-spec.md §5); JS and the future C++
 * port both compare against this file with strict equality.
 *
 * Re-run only when the spec changes (and bump `version`).
 *
 * Usage:  php tests/lmsr_vectors_generate.php
 *         (writes tests/lmsr_fixed_vectors.json relative to repo root)
 */
require_once __DIR__ . '/lmsr_fixed.php';

$C = _lmsr_q96_const();
$ONE = $C['ONE'];
$LN2 = $C['LN2'];

function gstr($g) { return gmp_strval($g, 10); }
function gG($s)   { return gmp_init($s, 10); }

// ---- Primitive vectors (Q96 strings) ----
$exp_q_cases = [
    gmp_init(0),
    $LN2,
    gmp_neg($LN2),
    $ONE,                                   // 1.0
    gmp_neg($ONE),                          // -1.0
    gmp_mul($ONE, gmp_init(2)),             // 2.0
    gmp_mul($ONE, gmp_init(-5)),            // -5.0
    gmp_div_q($LN2, gmp_init(2)),           // ln(2)/2
    gmp_div_q($ONE, gmp_init(10)),          // 0.1 (truncated)
    gmp_init('12345678901234567890'),       // small-ish positive
    gmp_init('-98765432109876543210'),      // small-ish negative
];

$ln_q_cases = [
    $ONE,                                   // ln(1) = 0
    gmp_mul($ONE, 2),                       // ln(2) = LN2
    gmp_mul($ONE, 3),
    gmp_mul($ONE, 10),
    $C['HALF'],                             // ln(0.5) = -LN2
    gmp_div_q($ONE, gmp_init(10)),          // 0.1
    gmp_div_q($ONE, gmp_init(1000)),        // 0.001
    gmp_mul($ONE, gmp_init(1000000)),       // 1e6
];

$mul_q_cases = [
    [$ONE, $ONE],                                              // 1·1 = 1
    [$ONE, $C['HALF']],                                        // 1·0.5 = 0.5
    [$LN2, $LN2],
    [gmp_mul($ONE, 3), $ONE],
    [gmp_neg($ONE), $C['HALF']],
];

$div_q_cases = [
    [$ONE, $ONE],
    [$ONE, gmp_mul($ONE, 2)],                                  // 1/2 = 0.5
    [$LN2, $ONE],
    [gmp_mul($ONE, 7), gmp_mul($ONE, 3)],                      // 7/3
    [gmp_neg($ONE), gmp_mul($ONE, 4)],
];

// ---- LMSR vectors (int milli-VIZ) ----
$lmsr_cost_cases = [
    [[0,0],          100000],
    [[0,0,0],        100000],
    [[10000,0,0],    100000],
    [[1000,2000,3000], 5000],
    [[50000,10000], 20000],
    [[5000,5000,5000,5000], 200000],
    [[1,2,3,4,5,6,7,8,9,10], 100000],
    [[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0], 100000],         // N=16
    [[1000000,0],    50000],
    [[2500,2500],    5000],
];

$lmsr_prices_cases = $lmsr_cost_cases;

$lmsr_buy_cost_cases = [
    [[1000,2000,3000], 5000, 0, 1000],
    [[1000,2000,3000], 5000, 1, 1000],
    [[0,0,0],          8000, 0, 5000],
    [[0,0,0],          8000, 2, 50000],
    [[50000,10000],    20000, 0, 1000],
    [[50000,10000],    20000, 1, 100000],
];

$lmsr_sell_return_cases = [
    [[10000,5000,2000], 8000, 0, 1000],
    [[10000,5000,2000], 8000, 1, 2500],
    [[50000,10000],     20000, 0, 25000],
];

$lmsr_tokens_for_amount_cases = [
    [[1000,2000,3000], 5000,  0, 10000],
    [[0,0,0],          8000,  1, 50000],
    [[50000,10000],    20000, 0, 30000],
    [[0,0],            10000, 0, 1000],
    [[0,0],            10000, 0, 100000],
    [[1000000,0],      50000, 1, 50000],
];

$lmsr_b_from_liq_cases = [
    [100000,  2],
    [100000,  3],
    [100000,  4],
    [100000, 10],
    [100000, 16],
    [1000000, 5],
    [1234567, 7],
];

// ---- Build JSON ----
$out = [
    'version'            => 1,
    'spec'               => 'docs/lmsr-fixed-point-spec.md',
    'generated_by'       => 'module/lmsr_fixed.php (PHP+GMP, normative)',
    'numbers_are_decimal_strings' => true,
    'note'               => 'All numeric values, including Q96 intermediates, are decimal strings. Equality is exact (===); no tolerance is permitted.',
    'constants' => [
        'ONE'              => gstr($ONE),
        'HALF'             => gstr($C['HALF']),
        'LN2'              => gstr($LN2),
        'EXP_DOMAIN_LIMIT' => gstr($C['EXP_DOMAIN_LIMIT']),
        'MAX_B'            => gstr($C['MAX_B']),
        'MAX_Q'            => gstr($C['MAX_Q']),
    ],
    'mul_q' => array_map(function($p){
        return [
            'a'        => gstr($p[0]),
            'b'        => gstr($p[1]),
            'expected' => gstr(_mul_q($p[0], $p[1])),
        ];
    }, $mul_q_cases),
    'div_q' => array_map(function($p){
        return [
            'a'        => gstr($p[0]),
            'b'        => gstr($p[1]),
            'expected' => gstr(_div_q($p[0], $p[1])),
        ];
    }, $div_q_cases),
    'exp_q' => array_map(function($x){
        return [ 'x' => gstr($x), 'expected' => gstr(_exp_q($x)) ];
    }, $exp_q_cases),
    'ln_q' => array_map(function($x){
        return [ 'x' => gstr($x), 'expected' => gstr(_ln_q($x)) ];
    }, $ln_q_cases),
    'lmsr_cost' => array_map(function($c){
        return [ 'q' => $c[0], 'b' => $c[1], 'expected' => lmsr_cost($c[0], $c[1]) ];
    }, $lmsr_cost_cases),
    'lmsr_prices' => array_map(function($c){
        return [ 'q' => $c[0], 'b' => $c[1], 'expected' => lmsr_prices($c[0], $c[1]) ];
    }, $lmsr_prices_cases),
    'lmsr_buy_cost' => array_map(function($c){
        return [
            'q' => $c[0], 'b' => $c[1], 'i' => $c[2], 'delta' => $c[3],
            'expected' => lmsr_buy_cost($c[0], $c[1], $c[2], $c[3]),
        ];
    }, $lmsr_buy_cost_cases),
    'lmsr_sell_return' => array_map(function($c){
        return [
            'q' => $c[0], 'b' => $c[1], 'i' => $c[2], 'delta' => $c[3],
            'expected' => lmsr_sell_return($c[0], $c[1], $c[2], $c[3]),
        ];
    }, $lmsr_sell_return_cases),
    'lmsr_tokens_for_amount' => array_map(function($c){
        return [
            'q' => $c[0], 'b' => $c[1], 'i' => $c[2], 'amount' => $c[3],
            'expected' => lmsr_tokens_for_amount($c[0], $c[1], $c[2], $c[3]),
        ];
    }, $lmsr_tokens_for_amount_cases),
    'lmsr_b_from_liquidity' => array_map(function($c){
        return [ 'liquidity' => $c[0], 'n' => $c[1], 'expected' => lmsr_b_from_liquidity($c[0], $c[1]) ];
    }, $lmsr_b_from_liq_cases),
];

$path = __DIR__ . '/lmsr_fixed_vectors.json';
$json = json_encode($out, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n";
file_put_contents($path, $json);
fwrite(STDERR, "wrote " . strlen($json) . " bytes to $path\n");
