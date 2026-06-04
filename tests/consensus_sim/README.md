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

- Milestone 2: deterministic harness, single-witness genesis, 7-node smoke,
  determinism replay, failure log.
- Milestone 3: `fault_injector` facade with real equivocation. A shadow
  `simulated_node` is caught up to canonical state at height N-1, a no-op
  `account_metadata_operation` tx is injected into the shadow's pool to
  force a different transaction_merkle_root, and the shadow produces
  block_b at the same `(witness, slot, when)` as prod's block_a. Bus is
  partitioned {prod} vs {everyone else} for that slot, so block_a stays
  with prod and block_b reaches the rest. `chains_consistent` fires at
  the equivocation slot.

## Known limitations

- Slot producer signs every block with the genesis witness; multi-witness
  key rotation is a follow-up. With `CHAIN_NUM_INITIATORS=0` genesis,
  `CHAIN_COMMITTEE_ACCOUNT` owns every slot, so the equivocation scenario
  is unaffected — but heterogeneous-witness scenarios cannot be expressed
  until rotation lands.
- No heal-and-reorg scenario yet: `instruct_equivocation` partitions the
  bus and never heals, so the divergence is detected but not resolved by
  the harness. Reorg behavior under heal is the next fault to script.
