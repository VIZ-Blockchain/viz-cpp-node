# Debugging

VIZ Ledger node provides several debugging tools: the `debug_node` plugin for state manipulation and replay, transaction signing utilities for cryptographic diagnosis, and P2P plugin logging with ANSI color codes for network analysis.

---

## Debug Node Plugin

The `debug_node` plugin exposes a JSON-RPC API for:
- Replaying blocks from a block log or JSON array
- Generating blocks locally with a configurable signing key
- Popping blocks to roll back state
- Inspecting validator schedule and hardfork state
- Applying database edits at specific block heights

**Enable with restricted RPC (localhost only):**

```ini
plugin = debug_node
webserver-http-endpoint = 127.0.0.1:8090
```

### API Reference

| Method | Description |
|--------|-------------|
| `debug_push_blocks(src, count)` | Load blocks from a block log directory |
| `debug_push_json_blocks(file, count, skip)` | Load blocks from a JSON array file |
| `debug_generate_blocks(key, count, skip, miss, edit)` | Produce blocks with the given signing key |
| `debug_generate_blocks_until(key, time, sparse, skip)` | Advance chain to a target time |
| `debug_pop_block()` | Remove the head block, returning it |
| `debug_get_witness_schedule()` | Retrieve the current validator schedule object |
| `debug_set_hardfork(id)` | Set hardfork state programmatically |
| `debug_has_hardfork(id)` | Check whether a hardfork has been applied |

### Usage Patterns

```json
// Replay 100 blocks from a block log
{"method":"debug_node.debug_push_blocks","params":["/data/blockchain",100]}

// Generate 10 blocks with a signing key (skip validation)
{"method":"debug_node.debug_generate_blocks","params":["5K...",10,2,0,{}]}

// Inspect validator schedule
{"method":"debug_node.debug_get_witness_schedule","params":[]}

// Activate hardfork 9 for testing
{"method":"debug_node.debug_set_hardfork","params":[9]}
```

**Block generation** temporarily modifies the active validator's signing key to accept self-signed blocks, then restores the original key.

**Database update hooks** allow injecting state changes at specific block heights:

```cpp
// From plugin code
debug_plugin.debug_update([&](database& db) {
    // Modify db state here
}, skip_flags);
```

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
4. Replay blocks containing the transaction with `debug_push_blocks` and observe logs.

### Consensus stall

1. Use `debug_generate_blocks` to advance the chain deterministically.
2. Inspect the validator schedule with `debug_get_witness_schedule`.
3. If needed, set hardfork state with `debug_set_hardfork` to test activation logic.

### Network connectivity problems

1. Check **cyan logs** for peer count and connection health.
2. Check **white logs** for block ingestion latency and gaps.
3. Check **gray logs** for DLT sync state during snapshot sync.
4. Check **orange/red logs** for termination events and peer bans.
5. Correlate block push exceptions with specific block numbers in the logs.

### Integration test acceleration

Replay blocks from a JSON log with skip flags to bypass expensive validation:

```json
{"method":"debug_node.debug_push_json_blocks","params":["/tmp/blocks.json",100,2]}
```

Skip flags: `1` = skip undo session, `2` = skip witness signature, `4` = skip merkle check.

---

See also: [Building](./building.md), [Testing](./testing.md), [P2P Overview](../p2p/overview.md), [Plugins Overview](../plugins/overview.md).
