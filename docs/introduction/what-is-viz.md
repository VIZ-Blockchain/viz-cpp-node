# What is VIZ Ledger?

VIZ Ledger is a distributed ledger technology (DLT) built on the **Fair-DPOS** (Fair Delegated Proof of Stake) consensus algorithm. It is designed for decentralized social, financial, and governance applications where fairness, transparency, and efficiency are critical.

> *VIZ Ledger uses a blockchain-based consensus mechanism with snapshot-assisted state storage, making it a hybrid DLT system.*

---

## VIZ Ledger vs. Traditional Blockchain

Traditional blockchains require every full node to store the complete history of all transactions from the genesis block. VIZ Ledger takes a different approach:

| Property | Traditional Blockchain | VIZ Ledger |
|----------|----------------------|------------|
| State storage | Full history on every node | Recent blocks + periodic snapshots |
| Sync method | Replay all blocks from genesis | Load snapshot, replay recent blocks |
| Storage requirement | Grows unboundedly | Bounded by snapshot interval |
| Security model | Full chain verification | Snapshot + consensus verification |
| Consensus | Varies | Fair-DPOS |

This architecture is closer to what the industry calls **DLT** in the broad sense — similar to Hedera Hashgraph or Corda — rather than a classical blockchain where every node keeps the full ledger history.

### Why "VIZ Ledger"?

The name follows the same pattern as [XRP Ledger](https://xrpl.org):
- It is neutral with respect to the underlying storage mechanism.
- It accurately reflects the core function: maintaining a distributed ledger of accounts, transactions, and state.
- It leaves room for architectural evolution without renaming.

In technical documentation the full description is used where needed: *"VIZ Ledger is a Fair-DPOS distributed ledger with snapshot-assisted state storage."*

---

## History

| Milestone | Date | Block |
|-----------|------|-------|
| Mainnet genesis | 29 September 2018, 10:23:27 GMT | Block 0 |
| Transition to VIZ Ledger (DLT) | March 2026 | Block 79,105,800 |

The network launched as a classical blockchain on **29 September 2018 at 10:23:27 GMT**. After seven and a half years of continuous operation — producing over 79 million blocks — the protocol evolved from a traditional full-history blockchain into a **distributed ledger (DLT)** at block 79,105,800. This transition introduced snapshot-assisted state storage, DLT-mode block logs, and the hybrid architecture described in this documentation.

---

## Core Properties

### Fair-DPOS Consensus

VIZ Ledger uses **Fair Delegated Proof of Stake**, an evolution of standard DPOS:

- Token holders vote for **validators** (block producers) using their staked SHARES.
- The top validators by vote weight are scheduled to produce blocks in round-robin order.
- **Fairness enforcement**: a validator that misses blocks has its participation score reduced. If participation drops below the required threshold, block production is paused until enough validators are active again.
- There are no unbounded rewards for inactive validators — production requires actual participation.

### Snapshot-Assisted State

- Nodes store the current state (accounts, balances, content, votes) in shared memory.
- Periodic snapshots capture the full state at a specific block height.
- New nodes can fast-sync by loading a recent snapshot and replaying only the blocks since the snapshot, instead of the entire chain history.
- The block log (binary format) stores all blocks for full nodes that need historical access.

### Social & Governance Primitives

VIZ Ledger embeds social and governance features directly into the protocol — they are not an application layer:

- **Energy system**: accounts have an energy pool (0–100%) that regenerates over 24 hours. Energy is spent to perform social actions (awards, votes) proportional to the account's stake impact.
- **Awards**: any account can award any other account using energy, distributing reward pool tokens.
- **Committee DAO**: on-chain committee worker requests, funding proposals, and voting.
- **Invites**: on-chain invite mechanism for bootstrapping new accounts.
- **Paid subscriptions**: on-chain subscription contracts between accounts.

---

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────────────┐
│                        vizd process                         │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐  ┌─────────┐  │
│  │  chain   │  │validator │  │database_api│  │  p2p    │  │
│  │  plugin  │  │  plugin  │  │   plugin   │  │ plugin  │  │
│  └────┬─────┘  └────┬─────┘  └─────┬──────┘  └────┬────┘  │
│       │              │              │               │       │
│  ┌────▼──────────────▼──────────────▼───────────────▼────┐  │
│  │              libraries/chain  (database)               │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────┐  ┌──────────────────────────────┐   │
│  │  libraries/network  │  │    libraries/protocol        │   │
│  └────────────────────┘  └──────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
         │                                     │
    Peer nodes                         Wallets / Apps
    (P2P port 2001)              (HTTP/WS ports 8090/8091)
```

Key components:

| Component | Role |
|-----------|------|
| `chain plugin` | Opens the database, coordinates block and transaction processing |
| `validator plugin` | Produces blocks on schedule using Fair-DPOS rules |
| `database_api plugin` | Exposes JSON-RPC read queries for wallets and apps |
| `p2p plugin` | Manages peer connections, block and transaction propagation |
| `webserver plugin` | HTTP and WebSocket transport for JSON-RPC |
| `snapshot plugin` | Creates and loads state snapshots |

---

## Token System

VIZ Ledger has two native tokens:

| Token | Purpose | Decimals |
|-------|---------|----------|
| `VIZ` | Liquid token for transfers and fees | 3 (`10.000 VIZ`) |
| `SHARES` | Staked token representing voting power and energy capacity | 6 (`10.000000 SHARES`) |

VIZ can be converted to SHARES via `transfer_to_vesting_operation`. SHARES can be withdrawn back to VIZ over 28 withdrawal intervals.

---

## Who Is This Documentation For?

| Audience | Start here |
|----------|-----------|
| Node operators | [Getting Started](../node/getting-started.md) |
| Validator operators | [Running a Validator Node](../node/validator-node.md) |
| Application developers | [JSON-RPC API](../api/json-rpc.md) |
| Wallet / library developers | [Data Types](../protocol/data-types.md) · [Operations](../protocol/operations/overview.md) |
| Protocol contributors | [Architecture](./architecture.md) · [Consensus](../consensus/fair-dpos.md) |
