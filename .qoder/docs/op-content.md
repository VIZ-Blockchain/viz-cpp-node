# VIZ Blockchain — Content & Custom Operations

Spec for implementing content and custom operations in PHP/Node.js libraries.

> **Note:** `vote_operation` (ID 0), `content_operation` (ID 1), and `delete_content_operation` (ID 9) are **deprecated**. They remain in the operation variant for historical compatibility but should not be used in new code. Shown here for completeness.

---

## `content_operation` *(deprecated)*

**Type ID:** `1`
**Required authority:** `regular` of `author`

Creates or updates content (post or comment).

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `parent_author` | `account_name_type` | yes | Author of parent content (empty `""` for root post) |
| `parent_permlink` | `string` | yes | Permlink of parent (category/tag for root posts) |
| `author` | `account_name_type` | yes | Content author |
| `permlink` | `string` | yes | Unique identifier per author |
| `title` | `string` | yes | Post title |
| `body` | `string` | yes | Post body (Markdown) |
| `curation_percent` | `int16_t` | yes | Curation reward share in basis points (0–10000) |
| `json_metadata` | `string` | yes | JSON metadata |
| `extensions` | `content_extensions_type` | yes | Optional beneficiaries |

### Beneficiaries Extension

To add beneficiaries, add to `extensions`:
```json
[
  [0, {
    "beneficiaries": [
      {"account": "bob", "weight": 2500}
    ]
  }]
]
```

### Checklist
- [ ] `permlink` must be unique per author
- [ ] `parent_author == ""` → root post; otherwise comment
- [ ] `curation_percent` range 0–10000 (must be within chain min/max)
- [ ] Beneficiary weights sum must be <= 10000 (100%)
- [ ] Beneficiaries list must be sorted by account name ascending
- [ ] **Deprecated** — avoid creating new content with this operation

---

## `vote_operation` *(deprecated)*

**Type ID:** `0`
**Required authority:** `regular` of `voter`

Casts a vote on content.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `voter` | `account_name_type` | yes | Voting account |
| `author` | `account_name_type` | yes | Content author |
| `permlink` | `string` | yes | Content permlink |
| `weight` | `int16_t` | yes | Vote weight (-10000 to 10000) |

### Checklist
- [ ] `weight` > 0 → upvote; `weight` < 0 → flag/downvote
- [ ] `weight == 0` → remove vote
- [ ] Flag votes may cost extra energy (see `flag_energy_additional_cost` chain property)
- [ ] **Deprecated** — avoid in new code

---

## `delete_content_operation` *(deprecated)*

**Type ID:** `9`
**Required authority:** `regular` of `author`

Deletes a piece of content.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `author` | `account_name_type` | yes | Content author |
| `permlink` | `string` | yes | Content permlink to delete |

### Checklist
- [ ] Content must have no pending payout to be deletable
- [ ] **Deprecated** — avoid in new code

---

## `custom_operation`

**Type ID:** `10`
**Required authority:** `active` or `regular` of signers

Posts arbitrary JSON data to the blockchain. Used by applications for custom logic/protocols.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `required_active_auths` | `flat_set<account_name_type>` | yes | Accounts requiring active auth |
| `required_regular_auths` | `flat_set<account_name_type>` | yes | Accounts requiring regular auth |
| `id` | `string` | yes | Application-defined ID (max 32 characters) |
| `json` | `string` | yes | Valid UTF-8 JSON string |

### JSON Example

```json
[10, {
  "required_active_auths": [],
  "required_regular_auths": ["alice"],
  "id": "my_app",
  "json": "{\"action\":\"follow\",\"target\":\"bob\"}"
}]
```

### PHP Example

```php
$op = [
    'type' => 'custom_operation',
    'value' => [
        'required_active_auths'  => [],
        'required_regular_auths' => ['alice'],
        'id'                     => 'my_app',
        'json'                   => json_encode(['action' => 'follow', 'target' => 'bob']),
    ],
];
```

### Node.js Example

```js
const op = ['custom', {
    required_active_auths: [],
    required_regular_auths: ['alice'],
    id: 'my_app',
    json: JSON.stringify({ action: 'follow', target: 'bob' }),
}];
```

### Checklist
- [ ] `id` must be <= 32 characters
- [ ] `json` must be valid UTF-8 JSON
- [ ] At least one of `required_active_auths` or `required_regular_auths` must be non-empty
- [ ] `required_active_auths` entries → sign with those accounts' active keys
- [ ] `required_regular_auths` entries → sign with those accounts' regular keys
- [ ] Both `required_active_auths` and `required_regular_auths` may be populated simultaneously
- [ ] Data operations may cost additional bandwidth (see `data_operations_cost_additional_bandwidth`)
- [ ] `json` field is considered a "data operation" for bandwidth purposes
