# Database Schema

VIZ Ledger uses ChainBase — a memory-mapped, multi-index persistent store built on Boost.Interprocess. All chain state lives in `shared_memory.bin`. Each object type is associated with a Boost.MultiIndex container that defines its primary and secondary indexes.

---

## Object Type Registry

Every persistent object has a unique numeric type ID declared in `chain_object_types.hpp`. The full set of tracked object types:

| Object | Notes |
|--------|-------|
| `dynamic_global_property` | Singleton: current chain state, head block, LIB, inflation |
| `account` | All registered accounts |
| `account_authority` | master / active / regular authority sets |
| `witness` (validator) | Validator registrations, signing keys, vote counts |
| `transaction` | Pending / recent transactions (TAPOS window) |
| `block_summary` | 65536-slot TAPOS buffer of block IDs |
| `witness_schedule` | Singleton: active validator schedule |
| `content` | Posts and comments (deprecated) |
| `content_type` | Content title/body metadata |
| `content_vote` | Votes on content |
| `witness_vote` | Validator votes from accounts |
| `hardfork_property` | Singleton: current/next hardfork tracking |
| `withdraw_vesting_route` | Withdrawal routing rules |
| `master_authority_history` | Master key change history |
| `account_recovery_request` | Pending account recovery |
| `change_recovery_account_request` | Pending recovery account changes |
| `escrow` | Escrow transfers |
| `vesting_delegation` | Active SHARES delegations |
| `fix_vesting_delegation` | Delegation fix records |
| `vesting_delegation_expiration` | Delegations in return window |
| `account_metadata` | Account JSON metadata |
| `proposal` | Governance proposals |
| `required_approval` | Proposal approval requirements |
| `committee_request` | Committee funding requests |
| `committee_vote` | Committee votes |
| `invite` | Account invites |
| `award_shares_expire` | Expiring award shares |
| `paid_subscription` | Subscription offerings |
| `paid_subscribe` | Active subscriptions |
| `witness_penalty_expire` | Validator miss-penalty expirations |
| `block_post_validation` | Block post-validation records |

---

## Account Object

Accounts store balances, vesting state, delegation metrics, bandwidth, auction/sale flags, and governance participation.

**Key fields:** `name`, `balance` (VIZ), `vesting_shares`, `delegated_vesting_shares`, `received_vesting_shares`, `energy`, `next_vesting_withdrawal`, `witnesses_voted_for`, `recovery_account`.

**Indexes:**

| Tag | Key | Type |
|-----|-----|------|
| `by_id` | `id` | unique |
| `by_name` | `name` | unique |
| `by_account_on_sale` | sale flag | non-unique |
| `by_account_on_auction` | auction flag | non-unique |
| `by_account_on_sale_start_time` | sale start time | non-unique |
| `by_subaccount_on_sale` | subaccount sale flag | non-unique |
| `by_next_vesting_withdrawal` | `(next_vesting_withdrawal, id)` | composite |

The `by_next_vesting_withdrawal` composite index enables O(log N) batch processing of upcoming withdrawal installments.

---

## Content Object

Content objects represent posts and comments with voting, payout, and nesting metadata. **These objects are deprecated** — new applications should use `custom_operation` instead.

**Indexes on `content`:**

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_cashout_time` | `(cashout_time, id)` |
| `by_permlink` | `(author, permlink)` |
| `by_root` | `(root_content, id)` |
| `by_parent` | `(parent_author, parent_permlink, id)` |
| `by_last_update` | `(parent_author, last_update, id)` — API-heavy |
| `by_author_last_update` | `(author, last_update, id)` — API-heavy |

**Indexes on `content_vote`:**

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_content_voter` | `(content, voter)` — unique |
| `by_voter_content` | `(voter, content)` — unique |
| `by_voter_last_update` | `(voter, last_update, content)` |
| `by_content_weight_voter` | `(content, weight, voter)` — for leaderboards |

---

## Validator Objects

**`validator_object` indexes:**

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_name` | `owner` — unique |
| `by_vote_name` | `(votes, owner)` |
| `by_counted_vote_name` | `(counted_votes, owner)` |
| `by_schedule_time` | `(virtual_scheduled_time, id)` — O(log N) slot scheduling |

**`witness_vote_object` indexes:**

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_account_witness` | `(account, validator)` — unique |
| `by_witness_account` | `(validator, account)` — unique |

The `by_schedule_time` index is how the block production scheduler picks the next validator in O(log N) time.

---

## Proposal and Required Approval Objects

**`proposal_object` indexes:**

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_account` | `(author, title)` — unique |
| `by_expiration` | `expiration` — non-unique |

**`required_approval_object` indexes:**

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_account` | `(account, proposal)` |

---

## Invite Object

| Tag | Key |
|-----|-----|
| `by_id` | `id` |
| `by_invite_key` | public key — non-unique |
| `by_status` | status — non-unique |
| `by_creator` | creator — non-unique |
| `by_receiver` | receiver — non-unique |

---

## Auxiliary Objects

**`withdraw_vesting_route`:**

| Tag | Key |
|-----|-----|
| `by_withdraw_route` | `(from_account, to_account)` — unique |
| `by_destination` | `(to_account, id)` |

**`escrow`:**

| Tag | Key |
|-----|-----|
| `by_from_id` | `(from, escrow_id)` — unique |
| `by_to` | `(to, id)` |
| `by_agent` | `(agent, id)` |
| `by_ratification_deadline` | `(is_approved, ratification_deadline, id)` |

**`vesting_delegation`:**

| Tag | Key |
|-----|-----|
| `by_delegation` | `(delegator, delegatee)` — unique |

**`vesting_delegation_expiration`:**

| Tag | Key |
|-----|-----|
| `by_expiration` | `expiration` — non-unique |
| `by_account_expiration` | `(delegator, expiration)` |

---

## Fork Database

The fork database (`fork_database`) maintains an in-memory tree of blocks for managing chain forks. It operates separately from the persistent chainbase store.

**Linked index** — canonical chain blocks, indexed by block ID and block number.  
**Unlinked index** — orphaned or out-of-order blocks whose parent is not yet known.

```
Push Block
  ├── Parent known in linked index?
  │     YES → link block, insert into linked index, update head
  │     NO  → insert into unlinked index
  └── Attempt to link pending unlinked blocks
```

When a new block arrives and its ID matches the parent of an unlinked block, `_push_next()` cascades through the unlinked index and promotes those blocks to the linked chain.

**Branch operations:**
- `fetch_branch_from(first, second)` — walks both branches to find their common ancestor. Returns `(first_branch, second_branch)` for fork switching.
- `set_max_size(n)` — prunes blocks older than n, caps memory usage.
- `walk_main_branch_to_num(n)` — iterates main chain to a specific block number.

**Block validity:** Blocks flagged invalid are never promoted. Pushing a block outside the max reordering window raises an assertion.

---

## Index Management

Core indices are registered during `database::initialize_indexes()`. Plugins register additional indices via `add_plugin_index<T>()` in their `plugin_startup()`.

```cpp
// Core index registration (database.cpp)
add_core_index<account_index>();
add_core_index<witness_index>();
// ...

// Plugin index registration (plugin startup)
db.add_plugin_index<my_custom_index>();
```

---

## Object Relationships

```
account ──(author)──► content ──► content_vote ◄──(voter)── account
account ──(delegator)──► vesting_delegation ──► account (delegatee)
account ──(account)──► witness_vote ──► witness (validator)
account ──(author)──► proposal ──► required_approval ◄──(account)── account
account ──(creator/receiver)──► invite
escrow: from + to + agent → escrow_object
```

---

## Query Optimization Guidelines

**Fast lookups:**
- Account by name → `by_name` (unique, O(log N))
- Validator schedule → `by_schedule_time` (ordered by virtual time)
- Content by author+permlink → `by_permlink` (unique composite)
- Votes by content+weight → `by_content_weight_voter` (leaderboards)

**Batch processing:**
- Vesting withdrawals → iterate `by_next_vesting_withdrawal` forward
- Expiring delegations → iterate `by_expiration` forward
- Expiring proposals → iterate `by_expiration` forward

**Avoid full scans:** always use an indexed tag. Composite indexes are ordered by the leftmost key first — put the most selective or frequently filtered field first.

---

## Schema Extension for Plugins

To add a custom object type:

1. Define an object class inheriting from `chainbase::object<type_id, MyObject>`.
2. Declare a `chainbase::shared_multi_index_container` with the desired indexes.
3. Register via `db.add_plugin_index<MyIndex>()` in `plugin_startup()`.
4. Add `FC_REFLECT` macros for serialization.

```cpp
class my_object : public chainbase::object<my_object_type, my_object> {
    id_type          id;
    account_name_type account;
    uint64_t          value;
};

using my_index = chainbase::shared_multi_index_container<
    my_object,
    indexed_by<
        ordered_unique<tag<by_id>,
            member<my_object, my_object::id_type, &my_object::id>>,
        ordered_unique<tag<by_account>,
            member<my_object, account_name_type, &my_object::account>>
    >
>;
```

---

## Schema Evolution

New hardfork → new fields or objects. Guidelines:

- Keep existing primary key semantics stable across hardforks.
- Add new fields as optional or with defaults; never change existing field layout.
- Gate new index usage behind `has_hardfork()` checks during replay.
- Add new MultiIndex tags alongside existing ones — never remove a tag that replaying nodes might query.

See also: [Hardfork Management](./hardfork-management.md), [Shared Memory](../storage/shared-memory.md).

---

See also: [Plugin Development](../development/plugin-development.md), [Virtual Operations](../protocol/virtual-operations.md), [Hardfork Management](./hardfork-management.md).
