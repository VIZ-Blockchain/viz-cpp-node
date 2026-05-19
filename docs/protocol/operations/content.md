# Content Operations

> **Deprecation notice:** `vote_operation` (ID 0), `content_operation` (ID 1), and `delete_content_operation` (ID 9) are **deprecated**. They remain in the operation variant for historical and archive compatibility but should not be used in new code. `custom_operation` (ID 10) is the active general-purpose data channel.

---

## `content_operation` *(deprecated)* (ID 1)

**Auth:** `regular` of `author`

Creates or updates a content object (post or comment). Content is addressed by `author` + `permlink`.

| Field | Type | Description |
|-------|------|-------------|
| `parent_author` | `account_name_type` | Author of parent content; `""` for a root post |
| `parent_permlink` | `string` | Permlink of parent; acts as category tag for root posts |
| `author` | `account_name_type` | Content author |
| `permlink` | `string` | Unique identifier within the author's namespace |
| `title` | `string` | Post title |
| `body` | `string` | Post body (Markdown) |
| `curation_percent` | `int16_t` | Curation reward share in basis points (0–10000) |
| `json_metadata` | `string` | JSON metadata string |
| `extensions` | `content_extensions_type` | Optional beneficiary list |

Beneficiary extension format (inside `extensions`):
```json
[[0, {
  "beneficiaries": [
    {"account": "bob", "weight": 2500}
  ]
}]]
```

- `parent_author == ""` → root post; otherwise a comment.
- `permlink` must be unique per author.
- `curation_percent` must be within the chain's `[min_curation_percent, max_curation_percent]` range.
- Beneficiary weights must sum to ≤ 10000 and be sorted by account name ascending.

---

## `vote_operation` *(deprecated)* (ID 0)

**Auth:** `regular` of `voter`

Casts a weighted vote on a piece of content.

| Field | Type | Description |
|-------|------|-------------|
| `voter` | `account_name_type` | Voting account |
| `author` | `account_name_type` | Content author |
| `permlink` | `string` | Content permlink |
| `weight` | `int16_t` | Vote weight: negative = flag, positive = upvote, 0 = remove vote |

- `weight` range: −10000 to 10000.
- Flag votes may incur extra energy cost (`flag_energy_additional_cost` chain property).

---

## `delete_content_operation` *(deprecated)* (ID 9)

**Auth:** `regular` of `author`

Deletes a content object.

| Field | Type | Description |
|-------|------|-------------|
| `author` | `account_name_type` | Content author |
| `permlink` | `string` | Content permlink to delete |

- Content with a pending payout cannot be deleted.

---

## `custom_operation` (ID 10)

**Auth:** `active` or `regular` of signers (at least one)

Posts arbitrary JSON data to the blockchain. Used by applications to build custom on-chain protocols.

| Field | Type | Description |
|-------|------|-------------|
| `required_active_auths` | `flat_set<account_name_type>` | Accounts requiring active-key signatures |
| `required_regular_auths` | `flat_set<account_name_type>` | Accounts requiring regular-key signatures |
| `id` | `string` | Application-defined namespace identifier (max 32 characters) |
| `json` | `string` | Valid UTF-8 JSON payload |

```json
[10, {
  "required_active_auths": [],
  "required_regular_auths": ["alice"],
  "id": "my_app",
  "json": "{\"action\":\"follow\",\"target\":\"bob\"}"
}]
```

- At least one of `required_active_auths` or `required_regular_auths` must be non-empty.
- Accounts in `required_active_auths` must sign with their **active** key.
- Accounts in `required_regular_auths` must sign with their **regular** key.
- Both sets may be populated simultaneously for multi-authority operations.
- The `json` field is counted as a data operation — may incur additional bandwidth cost (`data_operations_cost_additional_bandwidth` chain property).

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Awards](./awards.md).
