# Key history (HF15)

A DLT node starts from a snapshot and keeps no past blocks, so on its own it cannot tell which key an account held at some moment in the past. An account could sign something, change its keys and claim the signature was never theirs. From HF15 consensus keeps a permanent record of every key an account stopped standing behind, so the past can be proven from the current state alone.

## What is recorded

When an account's keys change, the node writes one row per member of the **old** authority of every role that actually changed:

| Field | Meaning |
|-------|---------|
| `account` | Whose key it was |
| `role` | `master`, `active`, `regular` or `memo` |
| `key` | The key; the null key when the member was an account |
| `auth_account` | The member account (`account_auths`); empty for a key |
| `weight` | Weight of this member; `0` for memo |
| `weight_threshold` | Threshold of the role; `0` for memo |
| `valid_until_block` | Last block in which the key still held (inclusive); the change is in the next block |
| `valid_until_time` | Time of that block |

If the old authority of a role was empty (no keys and no accounts), the node still writes one marker row for that role: `key` is the null key, `auth_account` is empty, `weight` is `0`. It keeps the rotation cooldown working for an empty→populated change; such rows also show up in `get_key_history_by_key` for the null key.

The row says: account A held key B in role R with weight D out of threshold E until block F at time G. "Since when" is the block after the `valid_until_block` of the previous row of the same role (or unknown, for keys that were already in place when HF15 activated — there is no earlier history to take it from). The current keys are in the account itself.

Rows are written by every path that changes keys: `account_update`, account recovery, direct sale and auction close. A role whose authority did not change writes nothing — re-sending the same authority is not a change. Rows are never removed. This is separate from the master authority history used by [recovery](operations/recovery.md), which is kept for 30 days only.

## Rate limit

From HF15 an **actual** change of `active`, `regular` or `memo_key` through `account_update` is allowed at most once an hour per role, like `master` already was. Each role is limited separately: changing regular does not block changing active in the same hour. Recovery and account sales are not limited.

## Read API

- [`database_api.get_key_history(account, from, limit)`](../plugins/database-api.md#get_key_historyaccount-from-limit) — the timeline of one account.
- [`database_api.get_key_history_by_key(key, limit)`](../plugins/database-api.md#get_key_history_by_keykey-limit) — who held a key and until when.
