# Onix Prediction-Market — VIZ DLT Implementation Bundle

Self-contained handoff package for the **C++ VIZ DLT plugin** that adds prediction markets (binary CPMM + multi-outcome LMSR) to the VIZ chain. Everything you need to start building consensus code is in this directory; nothing references the parent forecaster repo.

---

## Read order

1. **[viz-dlt-prediction-market-protocol-spec.md](viz-dlt-prediction-market-protocol-spec.md)** — the protocol spec.
   - §1 ChainBase objects (`pm_oracle`, `pm_market`, `pm_outcome`, `pm_bet`, `pm_liquidity`, `pm_commit`, `pm_dispute`, `pm_dispute_vote`, lazy pool)
   - §2 Object type registry additions
   - §3 Regular operations (op IDs 64..)
   - §4 Virtual operations
   - §5 Chain properties added to the median-vote set
   - §6 Settlement (binary + multi parimutuel)
   - §7 Open issues — **read carefully**, several still need a decision before code can land:
     - §7.2 (LMSR float determinism) — **RESOLVED** by `lmsr-fixed-point-spec.md`
     - §7.1, 7.3, 7.4, 7.5, 7.6, 7.7, 7.10, 7.11, 7.12 — still open, see "Status of open items" below.

2. **[lmsr-fixed-point-spec.md](lmsr-fixed-point-spec.md)** — frozen Q96 fixed-point LMSR algorithm (`exp_q`, `ln_q`, `lse_q`, `lmsr_*`). §6 has the C++ implementation guide (use `boost::multiprecision::int256_t`, no `-ffast-math`, frozen iteration counts). The C++ port MUST reproduce every value in `reference/lmsr_fixed_vectors.json` with strict equality.

3. **[parimutuel-settlement.md](parimutuel-settlement.md)** — unified parimutuel settlement applied to both binary CPMM (weights from reserves) and multi LMSR (weights = bought tokens). One `settle()` math, two market types.

4. **[batch-commit-reveal.md](batch-commit-reveal.md)** — Phase-2 anti-MEV path (`pm_commit_object`, batch settlement). Not required for binary-only Phase 1.

5. **[lazy-pool-properties.md](lazy-pool-properties.md)** — Phase-2 lazy liquidity pool: passive LPs auto-allocate to high-volume markets.

6. **[chain-properties-governance.md](chain-properties-governance.md)** — full list of ~25 PM chain properties added to the validator median set (caps, floors, fees, periods).

7. **[security-threat-model.md](security-threat-model.md)** — attacker scenarios, oracle abuse, dispute griefing, MEV, etc. Use it to derive negative-path tests.

---

## Reference implementations (in `reference/`)

The PHP and JS implementations are **normative**: the C++ port must produce bit-identical outputs.

| File | Role |
|---|---|
| `lmsr_fixed.php` | PHP+GMP reference — source of truth for the test vectors |
| `market_math.js` | JavaScript BigInt port — independent re-implementation that confirms PHP determinism |
| `lmsr_fixed_vectors.json` | Frozen test vectors (Q96 primitives + public LMSR API). **C++ must pass every one with strict equality.** |
| `lmsr_fixed_vectors_verify.php` | Asserts `lmsr_fixed.php` matches the JSON. `php reference/lmsr_fixed_vectors_verify.php` |
| `lmsr_fixed_vectors_verify.js` | Same for JS. `node reference/lmsr_fixed_vectors_verify.js` |
| `lmsr_vectors_generate.php` | Regenerates the JSON. Re-run only when the spec changes (bump `version`). |
| `lmsr_fixed_smoke.php` | 24 invariants (`exp(LN2) = 2·ONE` etc.) — handy sanity gate. |

### Quick verify

```bash
# PHP (needs gmp extension)
php reference/lmsr_fixed_vectors_verify.php
# expected: PHP verifier: 77 / 77 passed

# Node
node reference/lmsr_fixed_vectors_verify.js
# expected: JS verifier: 74 / 74 passed
```

### Acceptance criterion for the C++ port

```cpp
// Pseudocode for the C++ unit test:
auto V = parse_json("reference/lmsr_fixed_vectors.json");
for (auto& t : V["lmsr_cost"])  REQUIRE(lmsr_cost(t.q, t.b) == t.expected);
for (auto& t : V["lmsr_prices"]) REQUIRE(lmsr_prices(t.q, t.b) == t.expected);
// ...same for buy_cost, sell_return, tokens_for_amount, b_from_liquidity,
//                exp_q, ln_q, mul_q, div_q.
```
**No tolerance.** Any deviation = fork risk.

---

## Status of open items in §7 (what still needs a product decision)

| § | Topic | Required decision |
|---|---|---|
| 7.1 | Chain-property surface | Pick (a) median-vote all / (b) median-vote core + hardfork rest / (c) `pm_params_object` |
| 7.3 | Batch / commit-reveal ordering | Define exact in-block op ordering and per-block caps |
| 7.4 | Committee dispute voting | Quorum policy; vote-storage cap; integer tally formula; commit-reveal for votes? |
| 7.5 | Locked-funds custody | Pick (a) explicit fields on objects (recommended) / (b) escrow subsystem |
| 7.6 | Time-driven processing | Per-block cap N + fairness rule |
| 7.7 | Bandwidth cost class | Reuse generic class, or add `pm_operations_cost_*` |
| 7.10 | Dust routing | Where remainders go (committee fund? next round? oracle?) |
| 7.11 | LMSR sell churn | Anti-churn argument or per-block sell cap |
| 7.12 | Byte-length caps | Freeze `MAX_PM_*` table — **consensus-mechanical, not anti-spam** |

§§7.2, 7.8, 7.9 are either resolved or client-side / standard hardfork hygiene.

---

## Phasing (recap of §8 of the protocol spec)

- **Phase 1 (binary-only, integer-safe):** all objects, oracle lifecycle, create/accept, instant bet, liquidity, resolve, both dispute modes (oracle + committee), auto-payout, missed-resolution penalty. **No LMSR**, **no batch/commit-reveal**. Ships a usable consensus market with no §7.2-class risk.
- **Phase 2:** wire in the deterministic LMSR (per `lmsr-fixed-point-spec.md`) → multi markets; add batch + commit-reveal (`batch-commit-reveal.md`); lazy pool (`lazy-pool-properties.md`).
- **Phase 3:** shared/category liquidity, automated data oracles, advanced governance of PM params.

Recommendation: build Phase 1 to mainnet first, freeze, then add Phase 2 behind a separate hardfork.

---

## Off-chain reference (informational)

A working off-chain prototype (PHP backend + Telegram-WebApp frontend + the same `lmsr_fixed.php` and `market_math.js`) exists at the parent `forecaster/` repo. It is **not** required to read it, but it can serve as a behavioural oracle: any edge case you're unsure about, run the same scenario through the prototype and compare.
