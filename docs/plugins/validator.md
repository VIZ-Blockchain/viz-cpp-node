# Validator Plugin

The validator plugin handles block signing and production. It runs a dedicated 250 ms timer loop on its own OS thread, executes a series of safety checks on every tick, and calls `database::generate_block()` when all conditions are met.

**Source:** [plugins/validator/validator.cpp](../../plugins/validator/validator.cpp)

---

## Dependencies

```
chain::plugin, p2p::p2p_plugin, snapshot::snapshot_plugin
```

---

## Configuration

### Block production

| Option | Default | Description |
|--------|---------|-------------|
| `validator` / `-w` | — | Validator account name(s); may be repeated |
| `private-key` | — | WIF private key(s) for signing; may be repeated |
| `emergency-private-key` | — | WIF key for emergency consensus; auto-adds `CHAIN_EMERGENCY_WITNESS_ACCOUNT` to the validator set |
| `enable-stale-production` | `false` | Bypass participation and sync checks (testnet / network recovery only) |
| `required-participation` | `3300` | Minimum validator participation in **basis points** (3300 = 33%) |
| `fork-collision-timeout-blocks` | `21` | Consecutive fork-collision deferrals before forcing production (one full validator round) |

### NTP synchronization

| Option | Default | Description |
|--------|---------|-------------|
| `ntp-server` | `pool.ntp.org`, `time.google.com`, `time.cloudflare.com` | NTP servers; may be repeated |
| `ntp-request-interval` | `900` | Normal sync interval in seconds |
| `ntp-retry-interval` | `300` | Retry interval when no NTP reply |
| `ntp-round-trip-threshold` | `150` | Discard NTP replies with round-trip > N ms |
| `ntp-history-size` | `5` | Moving-average window for NTP delta smoothing |

### Debug

| Option | Default | Description |
|--------|---------|-------------|
| `debug-block-production` | `false` | Enable verbose debug logging in the chain database |

---

## Production Timer

The production loop runs on a **dedicated `production_io_service_`** and its own OS thread — completely separate from the AppBase/P2P shared io_service. This prevents P2P activity (peer disconnects, TLS handshakes, send-queue drains) from delaying the 250 ms timer callback.

**Tick alignment:**
```
timer tick every 250 ms aligned to wall-clock 250 ms boundaries
minimum sleep: 50 ms (to absorb OS jitter)
```

**Look-ahead:** `now = ntp_time + 250 ms` — shifts the production decision forward so the tick at `T_slot - 250 ms` aligns exactly to the slot boundary:
```
Slot at T=6.000s:
  Tick at T=5.750 → now=6.000 → slot matched → produce at lag=0ms
  Tick at T=6.000 → now=6.250 → lag=250ms → still within 500ms threshold
```

**Lag skip:** After a `lag` result, the same slot would re-trigger every tick for the rest of the 3-second slot interval. A guard skips ahead to the next slot boundary so the loop yields CPU instead of spinning.

---

## `maybe_produce_block()` — Safety Check Sequence

The following checks run **in order** on every tick where `slot > 0`.

| # | Check | Failure result |
|---|-------|---------------|
| 1 | DLT sync gate (DLT mode only): `chain().is_syncing()` is false, or this node is the emergency master | `not_synced` |
| 2 | Snapshot pause gate: `snapshot().is_snapshot_in_progress()` is false | `not_synced` |
| 3 | P2P catchup gate: `p2p().is_catching_up_after_pause()` is false | `not_synced` |
| 4 | HF12 three-state safety (see below) | `not_synced` / `low_participation` |
| 5 | `slot = db.get_slot_at_time(now) > 0` | `not_time_yet` |
| 6 | Scheduled validator is in our configured set | `not_my_turn` |
| 7 | Slot not already filled (`scheduled_time > head_block_time`) | `not_time_yet` |
| 8 | Validator's on-chain `signing_key` is non-zero | `not_my_turn` |
| 9 | Private key for `signing_key` is loaded | `no_private_key` |
| 10 | Pre-HF12: participation ≥ threshold | `low_participation` |
| 11 | `|scheduled_time - now| ≤ 500 ms` | `lag` |
| 12 | Fork collision check (see below) | `fork_collision` |
| 13 | Second snapshot pause check (race window) | `not_time_yet` |
| 14 | `db.generate_block()` + `p2p().broadcast_block()` | `produced` |

### HF12 three-state safety (check #4)

**Emergency consensus active:**
- Emergency master (has `emergency-private-key` + committee in schedule): proceeds unconditionally.
- Slave: requires `get_slot_time(1) >= now` (chain not stale) before producing.

**Normal mode (HF12+):**
- Participation ≥ 33%: healthy network; sync check via `get_slot_time(1)`.
- Participation < 33%: distressed network; participation vs `required-participation` threshold applies.
- `enable-stale-production=true`: bypasses both participation and sync checks.

**Pre-HF12:** Simple sync check via `get_slot_time(1)`.

### Fork collision resolution (check #12)

When a competing block exists at `head_block_num + 1`:

1. **Vote-weighted comparison (HF12+):** `compare_fork_branches()` computes total SHARES delegated to each branch. If our branch is heavier, proceed and remove the competing block. If tied or lighter, defer.
2. **Stuck-head timeout:** After `fork-collision-timeout-blocks` consecutive deferrals (default 21 = 63 seconds), the competing block is removed and production resumes. This handles dead-fork blocks from disconnected peers.

**Emergency mode:** Any competing block triggers a defer; the vote-weight path is not taken.

---

## Minority Fork Detection

Before each production attempt (after HF12 safety checks), the plugin walks the last 21 blocks in `fork_db`. If all 21 were produced by the node's own configured validators, the node is isolated on a minority fork.

- **Default action:** Call `p2p().resync_from_lib()` — pop blocks to LIB, reset fork DB, re-initiate P2P sync, reconnect seed nodes. Returns `minority_fork`.
- **With `enable-stale-production=true`:** Log a warning, continue producing.
- **Skipped when:** Emergency consensus is active (committee blocks would always match our configured set). A DLT-specific slave isolation check replaces it in emergency mode.

---

## NTP Stall Detection

If `get_slot_at_time(now)` returns 0 (NTP behind chain time), the `_slot_zero_streak` counter increments:

| Streak | Time | Action |
|--------|------|--------|
| 3 | ~750 ms | Warning |
| 10 | ~2.5 s | Force NTP resync |
| 60 | ~15 s | Prolonged stall warning |
| 120 | ~30 s | Critical error |

The counter resets on any non-zero slot result.

---

## Production Watchdog

If the node has ever produced a block and `should_be_producing` is true (derived from live chain state: participation ≥ 33% or emergency consensus active with our key), but no block has been produced for:
- Emergency master: **60 seconds**
- Regular validator: **180 seconds**

The watchdog fires every 30 seconds and logs a diagnostic. If recovery conditions are met (head advancing in the last 30 s, not syncing, has peer connections, has non-zero signing keys on-chain), it force-clears blocking conditions:

1. Clears `_minority_fork_recovering` flag.
2. Calls `p2p().clear_catchup_flag()` — clears the P2P post-pause catchup flag.
3. Calls `chain().clear_syncing()` — clears the chain sync flag.

Production resumes automatically on the next tick.

---

## `on_block_applied()` — Signal Handler

Connected to `database::applied_block`. Runs for every incoming block.

### Missed slot detection

When `block_num > prev_num + 1` (gap in block stream), the handler determines whether our validator was scheduled for any of the missed slots and logs full diagnostic state (production flags, NTP offset, sync status, signing key status, next slot time).

### Slot hijack detection (DLT emergency mode)

When emergency consensus is active, the emergency master may blank our validator's signing key and produce committee blocks in our scheduled slots. The handler tracks this via `_slot_hijack_count`. Resets when one of our own validators produces a block.

---

## Public API

### `is_witness_scheduled_soon()`

Returns `true` if a locally-controlled validator is scheduled to produce in the next 4 slots (~12 seconds). The snapshot plugin calls this before scheduling a snapshot to defer if production is imminent.

### `is_emergency_master()`

Returns `true` when:
1. `emergency-private-key` is configured (`CHAIN_EMERGENCY_WITNESS_ACCOUNT` in `_witnesses`).
2. The "committee" account is in the current validator schedule.

Only nodes where both conditions hold should produce solo during emergency mode; others are followers and must sync first.

### `is_emergency_key_configured()`

Returns `true` if `emergency-private-key` is configured, regardless of the current schedule. Used in P2P hello messages (`has_emergency_key` field).

### `get_production_diagnostics()`

Returns a compact diagnostic string:
```
validator[skip_flags=0x0 catching_up=0 head=#79881136 last_prod=45s_ago minority_rcv=0 slot_hijacks=0]
```
Included in P2P FORWARD stagnation logs when the node is stuck with no peer ahead.

---

## Key Invariants

1. **Never produce in DLT mode while syncing** — creates blocks on a stale head, causing fork oscillation.
2. **Never produce while snapshot is in progress** — write-lock deadlock.
3. **Never produce if the slot is already filled** — creates a micro-fork.
4. **Emergency master must always produce** — it is the sole block producer; waiting would deadlock.
5. **Slaves must sync before producing in emergency mode** — producing on stale head = minority fork.
6. **Participation < 33% stops production** — network partition guard (overridable).
7. **21 consecutive own-validator blocks → rollback to LIB** — minority fork recovery.
8. **All database reads are fresh** — no state caching; emergency mode can activate/deactivate any block.

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| `not_synced` logs | DLT sync active or snapshot in progress — wait; watchdog will auto-clear if stuck |
| `not_time_yet` repeated | NTP behind chain time; check `_slot_zero_streak` warnings and NTP offset |
| `not_my_turn` on our slot | Signing key blanked on-chain; send `validator_update_operation` to restore |
| `no_private_key` | Config missing `private-key` for the signing key that's registered on-chain |
| `low_participation` | Network participation < 33%; check peer connectivity or set `enable-stale-production=true` |
| `fork_collision` | Competing block at next height; wait for vote-weight resolution or 21-deferral timeout |
| `minority_fork` | Isolated; plugin auto-resyncs to LIB |
| Watchdog fires repeatedly | Sync or catchup flag stuck; watchdog auto-clears if head is advancing |
| `SLOT-HIJACK` logs | Emergency master blanked our key; restore via `validator_update_operation` |

---

See also: [Validator Guard](../node/validator-guard.md), [Fair-DPOS](../consensus/fair-dpos.md), [Emergency Consensus](../consensus/emergency-consensus.md), [Block Processing](../consensus/block-processing.md).
