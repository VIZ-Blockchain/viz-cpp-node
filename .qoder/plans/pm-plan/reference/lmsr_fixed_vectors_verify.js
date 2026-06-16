/*
 * tests/lmsr_fixed_vectors_verify.js — asserts that the JS implementation
 * (market_math.js, BigInt) reproduces every value in
 * tests/lmsr_fixed_vectors.json with strict equality.
 *
 * Run:    node tests/lmsr_fixed_vectors_verify.js
 * Exit:   0 = all pass, 1 = at least one mismatch
 */
'use strict';

var path = require('path');
var fs   = require('fs');
var M    = require(path.join(__dirname, 'market_math.js'));

var vectorsPath = path.join(__dirname, 'lmsr_fixed_vectors.json');
if (!fs.existsSync(vectorsPath)) {
	console.error('[FAIL] missing ' + vectorsPath + ' — run `php tests/lmsr_vectors_generate.php` first');
	process.exit(1);
}
var V = JSON.parse(fs.readFileSync(vectorsPath, 'utf8'));

var pass = 0, fail = 0;
function check(label, expected, actual) {
	var ok;
	if (Array.isArray(expected) && Array.isArray(actual)) {
		ok = expected.length === actual.length;
		for (var k = 0; ok && k < expected.length; k++) {
			ok = String(expected[k]) === String(actual[k]);
		}
	} else {
		ok = String(expected) === String(actual);
	}
	if (ok) { pass++; return; }
	fail++;
	console.log('  FAIL  ' + label);
	console.log('    expected: ' + JSON.stringify(expected));
	console.log('    actual:   ' + (typeof actual === 'bigint' ? actual.toString() : JSON.stringify(actual)));
}

// ---- constants ----
var Q = M._Q96;
check('const ONE',  V.constants.ONE,  Q.ONE.toString());
check('const HALF', V.constants.HALF, Q.HALF.toString());
check('const LN2',  V.constants.LN2,  Q.LN2.toString());

// ---- primitives ----
V.mul_q.forEach(function (t, i) {
	check('mul_q[' + i + ']', t.expected, Q.mul_q(BigInt(t.a), BigInt(t.b)).toString());
});
V.div_q.forEach(function (t, i) {
	check('div_q[' + i + ']', t.expected, Q.div_q(BigInt(t.a), BigInt(t.b)).toString());
});
V.exp_q.forEach(function (t, i) {
	check('exp_q[' + i + '] x=' + t.x, t.expected, Q.exp_q(BigInt(t.x)).toString());
});
V.ln_q.forEach(function (t, i) {
	check('ln_q[' + i + '] x=' + t.x, t.expected, Q.ln_q(BigInt(t.x)).toString());
});

// ---- public API ----
V.lmsr_cost.forEach(function (t, i) {
	check('lmsr_cost[' + i + ']', t.expected, M.lmsr_cost(t.q, t.b));
});
V.lmsr_prices.forEach(function (t, i) {
	check('lmsr_prices[' + i + ']', t.expected, M.lmsr_prices(t.q, t.b));
});
V.lmsr_buy_cost.forEach(function (t, i) {
	check('lmsr_buy_cost[' + i + ']', t.expected, M.lmsr_buy_cost(t.q, t.b, t.i, t.delta));
});
V.lmsr_sell_return.forEach(function (t, i) {
	check('lmsr_sell_return[' + i + ']', t.expected, M.lmsr_sell_return(t.q, t.b, t.i, t.delta));
});
V.lmsr_tokens_for_amount.forEach(function (t, i) {
	check('lmsr_tokens_for_amount[' + i + ']', t.expected, M.lmsr_tokens_for_amount(t.q, t.b, t.i, t.amount));
});
V.lmsr_b_from_liquidity.forEach(function (t, i) {
	check('lmsr_b_from_liquidity[' + i + ']', t.expected, M.lmsr_b_from_liquidity(t.liquidity, t.n));
});

var total = pass + fail;
console.log('\nJS verifier: ' + pass + ' / ' + total + ' passed' + (fail ? '  (' + fail + ' FAILED)' : ''));
process.exit(fail ? 1 : 0);
