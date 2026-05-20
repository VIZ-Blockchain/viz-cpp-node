# consensus_sim

Deterministic in-process multi-node consensus harness for VIZ.

## Build

```
cmake -DBUILD_CONSENSUS_TESTS=ON ..
make consensus_sim_tests -j4
```

The harness compiles at `-O1 -g -fsanitize=address,undefined`. Chain code
itself is unchanged.

## Run

```
./tests/consensus_sim/consensus_sim_tests
```

Run a specific suite:

```
./tests/consensus_sim/consensus_sim_tests --run_test=equivocation_suite
./tests/consensus_sim/consensus_sim_tests --run_test=equivocation_suite/seed_sweep_one_hundred
```

The chain code has pre-existing ASan/UBSan findings (a base-pointer delete in
`evaluator_registry`, version/asset alignment) unrelated to the harness. Run
with `ASAN_OPTIONS=new_delete_type_mismatch=0:detect_leaks=0
UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=0` to get past them.

## Coverage

```
cmake -DBUILD_CONSENSUS_TESTS=ON -DWITH_COVERAGE=ON ..
make consensus_sim_tests -j4
./tests/consensus_sim/consensus_sim_tests
make consensus_sim_coverage
open build/coverage/index.html
```

Coverage instruments `graphene_chain`, `graphene_protocol`, and the harness
itself. The report filters to those three trees.

## Reproducing failures

When an invariant violates, the scenario writes
`tests/consensus_sim/failures/<seed>-<scenario>.log` with the seed, scenario
name, config, full event log, final per-node state, and the triggering
report. Re-run the same seed by re-invoking the test — seeds are stored in
the scenario configs and deterministic.

## Determinism

Every scenario is seed-driven. Same seed -> byte-identical event log
(verified by `test_determinism_replay`). If a run flakes, that itself is a
bug — most likely a stray `now()` or a hash-randomized container in chain
code.

## Current state

- 17 test cases across 8 suites; all pass.
- Milestone 2: deterministic harness, single-witness genesis, 7-node smoke,
  determinism replay, failure log.
- Milestone 3 (this commit set): `fault_injector` facade, single-seed +
  100-seed equivocation scenarios, coverage tooling.

## Known limitations

- `fault_injector::instruct_equivocation` ships block_a only; producing a
  second validly-signed block for the same (witness, slot) requires either
  extending `simulated_node` to return full `signed_block` bodies (to
  catch up a shadow node) or constructing a no-op transaction inside
  `fault_injector` and re-signing. Either path is a focused follow-up; the
  invariant plumbing and sweep scaffolding are in place.
- Slot producer signs every block with the genesis witness; multi-witness
  key rotation will land alongside the shadow-block work.
