# Agent Access (HF15)

An account (the **principal**) can register **agents**: named public keys that may broadcast a listed set of operations on the principal's behalf. An agent is not an account — it is a record on the principal. The principal's own keys are never shared, and one operation revokes the agent.

Typical uses: a trading bot that places prediction-market bets, a service that pays out transfers, or a key an external service (for example vizhub) accepts for its own login and actions.

---

## `set_agent_permission_operation` (ID 105)

**Auth:** `active` of `account`. Rejected before HF15.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Principal |
| `agent_name` | `string` | Agent name, unique per principal; `[a-z0-9_-]`, non-empty |
| `agent_key` | `public_key_type` | Agent key; required when granting, ignored on revoke |
| `operations` | `flat_set<string>` | Wire names of operations the key may sign (`transfer`, `pm_place_bet`, …) |
| `expiration` | `time_point_sec` | `1970-01-01T00:00:00` = perpetual; a past time revokes |
| `addons` | `flat_set<string>` | Off-chain scopes for external services (e.g. `vizhub`); at most 10, each shorter than 64 bytes, no `,`. The node stores them but never interprets them: they grant nothing on chain |
| `extensions` | `extensions_type` | Always `[]` |

Grant (bot may bet and transfer, vizhub accepts its key):

```json
["set_agent_permission", {
  "account": "alice",
  "agent_name": "trade-bot",
  "agent_key": "VIZ6MyX5QiXAXRZk7SYCiqpi6Mtm8UbHWDFSV8HPpt7FJyahCnc2T",
  "operations": ["pm_place_bet", "transfer"],
  "expiration": "2027-01-01T00:00:00",
  "addons": ["vizhub"],
  "extensions": []
}]
```

Addon-only agent (a key for an external service, no chain operations):

```json
["set_agent_permission", {
  "account": "alice",
  "agent_name": "hub-login",
  "agent_key": "VIZ7…",
  "operations": [],
  "expiration": "1970-01-01T00:00:00",
  "addons": ["vizhub"],
  "extensions": []
}]
```

Revoke — both lists empty (the key may be the null key `VIZ1111111111111111111111111111111114T1Anm`):

```json
["set_agent_permission", {
  "account": "alice", "agent_name": "trade-bot",
  "agent_key": "VIZ1111111111111111111111111111111114T1Anm",
  "operations": [], "expiration": "1970-01-01T00:00:00", "addons": [], "extensions": []
}]
```

Re-issuing by the same name replaces the key, operations, addons and expiration (key rotation = re-issue).

## Rules

- **Delegable only for direct active/regular requirements:** `set_agent_permission`, proposal wrappers and `account_update` are allowed explicitly; an agent granted `set_agent_permission` can grant broader rights, and an agent granted active-only `account_update` may rotate active keys. The agent never satisfies a master requirement, including an `account_update` carrying a master field. Master-only operations (`recover_account`, `change_recovery_account`, `set_account_price`, `set_subaccount_price`, `target_account_sale`), virtual operations and deprecated aliases are rejected.
- **Shared keys:** multiple names for one principal may use the same public key; their effective operation lists are combined while each row remains live. Revoking one name does not revoke another.
- **At most 16 agents** per principal. A grant first removes the principal's expired agents.
- **When an agent signature counts:** only a direct active/regular requirement for the principal and the named operation is covered. Every operation in a transaction is checked independently; unrelated requirements still need their own signatures. The agent never grants authority through nested `account_auths` or `other` authorities. A revoke or rotation earlier in the same transaction takes effect before later operations.
- **Wipes:** all agents of the principal are removed on master change, active change, account recovery, direct sale and auction close. A regular-only change keeps them. "Change" means the field is **present** in `account_update`: sending `master` or `active` wipes the agents even if the key is the same, so a client that only edits regular or memo must leave `master`/`active` out of the operation (viz-php-lib `build_account_update` always sends all three — it wipes).

## Reading agents

`database_api.get_agent_permissions(account)` — see [database_api](../../plugins/database-api.md#get-agent-permissions-account).
