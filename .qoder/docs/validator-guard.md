# validator guard Plugin

The `witness_guard` plugin is an automated maintenance tool for VIZ validator node operators. It monitors configured validator accounts and automatically restores their signing keys when they are reset to null (which disables the validator).

## Purpose

In VIZ a validator can be disabled (signing key set to null) by manual intervention, security protocols, or certain network conditions. This plugin automates the restore process so the validator stays active without manual monitoring.

When the plugin detects a null signing key on-chain, it constructs, signs, and broadcasts a `witness_update_operation` to re-enable the validator using the provided private keys.

## Configuration

The plugin is configured via `config.ini` or command-line arguments.

### Options

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `validator-guard-enabled` | boolean | `true` | Enables or disables the plugin logic globally. |
| `validator-guard-interval` | uint32 | `20` | Frequency of periodic checks in blocks (20 blocks ≈ 60 seconds). |
| `validator-guard-validator` | vector\<string\> | N/A | A JSON triplet: validator name, signing WIF, active WIF. Repeatable. |
| `validator-guard-disable` | uint32 | `5` | Number of consecutive blocks produced by the same validator before auto-disabling (setting signing key to null). Set to `0` to disable this feature. |

The plugin also reads the shared `enable-stale-production` option from the Validator Plugin configuration.

### Enabling the Plugin

Add it to the active plugins in `config.ini`:

```ini
plugin = witness_guard
```

## Usage Example

```ini
# Monitor and auto-restore winet1
validator-guard-validator = ["winet1", "5K_SIGNING_PRIVATE_WIF", "5K_ACTIVE_PRIVATE_WIF"]

# Monitor multiple validators by repeating the option
validator-guard-validator = ["winet2", "5J_SIGNING_PRIVATE_WIF", "5J_ACTIVE_PRIVATE_WIF"]

# Check every 10 blocks instead of 20
validator-guard-interval = 10
```

## Internal Logic

### Startup

1. **Key parsing**: Each `validator-guard-validator` entry is parsed as a JSON array of three strings. Both WIF keys are validated.
2. **Stale production detection**: If `enable-stale-production=true` is set in the validator config, auto-restore is initially disabled (see Safety Guards below).
3. **Disable threshold**: If `validator-guard-disable` is greater than 0, the consecutive-block auto-disable feature is enabled (see below).
4. **Authority validation**: After the chain database is open, the plugin verifies that each configured active key actually has authority on-chain. validators whose accounts are not found are removed from monitoring.
5. **Initial check**: An immediate check is attempted. If the node is already in sync, the result is cached so the plugin switches to its normal periodic schedule.

### Per-Block Signal Handler (`applied_block`)

On every new block the plugin:

0. **Consecutive-block auto-disable**: If `validator-guard-disable > 0` and the block was produced by one of our monitored validators, the plugin increments a per-validator consecutive-block counter. When the counter reaches the configured threshold, a `witness_update_operation` with a null signing key is broadcast to disable the validator, and it is marked as auto-disabled. If the block was produced by a *different* validator, all counters are reset to zero (the streak is broken).
1. **Transaction confirmation**: Scans the block for any pending restore transaction IDs. When found, the restore is marked as confirmed and tracking state is cleared.
2. **Look-ahead scheduling**: If any monitored validator is scheduled to produce within the next 3 slots, an immediate check is triggered so the key can be restored before the slot arrives.
3. **Periodic check**: Otherwise runs `check_and_restore_internal` at the configured interval. While the node is still syncing after startup, checks run every 10 blocks instead.

### Core Check (`check_and_restore_internal`)

1. **Stale production guard**: If `enable-stale-production` is active and network participation is below 33%, all checks are skipped. Once participation reaches ≥ 33% the stale flag is auto-cleared (same logic as the Validator Plugin) and auto-restore resumes. During emergency consensus mode the guard is bypassed.
2. **Sync check**: Head block time must be within `2 × CHAIN_BLOCK_INTERVAL` seconds of wall-clock time.
3. **Long fork safety**: If the Last Irreversible Block (LIB) is older than 200 seconds, restoration is skipped to avoid acting on a stale fork.
4. **Expiry cleanup**: Stale entries in `_pending_confirmations` are expired so that failed broadcasts can be retried.
5. **validator iteration**: For each configured validator, the on-chain signing key is checked. If the key is present, any pending restore state and auto-disabled flag are cleared. If null and the validator was auto-disabled by the consecutive-block guard, auto-restore is skipped (the operator must investigate and restart). Otherwise, if no restore is currently in-flight (or the previous one expired), `send_witness_update` is called.

### Restore Transaction (`send_witness_update`)

1. Builds a `witness_update_operation` preserving the current on-chain URL and setting the signing key to the configured public key.
2. Wraps it in a `signed_transaction` with a 30-second expiration and head block reference.
3. Signs with the configured active private key.
4. Broadcasts via the P2P plugin.
5. Tracks the transaction ID in `_pending_confirmations` and the validator in `_restore_pending` to prevent duplicate broadcasts.

### Disable Transaction (`send_witness_disable`)

1. Builds a `witness_update_operation` preserving the current on-chain URL and setting the signing key to **null** (effectively disabling block production).
2. Wraps it in a `signed_transaction` with a 30-second expiration and head block reference.
3. Signs with the configured active private key.
4. Broadcasts via the P2P plugin.
5. Adds the validator to the `_auto_disabled_witnesses` set so that the auto-restore logic does **not** re-enable it automatically.

## Safety Guards

| Guard | Behavior |
| :--- | :--- |
| **Stale production** | When `enable-stale-production=true`, auto-restore is disabled to avoid broadcasting on a minority fork. **Auto-cleared** when participation ≥ 33%. |
| **Emergency mode** | During emergency consensus (`dgp.emergency_consensus_active`), the stale production guard is bypassed — key restoration may still be needed for recovery. |
| **Sync check** | Restoration only runs when the node is synchronized (head block recent). |
| **Long fork detection** | If LIB is older than 200 seconds, restoration is skipped. |
| **Authority validation** | At startup, configured active keys are verified against on-chain authority. |
| **Consecutive-block auto-disable** | When a monitored validator produces `validator-guard-disable` consecutive blocks, it is automatically disabled (signing key set to null). Auto-restore is suppressed until the operator manually restores the key (at which point the flag is cleared). |
| **Duplicate prevention** | In-flight restores are tracked with expiration; no duplicate transactions are sent. |

## Security Considerations

> [!CAUTION]
> **Private Key Exposure**
> This plugin requires the **Active Private Key** in plain text in `config.ini`. The active key has significant control over your account (transfers, permission changes). Ensure `config.ini` has strictly restricted file system permissions (e.g., `chmod 600 config.ini` on Linux).

## Logs

* **Initialization**:
  `witness_guard: monitoring validator 'winet1' (signing key: VIZ...)`
* **Stale production detected**:
  `witness_guard: enable-stale-production detected — auto-restore is DISABLED until network participation >= 33%`
* **Stale production auto-cleared**:
  `witness_guard: network is healthy (participation XX%), auto-clearing stale production override`
* **Restore triggered**:
  `witness_guard: 'winet1' has null signing key on-chain — initiating restore`
* **Broadcast sent**:
  `witness_guard: broadcasting witness_update [ID: ...] for 'winet1' — restoring key to VIZ...`
* **Confirmed on-chain**:
  `witness_guard: CONFIRMED restoration for 'winet1' in block #N [TX: ...]`
* **Long fork warning**:
  `witness_guard: POTENTIAL LONG FORK DETECTED! LIB #N is Xs old. Skipping restoration.`
* **Consecutive-block auto-disable triggered**:
  `witness_guard: validator '${w}' produced ${c} consecutive blocks — auto-disabling (threshold=${t})`
* **Disable broadcast sent**:
  `witness_guard: broadcasting witness_update [ID: ...] for '${w}' — DISABLING (setting key to null)`
* **Auto-restore skipped (auto-disabled validator)**:
  `witness_guard: '${w}' was auto-disabled (consecutive block limit), skipping auto-restore`
* **Failure**:
  `witness_guard: witness_update FAILED for 'winet1': [error details]`

## Troubleshooting

**Error: validator-guard-validator expects [name, signing_wif, active_wif]**
Ensure each entry is a valid JSON array with exactly 3 strings. Use double quotes inside the brackets.

**Restore not triggering**
1. Check that `validator-guard-enabled` is `true`.
2. Ensure the node is fully synchronized.
3. Verify the account name is a registered validator on the network.
4. If `enable-stale-production=true` is set, auto-restore is disabled until network participation reaches ≥ 33%.

**Transaction failed**
Check that the `active_wif` belongs to the validator account. If the account's active authority has been changed, the plugin cannot sign the update. The startup log will warn about mismatched keys.

**validator auto-disabled and not restoring**
If the log shows `was auto-disabled (consecutive block limit), skipping auto-restore`, the validator produced too many consecutive blocks and was automatically disabled as a safety measure. The operator must investigate, manually restore the signing key (or restart the node), at which point the auto-disabled flag clears.

**Authority warning at startup**
`WARNING: Configured active key for validator 'X' does NOT have authority on-chain` — the configured active WIF does not match any key in the account's active authority. Update the key in `config.ini`.
