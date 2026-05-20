# Validator Guard

The `validator_guard` plugin automates signing-key restoration for validator accounts. When a validator's signing key is reset to null (disabling block production), the plugin detects the change and broadcasts a `witness_update_operation` to restore the key — without manual intervention.

---

## When to Use This Plugin

- Your validator account can have its signing key nulled by an emergency consensus master, a security protocol, or manual action.
- Without this plugin you must monitor the on-chain key and restore it by hand before your scheduled slot.
- With this plugin the node watches for null keys every N blocks and restores them automatically.

---

## Enabling the Plugin

```ini
plugin = validator_guard
```

---

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `validator-guard-enabled` | `true` | Enable or disable the plugin globally. |
| `validator-guard-interval` | `20` | Check interval in blocks (~60 s at 3 s/block). |
| `validator-guard-validator` | — | JSON triplet `[name, signing_wif, active_wif]`. Repeatable. |
| `validator-guard-disable` | `5` | Consecutive blocks produced by a single validator before auto-disabling it. `0` = disabled. |

The plugin also reads `enable-stale-production` from the validator plugin config.

### Example

```ini
plugin = validator_guard

# Monitor one validator
validator-guard-validator = ["alice", "5K_SIGNING_WIF", "5K_ACTIVE_WIF"]

# Monitor a second validator
validator-guard-validator = ["alice.backup", "5J_SIGNING_WIF", "5J_ACTIVE_WIF"]

# Check every 10 blocks
validator-guard-interval = 10
```

> **Security:** The active private key is stored in plain text in `config.ini`. Restrict file permissions (`chmod 600 config.ini`) and avoid exposing the file to untrusted processes.

---

## How It Works

### Startup

1. Parses and validates all configured WIF keys.
2. If `enable-stale-production = true`, auto-restore starts disabled (see Safety Guards).
3. After the chain database opens, verifies each configured active key against on-chain authority. Validators whose accounts are not found or whose keys do not match are removed from monitoring with a warning.
4. Runs an immediate check; caches the result to align with the periodic schedule.

### Per-Block Handler

On every block:

1. **Consecutive-block auto-disable**: If a monitored validator produced `validator-guard-disable` consecutive blocks, a `witness_update_operation` with a null key is broadcast to disable it, and the validator is flagged as auto-disabled. Any block by a *different* validator resets all consecutive counters.
2. **Transaction confirmation**: Scans for pending restore transaction IDs in the block. On match, marks the restore confirmed and clears tracking state.
3. **Look-ahead scheduling**: If any monitored validator is scheduled within the next 3 slots, triggers an immediate check so the key can be restored before the slot arrives.
4. **Periodic check**: Otherwise runs the core check every `validator-guard-interval` blocks. While the node is still catching up after startup, checks run every 10 blocks.

### Core Check

Each check (in order):

1. **Stale production guard**: If `enable-stale-production` is active and network participation < 33%, skips all restoration. Auto-clears when participation reaches ≥ 33%.
2. **Sync check**: Skips if head block time is more than 2 block intervals behind wall clock.
3. **Long fork safety**: Skips if LIB is older than 200 seconds.
4. **Expiry cleanup**: Expires stale in-flight restore attempts so they can be retried.
5. **Key check per validator**: Reads the on-chain signing key.
   - Key present → clears pending restore state and auto-disabled flag.
   - Key null + validator was auto-disabled → skips auto-restore (operator must investigate).
   - Key null + no restore in-flight → calls `send_witness_update`.

### Restore Transaction

1. Builds a `witness_update_operation` preserving the current on-chain URL and setting the signing key to the configured public key.
2. Wraps in a `signed_transaction` with 30-second expiration and current head block reference.
3. Signs with the configured active private key.
4. Broadcasts via P2P.
5. Tracks the transaction ID in `_pending_confirmations` to prevent duplicate sends.

---

## Safety Guards

| Guard | Behavior |
|-------|----------|
| **Stale production** | When `enable-stale-production = true`, auto-restore is disabled to avoid broadcasting on a minority fork. Auto-cleared when participation ≥ 33%. |
| **Emergency mode** | During emergency consensus, the stale production guard is bypassed — key restoration may be needed for recovery. |
| **Sync check** | Only runs when the node is synchronized. |
| **Long fork detection** | Skips if LIB is older than 200 seconds. |
| **Authority validation** | Active keys verified against on-chain authority at startup. |
| **Consecutive-block auto-disable** | Automatically nulls the signing key after N consecutive blocks from the same validator. Auto-restore suppressed until the operator manually fixes the key. |
| **Duplicate prevention** | In-flight restores tracked with expiration; no duplicate transactions sent. |

---

## Log Messages

| Message | Meaning |
|---------|---------|
| `monitoring validator 'alice' (signing key: VIZ...)` | Plugin started for this validator |
| `enable-stale-production detected — auto-restore is DISABLED` | Stale production mode active; restore suppressed |
| `network is healthy (XX%), auto-clearing stale production override` | Stale guard lifted |
| `'alice' has null signing key on-chain — initiating restore` | Null key detected, about to broadcast |
| `broadcasting witness_update [ID: ...] for 'alice' — restoring key to VIZ...` | Restore transaction sent |
| `CONFIRMED restoration for 'alice' in block #N` | Restore confirmed on-chain |
| `POTENTIAL LONG FORK DETECTED! LIB #N is Xs old. Skipping restoration.` | Restoration skipped due to stale LIB |
| `validator 'alice' produced N consecutive blocks — auto-disabling` | Consecutive-block threshold reached |
| `'alice' was auto-disabled (consecutive block limit), skipping auto-restore` | Auto-restore suppressed after auto-disable |
| `witness_update FAILED for 'alice': [error]` | Broadcast failed |

---

## Troubleshooting

| Problem | Check |
|---------|-------|
| Restore not triggering | Verify `validator-guard-enabled = true`; ensure node is synced; confirm account is a registered validator |
| Disabled when `enable-stale-production = true` | Expected — waits for network participation ≥ 33% |
| Transaction failed | Verify `active_wif` matches the account's active authority. Check for startup warning about mismatched keys |
| Config parse error | Each entry must be a valid 3-element JSON array: `["name", "signing_wif", "active_wif"]` |
| Validator auto-disabled and not restoring | Consecutive-block threshold was hit. Investigate the cause, manually restore the signing key on-chain; the auto-disabled flag clears once the key is detected as non-null |
| Authority warning at startup | `WARNING: Configured active key ... does NOT have authority on-chain` — update the key in config |

---

See also: [Validator Node](./validator-node.md) for signing key setup and [Validator Plugin](../plugins/validator.md) for the production loop internals.
