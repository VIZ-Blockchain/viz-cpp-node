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

- **Only direct active/regular requirements can be delegated:** `proposal_create` and `proposal_delete` are grantable. A create grant allows proposing operations outside the agent's list but does not approve or execute them; normal required approvals are still needed. A delete grant allows only proposals the principal may ordinarily veto (as author or required authority), never unrelated proposals. `proposal_update` is denied: an approval could execute operations outside the agent's list. `account_update` is denied because its active variant can rotate the principal's authority. Grant validation and transaction execution both reject those two names, including stale invalid rows. Master-only operations (`recover_account`, `change_recovery_account`, `set_account_price`, `set_subaccount_price`, `target_account_sale`), virtual operations, legacy aliases and HF4-deprecated wire operations (`vote`, `content`, `delete_content`) are rejected at grant time; master authority is never substituted.
- **Sensitive grantable rights:** `set_agent_permission` is a broad-right operation: an agent granted it can create another agent with `transfer` or any other grantable scope, including a second key it controls. Treat it as equivalent to granting all delegable operations, not a narrow management permission (the chain regression exercises this escalation). `set_withdraw_vesting_route` can redirect future withdrawals; `account_create`, `validator_update`, `chain_properties_update`, `versioned_chain_properties_update` and committee operations can have substantial account or governance effects under their ordinary evaluator rules. Scope is by operation name, not amount, destination or account state; asset-moving operations likewise have their normal effects.
- **Shared keys:** multiple names for one principal may use the same public key; their effective operation lists are combined while each row remains live. Revoking one name does not revoke another.
- **At most 16 agents** per principal. A grant first removes the principal's expired agents.
- **When an agent signature counts:** only a direct active/regular requirement for the principal and the named operation is covered. Every operation in a transaction is checked independently; unrelated requirements still need their own signatures. The agent never grants authority through nested `account_auths` or `other` authorities. A revoke or rotation earlier in the same transaction takes effect before later operations.
- **Wipes:** all agents of the principal are removed on master change, active change, account recovery, direct sale and auction close. A regular-only change keeps them. "Change" means the field is **present** in `account_update`: sending `master` or `active` wipes the agents even if the key is the same, so a client that only edits regular or memo must leave `master`/`active` out of the operation (in viz-php-lib `build_account_update` pass `null` for a role to leave it out).

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

## In wallets

**Granting (principal).** WebVIZWallet → *Agents*: agent name, public key (the *Generate* button creates a fresh pair — save the private key, it is shown once), tick the allowed operations, expiration (*perpetual* or a date), addons as a comma-separated list. The page lists current agents with a *Revoke* button. The grant is signed by the principal's **active** key. Vizonator exposes the same operation to sites through `window.vizonator`, always behind a confirmation window that shows the key, operations, expiration and addons.

**Signing in as an agent.** WebVIZWallet and Vizonator have a separate *agent sign-in*: principal account + agent private key. The client checks the key against `get_agent_permissions` and refuses a key that is not granted or has expired. An agent session is marked 🤖 and shows the allowed operations and the time left; actions outside the list are disabled. An agent key cannot pass passwordless site auth or `sign_data` — it only signs the granted operations.

**When agents disappear.** Changing master or active (including sending the same key), recovery and account sale wipe all agents — grant them again afterwards.
