# PM replay cases

Standalone replay programs used to verify the PR #124 review findings and the fix round.
They are **not** part of the CMake build and are not run by CI — each one is a `main()` that
replays a concrete money path and prints the numbers, so a claim about the PM math can be checked
in about five seconds without building a node.

## Why they run without a chain build

`libraries/chain/pm/parimutuel.cpp` and `libraries/chain/pm/leverage.cpp` are deliberately free of
database and chainbase types — their only dependency is `fc::uint128_t`. So the money math links on
its own, against the real consensus source, with no libfc and no node.

`fc_u128_shim.cpp` supplies the only two out-of-line `fc::uint128_t` members those files reference
(`operator*=`, `operator/=`) using native `unsigned __int128`. fc's real `operator/=` delegates to
`boost::multiprecision::uint128_t` truncating division and `operator*=` is a wrapping 128-bit
multiply (`thirdparty/fc/src/uint128_t.cpp:224,257`) — both bit-identical to the native ops, so
truncation behaves exactly as it does in consensus.

The cases that replay *evaluator* transitions (`pm_place_bet`, `pm_cancel_bet`,
`pm_add_liquidity`, `pm_withdraw_liquidity`) model them inline rather than linking
`pm_evaluator.cpp`, which does need the database. Each such model is transcribed from the evaluator
and cites its line numbers; they preserve the truncating `uint128` division, `share_type`'s
`int64_t` width, and the `k = reserve_a × reserve_b` rebuild.

## Build & run

```sh
tests/pm/replay/build.sh              # all cases -> tests/pm/replay/out/
tests/pm/replay/build.sh t12_chain    # just one
out/t12_chain
```

`BOOST_INC` and `CXX` are overridable. Building from a git worktree needs
`FC_INC=<main checkout>/thirdparty/fc/include`, since a worktree carries no submodules.

## Cases

Round 1 — against PR head `84b502c`:

| file | finding | what it shows |
| --- | --- | --- |
| `t1-parimutuel-test.patch` | B3 | 2 extra Boost.Test cases for `tests/pm/parimutuel_test.cpp`; `negative_forfeit_pool_never_mints` failed on `84b502c` (payout `4611686018427388654`) |
| `dump.cpp` | B3 | payout figures for 1 / 2-equal / 2-unequal winners, plus a positive control |
| `t2_reach.cpp` | B3 | `curve_residual < 0` on every profitable leveraged close |
| `t3_lp.cpp` | B4 | reserves and `k` never shrank on LP withdraw |
| `t3b_loan.cpp` | B4 | `max_leverage_loan` does *not* grow with phantom depth — hypothesis rejected |
| `t3c_weight.cpp` | B4 | the real money path: +29.6% settlement claim weight per VIZ, free |
| `t4_cancel.cpp` | B6 | `reserve_a` = −58 824, `k` 1e12 → ~4.8e25 |
| `t5_threshold.cpp` | B3 | detonation boundary: `forfeit_pool = -9600` fine, `-9601` gives `4611686018427487903` |
| `t6_cycle.cpp` | B4/B5 | the 9-step bet/cancel + LP cycle |

Round 2 — against the fix commits (`172d87c` → `b06bdbf` → `26fd26a` → `f697e5c`):

| file | target | what it shows |
| --- | --- | --- |
| `t7_fixed.cpp` | B4/B5 fix | exploit gone (+2.69% → +0.00%), phantom depth 0, no ratchet over 10 cycles; but the round trip is not exact under truncation |
| `t8_shrink.cpp` | B4 fix regression | the residual excess is bet-splitting truncation, not the LP cycle; and a legal LP withdrawal between bet and cancel drove `reserve_a` to −25 000 (now refused by `b06bdbf`) |
| `t9_mag.cpp` | B4 fix residual | sizes the truncation drift: ≤1 satoshi per round trip, but unbounded — 500 cycles decay a 100-VIZ stake's weight 62 501 → 62 431 |
| `t10_conserve.cpp` | B3 fix | the `winners_pool < 0 → 0` floor emits exactly `\|winners_pool\|`; 1163/1988 swept settlements emit. Checks the contract stated in `parimutuel.hpp:11-13` |
| `t11_cancel_move.cpp` | B6 fix | the guard refuses `t8`'s detonation, but a *passing* cancel on a moved curve still rebuilds `k` wrong — an opposing bet is enough, no LP op needed. 4752/5400 triples inflate the next claim, 0 deflate |
| `t12_chain.cpp` | B6 residual | bet A / bet B / cancel A / bet A → +64.06% claim weight for the same stake, compounding to +75.79% over 5 rounds; final reserves bit-identical to honest |
| `t12b_asym.cpp` | same, asymmetric | 65 890 chains, 0 adverse, best +10 202% |
| `t12c_trace.cpp` | same, worst case | 1000 at risk → weight 102 093 vs 991 honest (103×); market `reserve_b` 105 500 → 3 407 |
| `t13_penalty.cpp` | B9 wiring | `pm_max_time_penalty` is unbounded in `chain_properties_pm::validate()`; at 2e6 a winner's payout goes to −50 000 while `lp_bonus` holds 200 000 of 150 000 available |

Round 3 — against the F1/F2/F3 fix commits (`93e43e7` → `5ff694e` → `f3ff915` → `a01016b`):

| file | target | what it shows |
| --- | --- | --- |
| `t14_f2_ledger.cpp` | F2 fix + F1 fix | F2 holds: the `t12` chain now costs 27 457 instead of being free and `k` is invariant under the same truncation convention as `place_bet`. F1 does not: `compute_settlement` never assigns `settle_result::uncovered`, so `5ff694e`'s LP charge is dead code — and the curve-priced cancel makes a negative `forfeit_pool` reachable with no leverage, emitting up to 138 463 on a 200 000 market |

Round 4 — early-exit / deferred-claim settlement pass (against head `dd6c5d1`):

| file | target | what it shows |
| --- | --- | --- |
| `t15_early_exit_headroom.cpp` | `499246e9` (headroom clamp) + F1/#300 conservation | The early-exit deferred-claim pass had NO test coverage. Models the settle-pass verbatim (`pm_evaluator.cpp:647-700`) and settles through the real `compute_settlement`. **A:** with the bucket UNCLAMPED, the default 33% cap + a valid 90%-fee market overdraws the pot → `uncovered = 23 000` charged to LP principal, reachable with default params. **B:** the real clamped code caps the bucket to headroom (33 000 → 10 000), `uncovered = 0`, ledger balances. **C:** a 1 680-combination sweep of valid (fees, cap, forfeit, claim) confirms the clamp is solvency-neutral — 0 unbalanced ledgers and 0 claim-induced `uncovered` regressions vs the no-claim baseline. The 400 `uncovered_hits` are the pre-existing negative-`forfeit_pool` case (the F1 LP-charge target), not the claim path. Exit code 0 on pass. |

`t10_conserve`, `t13_penalty` and `t14_f2_ledger` re-run unchanged against this head; `t15` is new.

## Live corroboration

`prediction_market_api` on testnet: market **19** carried `forfeit_pool = -69227`, reconciling
exactly from its three leverage positions (−88 579 / +19 352 / 0). Settlement zeroes the field
(`pm_evaluator.cpp:411,473`), so state cannot rule out earlier occurrences.
