# VIZ Blockchain — Operations & Structures Spec

Full specification and implementation checklist for building VIZ blockchain client libraries in PHP, Node.js, and other languages.

---

## Files in This Directory

| File | Contents |
|---|---|
| [data-types.md](data-types.md) | Primitive types, `asset`, `authority`, `public_key_type`, operation type index |
| [plugins.md](plugins.md) | All node plugins, dependencies, status, JSON-RPC method tables |
| [op-account.md](op-account.md) | Account create, update, metadata operations |
| [op-transfer-vesting.md](op-transfer-vesting.md) | Transfer, transfer_to_vesting, withdraw_vesting, set route, delegate |
| [op-witness.md](op-witness.md) | Witness update, vote, proxy, chain properties |
| [op-content.md](op-content.md) | Content, vote, delete_content (deprecated), custom |
| [op-recovery.md](op-recovery.md) | Request/recover account, change recovery account |
| [op-escrow.md](op-escrow.md) | Escrow transfer, approve, dispute, release |
| [op-committee.md](op-committee.md) | Committee worker create/cancel request, vote |
| [op-invite.md](op-invite.md) | Create invite, claim balance, register, use balance |
| [op-award.md](op-award.md) | Award, fixed_award operations |
| [op-subscription.md](op-subscription.md) | Set paid subscription, paid subscribe |
| [op-account-market.md](op-account-market.md) | Set account/subaccount price, buy account, target sale |
| [op-proposal.md](op-proposal.md) | Proposal create, update, delete (multi-sig) |
| [virtual-operations.md](virtual-operations.md) | All virtual operations (read-only, blockchain-generated) |

---

## Quick Reference: All Operations

### Regular Operations (user-broadcast)

| ID | Name | Auth | Key File |
|---|---|---|---|
| 0 | `vote_operation` *(deprecated)* | regular | op-content.md |
| 1 | `content_operation` *(deprecated)* | regular | op-content.md |
| 2 | `transfer_operation` | active (VIZ) / master (SHARES) | op-transfer-vesting.md |
| 3 | `transfer_to_vesting_operation` | active | op-transfer-vesting.md |
| 4 | `withdraw_vesting_operation` | active | op-transfer-vesting.md |
| 5 | `account_update_operation` | master/active | op-account.md |
| 6 | `witness_update_operation` | active | op-witness.md |
| 7 | `account_witness_vote_operation` | active | op-witness.md |
| 8 | `account_witness_proxy_operation` | active | op-witness.md |
| 9 | `delete_content_operation` *(deprecated)* | regular | op-content.md |
| 10 | `custom_operation` | active/regular | op-content.md |
| 11 | `set_withdraw_vesting_route_operation` | active | op-transfer-vesting.md |
| 12 | `request_account_recovery_operation` | active | op-recovery.md |
| 13 | `recover_account_operation` | master (×2) | op-recovery.md |
| 14 | `change_recovery_account_operation` | master | op-recovery.md |
| 15 | `escrow_transfer_operation` | active | op-escrow.md |
| 16 | `escrow_dispute_operation` | active | op-escrow.md |
| 17 | `escrow_release_operation` | active | op-escrow.md |
| 18 | `escrow_approve_operation` | active | op-escrow.md |
| 19 | `delegate_vesting_shares_operation` | active | op-transfer-vesting.md |
| 20 | `account_create_operation` | active | op-account.md |
| 21 | `account_metadata_operation` | regular | op-account.md |
| 22 | `proposal_create_operation` | active | op-proposal.md |
| 23 | `proposal_update_operation` | varies | op-proposal.md |
| 24 | `proposal_delete_operation` | active | op-proposal.md |
| 25 | `chain_properties_update_operation` | active | op-witness.md |
| 35 | `committee_worker_create_request_operation` | regular | op-committee.md |
| 36 | `committee_worker_cancel_request_operation` | regular | op-committee.md |
| 37 | `committee_vote_request_operation` | regular | op-committee.md |
| 43 | `create_invite_operation` | active | op-invite.md |
| 44 | `claim_invite_balance_operation` | active | op-invite.md |
| 45 | `invite_registration_operation` | active | op-invite.md |
| 46 | `versioned_chain_properties_update_operation` | active | op-witness.md |
| 47 | `award_operation` | regular | op-award.md |
| 50 | `set_paid_subscription_operation` | active | op-subscription.md |
| 51 | `paid_subscribe_operation` | active | op-subscription.md |
| 54 | `set_account_price_operation` | master | op-account-market.md |
| 55 | `set_subaccount_price_operation` | master | op-account-market.md |
| 56 | `buy_account_operation` | active | op-account-market.md |
| 58 | `use_invite_balance_operation` | active | op-invite.md |
| 60 | `fixed_award_operation` | regular | op-award.md |
| 61 | `target_account_sale_operation` | master | op-account-market.md |

### Virtual Operations (read-only, not broadcastable)

| ID | Name | Trigger | Key File |
|---|---|---|---|
| 26 | `author_reward_operation` | Content payout | virtual-operations.md |
| 27 | `curation_reward_operation` | Content payout | virtual-operations.md |
| 28 | `content_reward_operation` | Content payout | virtual-operations.md |
| 29 | `fill_vesting_withdraw_operation` | Withdrawal interval | virtual-operations.md |
| 30 | `shutdown_witness_operation` | Witness deactivated | virtual-operations.md |
| 31 | `hardfork_operation` | Hardfork activation | virtual-operations.md |
| 32 | `content_payout_update_operation` | Content payout update | virtual-operations.md |
| 33 | `content_benefactor_reward_operation` | Content payout | virtual-operations.md |
| 34 | `return_vesting_delegation_operation` | Delegation limbo ends | virtual-operations.md |
| 38 | `committee_cancel_request_operation` | Request expires | virtual-operations.md |
| 39 | `committee_approve_request_operation` | Request approved | virtual-operations.md |
| 40 | `committee_payout_request_operation` | Payout processed | virtual-operations.md |
| 41 | `committee_pay_request_operation` | Worker paid | virtual-operations.md |
| 42 | `witness_reward_operation` | Block produced | virtual-operations.md |
| 48 | `receive_award_operation` | Award given | virtual-operations.md |
| 49 | `benefactor_award_operation` | Award with beneficiary | virtual-operations.md |
| 52 | `paid_subscription_action_operation` | Subscription payment | virtual-operations.md |
| 53 | `cancel_paid_subscription_operation` | Subscription ends | virtual-operations.md |
| 57 | `account_sale_operation` | Account sold | virtual-operations.md |
| 59 | `expire_escrow_ratification_operation` | Escrow deadline missed | virtual-operations.md |
| 62 | `bid_operation` | Bid placed (HF11) | virtual-operations.md |
| 63 | `outbid_operation` | Outbid (HF11) | virtual-operations.md |

---

## Library Implementation Master Checklist

### Serialization
- [ ] Operations serialized as `[type_id, object]` (2-element array)
- [ ] `asset` values as string `"10.000 VIZ"` or as `{"amount": int, "symbol": int}` depending on API
- [ ] `authority` serialized with `weight_threshold`, `account_auths`, `key_auths`
- [ ] `public_key_type` as VIZ-prefixed base58check string
- [ ] `time_point_sec` as ISO 8601 UTC string `"2024-01-15T12:00:00"` (no timezone suffix)
- [ ] `optional<T>` as `null` when absent, or the value when present
- [ ] `extensions_type` always `[]`
- [ ] `flat_set` and `vector` as JSON arrays
- [ ] `flat_map` as JSON array of `[key, value]` pairs

### Transaction Construction
- [ ] Fetch current block header for `ref_block_num` and `ref_block_prefix`
- [ ] Set `expiration` = current time + desired TTL (max 60 seconds recommended)
- [ ] Sign transaction with required keys (see each operation's authority requirements)
- [ ] `ref_block_num` = `head_block_number & 0xFFFF`
- [ ] `ref_block_prefix` = first 4 bytes (little-endian uint32) of `block_id` starting at byte 4
- [ ] Chain ID must match target network (mainnet vs testnet)

### Key Management
- [ ] Private keys in WIF format (Base58Check with version byte 0x80)
- [ ] Public keys in VIZ-prefixed compressed base58 format
- [ ] Derive public key from private key using secp256k1
- [ ] Sign: sha256d(chain_id + serialized_tx) → compact ECDSA signature

### Energy System
- [ ] Energy is in basis points (0–10000 = 0%–100%)
- [ ] Energy regenerates at 100% per day (`CHAIN_ENERGY_REGENERATION_SECONDS = 86400`)
- [ ] Formula: `current_energy = min(10000, last_energy + elapsed_seconds / REGEN_RATE)`
- [ ] Spending 1000 energy (10%) costs proportional share of reward pool

### Asset Formatting
- [ ] VIZ: 3 decimal places, e.g. `"10.000 VIZ"`
- [ ] SHARES: 6 decimal places, e.g. `"10.000000 SHARES"`
- [ ] Parse: split on space, parse amount with decimal, check symbol

### Account Name Validation
- [ ] Length: 3–16 characters
- [ ] Only lowercase letters, digits, hyphens, dots
- [ ] Each segment (dot-separated) starts with letter, ends with letter/digit
- [ ] Each segment >= 3 characters

### Authority Validation
- [ ] `weight_threshold` must be satisfiable (sum of weights >= threshold)
- [ ] `key_auths` entries: `[public_key_string, uint16]`
- [ ] `account_auths` entries: `[account_name_string, uint16]`

### Bandwidth & Fees
- [ ] Operations consume bandwidth proportional to their serialized byte size
- [ ] Data operations (`custom_operation.json` etc.) have additional bandwidth cost
- [ ] Account creation requires `fee` >= chain `account_creation_fee`
- [ ] Committee/invite/subscription operations charge network fees from chain properties
