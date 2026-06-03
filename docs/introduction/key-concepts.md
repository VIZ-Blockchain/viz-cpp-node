# Key Concepts

This page explains the fundamental concepts you need to understand before working with VIZ Ledger as a developer, node operator, or application builder.

---

## Accounts

Every actor on VIZ Ledger is an **account**. Accounts hold balances, produce content, vote for validators, and interact with all protocol features.

### Account Name Rules

- Length: 3–16 characters total
- Dot-separated labels: each label ≥ 3 characters
- Each label starts with a letter, ends with a letter or digit
- Only lowercase letters (`a-z`), digits (`0-9`), hyphens (`-`)
- Example valid names: `alice`, `alice.bob`, `viz-user1`

### Authority Levels

Each account has three authority levels, each holding a set of keys or account delegates:

| Level | Used for | Key to use |
|-------|---------|-----------|
| `master` | Changing keys, account recovery, high-security ops | Master key (keep offline) |
| `active` | Token transfers, vesting, validator voting | Active key |
| `regular` | Content, awards, committee voting, social ops | Regular key |

An authority is a multi-sig structure: `{ weight_threshold, account_auths[], key_auths[] }`. A transaction is authorized when the sum of weights of provided signatures meets or exceeds `weight_threshold`.

---

## Tokens

### VIZ — Liquid Token

- 3 decimal places: `"10.000 VIZ"`
- Used for transfers, fees, and funding operations
- Can be converted to SHARES via `transfer_to_vesting_operation`

### SHARES — Staked Token

- 6 decimal places: `"10.000000 SHARES"`
- Represents voting power and energy capacity
- Created by staking VIZ; withdrawn back to VIZ over 28 intervals (≈28 days)
- Not directly transferable; can be delegated to other accounts

### Community Symbol: Ƶ

The community has chosen **Ƶ** as the short symbol for VIZ. Most wallets, explorers, and applications display it instead of the full ticker.

It is also common practice to show balances with **2 decimal places** regardless of the underlying token type. Even staked funds (SHARES) are often displayed as `Ƶ` with a note that they are staked in the account, rather than switching to the `SHARES` unit and its 6-decimal format.

---

## Energy System

Energy is the resource that controls the impact of social actions (awards, votes) relative to an account's SHARES.

| Property | Value |
|----------|-------|
| Unit | Basis points: `0` = 0%, `10000` = 100% |
| Regeneration rate | Full recovery in 24 hours (86400 seconds) |
| Regeneration formula | `current_energy = min(10000, last_energy + elapsed_sec * 10000 / 86400)` |

When an account performs an award with `energy = 500` (5%), that fraction of the account's SHARES is used to determine the reward pool distribution. Spending energy does not "destroy" tokens — it determines weight in the reward pool.

---

## Validators (Block Producers)

**Validators** (previously called "witnesses") are accounts that produce blocks and maintain the network.

- Any account can register as a validator via `validator_update_operation`.
- Token holders vote for validators using their SHARES weight.
- The top validators by vote are scheduled round-robin to produce blocks.
- Each block slot is exactly 3 seconds.
- A round of 21 validators = 21 blocks = 63 seconds.

### Fair-DPOS Participation

Unlike standard DPOS, VIZ Ledger penalizes inactivity:
- Each validator has a **participation score** based on recent block production.
- If network-wide participation drops below `required-participation` (default 33%), block production pauses.
- Validators that miss too many blocks receive a vote penalty applied for `validator_miss_penalty_duration` seconds.

---

## Blocks and Transactions

### Block

A signed bundle of transactions produced by a validator at its scheduled slot. Contains:
- `previous`: hash of the previous block (chain linkage)
- `timestamp`: the exact slot time
- `witness`: name of the producing validator
- `transactions[]`: list of signed transactions
- `validator_signature`: validator's signature

### Transaction

One or more operations grouped and signed. Properties:
- `ref_block_num = head_block_number & 0xFFFF`
- `ref_block_prefix` = bytes 4–7 of the referenced block ID (little-endian uint32)
- `expiration`: must be within 60 seconds of current time (recommended)
- `operations[]`: 1 or more operations
- `signatures[]`: ECDSA signatures satisfying all required authorities

### Operation

The atomic unit of state change. Serialized as `[type_id, operation_object]` when inside a transaction. There are 64+ operation types covering transfers, social actions, governance, and more — see [Operations Overview](../protocol/operations/overview.md).

---

## Reward Pool

Inflation is continuously added to the reward pool. Validators and content creators draw from this pool:

| Recipient | Source |
|-----------|--------|
| Validators | `inflation_validator_percent` of block reward |
| Committee | `inflation_ratio_committee_vs_reward_fund` fraction |
| Reward fund | Remainder — distributed via awards and content votes |

The exact percentages are set by validator consensus via `versioned_chain_properties_update_operation` and voted on by the top validators.

---

## Fork and LIB

**Fork database** (`fork_db`): in-memory tree of recently received blocks that may not yet be part of the canonical chain. The node tracks all candidate forks and always extends the heaviest (most approved) fork.

**LIB (Last Irreversible Block)**: the most recent block that has been confirmed by more than 2/3 of validators. Blocks at or below LIB can never be reorganised. Once a block is below LIB, it is written to the permanent block log.

---

## Snapshot

A snapshot is a binary dump of the entire database state at a specific block number. It allows a new node to:
1. Download the snapshot file
2. Load it in seconds (rather than replaying the entire block history)
3. Resume syncing from the snapshot block height

Snapshots are created by the `snapshot` plugin and have no effect on the canonical chain — they are purely an operational tool.

---

## Chain Properties (Governance Parameters)

On-chain consensus parameters are controlled by validators via `versioned_chain_properties_update_operation`. The active parameters are the **median** of the values submitted by the top 21 validators.

Key parameters include:
- `account_creation_fee` — cost to create a new account
- `maximum_block_size` — max bytes per block
- `inflation_validator_percent` — validator share of block reward
- `validator_miss_penalty_percent` / `validator_miss_penalty_duration` — miss penalty
- `withdraw_intervals` — number of vesting withdrawal intervals

See [Chain Properties Governance](../governance/chain-properties.md) for the full parameter list.

---

## Hardforks

Protocol upgrades are deployed as **hardforks** — scheduled activations at a specific block number. Once ≥17/21 validators signal support for a hardfork, it activates at the next scheduled block. Hardforks can add new operation types, change consensus rules, or introduce new chain properties.

See [Hardforks](../consensus/hardforks.md) for the history and upgrade process.
