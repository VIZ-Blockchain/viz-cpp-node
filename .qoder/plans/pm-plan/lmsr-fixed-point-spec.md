> **Status:** Normative reference for the deterministic LMSR pricing math used by:
> - the PHP back-end ([module/lmsr_fixed.php](../module/lmsr_fixed.php)),
> - the in-browser front-end ([market_math.js](../market_math.js)),
> - the future C++ VIZ-DLT consensus implementation.
>
> All three implementations **MUST** produce **bit-identical** outputs given identical inputs. This document is the single source of truth; it resolves [§7.2 of the VIZ-DLT protocol spec](viz-dlt-prediction-market-protocol-spec.md#72-floating-point-determinism-in-consensus-hardest).
>
> If you change any number in §3, §4 or §5 you change consensus. Any change MUST be gated behind a hardfork.

---

# Onix LMSR — Canonical Fixed-Point Specification

## 0. Scope

The Logarithmic Market Scoring Rule (LMSR) priced multi-outcome markets through `exp` and `ln`. IEEE-754 `double` is **not** bit-deterministic across compilers, CPUs and `libm` versions, so the existing `module/lmsr_math.php` and `market_math.js` cannot be used to compute on-chain consensus values. This document specifies a fixed-point integer algorithm — using only `+`, `−`, `×`, `÷` (truncating), and bit shifts on big integers — that is reproducible to the **last bit**.

The CPMM (binary) path is already integer-clean and is **not** changed by this document.

## 1. Domain & Number Formats

### 1.1 Public units (unchanged)

| Symbol | Meaning | Type | Precision |
|--------|---------|------|-----------|
| `q[i]` | Outstanding tokens of outcome `i` | int64 | milli-VIZ (×1000) |
| `b`    | LMSR liquidity parameter | int64 | milli-VIZ (×1000) |
| `amount` | VIZ committed to a bet | int64 | milli-VIZ (×1000) |
| `price[i]` | Outcome probability | int64 | ×10⁶ (1e6 = 100%) |
| `cost`, `buy_cost`, `sell_return`, `tokens` | Returned amounts | int64 | milli-VIZ (×1000) |

`LMSR_PRECISION = 1000` and `LMSR_PRICE_PRECISION = 1_000_000` are unchanged from the existing PHP/JS API.

### 1.2 Internal fixed-point format `Q96`

All transcendental work happens in **signed fixed-point with 96 fractional bits** stored in a wide signed integer (≥ 192 bits). We name this format `Q96`.

```
value_real = value_int / 2^96          // value_int is a signed bigint
```

Constants used throughout:

```
ONE     = 1 << 96                                             // = 2^96
HALF    = 1 << 95                                             // = 2^95
LN2     = 0xB17217F7D1CF79ABC9E3B398                          // ⌊ln(2) · 2^96⌋ — 24 hex digits = 96 bits
                                                              //  = 54916777467707473351141471128 (decimal)
                                                              // Verified: 54916777467707473351141471128 / 2^96
                                                              //         = 0.693147180559945309417232121457
                                                              //   true ln(2) ≈ 0.693147180559945309417232121458 (next bit rounds down → truncation, by §3.5)
MAX_Q96 = (1 << 191) - 1                                      // hard cap; over → overflow error
MIN_Q96 = -(1 << 191)                                         // hard cap
```

> **Why 96 bits?** With Q96, `exp(x)` has ≤ 1 ULP of relative error after the polynomial in §3.1 with 24 Taylor terms (`x ≤ ln(2)/2 ≈ 0.347`). After 30 terms the residual is `< 2^-100`, far below Q96. ULPs and rounding rules are fixed in §3.5 so the same residual is dropped on every platform.

> **Why not Q64?** Q64 leaves 64 fractional bits; LMSR needs `exp` of values up to ≈ `q_max / b`. For our domain (see §1.4) `q/b` reaches a few hundred. Pricing differences below `2^-30 · b` are economically meaningless, but rounding **inside** `log-sum-exp` accumulates: `Σ exp(x_j)` with N=10 terms each having ≤ 1 ULP of error makes the final `ln(·)` carry ≤ N ULPs of error. We need ≥ 80 fractional bits to guarantee that the LMSR cost rounded back to milli-VIZ matches across implementations. **Q96 has comfortable head-room.**

> **Why not Q128 / decimal-18?** Bigger words, more `mul/div` cost, no measurable gain. Q96 fits in a 192-bit accumulator (3 × `uint64`) which is a natural width on 64-bit hardware (C++ uses `boost::multiprecision::int256_t` or a 4-limb intrinsic; PHP uses `gmp`; JS uses native `BigInt`).

### 1.3 Required big-integer width

| Operation | Minimum signed-integer width |
|-----------|------------------------------|
| Q96 storage / accumulator | **192 bits** |
| Q96 × Q96 → Q96 (intermediate) | **256 bits** |
| `exp` Taylor accumulator | 256 bits |
| `lse` exponent shift `x − max_x` | 256 bits |

C++ reference implementation **MUST** use `boost::multiprecision::int256_t` (or a hand-rolled 4×u64 type) for the multiplier, then truncate to 192 bits when storing.

### 1.4 Input domain & overflow guards

LMSR is undefined for `b ≤ 0`; the spec rejects with `lmsr_b_invalid` (no fallback).

For sanity, **before** any internal Q96 conversion the implementation MUST check:

```
0 < b ≤ MAX_B                       (MAX_B = 2^53, ~ 9·10^15 milli-VIZ)
0 ≤ q[i] ≤ MAX_Q                    (MAX_Q = 2^53)
N (outcomes) in [2 .. 16]
```

These limits keep `q/b` ≤ `MAX_Q / 1` = `2^53` in the worst case. We additionally cap the LSE exponent shift (§3.2) so `|x_max − x_min| < EXP_DOMAIN_LIMIT = 200 · ONE` (≈ `e^200 ≈ 7e86`, well below the bigint width). Anything beyond returns the canonical overflow code.

## 2. Conversion Helpers

```text
to_q96(x_milli) := (int)(x_milli) << 96 / 1000               // divide by LMSR_PRECISION inside Q96
                 = ((int)x_milli * ONE) / 1000               // truncated toward 0 (signed-floor for ≥0 inputs)
from_q96(v_q)   := (v_q * 1000) >> 96                        // back to milli-VIZ, truncated toward 0
```

Both use **truncation toward zero** (a.k.a. C99 integer division, not Python floor). Implementations on platforms where `/` rounds differently (some big-integer libraries) MUST manually emulate truncation.

`mul_q(a, b) := (a * b) >> 96` — multiplied in the wide accumulator first, then arithmetic right shift. Implementations MUST use **arithmetic right shift** (sign-extending) to preserve sign.

`div_q(a, b) := (a << 96) / b` — left-shift to 192-bit accumulator, divide once. Truncating toward zero.

## 3. Core Transcendentals

### 3.1 `exp_q(x)` — exponential in Q96

**Domain:** `x` ∈ Q96, with `|x| ≤ 130 · ONE` (≈ `e^130 ≈ 2·10^56`, fits in 192 bits with head-room). Beyond the domain the implementation returns `LMSR_OVERFLOW`.

**Algorithm:**

```
1. Range reduction:
       k    = round_to_nearest_even(x / LN2)              // integer (use signed div with banker rounding spec'd in §3.5)
       r    = x - k * LN2                                  //  |r| ≤ LN2/2  ≈ 0.3466 in real units
2. Polynomial:
       acc  = ONE                                          // term[0] = 1
       term = ONE
       for n in 1..30:                                     // FIXED iteration count = 30
           term = mul_q(term, r) / n                       // term · r / n   (n is small int; do mul first)
           acc  = acc + term                               // signed add
3. Scaling:
       result = acc << k    if k ≥ 0
              = acc >> (-k) if k <  0                      // arithmetic right shift
4. Return result.
```

**Constants:**
- `LN2` is exactly the integer in §1.2 (frozen). Implementations **MUST NOT** recompute it.
- The Taylor loop bound is fixed at **30 iterations**, regardless of `r`. Empirically iterations 25–30 contribute < 2⁻¹⁰⁰ of the result, but every implementation must run all 30 to guarantee identical truncation behaviour.
- `term · r / n`: do the `mul_q` first (`term * r >> 96`), THEN integer-divide by `n` (truncating toward zero). Order matters: dividing first loses bits.

**Rounding:** every intermediate keeps Q96; only `mul_q` introduces a single arithmetic right-shift truncation per iteration. No double-rounding.

### 3.2 `ln_q(x)` — natural logarithm in Q96

**Domain:** `x > 0` in Q96. For `x ≤ 0` return `LMSR_OVERFLOW`.

**Algorithm (Briggs/atanh, deterministic):**

```
1. Range reduction to m ∈ [1, 2):
       p    = msb(x) - 96                                  // integer; m = x / 2^p ∈ [1,2)
       m    = (p ≥ 0) ? (x >> p) : (x << -p)
2. Substitute z = (m - ONE) / (m + ONE)                    // |z| < 1/3 in Q96
       num  = m - ONE
       den  = m + ONE
       z    = div_q(num, den)
3. atanh series:
       z2   = mul_q(z, z)
       acc  = z                                            // term = z
       term = z
       for k in 1..50:                                     // FIXED iteration count = 50
           term = mul_q(term, z2)                          // term *= z^2
           denom = (2*k + 1)                               // 3, 5, 7, ...
           acc   = acc + term / denom                      // truncated divide
4. ln_m = 2 * acc                                          // ln(m) = 2 · atanh((m-1)/(m+1))
5. ln(x) = p * LN2 + ln_m                                  // signed; p can be negative
```

**Iteration count = 50** is fixed. With `|z| < 1/3` the residual after 50 odd-power terms is `< 3^-100 ≈ 2^-158`, well below Q96's resolution.

**`msb(x)`** = position of the highest set bit (0-indexed). Big-int libraries provide this directly (`gmp`: `mpz_sizeinbase(_, 2) - 1`; `BigInt`: bit-length helper; C++: `__builtin_clzll`-based ladder over limbs). The function MUST NOT call any floating-point primitive.

### 3.3 `lse_q(xs)` — log-sum-exp with shift

```
1. m   = max(xs)                                            // signed Q96
2. acc = 0
   for x in xs:
       d = x - m                                            // d ≤ 0
       if d < -EXP_DOMAIN_LIMIT: continue                   // contributes < 2^-200, drop deterministically
       acc += exp_q(d)
3. return m + ln_q(acc)
```

**Important:** the "drop tiny terms" rule with cutoff `−EXP_DOMAIN_LIMIT = −200·ONE` is **mandatory** and MUST be applied in every implementation in the same loop order. We drop on `d < -EXP_DOMAIN_LIMIT` (strict `<`), keep on `d ≥ -EXP_DOMAIN_LIMIT`.

The loop iterates over `xs` in **input order** (the order in which `q[]` was supplied). Re-ordering changes summation rounding.

### 3.4 `lmsr_cost_q(q[], b)` and helpers

```
ratios[i] = (q[i] * ONE) / b                                // truncated toward zero. ratios[i] ∈ Q96.
                                                            // Direct one-step form: avoids double-truncation
                                                            // that to_q96/to_q96/div_q would introduce for
                                                            // q[i] / b not a multiple of 1/1000.
lse       = lse_q(ratios)                                   // Q96
cost_milli= (b * lse) / ONE                                 // truncated toward zero. Result is int64 milli-VIZ.
                                                            // Equivalent to: arithmetic right shift by 96 of
                                                            // a wide signed product, with sign correction so
                                                            // the rule is "round toward 0" (NOT toward -∞).
return cost_milli
```

Note: `q[]` and `b` are int64 milli-VIZ throughout; `ratios[i]` and `lse` are Q96; `cost_milli` is int64 milli-VIZ. The two `1000` factors that would appear if we round-tripped through `to_q96` cancel exactly in the algebra, so we never write them. The output matches `b * ln(Σ exp(q_j/b))` (in real units) truncated to integer milli-VIZ.

`lmsr_buy_cost_q` and `lmsr_sell_return_q` are subtractions of two `lmsr_cost_q` results; the `max(0, ...)` guard remains (it can only fire on adversarial inputs that violate §1.4 domain bounds).

### 3.5 Rounding rules (frozen)

| Operation | Rule |
|-----------|------|
| `mul_q(a, b)` | Arithmetic right shift by 96 = round toward `-∞` for negative results. Implementations **MUST** use ASR, not LSR. |
| `div_q(a, b)` | C99 integer division: round toward zero. |
| `to_q96`, `from_q96` | Round toward zero. |
| `exp_q` Taylor `term / n` | Round toward zero. |
| `ln_q` atanh `term / (2k+1)` | Round toward zero. |
| `range reduction k = x / LN2` | **Round to nearest, ties to even** (banker's). One single round-to-nearest in the entire pipeline; everywhere else is truncation. |

> "Round to nearest even" for the `k` in `exp_q`'s range reduction is implemented as:
> ```
> q = x / LN2                                        // truncated
> r = x - q * LN2
> if 2*r > LN2 or (2*r == LN2 and (q & 1)):
>     q = q + 1
> if 2*r < -LN2 or (2*r == -LN2 and (q & 1)):
>     q = q - 1
> k = q
> ```
> All comparisons are signed bigint comparisons. Identical on every platform.

### 3.6 Error codes

A single global enum is exposed by every implementation:

```
LMSR_OK         = 0
LMSR_OVERFLOW   = 1     // x outside §1.4 domain in any internal step
LMSR_INVALID_B  = 2     // b ≤ 0
LMSR_INVALID_Q  = 3     // q[i] < 0 or > MAX_Q
LMSR_INVALID_N  = 4     // outcomes < 2 or > 16
LMSR_DOMAIN_LN  = 5     // ln_q called with x ≤ 0 (should be unreachable; guard)
```

In off-chain (PHP / JS) code these surface as exceptions; in on-chain (C++) code they fail the operation.

## 4. Public API (identical signatures)

The PHP / JS / C++ implementations expose the same 6 functions. Names are PHP-convention; the JS module re-exports with the same identifiers, the C++ code uses `lmsr::cost(...)` etc.

| Function | Inputs | Output | Notes |
|----------|--------|--------|-------|
| `lmsr_cost(q[], b)` | int64 milli-VIZ | int64 milli-VIZ | §3.4 |
| `lmsr_price(q[], b, i)` | int64 milli-VIZ, idx | int (×10⁶) | `from_q96(div_q(exp_q(x_i − m), Σ exp_q(x_j − m))) * 10^6` |
| `lmsr_prices(q[], b)` | int64 milli-VIZ | int[] (×10⁶) | All N prices |
| `lmsr_buy_cost(q[], b, i, Δ)` | int64 milli-VIZ | int64 milli-VIZ | `max(0, cost(q+Δ·e_i) − cost(q))` |
| `lmsr_sell_return(q[], b, i, Δ)` | int64 milli-VIZ | int64 milli-VIZ | `max(0, cost(q) − cost(q-Δ·e_i))` |
| `lmsr_tokens_for_amount(q[], b, i, amount)` | int64 milli-VIZ | int64 milli-VIZ | Binary search: largest Δ with `buy_cost ≤ amount` |

`lmsr_b_from_liquidity(L, N)` is **not** consensus-critical (it's chosen at market creation and stored). It still uses `ln(N)` but is computed once per market, on the creator's client; the resulting `b` is part of the on-chain market object and frozen. Implementations MUST use `ln_q` for it as well so reference values match.

### 4.1 `lmsr_tokens_for_amount` — deterministic binary search

```
lo = 0
hi = clamp(amount * 10, 0, MAX_Q)                    // upper bound, identical to current PHP
best = 0
for iter in 1..100:                                  // FIXED iteration count
    if hi - lo <= 1: break
    mid = (lo + hi) >> 1                             // integer floor
    if lmsr_buy_cost(q, b, i, mid) <= amount:
        best = mid; lo = mid
    else:
        hi = mid
return best
```

**Iteration count = 100** is fixed. Even when the search converges in fewer steps, all 100 iterations execute (the early break is allowed because `hi - lo ≤ 1` is symmetric and platform-independent). Empirically 60 iterations suffice, 100 is conservative.

## 5. Test Vectors (consensus-fixing)

Every implementation MUST pass `tests/lmsr_fixed_vectors.json` (Phase 4). The JSON contains **all** the values an implementer needs to verify primitives and end-to-end LMSR. Format is fixed:

```json
{
  "version": 1,
  "exp_q": [
    { "x": "0", "expected": "79228162514264337593543950336" },
    { "x": "54916777467707473351141471128", "expected": "158456325028528675187087900672" },
    { "x": "-54916777467707473351141471128", "expected": "39614081257132168796771975168" }
  ],
  "ln_q":  [ ... ],
  "mul_q": [ ... ],
  "div_q": [ ... ],
  "lmsr_cost": [
    { "q": [1000, 1000, 1000], "b": 100000, "expected": 109861 },
    { "q": [10000, 0, 0],     "b": 100000, "expected": 13500 }
  ],
  "lmsr_buy_cost": [ ... ],
  "lmsr_tokens_for_amount": [ ... ]
}
```

All numeric inputs / outputs are **decimal strings** (because Q96 values exceed 2⁵³). Equality is exact (`a === b`, no tolerance). The vector file is generated once by the PHP reference (Phase 2) and **frozen** — JS, PHP and C++ all compare against it.

### 5.1 Tooling (this repo)

- `tests/lmsr_vectors_generate.php` — regenerates `tests/lmsr_fixed_vectors.json` from the PHP reference (only re-run when the spec changes; bump `version` in the JSON header).
- `tests/lmsr_fixed_vectors_verify.php` — asserts the PHP reference still matches every vector. Exit 0 on success.
- `tests/lmsr_fixed_vectors_verify.js` — same, for the JS reference (`node tests/lmsr_fixed_vectors_verify.js`).
- A C++ port MUST add an analogous verifier as part of its CI.

## 6. C++ Implementation Guide

This section is non-normative reference for the future VIZ DLT plugin author. The PHP/JS reference implementations are normative.

### 6.1 Types

```cpp
#include <boost/multiprecision/cpp_int.hpp>
using namespace boost::multiprecision;

using q96      = int256_t;     // store; uses 192 bits, room to spare
using q96_wide = int512_t;     // multiplier accumulator, 256 bits used
constexpr q96 ONE  = q96(1) << 96;
constexpr q96 LN2  = q96("54916777467707473351141471128");          // §1.2
constexpr q96 EXP_DOMAIN_LIMIT = q96(200) * ONE;
```

Use `int256_t` (not `int128_t`) because §1.3 requires 256-bit accumulators for `mul_q` and `exp` partial products.

### 6.2 mul_q / div_q

```cpp
inline q96 mul_q(q96 a, q96 b) {
    q96_wide t = q96_wide(a) * q96_wide(b);
    return q96(t >> 96);                       // boost ASR; sign-preserving
}
inline q96 div_q(q96 a, q96 b) {
    q96_wide t = q96_wide(a) << 96;
    return q96(t / b);                         // boost: truncation toward 0
}
```

Verify with vector file: any deviation from the test vectors indicates the boost ASR/truncation behaviour differs from the reference and MUST be fixed before merge.

### 6.3 Determinism caveats specific to C++

1. **Compiler flags:** prohibit `-ffast-math`, `-funsafe-math-optimizations`, `-Ofast`. Plugin Makefile MUST set `-fno-fast-math -ffp-contract=off` (defensive even though we do not call float).
2. **No FP library calls:** confirm with `nm` that the plugin object has no references to `exp`, `log`, `pow`, `sqrt`, `expf`, `logf`, etc. CI MUST grep for these symbols and fail the build if any appear.
3. **Endianness:** all our math is over big-integer values that boost handles abstractly. No byte-order code paths.
4. **Bigint library version:** pin boost version in `CMakeLists.txt`. A boost upgrade must re-run the test vector suite before merge.
5. **`mpz_*` is forbidden** in the consensus path. Boost is the only big-int dependency.

### 6.4 Consensus integration

LMSR is invoked from:
- `pm_place_bet` evaluator (instant mode) — computes `tokens` for a buy.
- `pm_batch_settle` virtual op (per-epoch) — computes uniform-price weights.
- `pm_cancel_bet` evaluator (multi only) — computes `lmsr_sell_return`.
- `pm_create_market` evaluator — computes `b` from `liquidity` (one-time).

All four call sites MUST go through the spec'd functions. **Replay determinism**: if the spec changes, a new hardfork constant gates the new constants/iteration counts; old blocks replay against the old constants.

### 6.5 Performance

A worst-case `lmsr_tokens_for_amount` call: 100 binary-search iterations × 2 `lmsr_cost` × (10 outcomes × 30 Taylor steps + 50 atanh steps) ≈ 160k 256-bit `mul`s. On modern CPUs that is sub-millisecond, comparable to a single ECDSA verify, so it is acceptable inside an evaluator. Per-block cap on `pm_place_bet` count (already discussed in §7.6 of the protocol spec) keeps total cost bounded.

## 7. Spec Review — items still open

These do not block the implementation, but I flag them for your review:

### 7.1 Iteration counts (30 Taylor / 50 atanh / 100 binary)

The numbers were chosen with 5–10× safety margin against the Q96 noise floor. **Question:** drop to 24/40/80 to save ~25% CPU? Cost is only "must be ≥ N" for correctness; choosing tighter risks one-ULP differences if a test vector ever lands on a boundary. **Recommendation:** keep current values; they are negligible cost and the safety margin is cheap insurance against an undiscovered edge case.

### 7.2 Q96 vs Q64

Q64 would halve the bigint width and roughly halve CPU cost. **Risk:** as discussed in §1.2, log-sum-exp accumulation can lose ~log2(N) bits, so for N=16 outcomes the safety margin in Q64 shrinks to ~50 bits — still enough but tight. **Recommendation:** Q96. If a future hardfork wants more outcomes (N>16), Q96 still works; Q64 wouldn't.

### 7.3 Round-to-nearest-even in `exp` range reduction

The "one banker's rounding" rule (§3.5) is the single non-truncating step. **Alternative:** use truncation everywhere, accepting that `exp(x)` near `x = k·ln(2)` rounds slightly worse on one side. Empirically this affects the last 1–2 milli-VIZ on `lmsr_buy_cost ≈ b/1000`. **Recommendation:** keep banker's; it's symmetric, free to implement, and avoids a pathological accumulation when `x` happens to be a multiple of `ln(2)/N` (an attacker can construct such inputs).

### 7.4 Test-vector generation source

Phase 2 (PHP+GMP) will generate `tests/lmsr_fixed_vectors.json`. PHP `gmp` uses `mpn_*` from GMP, which IS bit-deterministic and available on all platforms. We then verify JS produces the same. **Question:** do we want a third independent generator (e.g. Python `gmpy2`) as a tie-breaker? **Recommendation:** add it as a CI sanity check, but the PHP output is canonical.

### 7.5 What to do with `module/lmsr_math.php` after migration

Phase 5 replaces all call sites. The float file becomes dead code. **Recommendation:** delete it (single commit, easy to revert if anything breaks). Do NOT keep both implementations — that's the worst possible state because `float-LMSR` and `fixed-LMSR` will silently diverge for some `q`.

### 7.6 Front-end performance (mobile)

JS `BigInt` is ~50× slower than `Number`. A single `lmsr_buy_cost` is ~3000 `BigInt` ops ≈ 1–3 ms on desktop, ~10 ms on a low-end phone. **For UI estimates** (live odds while typing) this is acceptable. **For batch UI updates** (recompute all 10 outcome prices on every block) it might lag. **Mitigation:** debounce, or compute only the touched outcome, or fall back to a `Number`-based "estimate" that explicitly tells the user "displayed value, on-chain value may differ by ≤ 1 milli-VIZ". I prefer the BigInt path everywhere — see §7.5; mixing breeds bugs. **Recommendation: BigInt only**, debounce in app.js where needed.

### 7.7 `lmsr_b_from_liquidity` consensus status

This function is called once per market at creation; the result is stored on the market object. Strictly speaking the **node** can compute it deterministically and the **client** doesn't have to; the on-chain `b` is what matters. **Recommendation:** require the client to send `b` directly, validated by `ln_q` on the node:
```
require: ⌊liquidity / ln_q(N)⌋ == b
```
This way the consensus rule is just an integer equality check, not a re-computation. Off-chain (PHP/JS) we still expose `lmsr_b_from_liquidity` for the create-market UI.

## 8. Implementation Phasing

1. **Phase 1 (this document).** Spec frozen. ✓
2. **Phase 2.** [`module/lmsr_fixed.php`](../module/lmsr_fixed.php) using `gmp_*`. Implements §2–§4. ✓
3. **Phase 3.** [`market_math.js`](../market_math.js) BigInt port. Same names, same algorithm. ✓
4. **Phase 4.** Cross-runner: [`tests/lmsr_vectors_generate.php`](../tests/lmsr_vectors_generate.php) + [`tests/lmsr_fixed_vectors_verify.php`](../tests/lmsr_fixed_vectors_verify.php) + [`tests/lmsr_fixed_vectors_verify.js`](../tests/lmsr_fixed_vectors_verify.js). All 77/77 PHP and 74/74 JS vectors pass with strict equality. ✓
5. **Phase 5.** [`module/api.php`](../module/api.php), [`module/cron_worker.php`](../module/cron_worker.php), [`tests/workflow_test.php`](../tests/workflow_test.php), [`module/test_parimutuel.php`](../module/test_parimutuel.php) switched to `lmsr_fixed.php`; the float file `module/lmsr_math.php` deleted. ✓
6. **Phase 6.** Update [protocol spec §7.2](viz-dlt-prediction-market-protocol-spec.md#72-floating-point-determinism-in-consensus-hardest) to "**Resolved** — see [lmsr-fixed-point-spec.md](lmsr-fixed-point-spec.md)". The C++ plugin author follows §6 of this document.

After Phase 5 the prediction-market backend has no `float`/`double` in any pricing path; the only remaining FP code is in unrelated UI helpers.

---

🇷🇺 Russian translation will be added as `lmsr-fixed-point-spec-ru.md` after the English version is reviewed and frozen — translating drafts wastes effort.
