# Debugging

VIZ Ledger node provides several debugging tools: transaction signing utilities for cryptographic diagnosis, P2P plugin logging with ANSI color codes for network analysis, and a debug configuration template.

---

## Transaction Signing Utilities

### sign_transaction

Reads JSON signing requests from stdin (one per line), computes the transaction digest and signature, and prints results:

```bash
echo '{"ref_block_num":1234,"ref_block_prefix":5678,...}' | ./sign_transaction
```

Output includes `digest`, `sig_digest`, `key` (public key), and `signature`.

**Diagnosing signing failures:**
1. Compute `sig_digest` with `sign_transaction`.
2. Compare against the wallet's `sig_digest(chain_id)`.
3. Verify the WIF key corresponds to the claimed signing key.

### sign_digest

Signs a raw SHA-256 digest with a WIF key:

```bash
echo '{"digest":"abc123...","wif":"5K..."}' | ./sign_digest
```

Useful for confirming chain ID correctness and isolating signature malleability issues.

---

## Network Debugging (P2P Logs)

The P2P plugin uses ANSI color codes for visual distinction in console output:

| Color | ANSI Code | Content |
|-------|-----------|---------|
| White | `\033[97m` | Block processing: transaction count, latency |
| Cyan | `\033[96m` | Peer statistics: connection count, bytes, RTT |
| Gray | `\033[90m` | Detailed debug context: DLT mode, sync state |
| Orange | — | Connection warnings and termination notices |
| Red | — | Critical connection termination events |

**Reading P2P logs:**
- **White**: Spot block processing activity and transaction volume at a glance.
- **Cyan**: Monitor peer count and connection health in real-time.
- **Gray**: Investigate DLT mode and sync protocol details.
- **Orange/Red**: Identify connection failures and peer blocking events.

### Network-specific logger

Sync negotiation messages go through the `"sync"` logger. Enable in `config.ini`:

```ini
[logger.sync]
level = info
appenders = stderr
```

P2P node messages use the `"p2p"` logger (not the default logger):

```ini
[logger.p2p]
level = info
appenders = stderr
```

---

## Debug Configuration

`share/vizd/config/config_debug.ini` is a configuration template tuned for debugging:

- Larger shared memory sizes and growth thresholds for long replays.
- Single write thread for deterministic block generation.
- Tuned read/write lock retry counts.

Key settings:

```ini
shared-file-size = 12G
shared-file-full-threshold = 97
shared-file-scale-rate = 3
chainbase-check-locking = 0
```

---

## Debugging Workflows

### Transaction validation failure

1. Run `sign_transaction` on the failing transaction JSON.
2. Compare the computed `sig_digest` against the wallet-produced value.
3. Verify the WIF key corresponds to the account's authority.
4. Check block logs for validation exceptions.

### Consensus stall

1. Check **white logs** for block ingestion gaps.
2. Inspect the validator schedule via `database_api.get_validator_schedule`.
3. Check **orange/red logs** for peer disconnections affecting sync.

### Network connectivity problems

1. Check **cyan logs** for peer count and connection health.
2. Check **white logs** for block ingestion latency and gaps.
3. Check **gray logs** for DLT sync state during snapshot sync.
4. Check **orange/red logs** for termination events and peer bans.
5. Correlate block push exceptions with specific block numbers in the logs.

### Block replay

Replay blocks from a snapshot to reproduce issues:

```bash
./vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot --data-dir /data/vizd
```

---

See also: [Building](./building.md), [Testing](./testing.md), [P2P Overview](../p2p/overview.md), [Plugins Overview](../plugins/overview.md).
