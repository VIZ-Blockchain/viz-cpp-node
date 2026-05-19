# VIZ Ledger Documentation — Build Plan

> Принцип: `new-docs.md` — позиционирование **VIZ Ledger** (hybrid DLT), инструмент **Redocly Realm**, три языка EN / RU / ZH-CN, структура как у XRPL.org, контент колоцирован с кодом в репозитории.

---

## 1. Toolchain Setup

| Step | Action | File / Command |
|------|--------|----------------|
| 1 | Init npm project | `npm init -y` |
| 2 | Install Redocly Realm | `npm install --save-dev @redocly/realm` |
| 3 | Create site config | `redocly.yaml` (see template below) |
| 4 | Add npm scripts | `"start": "realm dev"`, `"build": "realm build"` |
| 5 | Add CI/CD | `.github/workflows/docs.yml` → GitHub Pages |

### `redocly.yaml` template

```yaml
l10n:
  defaultLocale: en-US
  locales:
    - code: en-US
      name: English
    - code: ru
      name: Русский
    - code: zh-CN
      name: 中文

navbar:
  items:
    - label: Introduction
      page: docs/introduction/what-is-viz.md
    - label: Run a Node
      page: docs/node/getting-started.md
    - label: Protocol
      page: docs/protocol/data-types.md
    - label: API Reference
      page: docs/api/json-rpc.md
```

---

## 2. Repository Structure

```
viz-cpp-node/
├── docs/                          ← English source (primary)
│   ├── introduction/
│   ├── node/
│   ├── consensus/
│   ├── p2p/
│   ├── plugins/
│   ├── protocol/
│   │   └── operations/
│   ├── storage/
│   ├── governance/
│   ├── development/
│   ├── api/
│   └── advanced/
├── @l10n/
│   ├── ru/docs/                   ← Russian translation (mirrors docs/)
│   └── zh-CN/docs/                ← Chinese translation (mirrors docs/)
├── translations.yaml              ← UI strings (buttons, nav labels)
└── redocly.yaml                   ← Site config
```

---

## 3. Content Plan

Each row: **target file** | **primary source** | **supplementary sources** | **priority**

### 3.1 Introduction

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/introduction/what-is-viz.md` | `new-docs.md` (VIZ Ledger concept) | `repowiki/Project Overview.md` | P0 |
| `docs/introduction/architecture.md` | `repowiki/Architecture Overview/Architecture Overview.md` | `repowiki/Architecture Overview/Design Patterns*.md` | P0 |
| `docs/introduction/key-concepts.md` | `docs/data-types.md` (energy, shares, validators) | `repowiki/Core Libraries/Emergency Consensus*.md` | P1 |

**`what-is-viz.md` key content:**
- VIZ is a hybrid DLT (not pure blockchain): Fair-DPOS consensus + snapshot-assisted state storage
- Nodes store recent blocks + periodic snapshots, not full history on every node
- Compare: "VIZ Ledger" vs "VIZ Blockchain" vs "VIZ DLT" — adopt "VIZ Ledger"
- Quote: *"VIZ Ledger uses a blockchain-based consensus mechanism with snapshot-assisted state storage, making it a hybrid DLT system."*

---

### 3.2 Node Operation

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/node/getting-started.md` | `repowiki/Getting Started.md` | `documentation/building.md` | P0 |
| `docs/node/configuration.md` | `repowiki/Configuration Management/Node Configuration.md` | `share/vizd/config/config.ini` | P0 |
| `docs/node/docker.md` | `repowiki/Development Tools/Build System/Docker Integration/` | `share/vizd/docker/` | P1 |
| `docs/node/building.md` | `repowiki/Development Tools/Build System/` | `documentation/building.md` | P1 |
| `docs/node/validator-node.md` | `docs/validator-plugin.md` | `repowiki/Validator.md`, `config_witness.ini` | P0 |
| `docs/node/validator-guard.md` | `docs/validator-guard.md` | `docs/witness-guard-spec.json` | P2 |
| `docs/node/monitoring.md` | `repowiki/Deployment and Operations/Monitoring and Maintenance.md` | `docs/dlt-p2p-stats-reference.md` | P2 |
| `docs/node/snapshot.md` | `docs/snapshot-plugin.md` | `docs/snapshot-pause-workflow.md` | P1 |

---

### 3.3 Consensus

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/consensus/fair-dpos.md` | `repowiki/Architecture Overview/Core Libraries/Chain Library/Fork Resolution and Consensus.md` | `repowiki/Project Overview.md` (Fair-DPOS section) | P0 |
| `docs/consensus/block-processing.md` | `docs/block-processing.md` | `repowiki/.../Block Processing and Validation.md` | P1 |
| `docs/consensus/fork-resolution.md` | `docs/fork-collision-hardfork-proposal.md` | `repowiki/.../Fork Resolution and Consensus.md` | P1 |
| `docs/consensus/emergency-consensus.md` | `docs/emergency-consensus-workflow.md` | `docs/consensus-emergency-params.md`, `docs/emergency-consensus-review.md` | P1 |
| `docs/consensus/hardforks.md` | `docs/hardfork-guide.md` | `docs/hf13-validator-reward-sharing.md`, `docs/dlt-hardfork-new-objects.md` | P2 |

---

### 3.4 P2P Network

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/p2p/overview.md` | `docs/p2p-sync-workflow.md` | `docs/dlt-p2p-network-redesign.md`, `repowiki/P2p Plugin.md` | P1 |
| `docs/p2p/messages.md` | `docs/p2p-messages.md` | `repowiki/.../Message Handling and Protocol.md` | P2 |
| `docs/p2p/stats-reference.md` | `docs/dlt-p2p-stats-reference.md` | `docs/dlt-p2p-stats-reference-ru.md` | P2 |
| `docs/p2p/sync-scenarios.md` | `docs/dlt-4-node-sync-scenarios.md` | — | P2 |
| `docs/p2p/forward-mode.md` | `docs/dlt-forward-mode.md` | — | P3 |

---

### 3.5 Plugins

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/plugins/overview.md` | `docs/plugins.md` | `repowiki/Plugin System/Plugin System.md` | P1 |
| `docs/plugins/validator.md` | `docs/validator-plugin.md` | `repowiki/Validator.md` | P0 |
| `docs/plugins/snapshot.md` | `docs/snapshot-plugin.md` | `docs/snapshot-pause-workflow.md` | P1 |
| `docs/plugins/chain.md` | `repowiki/Chain Plugin.md` | `repowiki/.../Chain Library.md` | P1 |
| `docs/plugins/database-api.md` | `repowiki/API Reference.md` | `docs/plugins.md` (JSON-RPC tables) | P1 |
| `docs/plugins/social-network.md` | `repowiki/Plugin System/Plugin System.md` | — | P2 |
| `docs/plugins/webserver.md` | `repowiki/Webserver Plugin.md` | — | P2 |

---

### 3.6 Protocol — Data Types & Operations

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/protocol/data-types.md` | `docs/data-types.md` | `docs/index.md` (serialization checklist) | P0 |
| `docs/protocol/operations/overview.md` | `docs/index.md` (Quick Reference tables) | — | P0 |
| `docs/protocol/operations/accounts.md` | `docs/op-account.md` | — | P1 |
| `docs/protocol/operations/transfers.md` | `docs/op-transfer-vesting.md` | — | P1 |
| `docs/protocol/operations/validators.md` | `docs/op-validator.md` | — | P1 |
| `docs/protocol/operations/content.md` | `docs/op-content.md` | — | P2 |
| `docs/protocol/operations/recovery.md` | `docs/op-recovery.md` | — | P2 |
| `docs/protocol/operations/escrow.md` | `docs/op-escrow.md` | — | P2 |
| `docs/protocol/operations/committee.md` | `docs/op-committee.md` | — | P2 |
| `docs/protocol/operations/invites.md` | `docs/op-invite.md` | — | P2 |
| `docs/protocol/operations/awards.md` | `docs/op-award.md` | — | P2 |
| `docs/protocol/operations/subscriptions.md` | `docs/op-subscription.md` | — | P2 |
| `docs/protocol/operations/account-market.md` | `docs/op-account-market.md` | — | P3 |
| `docs/protocol/operations/proposals.md` | `docs/op-proposal.md` | — | P2 |
| `docs/protocol/virtual-operations.md` | `docs/virtual-operations.md` | — | P2 |

---

### 3.7 Storage

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/storage/shared-memory.md` | `docs/shared-memory.md` | `repowiki/.../Memory Management System.md` | P1 |
| `docs/storage/block-log.md` | `docs/block-log-spec.md` | `docs/block-log-reader.js`, `docs/block-log-viewer.js` | P2 |
| `docs/storage/snapshots.md` | `docs/snapshot-plugin.md` | `docs/snapshot-pause-workflow.md` | P1 |

---

### 3.8 Governance

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/governance/chain-properties.md` | `docs/chain-properties-governance.md` | — | P2 |
| `docs/governance/staking-and-dao.md` | `docs/staking-and-dao-governance.md` | `repowiki/Advanced Topics/Database Schema Design.md` | P2 |
| `docs/governance/committee.md` | `docs/committee-dao-and-prediction-markets.md` | — | P2 |

---

### 3.9 Development

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/development/library-checklist.md` | `docs/index.md` (Library Implementation Master Checklist) | — | P1 |
| `docs/development/building.md` | `repowiki/Development Tools/Build System/` | `documentation/building.md` | P1 |
| `docs/development/testing.md` | `repowiki/Development Tools/Testing Framework/` | — | P2 |
| `docs/development/debugging.md` | `repowiki/Development Tools/Debugging Tools/` | — | P2 |
| `docs/development/plugin-development.md` | `repowiki/Advanced Topics/Advanced Plugin Development.md` | `repowiki/.../Plugin API Design Patterns.md` | P3 |

---

### 3.10 API Reference

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/api/json-rpc.md` | `docs/plugins.md` (all JSON-RPC method tables) | `repowiki/API Reference.md` | P1 |
| `docs/api/cli-wallet.md` | `docs/cli-wallet.md` | — | P1 |

---

### 3.11 Advanced

| File | Primary Source | Supplementary | Priority |
|------|---------------|---------------|----------|
| `docs/advanced/security.md` | `repowiki/Advanced Topics/Security Implementation.md` | `repowiki/Deployment and Operations/Node Deployment/Security Hardening.md` | P2 |
| `docs/advanced/database-schema.md` | `repowiki/Advanced Topics/Database Schema Design.md` | — | P3 |
| `docs/advanced/dlt-architecture.md` | `docs/dlt-p2p-network-redesign.md` | `docs/dlt-4-node-code-audit-2026-05-08.md` | P3 |
| `docs/advanced/hardfork-management.md` | `repowiki/Advanced Topics/Hardfork Management.md` | `docs/hardfork-guide.md` | P2 |

---

## 4. Execution Phases

### Phase 0 — Skeleton (now)
- [ ] Create all target directories under `docs/`
- [ ] Set up `redocly.yaml`, `package.json`, `translations.yaml`
- [ ] Create `docs/introduction/what-is-viz.md` (VIZ Ledger positioning)

### Phase 1 — Core pages (P0 items)
All P0 files: `what-is-viz`, `architecture`, `getting-started`, `configuration`, `validator-node`, `fair-dpos`, `data-types`, `operations/overview`, `plugins/validator`

### Phase 2 — Technical depth (P1 items)
Block processing, P2P overview, all P1 plugins/storage/protocol pages, `api/json-rpc`, `api/cli-wallet`, `development/library-checklist`

### Phase 3 — Full coverage (P2 items)
All remaining operations, governance, advanced topics

### Phase 4 — Translations
Copy EN structure to `@l10n/ru/docs/` and `@l10n/zh-CN/docs/`, translate P0+P1 pages first

---

## 5. Naming & Terminology Rules

| Old term | New term | Notes |
|----------|----------|-------|
| VIZ Blockchain | **VIZ Ledger** | Public name; explain hybrid DLT on intro page |
| witness | **validator** | Already renamed in codebase (branch `witness-rename`) |
| blockchain | ledger / chain | Context-dependent; "chain" OK in technical contexts |
| witness_update_operation | `validator_update_operation` | Use current operation name in docs |

---

## 6. Source File Cross-Reference

Quick lookup: internal `.qoder/docs/` files → docs target

| Source | Target |
|--------|--------|
| `docs/data-types.md` | `docs/protocol/data-types.md` |
| `docs/plugins.md` | `docs/plugins/overview.md` + `docs/api/json-rpc.md` |
| `docs/block-processing.md` | `docs/consensus/block-processing.md` |
| `docs/shared-memory.md` | `docs/storage/shared-memory.md` |
| `docs/block-log-spec.md` | `docs/storage/block-log.md` |
| `docs/snapshot-plugin.md` + `docs/snapshot-pause-workflow.md` | `docs/storage/snapshots.md` + `docs/node/snapshot.md` |
| `docs/fork-collision-hardfork-proposal.md` | `docs/consensus/fork-resolution.md` |
| `docs/consensus-emergency-params.md` + workflow + review | `docs/consensus/emergency-consensus.md` |
| `docs/hardfork-guide.md` + `hf13-*.md` | `docs/consensus/hardforks.md` |
| `docs/p2p-messages.md` | `docs/p2p/messages.md` |
| `docs/p2p-sync-workflow.md` | `docs/p2p/overview.md` |
| `docs/dlt-p2p-stats-reference.md` | `docs/p2p/stats-reference.md` |
| `docs/dlt-4-node-sync-scenarios.md` | `docs/p2p/sync-scenarios.md` |
| `docs/dlt-forward-mode.md` | `docs/p2p/forward-mode.md` |
| `docs/validator-plugin.md` | `docs/plugins/validator.md` |
| `docs/validator-guard.md` | `docs/node/validator-guard.md` |
| `docs/cli-wallet.md` | `docs/api/cli-wallet.md` |
| `docs/chain-properties-governance.md` | `docs/governance/chain-properties.md` |
| `docs/staking-and-dao-governance.md` | `docs/governance/staking-and-dao.md` |
| `docs/committee-dao-and-prediction-markets.md` | `docs/governance/committee.md` |
| `docs/index.md` | `docs/protocol/operations/overview.md` + `docs/development/library-checklist.md` |
| `docs/op-*.md` (13 files) | `docs/protocol/operations/*.md` |
| `docs/virtual-operations.md` | `docs/protocol/virtual-operations.md` |
| `repowiki/Getting Started.md` | `docs/node/getting-started.md` |
| `repowiki/Architecture Overview/` | `docs/introduction/architecture.md` |
| `repowiki/Configuration Management/` | `docs/node/configuration.md` |
| `repowiki/Deployment and Operations/` | `docs/node/monitoring.md` + `docs/advanced/security.md` |
| `repowiki/Development Tools/` | `docs/development/` |
| `repowiki/API Reference.md` | `docs/api/json-rpc.md` |
| `repowiki/Validator.md` | supplement for `docs/plugins/validator.md` |
| `repowiki/Advanced Topics/` | `docs/advanced/` |
