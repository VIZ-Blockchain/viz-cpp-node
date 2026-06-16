/*
 * market_math.js — pure market math for Onix prediction markets.
 *
 * All amounts are integers in milli-VIZ (precision 1000). LMSR pricing is
 * bit-deterministic via Q96 fixed-point BigInt arithmetic: this file is a
 * 1:1 mirror of module/lmsr_fixed.php (PHP+GMP) and the future C++ VIZ DLT
 * plugin. Every implementation MUST produce identical outputs.
 *
 * Two market types, ONE settlement model: the AMM assigns weights
 * (CPMM for binary, LMSR for multi) and losers fund winners pro-rata.
 *
 * Loaded as a browser global (before app.js) AND as a CommonJS module (Node tests).
 *
 * @see docs/lmsr-fixed-point-spec.md
 */
(function (root) {
	'use strict';

	// ============================================================
	// Constants — frozen by docs/lmsr-fixed-point-spec.md
	// ============================================================
	var LMSR_PRECISION       = 1000;        // 1 VIZ = 1000 milli-VIZ
	var LMSR_PRICE_PRECISION = 1000000;     // price ×10^6, 1e6 = 100%

	// Q96 anchors as BigInt
	var ONE              = 1n << 96n;                                                 // 2^96
	var HALF             = 1n << 95n;                                                 // 2^95
	var LN2              = 0xB17217F7D1CF79ABC9E3B398n;                                // ⌊ln 2 · 2^96⌋
	var NEG_LN2          = -LN2;
	var EXP_DOMAIN_LIMIT = 200n * ONE;                                                // §3.3 cutoff
	var EXP_INPUT_LIMIT  = 130n * ONE;                                                // §3.1 hard cap
	var MAX_B            = 1n << 53n;
	var MAX_Q            = 1n << 53n;
	var THOUSAND         = 1000n;
	var PRICE_PRECISION  = 1000000n;

	// ============================================================
	// Q96 primitives (§2)
	// JS BigInt: `/` truncates toward 0; `>>` is arithmetic right shift
	// (rounds toward -∞ for negatives). These are the spec-mandated rules.
	// ============================================================

	function mul_q(a, b) {
		// (a · b) >> 96, ASR — matches §3.5
		return (a * b) >> 96n;
	}

	function div_q(a, b) {
		// (a << 96) / b, truncated toward 0
		return (a << 96n) / b;
	}

	function trunc_div(a, b) {
		// truncation toward zero. JS BigInt `/` already truncates, BUT for negative
		// dividend with negative remainder it returns the truncating quotient. To be
		// identical to PHP gmp_div_q(_, _, GMP_ROUND_ZERO), we wrap.
		return a / b;
	}

	function isOdd(n) {
		// works for negative bigints too: parity is invariant under sign
		return ((n % 2n) !== 0n);
	}

	function abs(n) { return n < 0n ? -n : n; }

	// MSB position (0-indexed) of a positive bigint. Returns -1 for 0.
	// Used by ln_q range reduction. No FP calls.
	function msb(x) {
		if (x <= 0n) return -1;
		return x.toString(2).length - 1;
	}

	// ============================================================
	// Range reduction for exp_q — round-to-nearest-even, §3.5
	// ============================================================
	function round_div_LN2(x) {
		// q = trunc(x / LN2)
		var q = x / LN2;
		var r = x - q * LN2;
		var two_r = r * 2n;

		// Positive side: 2r > LN2 → bump up. 2r == LN2 and q odd → bump up (ties to even).
		if (two_r > LN2 || (two_r === LN2 && isOdd(q))) {
			q = q + 1n;
		}
		// Negative side: 2r < -LN2 → bump down. 2r == -LN2 and q odd → bump down.
		if (two_r < NEG_LN2 || (two_r === NEG_LN2 && isOdd(q))) {
			q = q - 1n;
		}
		return q;
	}

	// ============================================================
	// exp_q — Taylor with range reduction (§3.1)
	// ============================================================
	function exp_q(x) {
		if (abs(x) > EXP_INPUT_LIMIT) {
			throw new Error('LMSR_OVERFLOW: exp_q domain exceeded');
		}
		// 1. Range reduction
		var k = round_div_LN2(x);
		var r = x - k * LN2;

		// 2. Polynomial — exactly 30 iterations
		var acc  = ONE;
		var term = ONE;
		for (var n = 1; n <= 30; n++) {
			// term = mul_q(term, r) / n  (mul_q first; truncated divide by integer n)
			term = mul_q(term, r) / BigInt(n);
			acc  = acc + term;
		}

		// 3. Scale by 2^k
		if (k >= 0n) {
			// Number(k) is safe: |k| ≤ ⌈130/ln 2⌉ ≈ 188
			return acc << k;
		} else {
			return acc >> (-k);
		}
	}

	// ============================================================
	// ln_q — atanh series with range reduction (§3.2)
	// ============================================================
	function ln_q(x) {
		if (x <= 0n) {
			throw new Error('LMSR_DOMAIN_LN: ln_q called with x ≤ 0');
		}
		// 1. p = msb(x) - 96; m = x scaled into [ONE, 2·ONE)
		var p = msb(x) - 96;
		var m;
		if (p >= 0) {
			m = x >> BigInt(p);     // ASR; positive ⇒ truncation
		} else {
			m = x << BigInt(-p);
		}

		// 2. z = (m - ONE) / (m + ONE) in Q96, |z| < 1/3
		var num = m - ONE;
		var den = m + ONE;
		var z   = div_q(num, den);

		// 3. atanh series, 50 iterations
		var z2   = mul_q(z, z);
		var acc  = z;
		var term = z;
		for (var k = 1; k <= 50; k++) {
			term = mul_q(term, z2);
			var denom = BigInt(2 * k + 1);
			acc = acc + (term / denom);   // truncated divide
		}

		// 4. ln(m) = 2 · acc
		var ln_m = acc * 2n;

		// 5. ln(x) = p · LN2 + ln(m)
		return BigInt(p) * LN2 + ln_m;
	}

	// ============================================================
	// log-sum-exp (§3.3)
	// ============================================================
	function lse_q(ratios) {
		if (ratios.length === 0) return 0n;
		var max = ratios[0];
		for (var i = 1; i < ratios.length; i++) {
			if (ratios[i] > max) max = ratios[i];
		}
		var acc = 0n;
		var cutoff = -EXP_DOMAIN_LIMIT;
		for (var j = 0; j < ratios.length; j++) {       // input order, do NOT sort
			var d = ratios[j] - max;
			if (d < cutoff) continue;                    // strict <
			acc = acc + exp_q(d);
		}
		if (acc <= 0n) return max;
		return max + ln_q(acc);
	}

	// ============================================================
	// Domain validation (§1.4)
	// ============================================================
	function validate_domain(q, b) {
		if (q.length < 2 || q.length > 16) {
			throw new Error('LMSR_INVALID_N: outcomes must be 2..16');
		}
		var bb = BigInt(b);
		if (bb <= 0n) throw new Error('LMSR_INVALID_B: b must be > 0');
		if (bb > MAX_B) throw new Error('LMSR_INVALID_B: b exceeds MAX_B (2^53)');
		for (var i = 0; i < q.length; i++) {
			var qi = BigInt(q[i]);
			if (qi < 0n) throw new Error('LMSR_INVALID_Q: q[i] < 0');
			if (qi > MAX_Q) throw new Error('LMSR_INVALID_Q: q[i] exceeds MAX_Q (2^53)');
		}
	}

	function ratios_q96(q, b) {
		var bb = BigInt(b);
		var out = [];
		for (var i = 0; i < q.length; i++) {
			var num = BigInt(q[i]) * ONE;
			out.push(num / bb);                     // truncated toward 0
		}
		return out;
	}

	// ============================================================
	// Public API (§4) — same signatures as module/lmsr_fixed.php
	// Inputs and outputs are Number (int milli-VIZ) for ergonomics; the
	// BigInt math is internal. Caller never sees BigInt.
	// ============================================================

	function lmsr_cost(q, b) {
		if (b <= 0) return 0;
		validate_domain(q, b);
		var ratios = ratios_q96(q, b);
		var lse    = lse_q(ratios);
		var cost   = (BigInt(b) * lse) / ONE;       // truncated toward 0
		return Number(cost);
	}

	function lmsr_prices(q, b) {
		var n = q.length;
		if (b <= 0) return new Array(n).fill(0);
		validate_domain(q, b);
		var ratios = ratios_q96(q, b);
		var max = ratios[0];
		for (var i = 1; i < n; i++) if (ratios[i] > max) max = ratios[i];
		var cutoff = -EXP_DOMAIN_LIMIT;
		var exps = [];
		var sum  = 0n;
		for (var j = 0; j < n; j++) {
			var d = ratios[j] - max;
			if (d < cutoff) {
				exps.push(0n);
			} else {
				var e = exp_q(d);
				exps.push(e);
				sum = sum + e;
			}
		}
		if (sum <= 0n) return new Array(n).fill(0);
		var out = [];
		for (var k = 0; k < n; k++) {
			var pq    = div_q(exps[k], sum);                  // Q96
			var scaled= pq * PRICE_PRECISION;
			var pi    = scaled / ONE;                          // truncated toward 0
			out.push(Number(pi));
		}
		return out;
	}

	function lmsr_price(q, b, i) {
		if (b <= 0 || q[i] === undefined) return 0;
		var p = lmsr_prices(q, b);
		return p[i] || 0;
	}

	function lmsr_buy_cost(q, b, i, delta) {
		if (b <= 0 || delta <= 0 || q[i] === undefined) return 0;
		var qa = q.slice(); qa[i] = qa[i] + delta;
		return Math.max(0, lmsr_cost(qa, b) - lmsr_cost(q, b));
	}

	function lmsr_sell_return(q, b, i, delta) {
		if (b <= 0 || delta <= 0 || q[i] === undefined) return 0;
		if (q[i] - delta < 0) return 0;
		var qa = q.slice(); qa[i] = qa[i] - delta;
		return Math.max(0, lmsr_cost(q, b) - lmsr_cost(qa, b));
	}

	function lmsr_tokens_for_amount(q, b, i, amount) {
		if (b <= 0 || amount <= 0 || q[i] === undefined) return 0;
		validate_domain(q, b);
		var lo = 0;
		var hi = amount * 10;
		var maxq = Number(MAX_Q);
		if (hi > maxq) hi = maxq;
		var best = 0;
		for (var iter = 0; iter < 100; iter++) {
			if (hi - lo <= 1) break;
			var mid = Math.floor((lo + hi) / 2);
			if (lmsr_buy_cost(q, b, i, mid) <= amount) { best = mid; lo = mid; }
			else { hi = mid; }
		}
		return best;
	}

	function lmsr_b_from_liquidity(liquidity, n) {
		if (liquidity <= 0 || n <= 1) return 0;
		if (n > 16) throw new Error('LMSR_INVALID_N');
		var nq = BigInt(n) * ONE;
		var ln_n = ln_q(nq);
		var num = BigInt(liquidity) * ONE;
		return Number(num / ln_n);
	}

	function lmsr_max_loss(b, n) {
		if (b <= 0 || n <= 1) return 0;
		var nq = BigInt(n) * ONE;
		var ln_n = ln_q(nq);
		return Number((BigInt(b) * ln_n) / ONE);
	}

	// Legacy float-style log_sum_exp helper kept for compatibility with any
	// caller that imported it directly. Implemented via the deterministic
	// pipeline so old call sites quietly become deterministic too.
	function lmsr_log_sum_exp(ratios) {
		// expects an array of floats q_j/b — we round-trip through Q96 to honour
		// the spec. Caller MUST migrate to lmsr_cost / lmsr_prices for new code.
		if (!ratios.length) return 0;
		var qratios = ratios.map(function(r){
			// scale float to Q96 with truncation toward 0 (best-effort; not
			// consensus, since the float input is non-deterministic anyway)
			var sign = r < 0 ? -1n : 1n;
			var abs_r = Math.abs(r);
			var int_part = BigInt(Math.trunc(abs_r));
			var frac     = abs_r - Math.trunc(abs_r);
			var frac_q96 = BigInt(Math.floor(frac * 1e15)) * (ONE / 1000000000000000n);
			return sign * (int_part * ONE + frac_q96);
		});
		var lse = lse_q(qratios);
		// return as Number (real units) — non-consensus convenience
		return Number(lse) / Number(ONE);
	}

	// ============================================================
	// Parimutuel settlement (binary AND multi) — INTEGER ONLY
	// Unchanged from the previous market_math.js; already deterministic.
	// ============================================================

	function parimutuel_winners_pool(winning_bets_sum, total_bets_sum, fee_permille) {
		var losers = Math.max(0, (parseInt(total_bets_sum) || 0) - (parseInt(winning_bets_sum) || 0));
		var wp = Math.floor(losers * (1000 - (parseInt(fee_permille) || 0)) / 1000);
		return wp > 0 ? wp : 0;
	}

	function parimutuel_payout(weight, bet_amount, winning_bets_sum, total_bets_sum, total_winning_weight, fee_permille) {
		var wp = parimutuel_winners_pool(winning_bets_sum, total_bets_sum, fee_permille);
		var tw = parseInt(total_winning_weight) || 0;
		var share = (tw > 0) ? Math.floor(wp * (parseInt(weight) || 0) / tw) : 0;
		return { winners_pool: wp, share: share, payout: (parseInt(bet_amount) || 0) + share, profit: share };
	}

	function market_fee_permille(m) {
		return (parseInt(m.oracle_fee) || 0) + (parseInt(m.creator_fee) || 0) + (parseInt(m.liquidity_fee) || 0);
	}

	function parimutuel_estimate(m, outcome, weight, bet_amount, is_new) {
		var fee = market_fee_permille(m), w = parseInt(weight) || 0;
		if (parseInt(m.market_type) == 1) {
			var oc = (m.outcomes || [])[parseInt(outcome)] || {};
			var win_bets = parseInt(oc.bets_sum) || 0;
			var total = parseInt(m.bets_sum) || 0;
			var win_w = (parseInt(oc.weight_sum) || 0) + (is_new ? w : 0);
			return parimutuel_payout(w, bet_amount, win_bets, total, win_w, fee);
		}
		var a_b = parseInt(m.a_bets_sum) || 0, b_b = parseInt(m.b_bets_sum) || 0;
		var win_bets2 = (0 == outcome) ? a_b : b_b;
		var win_w2 = ((0 == outcome) ? (parseInt(m.a_weight_sum) || 0) : (parseInt(m.b_weight_sum) || 0)) + (is_new ? w : 0);
		return parimutuel_payout(w, bet_amount, win_bets2, a_b + b_b, win_w2, fee);
	}

	// ============================================================
	// Exports
	// ============================================================
	var api = {
		LMSR_PRECISION:        LMSR_PRECISION,
		LMSR_PRICE_PRECISION:  LMSR_PRICE_PRECISION,
		// LMSR (deterministic Q96)
		lmsr_cost:               lmsr_cost,
		lmsr_prices:             lmsr_prices,
		lmsr_price:              lmsr_price,
		lmsr_buy_cost:           lmsr_buy_cost,
		lmsr_sell_return:        lmsr_sell_return,
		lmsr_tokens_for_amount:  lmsr_tokens_for_amount,
		lmsr_b_from_liquidity:   lmsr_b_from_liquidity,
		lmsr_max_loss:           lmsr_max_loss,
		lmsr_log_sum_exp:        lmsr_log_sum_exp,    // legacy compat
		// Q96 primitives — exported for tests / advanced clients
		_Q96: { ONE: ONE, HALF: HALF, LN2: LN2, exp_q: exp_q, ln_q: ln_q, mul_q: mul_q, div_q: div_q, msb: msb, lse_q: lse_q },
		// Parimutuel (already integer)
		parimutuel_winners_pool: parimutuel_winners_pool,
		parimutuel_payout:       parimutuel_payout,
		market_fee_permille:     market_fee_permille,
		parimutuel_estimate:     parimutuel_estimate
	};

	if (typeof module !== 'undefined' && module.exports) { module.exports = api; }
	for (var key in api) { root[key] = api[key]; }
	root.MarketMath = api;

})(typeof globalThis !== 'undefined' ? globalThis : (typeof window !== 'undefined' ? window : this));
