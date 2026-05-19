# Security Implementation

VIZ Ledger's security model rests on three pillars: threshold-based authority verification, deterministic signature validation, and encrypted peer transport. This page covers each subsystem and provides guidance for operators and plugin developers.

---

## Authority Model

Every account has three authority levels — master, active, and regular — each represented as a weighted set of keys and/or account references with a weight threshold.

```
Authority {
    weight_threshold: uint32
    key_auths:     { PublicKey → weight }
    account_auths: { AccountName → weight }
}
```

An operation is authorized when the sum of weights of provided signatures (and recursively resolved account authorities) meets or exceeds `weight_threshold`.

**Recursion depth** is bounded to prevent infinite loops in nested account authority chains.

The same authority structure is stored in shared memory as `SharedAuthority`, which uses inter-process allocators compatible with Boost.Interprocess mapped files.

---

## Signature Validation

Transaction signature validation uses deterministic secp256k1 ECDSA:

1. **Digest**: `sha256(chain_id || serialized_transaction)`
2. **Recovery**: `secp256k1_recover(signature, digest)` → public key
3. **Authority check**: `sign_state.check_authority(account, level)` walks the authority tree and verifies the recovered key set satisfies the threshold.

The `sign_state` engine:
- Maintains a set of provided signatures and their recovered keys.
- Recursively resolves account authorities up to the maximum depth.
- Filters unused signatures after verification.

**For plugin developers:** Use the `auth_util` plugin's `check_authority_signature` API to verify signatures before processing sensitive operations:

```json
{
  "method": "auth_util.check_authority_signature",
  "params": ["alice", "regular", "<hex_digest>", ["<sig1>", "<sig2>"]]
}
```

Returns the set of verified signing keys if the authority is satisfied, or an error if it is not.

---

## Peer Transport Encryption

All peer-to-peer connections use ECDH key exchange + AES stream encryption:

1. Each side generates an ephemeral key pair on connection.
2. ECDH produces a shared secret from the ephemeral keys.
3. AES encoder/decoder streams are initialized with the shared secret.
4. All subsequent messages are encrypted.

This prevents passive eavesdropping. Every connection uses a fresh ephemeral key, so compromise of one session does not affect others.

The implementation lives in `stcp_socket` (`libraries/network/stcp_socket.cpp`).

---

## API Exposure

The `webserver` plugin serves JSON-RPC over HTTP and WebSocket. Security configuration:

```ini
# Bind to loopback for internal access only
webserver-http-endpoint = 127.0.0.1:8090
webserver-ws-endpoint = 127.0.0.1:8091

# Use a reverse proxy (nginx, caddy) for public access with TLS
```

**Thread pool sizing:** The webserver runs a configurable number of threads. Set `webserver-thread-pool-size` to match expected concurrent request load. Undersizing causes request queuing; oversizing wastes resources.

```ini
webserver-thread-pool-size = 4
```

---

## Network Security Measures

- **Encrypted channels**: All peer connections are encrypted (ECDH + AES). Passive eavesdropping is not possible.
- **Peer database**: The P2P node maintains a peer database and propagation timing metadata.
- **Soft bans**: Peers that behave incorrectly (send invalid blocks, fork-only data with no progress) receive temporary soft bans rather than permanent disconnection.
- **Bandwidth limits**: Configurable via `max-send-buffer-size` and related P2P options.

---

## Vulnerability Assessment

**Common risks:**

| Risk | Mitigation |
|------|-----------|
| Authority bypass via malformed signatures | Bounded recursion depth; strict weight-threshold checking |
| Weak randomness in ephemeral keys | Uses secp256k1's deterministic key generation |
| MitM on unencrypted RPC | Bind webserver to loopback; use TLS reverse proxy for public endpoints |
| DoS via oversized payloads | JSON-RPC payload size limits; webserver thread pool controls concurrency |
| Nested authority exhaustion | Maximum recursion depth enforced in `sign_state` |

**Penetration testing checklist:**
- Submit malformed or incomplete signatures to verify authority bypass protection.
- Test recursion depth limits with deeply nested account authority chains.
- Verify transport encryption with a network capture (no plaintext should appear on the wire).
- Stress-test the webserver endpoint for memory exhaustion and queue saturation.

---

## Security Best Practices for Plugin Development

**Input validation:**
- Reject malformed or oversized JSON-RPC payloads at plugin boundaries.
- Validate all parameters against their expected types and ranges before processing.

**Authentication:**
- Always use `auth_util.check_authority_signature` before applying state changes that require authorization.
- Never trust account names or key references without verifying signatures.

**Constant-time comparisons:**
- Use `fc::crypto::secure_compare` or equivalent for secret comparisons to prevent timing side-channels.

**No plaintext credentials:**
- Never store private keys in plugin state or logs.
- Derive ephemeral keys per session; never reuse.

**Threat model per authority level:**
- `regular` authority: social/content operations — lowest privilege.
- `active` authority: funds, staking, voting — medium privilege.
- `master` authority: key rotation, recovery — highest privilege. Require explicit user confirmation in any UI.

---

## Monitoring and Incident Response

**Metrics to monitor:**
- Peer connection count and churn rate.
- Webserver thread pool queue depth and response latency.
- Failed signature validation rate (visible in logs at `warn` level).
- Bandwidth per peer connection.

**Incident response:**
1. Isolate the affected endpoint (restrict `webserver-http-endpoint` to loopback).
2. Rotate signing keys via the master authority if a validator key is compromised.
3. Re-validate account authorities after key rotation.
4. Review logs around `sign_state` failures and unusual authority chains.

**Key rotation:**
- Validator signing key: `update_validator` operation with new signing key.
- Account keys: `update_account` with new master/active/regular keys.
- All key changes take effect immediately on the next block.

---

See also: [Plugin Development](../development/plugin-development.md), [Data Types](../protocol/data-types.md), [Validators](../protocol/operations/validators.md), [Webserver Plugin](../plugins/webserver.md).
