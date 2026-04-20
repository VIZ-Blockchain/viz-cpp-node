# DLT Snapshot Plugin — Research Document

## 1. Overview

This document defines the design for a **trust state / snapshot plugin** that enables the VIZ blockchain node to operate in **DLT mode** — starting instantly from a serialized state snapshot without replaying the entire block history.

### Motivation

Currently, starting a VIZ node requires either:
- Full sync from genesis (replay all blocks)
- Reindex from block_log (still replay all blocks)

Both approaches are slow and resource-intensive. For many use cases (API nodes, witness nodes, monitoring), the full block history is unnecessary. A snapshot of the consensus state at a specific block allows:

- Near-instant node startup
- Minimal disk usage (no block_log required for historical blocks)
- Fast recovery and deployment
- Ability to skip synchronization of ancient blocks entirely

### Key Principle

The snapshot must contain **all state required to continue block processing and validation** from the snapshot block forward. Any object that participates in consensus evaluation, witness scheduling, balance tracking, or authority verification must be included. Objects that exist only for API queries or historical lookup can be excluded.

---

## 2. Object Categories

### Legend

| Category | Description |
|----------|-------------|
| **CRITICAL** | Directly required for consensus validation and block processing. Missing = node cannot validate blocks. |
| **IMPORTANT** | Required for correct state transitions during block processing. Missing = incorrect behavior on specific operations. |
| **OPTIONAL** | Useful for completeness but not strictly required for consensus. Can be reconstructed or skipped. |
| **EXCLUDED** | Not needed for DLT mode. Plugin-level, API-only, or purely historical data. |

---

## 3. CRITICAL Objects (Must Be in Snapshot)

These objects are directly involved in block validation, consensus, and state calculation. Without them, the node cannot process new blocks.

### 3.1 `dynamic_global_property_object`

- **File**: `libraries/chain/include/graphene/chain/global_property_object.hpp`
- **Index**: `dynamic_global_property_index` (by_id)
- **Why critical**: The single most important object. Tracks head block number/id, current supply, total vesting, reward fund, participation rate, last irreversible block, reserve ratio, and inflation parameters. All block processing begins by reading this object.
- **Fields to snapshot**: ALL fields
  - `id`, `head_block_number`, `head_block_id`, `genesis_time`, `time`, `current_witness`
  - `committee_fund`, `committee_requests`, `current_supply`, `total_vesting_fund`, `total_vesting_shares`
  - `total_reward_fund`, `total_reward_shares`
  - `average_block_size`, `maximum_block_size`, `current_aslot`
  - `recent_slots_filled`, `participation_count`
  - `last_irreversible_block_num`, `last_irreversible_block_id`
  - `last_irreversible_block_ref_num`, `last_irreversible_block_ref_prefix`
  - `max_virtual_bandwidth`, `current_reserve_ratio`
  - `vote_regeneration_per_day`, `bandwidth_reserve_candidates`
  - `inflation_calc_block_num`, `inflation_witness_percent`, `inflation_ratio`
- **Instance count**: Exactly 1

### 3.2 `witness_schedule_object`

- **File**: `libraries/chain/include/graphene/chain/witness_objects.hpp`
- **Index**: `witness_schedule_index` (by_id)
- **Why critical**: Contains the current witness rotation schedule, virtual time for DPOS scheduling, median chain properties, and majority version. Required to determine which witness produces the next block.
- **Fields to snapshot**: ALL fields
  - `id`, `current_virtual_time`, `next_shuffle_block_num`
  - `current_shuffled_witnesses`, `num_scheduled_witnesses`
  - `median_props`, `majority_version`
- **Instance count**: Exactly 1

### 3.3 `hardfork_property_object`

- **File**: `libraries/chain/hardfork.d/0-preamble.hf`
- **Index**: `hardfork_property_index` (by_id)
- **Why critical**: Tracks which hardforks have been applied and the current/next hardfork versions. Determines which code paths are active during block processing.
- **Fields to snapshot**: ALL fields
  - `id`, `processed_hardforks`, `last_hardfork`
  - `current_hardfork_version`, `next_hardfork`, `next_hardfork_time`
- **Instance count**: Exactly 1

### 3.4 `account_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `account_index` (by_id, by_name, by_next_vesting_withdrawal, by_account_on_sale, by_account_on_auction, by_account_on_sale_start_time, by_subaccount_on_sale)
- **Why critical**: Contains all balance information (liquid, vesting, delegated), voting power (energy), witness voting weight (proxied_vsf_votes), bandwidth tracking, and account sale/auction state. Required for every operation involving transfers, voting, vesting, bandwidth.
- **Fields to snapshot**: ALL fields
  - `id`, `name`, `memo_key`, `proxy`, `referrer`
  - `last_account_update`, `created`, `recovery_account`, `last_account_recovery`
  - `subcontent_count`, `vote_count`, `content_count`, `awarded_rshares`
  - `custom_sequence`, `custom_sequence_block_num`
  - `energy`, `last_vote_time`
  - `balance`, `vesting_shares`, `delegated_vesting_shares`, `received_vesting_shares`
  - `vesting_withdraw_rate`, `next_vesting_withdrawal`, `withdrawn`, `to_withdraw`, `withdraw_routes`
  - `curation_rewards`, `posting_rewards`, `receiver_awards`, `benefactor_awards`
  - `proxied_vsf_votes`, `witnesses_voted_for`, `witnesses_vote_weight`
  - `last_root_post`, `last_post`
  - `average_bandwidth`, `lifetime_bandwidth`, `last_bandwidth_update`
  - `valid`
  - `account_seller`, `account_offer_price`, `account_on_sale`, `account_on_sale_start_time`
  - `reserved_balance`
  - `target_buyer`, `account_on_auction`, `current_bid`, `current_bidder`, `current_bidder_key`, `last_bid`
  - `subaccount_seller`, `subaccount_offer_price`, `subaccount_on_sale`
- **Instance count**: Equal to total accounts on chain

### 3.5 `account_authority_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `account_authority_index` (by_id, by_account, by_last_master_update)
- **Why critical**: Contains master/active/regular authorities for every account. Required for transaction signature verification and authority checks.
- **Fields to snapshot**: ALL fields
  - `id`, `account`, `master`, `active`, `regular`, `last_master_update`
- **Instance count**: Equal to total accounts on chain

### 3.6 `witness_object`

- **File**: `libraries/chain/include/graphene/chain/witness_objects.hpp`
- **Index**: `witness_index` (by_id, by_name, by_vote_name, by_counted_vote_name, by_schedule_time, by_work)
- **Why critical**: Contains witness signing keys, vote counts, virtual scheduling state, and properties. Required for block validation (signing key lookup) and witness scheduling.
- **Fields to snapshot**: ALL fields
  - `id`, `owner`, `created`, `url`, `total_missed`
  - `last_aslot`, `last_confirmed_block_num`, `current_run`, `last_supported_block_num`
  - `signing_key`, `props`
  - `votes`, `penalty_percent`, `counted_votes`, `schedule`
  - `virtual_last_update`, `virtual_position`, `virtual_scheduled_time`
  - `last_work`, `running_version`, `hardfork_version_vote`, `hardfork_time_vote`
- **Instance count**: Equal to registered witnesses

### 3.7 `witness_vote_object`

- **File**: `libraries/chain/include/graphene/chain/witness_objects.hpp`
- **Index**: `witness_vote_index` (by_id, by_account_witness, by_witness_account)
- **Why critical**: Links accounts to their witness votes. Required when adjusting witness vote weights on account vesting changes.
- **Fields to snapshot**: ALL fields
  - `id`, `witness`, `account`
- **Instance count**: Equal to total witness votes

### 3.8 `block_summary_object`

- **File**: `libraries/chain/include/graphene/chain/block_summary_object.hpp`
- **Index**: `block_summary_index` (by_id)
- **Why critical**: Used for TaPOS (Transactions as Proof of Stake) validation. Transactions reference past blocks; without these, TaPOS checks fail.
- **Fields to snapshot**: ALL fields
  - `id`, `block_id`
- **Instance count**: 65536 (0x10000, fixed circular buffer)
- **Note**: Only summaries for recent blocks matter. The circular buffer means older entries are overwritten.

### 3.9 `content_object`

- **File**: `libraries/chain/include/graphene/chain/content_object.hpp`
- **Index**: `content_index` (by_id, by_cashout_time, by_permlink, by_root, by_parent; plus by_last_update, by_author_last_update in non-low-mem)
- **Why critical**: Content objects track rshares, cashout times, and payout calculations. Active content with pending payouts directly affects inflation and reward distribution. The `by_permlink` and `by_parent` indexes are used by consensus evaluators.
- **Fields to snapshot**: ALL fields
  - `id`, `parent_author`, `parent_permlink`, `author`, `permlink`
  - `last_update`, `created`, `active`, `last_payout`
  - `depth`, `children`, `children_rshares`
  - `net_rshares`, `abs_rshares`, `vote_rshares`
  - `cashout_time`, `total_vote_weight`
  - `curation_percent`, `consensus_curation_percent`
  - `payout_value`, `shares_payout_value`, `curator_payout_value`, `beneficiary_payout_value`
  - `author_rewards`, `net_votes`, `root_content`, `beneficiaries`

### 3.10 `content_vote_object`

- **File**: `libraries/chain/include/graphene/chain/content_object.hpp`
- **Index**: `content_vote_index` (by_id, by_content_voter, by_voter_content, by_voter_last_update, by_content_weight_voter)
- **Why critical**: Tracks individual votes on content. Required for curation reward calculations and vote-changing logic.
- **Fields to snapshot**: ALL fields
  - `id`, `voter`, `content`, `weight`, `rshares`, `vote_percent`, `last_update`, `num_changes`

### 3.11 `block_post_validation_object`

- **File**: `libraries/chain/include/graphene/chain/chain_objects.hpp`
- **Index**: `block_post_validation_index` (by_id)
- **Why critical**: Tracks block post-validation state used in consensus for witness participation verification.
- **Fields to snapshot**: ALL fields
  - `id`, `block_num`, `block_id`, `current_shuffled_witnesses`, `current_shuffled_witnesses_validations`

---

## 4. IMPORTANT Objects (Required for Correct State Transitions)

These objects contain state that will be acted upon during normal block processing. If missing, specific operations will fail or produce incorrect results.

### 4.1 `transaction_object`

- **File**: `libraries/chain/include/graphene/chain/transaction_object.hpp`
- **Index**: `transaction_index` (by_id, by_trx_id, by_expiration)
- **Why important**: Prevents duplicate transaction execution. Without it, replayed transactions could be applied twice.
- **Fields to snapshot**: ALL fields
  - `id`, `packed_trx`, `trx_id`, `expiration`
- **Note**: Only transactions not yet expired need to be included. Expired ones are cleaned up each block. At snapshot time, only include transactions with `expiration > snapshot_block_time`.

### 4.2 `vesting_delegation_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `vesting_delegation_index` (by_id, by_delegation, by_received)
- **Why important**: Tracks active vesting share delegations. Required for correct `effective_vesting_shares` calculation and delegation operations.
- **Fields to snapshot**: ALL fields
  - `id`, `delegator`, `delegatee`, `vesting_shares`, `min_delegation_time`

### 4.3 `vesting_delegation_expiration_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `vesting_delegation_expiration_index` (by_id, by_expiration, by_account_expiration)
- **Why important**: Tracks pending delegation expirations. These are processed in `clear_expired_delegations()` during block processing.
- **Fields to snapshot**: ALL fields
  - `id`, `delegator`, `vesting_shares`, `expiration`

### 4.4 `fix_vesting_delegation_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `fix_vesting_delegation_index` (by_id)
- **Why important**: Permanent delegation fix records. Required for correct vesting share accounting.
- **Fields to snapshot**: ALL fields
  - `id`, `delegator`, `delegatee`, `vesting_shares`

### 4.5 `withdraw_vesting_route_object`

- **File**: `libraries/chain/include/graphene/chain/chain_objects.hpp`
- **Index**: `withdraw_vesting_route_index` (by_id, by_withdraw_route, by_destination)
- **Why important**: Defines routing for vesting withdrawals. Required for `process_vesting_withdrawals()`.
- **Fields to snapshot**: ALL fields
  - `id`, `from_account`, `to_account`, `percent`, `auto_vest`

### 4.6 `escrow_object`

- **File**: `libraries/chain/include/graphene/chain/chain_objects.hpp`
- **Index**: `escrow_index` (by_id, by_from_id, by_to, by_agent, by_ratification_deadline)
- **Why important**: Active escrow contracts. Processed during `expire_escrow_ratification()`.
- **Fields to snapshot**: ALL fields
  - `id`, `escrow_id`, `from`, `to`, `agent`
  - `ratification_deadline`, `escrow_expiration`
  - `token_balance`, `pending_fee`
  - `to_approved`, `agent_approved`, `disputed`

### 4.7 `proposal_object`

- **File**: `libraries/chain/include/graphene/chain/proposal_object.hpp`
- **Index**: `proposal_index` (by_id, by_account, by_expiration)
- **Why important**: Active proposals. Processed during `clear_expired_proposals()`.
- **Fields to snapshot**: ALL fields
  - `id`, `author`, `title`, `memo`
  - `expiration_time`, `review_period_time`
  - `proposed_operations`
  - `required_active_approvals`, `available_active_approvals`
  - `required_master_approvals`, `available_master_approvals`
  - `required_regular_approvals`, `available_regular_approvals`
  - `available_key_approvals`

### 4.8 `required_approval_object`

- **File**: `libraries/chain/include/graphene/chain/proposal_object.hpp`
- **Index**: `required_approval_index` (by_id, by_account)
- **Why important**: Links accounts to proposals requiring their approval. Required for proposal authorization checking.
- **Fields to snapshot**: ALL fields
  - `id`, `account`, `proposal`

### 4.9 `committee_request_object`

- **File**: `libraries/chain/include/graphene/chain/committee_objects.hpp`
- **Index**: `committee_request_index` (by_id, by_request_id, by_status, by_creator, by_worker, by_creator_url)
- **Why important**: Active committee requests. Processed during `committee_processing()`.
- **Fields to snapshot**: ALL fields
  - `id`, `request_id`, `url`, `creator`, `worker`
  - `required_amount_min`, `required_amount_max`
  - `start_time`, `duration`, `end_time`
  - `status`, `votes_count`, `conclusion_time`
  - `conclusion_payout_amount`, `payout_amount`, `remain_payout_amount`
  - `last_payout_time`, `payout_time`

### 4.10 `committee_vote_object`

- **File**: `libraries/chain/include/graphene/chain/committee_objects.hpp`
- **Index**: `committee_vote_index` (by_id, by_voter, by_request_id)
- **Why important**: Committee votes on active requests.
- **Fields to snapshot**: ALL fields
  - `id`, `request_id`, `voter`, `vote_percent`, `last_update`

### 4.11 `invite_object`

- **File**: `libraries/chain/include/graphene/chain/invite_objects.hpp`
- **Index**: `invite_index` (by_id, by_invite_key, by_status, by_creator, by_receiver)
- **Why important**: Active invites with balance. Processed during `clear_used_invites()`.
- **Fields to snapshot**: ALL fields
  - `id`, `creator`, `receiver`, `invite_key`, `invite_secret`
  - `balance`, `claimed_balance`
  - `create_time`, `claim_time`, `status`

### 4.12 `award_shares_expire_object`

- **File**: `libraries/chain/include/graphene/chain/chain_objects.hpp`
- **Index**: `award_shares_expire_index` (by_id, by_expiration)
- **Why important**: Pending award share expirations. Processed during `expire_award_shares_processing()`.
- **Fields to snapshot**: ALL fields
  - `id`, `expires`, `rshares`

### 4.13 `paid_subscription_object`

- **File**: `libraries/chain/include/graphene/chain/paid_subscription_objects.hpp`
- **Index**: `paid_subscription_index` (by_id, by_creator)
- **Why important**: Active paid subscription offers. Required for `paid_subscribe_processing()`.
- **Fields to snapshot**: ALL fields
  - `id`, `creator`, `url`, `levels`, `amount`, `period`, `update_time`

### 4.14 `paid_subscribe_object`

- **File**: `libraries/chain/include/graphene/chain/paid_subscription_objects.hpp`
- **Index**: `paid_subscribe_index` (by_id, by_subscriber, by_creator, by_next_time, by_subscribe)
- **Why important**: Active subscriptions with pending payments. Required for `paid_subscribe_processing()`.
- **Fields to snapshot**: ALL fields
  - `id`, `subscriber`, `creator`, `level`, `amount`, `period`
  - `start_time`, `next_time`, `end_time`, `active`, `auto_renewal`

### 4.15 `witness_penalty_expire_object`

- **File**: `libraries/chain/include/graphene/chain/witness_objects.hpp`
- **Index**: `witness_penalty_expire_index` (by_id, by_account, by_expiration)
- **Why important**: Tracks witness penalties that will expire. Affects witness schedule calculations.
- **Fields to snapshot**: ALL fields
  - `id`, `witness`, `penalty_percent`, `expires`

---

## 5. OPTIONAL Objects (Can Be Deferred or Partially Included)

These objects are useful for full functionality but are not strictly required for consensus.

### 5.1 `account_metadata_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `account_metadata_index` (by_id, by_account)
- **Why optional**: Stores JSON metadata for accounts. Only created in non-low-mem builds (`#ifndef IS_LOW_MEM`). Not used in consensus.
- **Recommendation**: Include for API completeness; exclude in minimal DLT mode.

### 5.2 `content_type_object`

- **File**: `libraries/chain/include/graphene/chain/content_object.hpp`
- **Index**: `content_type_index` (by_id, by_content)
- **Why optional**: Stores content body/title/json_metadata. Only created in non-low-mem builds. Not used in consensus.
- **Recommendation**: Include for API completeness; exclude in minimal DLT mode.

### 5.3 `master_authority_history_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `master_authority_history_index` (by_id, by_account)
- **Why optional**: Tracks historical master authority changes for account recovery. Only needed if recovery of old authority is requested.
- **Recommendation**: Include — it is referenced during account recovery processing.

### 5.4 `account_recovery_request_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `account_recovery_request_index` (by_id, by_account, by_expiration)
- **Why optional**: Tracks pending recovery requests. Processed during `account_recovery_processing()`. If excluded, pending recoveries would be lost.
- **Recommendation**: Include — active recovery requests should be preserved.

### 5.5 `change_recovery_account_request_object`

- **File**: `libraries/chain/include/graphene/chain/account_object.hpp`
- **Index**: `change_recovery_account_request_index` (by_id, by_account, by_effective_date)
- **Why optional**: Tracks pending recovery account changes. Processed when effective date is reached.
- **Recommendation**: Include — pending changes should be preserved.

### 5.6 `custom_protocol_object` (Plugin-Conditional)

- **File**: `plugins/custom_protocol_api/include/graphene/plugins/custom_protocol_api/custom_protocol_api_object.hpp`
- **Index**: `custom_protocol_index` (by_id, by_account_custom_sequence_block_num)
- **Why optional**: Stores per-account custom sequence counters (`custom_sequence`, `custom_sequence_block_num`). Not used in consensus directly, but **requires full replay to reconstruct** if lost. Without it, applications relying on custom protocol sequencing will see incorrect sequence numbers.
- **Conditional inclusion logic**:
  - **Export**: Include in snapshot only if the `custom_protocol_api` plugin is enabled on the exporting node. If the plugin is disabled, its index does not exist in chainbase and there is nothing to export.
  - **Import**: Load from snapshot only if the `custom_protocol_api` plugin is enabled on the importing node. If the plugin is disabled, skip this section — the data will not be loaded and the plugin's index will remain empty (applications using the plugin will need a full replay to populate it).
- **Fields to snapshot**: ALL fields
  - `id`, `account`, `custom_protocol_id`, `custom_sequence`, `custom_sequence_block_num`

---

## 6. EXCLUDED Objects and Indexes

### 6.1 `block_stats_object`

- **Status**: Declared in `object_type` enum but no concrete class definition found in codebase. Likely a placeholder.
- **Decision**: Exclude from snapshot.

### 6.2 Plugin Indexes (All Excluded from Consensus Snapshot)

These indexes are maintained by plugins for API purposes only. They do not affect consensus and can be rebuilt after snapshot load.

| Plugin | Indexes | Reason to Exclude |
|--------|---------|-------------------|
| follow | `follow_index`, `feed_index`, `blog_index`, `follow_count_index`, `blog_author_stats_index` | Social graph data, not consensus |
| account_history | `account_history_index`, `account_range_index` | Historical operation index, not consensus |
| account_by_key | `key_lookup_index` | Key-to-account lookup, can be rebuilt |
| tags | `tag_index`, `tag_stats_index`, `author_tag_stats_index`, `language_index` | Content tagging, not consensus |
| private_message | `message_index` | Messaging data, not consensus |
| operation_history | `operation_index` | Historical operation records, not consensus |

**Note**: `custom_protocol_api` has been moved to OPTIONAL (section 5.6) because its data requires full replay to reconstruct. It is included conditionally based on plugin enablement on the exporting and importing nodes.

### 6.3 Non-Persistent Runtime State (Excluded)

| State | Location | Reason |
|-------|----------|--------|
| `_pending_tx` | `database.hpp:428` | In-flight transactions, will be resubmitted |
| `_popped_tx` | `database.hpp:427` | Temporary cache, not needed |
| `_current_trx_id` | `database.hpp:535` | Transient, reset on startup |
| `_current_block_num`, `_current_trx_in_block`, `_current_op_in_trx`, `_current_virtual_op` | `database.hpp:536-539` | Transient block processing state |
| `_checkpoints` | `database.hpp:541` | Node-specific configuration, not state |
| `_custom_operation_interpreters` | `database.hpp:556` | Plugin-registered, rebuilt on startup |

---

## 7. Non-Object State Required in Snapshot

Beyond chainbase objects, additional state must be captured for the node to resume correctly.

### 7.1 Snapshot Header (Metadata)

```
{
  "version": 1,                    // Snapshot format version
  "chain_id": "...",               // Chain identifier for validation
  "snapshot_block_num": 12345678,  // Block number at snapshot time
  "snapshot_block_id": "...",      // Block hash at snapshot time
  "snapshot_block_time": "...",    // Timestamp of snapshot block
  "last_irreversible_block_num": 12345000,  // LIB at snapshot time
  "last_irreversible_block_id": "...",      // LIB hash
  "snapshot_creation_time": "...", // When snapshot was created
  "object_counts": {               // For validation during load
    "account_object": 5000,
    "witness_object": 50,
    ...
  }
}
```

### 7.2 Fork Database Seed

The fork database (`_fork_db`) contains blocks that are not yet irreversible. After loading a snapshot, the fork database should be seeded with the head block:

```
{
  "fork_db_head_block": { ... }    // The signed_block at snapshot_block_num
}
```

This is necessary because `_fork_db.start_block()` is called with the head block during `database::open()`.

### 7.3 Block Log Position

The node needs to know where to start syncing from the network:

```
{
  "block_log_head_block_num": 12345678,  // Last block in block_log (if available)
  "block_log_head_block_id": "..."       // Its hash
}
```

In DLT mode, the block_log may be truncated or absent. The node will sync remaining blocks from P2P network starting from `last_irreversible_block_num + 1`.

### 7.4 Hardfork State Derivation

The `_hardfork_times[]` and `_hardfork_versions[]` arrays are populated from code in `init_hardforks()` and `apply_hardfork()`. These are **not** stored in the database — they are compiled into the binary. The snapshot only needs the `hardfork_property_object` which records which hardforks have been applied.

**Important**: The snapshot must be loaded by a binary whose compiled hardfork history is at least as recent as `hardfork_property_object.last_hardfork`. Otherwise, the node cannot correctly process the chain.

---

## 8. Snapshot Format Specification

### 8.1 Format Options

| Format | Pros | Cons |
|--------|------|------|
| **JSON** | Human-readable, debuggable, compatible with existing fc::reflect | Large file size, slow serialization |
| **Binary (fc::raw)** | Compact, fast, already used for chainbase | Not human-readable, version-sensitive |
| **JSON + Binary hybrid** | Header in JSON, data in binary | Complexity |

### 8.2 Recommended Format: Binary with JSON Header

```
[JSON Header]
[separator: \0\0\0\0]
[binary section: fc::raw packed objects by type]
[binary section: fork_db seed block]
[checksum: SHA256 of all preceding data]
```

Each object type section:

```
[uint32_t: object_type enum value]
[uint32_t: number of objects]
[fc::raw packed object 1]
[fc::raw packed object 2]
...
```

### 8.3 File Extension

`.viz-snapshot` (e.g., `snapshot-12345678.viz-snapshot`)

---

## 9. Snapshot Creation Process

### 9.1 When to Create a Snapshot

The snapshot should be created at an **irreversible block** boundary. This ensures:
- No undo state needs to be captured
- The state is final and will not be reverted
- Fork database is clean at this point

### 9.2 Snapshot Creation Approaches

There are several approaches to creating a snapshot while maintaining consistency:

#### 9.2.1 Full Node Pause (Simple, with Downtime)

```
1. Stop accepting new blocks from P2P
2. Wait for current block processing to complete
3. Lock all operations (API + block application)
4. Create snapshot
5. Resume operations
```

**Pros**: Simple to implement, guaranteed consistency
**Cons**: Node unavailable during snapshot creation (several seconds)

#### 9.2.2 Read Lock Without API Pause (Recommended)

Use `with_strong_read_lock` at the irreversible block boundary. This is the recommended approach.

```
1. Wait for current block to become irreversible (LIB)
   - Irreversible blocks cannot be reorganized
   - This guarantees state consistency
2. Call with_strong_read_lock([&]() { ... })
   - API read requests continue to work
   - Only new block application is blocked (write)
3. Serialize all objects to file
4. Release read lock automatically when lambda exits
5. Continue P2P sync (buffered blocks are applied in batch)
```

**Pros**:
- API remains available for reads
- Minimal downtime — only during serialization
- Consistency guaranteed by read lock + irreversible boundary

**Cons**:
- New blocks not applied for several seconds
- P2P buffers incoming blocks, then applies them in batch

**Implementation Example**:

```cpp
void snapshot_plugin::create_snapshot(const fc::path& output_path) {
    auto& db = _chain_db->db();

    // 1. Get current LIB
    uint32_t lib = db.last_non_undoable_block_num();
    auto lib_block = db.fetch_block_by_number(lib);

    // 2. Acquire strong read lock
    db.with_strong_read_lock([&]() {
        // 3. Write header with LIB info
        snapshot_header header;
        header.snapshot_block_num = lib;
        header.snapshot_block_id = lib_block->id();
        header.last_irreversible_block_num = lib;
        header.last_irreversible_block_id = lib_block->id();
        header.chain_id = db.get_chain_id();
        header.snapshot_creation_time = fc::time_point::now();

        // 4. Serialize all object types (CRITICAL + IMPORTANT + OPTIONAL)
        std::vector<char> payload;
        payload.reserve(64 * 1024 * 1024); // pre-allocate

        // Export each index...
        export_section<dynamic_global_property_index>(db, payload);
        export_section<account_index>(db, payload);
        // ... all other indexes ...

        // 5. Write fork_db seed block (the LIB block itself)
        fc::raw::pack(payload, *lib_block);

        // 6. Compute SHA256 checksum
        auto checksum = fc::sha256::hash(payload.data(), payload.size());
        header.payload_sha256 = checksum;

        // 7. Write to file (magic + version + header + payload)
        write_snapshot_file(output_path, header, payload);
    });

    // 8. Read lock released automatically here
    // 9. Node continues normal operation
}
```

**Key Points**:
- `with_strong_read_lock` is already used in VIZ codebase (see `database.cpp`)
- Lock is acquired at the database level, preventing any writes
- API read operations (`with_strong_read_lock` on their own) can proceed in parallel
- The lock duration depends on snapshot size (~44 MB takes ~1-3 seconds to serialize)
- P2P layer buffers incoming blocks during this time and applies them after lock release

#### 9.2.3 Asynchronous Copy-on-Write (Complex)

Make a file-level copy of shared_memory, then serialize from the copy.

**Pros**: No blocking at all
**Cons**: Complex implementation, requires filesystem support, disk space doubled temporarily

### 9.3 Creation Algorithm (Detailed)

```
1. Wait for block to become irreversible (or use current LIB)
2. Acquire strong read lock on database
3. Write JSON header with metadata
4. For each object type in CRITICAL + IMPORTANT categories:
   a. Iterate all objects via get_index<T>().indices()
   b. Serialize each object using fc::raw::pack()
   c. Write section to file
5. Write fork_db head block (the signed_block at LIB)
6. Compute and write checksum
7. Release read lock
8. Validate snapshot by loading it in a test instance
```

### 9.4 Optimization: Filter Expired Objects

At snapshot time, skip objects that are already expired:
- `transaction_object` with `expiration <= snapshot_time`
- `vesting_delegation_expiration_object` with `expiration <= snapshot_time`
- `account_recovery_request_object` with `expires <= snapshot_time`
- `award_shares_expire_object` with `expires <= snapshot_time`
- `invite_object` with `status == used`

---

## 10. Snapshot Loading Process (DLT Mode Startup)

### 10.1 Loading Algorithm

```
1. Open chainbase database with shared_memory_file
2. Call init_schema() and initialize_indexes()
3. Read snapshot file header, validate version and chain_id
4. For each object type section in the snapshot:
   a. Create objects in chainbase using create<T>()
   b. Validate object IDs match expected sequence
5. After all objects loaded:
   a. Validate dynamic_global_property_object exists and is consistent
   b. Validate witness_schedule_object exists
   c. Validate hardfork_property_object exists
   d. Verify object_counts match header
6. Call init_hardforks() (populates in-memory arrays from code)
7. Call initialize_evaluators()
8. Open block_log (if exists) or create empty one
9. Seed fork_db with head block from snapshot
10. Set chainbase revision to head_block_number
11. Begin P2P sync from last_irreversible_block_num + 1
```

### 10.2 Validation After Load

After loading, the following invariants must hold:

1. `dynamic_global_property_object.head_block_number` == `snapshot_block_num` (from header)
2. `dynamic_global_property_object.head_block_id` == `snapshot_block_id` (from header)
3. `hardfork_property_object.current_hardfork_version` >= compiled minimum
4. Sum of all `account_object.balance` + `committee_fund` + ... == `current_supply`
5. Sum of all `account_object.vesting_shares` == `total_vesting_shares`
6. At least one `witness_object` with valid `signing_key`

### 10.3 DLT Mode P2P Sync

After snapshot load, the node must sync blocks from `last_irreversible_block_num + 1` to the current chain head:

```
1. Connect to P2P peers
2. Request blocks starting from last_irreversible_block_num + 1
3. For each received block:
   a. Validate block header (witness, timestamp)
   b. Apply block with normal validation
   c. Add to block_log (if running with block_log)
4. Once caught up, operate normally
```

---

## 11. Implementation: Plugin Architecture

### 11.1 Plugin Design

The snapshot functionality should be implemented as an `appbase::plugin` named `snapshot_plugin`.

```
snapshot_plugin
  |-- snapshot_creation   (command: create snapshot)
  |-- snapshot_loading    (startup: --snapshot=path/to/snapshot)
  |-- snapshot_validation (verify loaded state)
```

### 11.2 CLI Options

```
--snapshot=path/to/snapshot.viz-snapshot   Load from snapshot instead of replay
--create-snapshot=path/to/output           Create snapshot at current head block
--snapshot-at-block=N                      Create snapshot when block N is reached
```

### 11.3 Integration with database::open()

Modified startup flow:

```
if (snapshot_path specified) {
    database::open_snapshot(snapshot_path);  // Load from snapshot
    // Skip reindex, skip block_log verification
    // Start P2P sync from LIB + 1
} else {
    database::open(data_dir, ...);  // Normal startup
}
```

### 11.4 Key Implementation Files to Modify/Create

| File | Action | Description |
|------|--------|-------------|
| `plugins/snapshot/CMakeLists.txt` | Create | Build configuration |
| `plugins/snapshot/snapshot_plugin.hpp` | Create | Plugin header |
| `plugins/snapshot/snapshot_plugin.cpp` | Create | Plugin implementation (create/load/validate) |
| `plugins/snapshot/include/graphene/plugins/snapshot/snapshot_serializer.hpp` | Create | Serialization/deserialization logic |
| `libraries/chain/database.hpp` | Modify | Add `open_snapshot()` method |
| `libraries/chain/database.cpp` | Modify | Implement `open_snapshot()` |
| `programs/vizd/main.cpp` | Modify | Add --snapshot CLI option |

---

## 12. Complete Object List Summary

### CRITICAL (Must Include)

| # | Object Type | Index Type | Key Fields | Est. Count |
|---|-------------|------------|------------|------------|
| 1 | `dynamic_global_property_object` | `dynamic_global_property_index` | head_block, supply, vesting totals | 1 |
| 2 | `witness_schedule_object` | `witness_schedule_index` | current schedule, virtual time | 1 |
| 3 | `hardfork_property_object` | `hardfork_property_index` | processed_hardforks, versions | 1 |
| 4 | `account_object` | `account_index` | balances, vesting, energy, proxy | ~accounts |
| 5 | `account_authority_object` | `account_authority_index` | master/active/regular auth | ~accounts |
| 6 | `witness_object` | `witness_index` | signing_key, votes, schedule | ~witnesses |
| 7 | `witness_vote_object` | `witness_vote_index` | account-witness link | ~votes |
| 8 | `block_summary_object` | `block_summary_index` | block_id (TaPOS) | 65536 |
| 9 | `content_object` | `content_index` | rshares, cashout, payouts | ~content |
| 10 | `content_vote_object` | `content_vote_index` | vote weight, rshares | ~votes |
| 11 | `block_post_validation_object` | `block_post_validation_index` | witness validations | ~recent blocks |

### IMPORTANT (Should Include)

| # | Object Type | Index Type | Key Fields | Est. Count |
|---|-------------|------------|------------|------------|
| 12 | `transaction_object` | `transaction_index` | trx_id, expiration | ~pending |
| 13 | `vesting_delegation_object` | `vesting_delegation_index` | delegator, delegatee, shares | ~delegations |
| 14 | `vesting_delegation_expiration_object` | `vesting_delegation_expiration_index` | expiration, delegator | ~pending |
| 15 | `fix_vesting_delegation_object` | `fix_vesting_delegation_index` | delegator, delegatee | ~fixes |
| 16 | `withdraw_vesting_route_object` | `withdraw_vesting_route_index` | from, to, percent | ~routes |
| 17 | `escrow_object` | `escrow_index` | from, to, agent, balances | ~escrows |
| 18 | `proposal_object` | `proposal_index` | author, title, ops, approvals | ~proposals |
| 19 | `required_approval_object` | `required_approval_index` | account, proposal | ~approvals |
| 20 | `committee_request_object` | `committee_request_index` | creator, worker, amounts | ~requests |
| 21 | `committee_vote_object` | `committee_vote_index` | voter, request, percent | ~votes |
| 22 | `invite_object` | `invite_index` | creator, key, balance | ~invites |
| 23 | `award_shares_expire_object` | `award_shares_expire_index` | expires, rshares | ~pending |
| 24 | `paid_subscription_object` | `paid_subscription_index` | creator, levels, period | ~subscriptions |
| 25 | `paid_subscribe_object` | `paid_subscribe_index` | subscriber, creator, timing | ~subscribers |
| 26 | `witness_penalty_expire_object` | `witness_penalty_expire_index` | witness, penalty, expires | ~penalties |

### OPTIONAL (Include for Full Mode)

| # | Object Type | Index Type | Key Fields | Est. Count |
|---|-------------|------------|------------|------------|
| 27 | `account_metadata_object` | `account_metadata_index` | account, json_metadata | ~accounts |
| 28 | `content_type_object` | `content_type_index` | content, body, title | ~content |
| 29 | `master_authority_history_object` | `master_authority_history_index` | account, prev_master | ~history |
| 30 | `account_recovery_request_object` | `account_recovery_request_index` | account, new_master | ~pending |
| 31 | `change_recovery_account_request_object` | `change_recovery_account_request_index` | account, recovery, date | ~pending |
| 32 | `custom_protocol_object` | `custom_protocol_index` | account, custom_sequence | ~accounts (plugin-conditional) |

### EXCLUDED

| # | Object Type | Reason |
|---|-------------|--------|
| - | `block_stats_object` | No implementation found |
| - | Plugin indexes (follow, tags, etc.) | Non-consensus, API-only, rebuildable |

---

## 13. Estimated Snapshot Size

Based on typical VIZ chain state:

| Category | Object Count | Avg Size (bytes) | Total |
|----------|-------------|-------------------|-------|
| `dynamic_global_property_object` | 1 | ~200 | ~0.2 KB |
| `witness_schedule_object` | 1 | ~500 | ~0.5 KB |
| `hardfork_property_object` | 1 | ~200 | ~0.2 KB |
| `account_object` | ~5,000 | ~800 | ~4 MB |
| `account_authority_object` | ~5,000 | ~300 | ~1.5 MB |
| `witness_object` | ~100 | ~600 | ~60 KB |
| `witness_vote_object` | ~5,000 | ~30 | ~150 KB |
| `block_summary_object` | 65,536 | ~40 | ~2.5 MB |
| `content_object` | ~50,000 | ~500 | ~25 MB |
| `content_vote_object` | ~200,000 | ~50 | ~10 MB |
| `transaction_object` | ~100 | ~500 | ~50 KB |
| `vesting_delegation_object` | ~2,000 | ~80 | ~160 KB |
| Other IMPORTANT objects | ~1,000 | ~200 | ~200 KB |
| **TOTAL** | | | **~44 MB** |

With compression (zstd/lz4): estimated **~8-15 MB**

This is dramatically smaller than the full shared_memory file (which can be several GB) and the block_log.

---

## 14. Security Considerations

### 14.1 Snapshot Trust Model

The snapshot is a **trusted state**. Loading a snapshot means trusting the creator of the snapshot. This is acceptable for:

- Node operators creating their own snapshots
- Community-provided snapshots with known hashes
- Snapshots verifiable against a known block hash

### 14.2 Snapshot Integrity

- SHA256 checksum of the entire snapshot file
- Chain ID verification to prevent cross-chain loading
- Block ID verification against known checkpoints (if available)
- Supply invariant checks after loading

### 14.3 Snapshot Authenticity

For production deployments, snapshot files should be distributed with:
- A SHA256 hash signed by a trusted key
- Or a Merkle proof linking the snapshot to a known block hash

---

## 15. Open Questions and Future Work

1. **Incremental snapshots**: Support creating delta snapshots from a previous snapshot (reduce creation time and storage)
2. **Snapshot compression**: Integrate zstd compression for smaller files
3. **Partial snapshot loading**: Load only specific object types (e.g., only consensus-critical for minimal nodes)
4. **Snapshot streaming**: Stream snapshot creation to avoid holding entire state in memory
5. **Block log pruning**: After snapshot load, trim block_log to only keep blocks after LIB
6. **Automatic snapshot schedule**: Periodically create snapshots at configurable intervals
7. **Snapshot validation tool**: Standalone tool to verify a snapshot file without loading it into a running node
8. **Cross-version compatibility**: Handle snapshot migration when object schemas change between hardforks
