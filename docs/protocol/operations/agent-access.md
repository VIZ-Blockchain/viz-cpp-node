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

- **Delegable only for direct active/regular requirements:** proposal wrappers (`proposal_create`, `proposal_update`, `proposal_delete`) are denied because approvals can execute operations outside the agent's list later. `account_update` is denied because its active variant can rotate the principal's authority. Master-only operations (`recover_account`, `change_recovery_account`, `set_account_price`, `set_subaccount_price`, `target_account_sale`), virtual operations, legacy aliases and the HF4-deprecated wire operations (`vote`, `content`, `delete_content`) are also rejected at grant time; master authority is never substituted.
- **Sensitive grantable rights:** `set_agent_permission` is a broad-right operation: an agent granted it can create another agent with `transfer` or any other grantable scope, including a second key it controls. Treat it as equivalent to granting all delegable operations, not a narrow management permission (the chain regression exercises this escalation). `set_withdraw_vesting_route` can redirect future withdrawals; `account_create`, `validator_update`, `chain_properties_update`, `versioned_chain_properties_update` and committee operations can have substantial account or governance effects under their ordinary evaluator rules. Scope is by operation name, not amount, destination or account state; asset-moving operations likewise have their normal effects.
- **Shared keys:** multiple names for one principal may use the same public key; their effective operation lists are combined while each row remains live. Revoking one name does not revoke another.
- **At most 16 agents** per principal. A grant first removes the principal's expired agents.
- **When an agent signature counts:** only a direct active/regular requirement for the principal and the named operation is covered. Every operation in a transaction is checked independently; unrelated requirements still need their own signatures. The agent never grants authority through nested `account_auths` or `other` authorities. A revoke or rotation earlier in the same transaction takes effect before later operations.
- **Wipes:** all agents of the principal are removed on master change, active change, account recovery, direct sale and auction close. A regular-only change keeps them. "Change" means the field is **present** in `account_update`: sending `master` or `active` wipes the agents even if the key is the same, so a client that only edits regular or memo must leave `master`/`active` out of the operation (viz-php-lib `build_account_update` always sends all three — it wipes).

## Snapshot round-trip gate (isolated testnet)

A serializer-only check is not a substitute for a snapshot import. On a disposable
HF15 testnet database containing at least one live agent with nonempty `operations`
and `addons` and non-epoch `expiration`, stop the node and record
`database_api.get_agent_permissions(account)` and the head block ID. Then run the
*same testnet binary* with `--data-dir <source-dir> --plugin snapshot
--create-snapshot <scratch>/agents.json` (one-shot; it exits). Confirm the JSON
`state.agent_permission` section is present and nonempty. Start a second node with
an **empty, distinct** data directory and network disabled, using `--snapshot
<scratch>/agents.json --plugin snapshot`. Query `get_agent_permissions(account)`
on the imported node; compare name, public key, operations, addons, expiration,
object count and head block ID against the source. Never point either command at
mainnet data or reuse source shared memory for the import.

## Reading agents

`database_api.get_agent_permissions(account)` — see [database_api](../../plugins/database-api.md#get-agent-permissions-account).
