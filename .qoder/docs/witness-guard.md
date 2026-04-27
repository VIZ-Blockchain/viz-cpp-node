# Witness Guard Plugin

The `witness_guard` plugin is an automated maintenance tool designed for VIZ witness node operators. Its primary purpose is to monitor configured witness accounts and automatically restore their signing keys if they are reset to a null state (effectively disabling the witness).

## Purpose

In Graphene-based networks like VIZ, a witness might be disabled (signing key set to null) due to manual intervention, security protocols, or certain network conditions. If an operator wants to ensure their witness stays active without manual monitoring, this plugin automates the "restore" process.

When the plugin detects a null signing key on-chain for a monitored account, it constructs, signs, and broadcasts a `witness_update_operation` to re-enable the witness using the provided private keys.

## Configuration

The plugin can be configured via the `config.ini` file or command-line arguments.

### Options

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `witness-guard-enabled` | boolean | `true` | Enables or disables the plugin logic globally. |
| `witness-guard-interval` | uint32 | `20` | Frequency of checks measured in blocks (default 20 blocks $\approx$ 60 seconds). |
| `witness-guard-witness` | vector\<string\> | N/A | A JSON triplet containing the witness name, the signing WIF, and the active WIF. |

### Enabling the Plugin

To use the plugin, add it to the list of active plugins in your `config.ini`:

```ini
plugin = witness_guard
```

## Usage Example

To monitor one or more witnesses, add the following lines to your `config.ini`. Note that the witness configuration must be a valid JSON array string.

```ini
# Automatically restore winet1
witness-guard-witness = ["winet1", "5K_SIGNING_PRIVATE_WIF", "5K_ACTIVE_PRIVATE_WIF"]

# You can monitor multiple witnesses by repeating the option
witness-guard-witness = ["winet2", "5J_SIGNING_PRIVATE_WIF", "5J_ACTIVE_PRIVATE_WIF"]

# Check every 10 blocks instead of 20
witness-guard-interval = 10
```

## Internal Logic (How it works)

1.  **Sync Check**: The plugin only executes if the node's head block time is within a reasonable range (2 * `CHAIN_BLOCK_INTERVAL`), ensuring it doesn't attempt restores while the node is still catching up to the network.
2.  **On-Chain Verification**: Every `X` blocks (configured by interval), the plugin looks up the witness object for the configured account names.
3.  **Null Key Detection**: If the `block_signing_key` found on the blockchain matches the `null_key` (all zeros):
    *   The plugin prepares a `witness_update_operation`.
    *   It preserves the existing `url` from the on-chain object.
    *   It sets the `block_signing_key` to the public key derived from the provided `signing_wif`.
4.  **Signing and Broadcast**: The transaction is signed using the `active_wif`. This is necessary because updating a witness object requires active authority.
5.  **Anti-Spam Protection**: Once a restore transaction is sent, the plugin will not attempt to send another one for that specific witness until the node is restarted or the key is successfully reset on-chain (preventing a loop of transactions if the first one is pending).

## Security Considerations

> [!CAUTION]
> **Private Key Exposure**
> This plugin requires the **Active Private Key** to be stored in plain text within your `config.ini`. Because the active key has significant control over your account (including the ability to transfer funds or change permissions), ensure that your `config.ini` file has strictly restricted file system permissions (e.g., `chmod 600 config.ini` on Linux).

## Logs

The plugin provides clear feedback in the node logs:

*   **Initialization**:
    `witness_guard: monitoring witness 'winet1' (signing key: VIZ...)`
*   **Restore Triggered**:
    `witness_guard: 'winet1' has null signing key on-chain — initiating restore`
*   **Success**:
    `witness_guard: witness_update for 'winet1' sent successfully`
*   **Failure**:
    `witness_guard: witness_update FAILED for 'winet1': [error details]`

## Troubleshooting

**Erorr: witness-guard-witness must be triplets**
Ensure each entry is a valid JSON array with exactly 3 strings: `["name", "signing_wif", "active_wif"]`. Ensure you are using double quotes for strings inside the brackets.

**Restore not triggering**
1.  Check if `witness-guard-enabled` is set to `true`.
2.  Ensure the node is fully synchronized with the network.
3.  Verify that the account name provided is an actual registered witness on the network.

**Transaction failed**
Check that the `active_wif` provided actually belongs to the witness account. If the account's active authority has been changed, the plugin will be unable to sign the update.