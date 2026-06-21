# Onix Prediction Market — VIZ DLT Protocol Specification

> Node-side (consensus-level) design for implementing the Onix Protocol as **first-class operations and ChainBase objects** on VIZ DLT — not `custom_json`, not smart contracts. Grounded in the VIZ node docs: [data-types](../viz-cpp-node/docs/protocol/data-types.md), [operations overview](../viz-cpp-node/docs/protocol/operations/overview.md), [escrow](../viz-cpp-node/docs/protocol/operations/escrow.md), [committee](../viz-cpp-node/docs/governance/committee.md), [chain-properties](../viz-cpp-node/docs/governance/chain-properties.md), [database-schema](../viz-cpp-node/docs/advanced/database-schema.md), [virtual-operations](../viz-cpp-node/docs/protocol/virtual-operations.md), [plugin-development](../viz-cpp-node/docs/development/plugin-development.md).
>
> Settlement model (both market types): **the AMM assigns weights (CPMM binary / LMSR multi); losers fund winners pro-rata by weight** — see [betting-rules](betting-rules-and-system-overview.md), [plan_unified_parimutuel_binary](plan_unified_parimutuel_binary.md). Front-running mitigation (instant default + opt-in batch/commit-reveal): [plan_batch_commit_reveal_betting](plan_batch_commit_reveal_betting.md).

---

> ## ⬛ СТАТУС РЕАЛИЗАЦИИ (HF14, на 2026-06-19)
>
> Аннотации ниже сверены с реально реализованным кодом (`libraries/chain/pm_evaluator.cpp`,
> `pm_objects.hpp`, `database.cpp`, ops в `libraries/protocol/...`, плагин `prediction_market_api`,
> snapshot) и интеграционными тестами `tests/consensus_sim/.../test_pm_lifecycle.cpp` (28 кейсов) +
> `tests/pm/*` (LMSR-векторы, parimutuel, leverage, meta-parse).
>
> **Легенда пометок:**
> - 🟢 **СДЕЛАНО** — реализовано как в спеке.
> - 🟡 **СДЕЛАНО С ОТЛИЧИЯМИ** — реализовано, но модель/детали изменены (см. «почему»).
> - 🔴 **НЕ СДЕЛАНО / ОТЛОЖЕНО** — особо выделено.
> - ➕ **СВЕРХ СПЕКИ** — добавлено того, чего в этом документе нет.
>
> **Главное:** фазирование из §8 НЕ соблюдено — вместо «Phase 1 binary-only» реализованы
> сразу **все три фазы**: binary CPMM + multi LMSR + batch/commit-reveal + полный Lazy Pool +
> **подсистема leverage (маржа)**, которой в этой спеке нет вовсе. Решение принято осознанно
> («у нас нет миграций, это не прод» — можно менять раскладку объектов свободно).

## 0. Conventions

- **Amounts:** `asset` (VIZ, 3 decimals) in operation params; stored internally as `share_type` (int64 satoshi). Permille fees are `uint16` basis-of-1000.
- **Accounts:** `account_name_type` (≤16 bytes). **Stake weight** for any voting = `effective_vesting_shares = vesting_shares − delegated_vesting_shares + received_vesting_shares` (same as committee voting).
- **Auth levels** (mirroring VIZ): financial actions → `active`; stake-weighted dispute voting → `regular` (same as `committee_vote_request`).
- **Time:** `time_point_sec`. Deterministic deadlines processed during block application via `by_<time>` indexes (same pattern as vesting withdrawals / escrow ratification).
- **New op IDs** start at **64** (regular) and **96** (virtual) to avoid collision with existing IDs (0–63). New `chain_object_types` IDs appended after the existing registry.
- **Hardfork:** all new objects/operations/chain-property version are gated behind a new hardfork (`CHAIN_HARDFORK_PM`); see [hardfork-management](../viz-cpp-node/docs/advanced/hardfork-management.md).

> 🟡 **§0 СДЕЛАНО С ОТЛИЧИЯМИ.**
> - **Op IDs «start at 64» — НЕ соблюдено (осознанно).** Операции просто **дописаны в конец**
>   `operation` static_variant; индекс варианта = консенсусный op-id. Нумерация «с 64» из спеки —
>   наследие прототипа и для VIZ неприменима (см. комментарий в `pm_operations.hpp`). Аналогично
>   виртуальные op-id 96+ из §4 не используются как отдельные номера.
> - **Хардфорк назван `CHAIN_HARDFORK_14`** (не `CHAIN_HARDFORK_PM`).
> - Amounts/accounts/auth/time/stake-weight — 🟢 как в спеке.

---

## 1. ChainBase Objects

Each object is `chainbase::object<type_id, T>` with a `shared_multi_index_container`. All financial sums are `share_type`. Indexes follow the VIZ convention (`by_id` unique + secondary indexes by account name / status / time for O(log N) lookups and bounded cron scans).

### 1.1 `pm_oracle_object`

Registered oracle: bonded insurance, fee policy, reputation counters.

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `owner` | account_name_type | Oracle account |
| `insurance` | share_type | Locked insurance bond (VIZ) |
| `fee_permille` | uint16 | Per-market % fee on losers' pool (≤ `pm_max_oracle_fee_permille`) |
| `fixed_fee` | share_type | Per-market fixed fee (paid by creator at acceptance) |
| `rules_url` | string (≤255) | Oracle policy/terms |
| `active_since` | time_point_sec | Registration time |
| `last_active_time` | time_point_sec | Last accept/resolve |
| `banned_until` | time_point_sec | 0 = not banned; >0 = temp; `time_point_sec::maximum()` = permanent |
| `markets_accepted` … `bans_received` | uint32 / share_type | The 14 reputation counters (accepted, resolved, no_contest, missed, disputes_received/lost/won/auto_closed, dispute_responses_missed, total_volume_resolved, total_insurance_slashed, avg_resolution_time, penalty_stamps) |

**Indexes:** `by_id`; `by_owner` (unique); `by_status` (banned/active); `by_insurance` (for min-bond checks).

> Reputation **score** is *computed on read* in the API plugin (not stored) — same approach as the prototype and as `compute_oracle_reliability_score`.

> 🟡 **§1.1 СДЕЛАНО С ОТЛИЧИЕМ — модель `fixed_fee` и оракульской фи изменена (offer→quote).**
> Спека: фикс-фи *платит создатель оракулу при acceptance* (перевод creator→oracle); оракульская
> фи задаётся создателем. **Реализовано иначе:** (1) фикс-фи и фи **финансируются из пула
> проигравших на сеттлменте**, не минтятся (`oracle_take = oracle_fee + oracle_fixed_paid`, см.
> `parimutuel.hpp`), держит zero-sum [[project_pm_zero_sum]]. (2) Условия фиксируются по схеме
> **offer→quote**: создатель на `create` объявляет *потолок* (`oracle_fee_percent`+`oracle_fixed_fee`),
> оракул на `accept` **котирует свои фактические** (≤ потолка и ≤ медианного кэпа), они замораживаются
> в объект рынка; self-oracle фиксирует своё при создании. Объект оракула хранит `fee_percent`/
> `fixed_fee` как **публичный прайс-лист** (advisory). Резолв читает только замороженные поля рынка,
> к медиане не обращается. Проверено `oracle_fixed_fee_external_vs_self` (#56, offer/quote + negative).
> Все 14 reputation-счётчиков + `penalty_stamps` — 🟢. **Все ‰ → bp (10000=100%).**

### 1.2 `pm_market_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK; referenced by bets/liquidity |
| `creator` | account_name_type | Market maker |
| `oracle` | account_name_type | Designated oracle |
| `market_type` | uint8 | 0 = binary (CPMM), 1 = multi (LMSR) |
| `outcome_count` | uint8 | 2 for binary; 3–10 for multi (≤ `pm_max_outcomes`) |
| `url` | string (≤255) | Question / resolution criteria |
| `status` | uint8 | −1 deleted, 0 waiting, 1 active, 2 closed, 3 resolved |
| `payout_status` | uint8 | 0 none, 1 calculated/pending, 2 paid, 3 disputed |
| `created_time` / `betting_expiration` / `result_expiration` | time_point_sec | Lifecycle timestamps |
| `resolved_outcome` | int16 | −1 unresolved/no-contest; else outcome index/side |
| **Binary CPMM** | | |
| `reserve_a`, `reserve_b`, `k` | share_type / uint128 | CPMM pricing state (`k` = `reserve_a·reserve_b`) |
| `a_bets_sum`, `b_bets_sum` | share_type | Stakes per side (for parimutuel + UI) |
| **Multi LMSR** | | |
| `lmsr_b` | share_type | Liquidity parameter (`= subsidy / ln(N)`) |
| `lmsr_subsidy` | share_type | LP subsidy (returned unconditionally) |
| **Common** | | |
| `bets_sum`, `liquidity_sum` | share_type | Totals |
| `oracle_fee_permille`, `creator_fee_permille`, `liquidity_fee_permille` | uint16 | Resolution-time fees from losers' pool |
| `oracle_fixed_fee` | share_type | Snapshotted from oracle at acceptance |
| `liquidity_fee_earned` | share_type | Already-paid LP fees (early withdrawals) |
| `forfeit_pool` | share_type | Non-revealed commit penalties → winners' pool |
| `time_penalty_type`, `time_penalty_value`, `penalty_curve_type` | uint8/uint32 | Late-bet penalty config |
| `allow_early_resolution`, `allow_cancellation`, `allow_batch` | bool | Flags |
| `allow_instant_bet` | bool | Default `true`. When `false`, instant `pm_place_bet` (mode=0) is rejected — every order MUST go through batch / commit-reveal (anti-MEV). Multi-outcome markets MUST keep this `true` until LMSR batch settlement is implemented. Mutual constraint: `allow_instant_bet || allow_batch` MUST be true at creation, otherwise the market would be unbettable. |
| `endogeneity_tier` | uint8 | 1 econ-data / 2 sports / 3 political (reflexivity-risk tag) |
| `current_epoch` | uint32 | Batch epoch counter |
| **Dispute routing** | | |
| `dispute_mode` | uint8 | 0 = **committee** (public stake vote); 1 = **account** (centralized resolver) |
| `dispute_resolver` | account_name_type | Resolver account when `dispute_mode==1` (e.g. a regulator multisig); empty for committee |

**Indexes:** `by_id`; `by_creator`; `by_oracle`; `by_status`; `by_betting_expiration` `(status, betting_expiration, id)`; `by_result_expiration` `(status, result_expiration, id)` (bounded cron scan for missed-resolution penalty); `by_payout_status`.

> 🟡 **§1.2 СДЕЛАНО + ➕ 2 поля сверх спеки.**
> К `pm_market_object` добавлены:
> - ➕ `dispute_penalty_percent` (int16, −10000..+10000) — политика наказания оракула на успешном
>   диспуте: `>0` слэш % страховки ×consensus_strength; `<0` good-faith (без слэша, оракулу бонус
>   из fee); `0` нет. Задаётся в `pm_create_market`. Покрыто кейсами committee/good-faith диспутов.
> - ➕ `metadata` (`shared_string`, **без cap**, как `custom_op`) — свободный клиентский JSON,
>   **консенсус-непрозрачный** (нода не валидирует/не интерпретирует), парсится офчейн плагином
>   (категория/теги/юрисдикции). Добавлено по запросу «отдельное json-поле metadata».
> Остальные поля (CPMM/LMSR/fees/flags/dispute routing) — 🟢 как в спеке.

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `outcome_index` | uint8 | 0…N−1 |
| `label` | string (≤64) | Display label |
| `q` | share_type | LMSR quantity |
| `bets_sum` | share_type | Stakes on this outcome (parimutuel losers/winners base) |
| `weight_sum` | share_type | Σ token weights on this outcome (parimutuel denominator) |
| `bets_count` | uint32 | Count |

**Indexes:** `by_id`; `by_market_outcome` `(market, outcome_index)` unique.

### 1.4 `pm_bet_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `account` | account_name_type | Bettor |
| `side` | int8 | Binary: 0/1; multi: −1 (use `outcome_index`) |
| `outcome_index` | int16 | Multi: 0…N−1; binary: −1 |
| `amount` | share_type | Stake |
| `weight` | share_type | Tokens (CPMM/LMSR) — relative claim |
| `price` | uint64 | Entry price (display, precision 1e6) |
| `time_penalty` | uint32 | Penalty ratio at placement (precision 1e6) |
| `mode` | uint8 | 0 instant, 1 batch, 2 commit-reveal |
| `epoch` | uint32 | Settlement epoch (batch/reveal) |
| `status` | uint8 | 0 active, 1 cancelled, 2 refunded, 3 resolved, 5 queued, 6 revealed-pending |
| `resolved_amount` | share_type | Final payout (set at resolution) |
| `created_time` | time_point_sec | Placement/submit time (time-penalty basis) |

**Indexes:** `by_id`; `by_market` `(market, id)`; `by_account` `(account, id)`; `by_market_account` `(market, account, id)`; `by_market_outcome` `(market, outcome_index, id)` and/or `(market, side, id)` (for Σweight); `by_epoch` `(market, epoch, status)` (batch settlement).

### 1.5 `pm_liquidity_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `provider` | account_name_type | LP (0/empty = Lazy Pool) |
| `amount` | share_type | Principal |
| `weight_a`, `weight_b` | share_type | Binary reserve shares (for principal-safe withdrawal) |
| `b_share` | share_type | Multi: share of `lmsr_b`/subsidy |
| `sec_to_expiration` | uint32 | Time-weight basis at deposit |
| `deposit_time` | time_point_sec | |
| `earned_fee` | share_type | Paid-out fees (early withdrawal) |
| `status` | uint8 | 0 active, 3 resolved/closed |

**Indexes:** `by_id`; `by_market` `(market, id)`; `by_provider` `(provider, id)`.

### 1.6 `pm_commit_object` (commit-reveal)

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `account` | account_name_type | Committer |
| `commitment` | sha256 | `H(market‖account‖side/outcome‖amount‖min_tokens‖salt)` |
| `escrow_amount` | share_type | Locked stake |
| `no_reveal_fee_permille` | uint16 | Penalty rate **snapshotted at commit** (consensus-checked, see §3.6) |
| `commit_time` | time_point_sec | |
| `reveal_deadline` | time_point_sec | |
| `status` | uint8 | 0 committed, 1 revealed, 2 forfeited |

**Indexes:** `by_id`; `by_market`; `by_account`; `by_reveal_deadline` `(status, reveal_deadline, id)` (bounded forfeit cron).

### 1.7 `pm_dispute_object`

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK (one active dispute per market) |
| `disputer` | account_name_type | Filer |
| `dispute_fee` | share_type | Escrowed fee |
| `filed_time` | time_point_sec | |
| `oracle_response_deadline` | time_point_sec | |
| `dispute_mode` | uint8 | Copied from market (0 committee / 1 account) |
| `voting_end_time` | time_point_sec | Committee mode: stake-vote tally time |
| `auto_close_time` | time_point_sec | Anti-freeze fallback |
| `proposed_outcome` | int16 | Disputer's claim |
| `status` | uint8 | 0 open, 1 oracle-wrong, 2 oracle-right, 3 auto-closed |

**Indexes:** `by_id`; `by_market` (unique active); `by_voting_end` `(status, voting_end_time, id)`; `by_auto_close` `(status, auto_close_time, id)`.

### 1.8 `pm_dispute_vote_object` (committee mode only)

| Field | Type | Description |
|-------|------|-------------|
| `id` | id_type | PK |
| `market` | pm_market_id_type | FK |
| `voter` | account_name_type | Any SHARES holder |
| `vote_outcome` | int16 | Outcome the voter believes correct; −1 = uphold oracle |
| `vote_percent` | int16 | Conviction / penalty intensity (−10000…10000) |
| `time` | time_point_sec | Last update |

**Indexes:** `by_id`; `by_market_voter` `(market, voter)` unique; `by_voter` `(voter, id)`.

> Voting weight is read at **tally time** as `effective_vesting_shares` (live), exactly like committee requests — votes store only the choice + percent, not a snapshotted weight.

### 1.9 Lazy Liquidity Pool (optional, phase 2)

- `pm_lazy_pool_object` (singleton): `total_shares`, `free_balance`, `allocated_balance`, `reward_per_share`, params.
- `pm_lazy_deposit_object` (per user): `account`, `shares`, `reward_snapshot`, `unlock_time`.
- `pm_lazy_allocation_object` (per market): `market`, `amount`, `recalled_amount`, `check_step`, `status`.

Indexes mirror the prototype's MasterChef accounting. Deferred to a later phase; not required for v1.

> 🟡 **§1.9 СДЕЛАНО ПОЛНОСТЬЮ — НЕ отложено** (вопреки «deferred to a later phase»).
> Реализован весь Lazy Pool: deposit/shares/lock, MasterChef `reward_per_share` (1e9), late-depositor
> fairness, unlock-consolidation, planned + emergency withdraw (со штрафом на залоченный профит),
> авто-аллокация в рынки, **graduated early recall с тайм-гейтом** (1/10 шаг за `(result−created)/10`),
> fault-stamp защита. Поля объектов расширены против эскиза (`earned_balance`, `original_amount`,
> `bets_sum_at_check`, `last_check_time`, `leverage_fund_used` и т.д.).
> **Найдены и починены консенсус-баги:** отсутствие синглтона пула (создаётся в HF14-хуке),
> эмиссия pending-награды при withdraw, recall без тайм-гейта. См. [[project_pm_leverage]],
> [[project_undo_all_recovery_hang]] не связан.
>
> ➕ **СВЕРХ СПЕКИ — подсистема Leverage (маржа), в этом документе отсутствует.**
> Добавлен `pm_leverage_position_object` (collateral+loan, `liquidation_threshold`,
> `cancel_value`, статусы liquidated/closed/converted) и 3 операции (см. §3). Заём фронтит
> Lazy Pool (`leverage_fund_used`), проценты пула — в `earned_balance`. Каскадная ликвидация при
> встречной/отменённой ставке, bad-debt поглощается пулом. Kill-switch `pm_leverage_enabled`
> (по умолчанию **false** — выключено на мейннете до решения governance). Frozen-математика в
> `pm/leverage.{hpp,cpp}`. Покрыто кейсами open/close, cascade-bad-debt, convert (#49/#50/#52).
>
> ➕ **СВЕРХ СПЕКИ — `pm_creator_ban_object`** (account → `banned_until`, `ban_count`). Введён,
> чтобы поле `ban_creator`/`ban_creator_until` в `pm_dispute_resolve` (см. §3.9) перестало быть
> no-op: account-mode резолвер банит создателя, `pm_create_market` отклоняет новые рынки пока бан
> в силе. Покрыто кейсом `dispute_bans_creator_from_new_markets` (#14).

---

## 2. Object Type Registry additions

Append to `chain_object_types.hpp` (after existing IDs): `pm_oracle`, `pm_market`, `pm_outcome`, `pm_bet`, `pm_liquidity`, `pm_commit`, `pm_dispute`, `pm_dispute_vote`, `pm_lazy_pool`, `pm_lazy_deposit`, `pm_lazy_allocation`. All registered via `db.add_core_index<...>()` during `initialize_indexes()` (these are **consensus** objects, not plugin-only).

> 🟡 **§2 СДЕЛАНО — фактический реестр шире (13 типов, не 11).** Порядок APPEND-ONLY соблюдён:
> 11 из спеки + ➕ `pm_leverage_position` + ➕ `pm_creator_ban` (дописаны в конец enum
> `chain_object_types`, id-typedef, FC_REFLECT_ENUM, `add_core_index` в `database.cpp`). Все —
> консенсусные core-индексы, все попадают в snapshot (§7.9).

---

## 3. Regular Operations (user-broadcast)

All carry standard validation in `validate()` (static) + `do_apply()` (stateful, in an evaluator). `has_hardfork(CHAIN_HARDFORK_PM)` gates all of them.

> 🟡 **§3 СДЕЛАНО + ➕ 5 операций сверх нумерованного списка спеки.**
> Реализованы все операции §3.2–§3.10, **плюс** (т.к. Lazy Pool и Leverage реализованы сразу):
> ➕ `pm_lazy_deposit`, ➕ `pm_lazy_withdraw`, ➕ `pm_leverage_open`, ➕ `pm_leverage_close`,
> ➕ `pm_leverage_convert`. Все дописаны в конец `operation` variant (см. §0). Гейт —
> `CHAIN_HARDFORK_14`.

### 3.1 Roles → who sends what

| Role | Operations |
|------|-----------|
| **Oracle** | `pm_oracle_register`, `pm_oracle_update`, `pm_oracle_accept_market`, `pm_resolve_market`, `pm_no_contest` |
| **Market maker / creator** | `pm_create_market`, `pm_add_liquidity`, `pm_withdraw_liquidity` (creator is auto first LP) |
| **User / bettor** | `pm_place_bet`, `pm_commit_bet`, `pm_reveal_bet`, `pm_cancel_bet`, `pm_transfer_position`, `pm_dispute_create`, `pm_add_liquidity` |
| **Stakeholder (committee dispute)** | `pm_dispute_vote` (any SHARES holder) |
| **Resolver account (centralized dispute)** | `pm_dispute_resolve` |

### 3.2 `pm_oracle_register` (ID 64) — auth: `active`

| Field | Type | Constraints |
|-------|------|-------------|
| `owner` | account_name_type | Must exist; not already registered |
| `insurance` | asset (VIZ) | ≥ `pm_min_oracle_insurance` |
| `fee_permille` | uint16 | ≤ `pm_max_oracle_fee_permille` |
| `fixed_fee` | asset (VIZ) | ≥ 0 |
| `rules_url` | string | ≤255 |

Charges `pm_oracle_registration_fee` → committee fund. Locks `insurance` from `owner` balance.

### 3.3 `pm_oracle_update` (ID 65) — auth: `active`
Top-up/withdraw insurance (withdraw blocked while oracle has active accepted markets or below min), change `fee_permille`/`fixed_fee`/`rules_url`.

### 3.4 `pm_create_market` (ID 66) — auth: `active`

| Field | Type | Constraints |
|-------|------|-------------|
| `creator` | account_name_type | Payer of creation fee + initial liquidity |
| `oracle` | account_name_type | Registered oracle (or `creator` if self-oracle) |
| `market_type` | uint8 | 0/1 |
| `outcomes` | vector<string> | size 2 (binary) or 3…`pm_max_outcomes` (multi) |
| `url` | string | resolution criteria, ≤255 |
| `oracle_fee_permille`, `creator_fee_permille`, `liquidity_fee_permille` | uint16 | each ≤ caps; Σ ≤ `pm_max_total_fee_permille` |
| `liquidity` | asset (VIZ) | ≥ `pm_min_liquidity` (binary reserves) / ≥ subsidy floor (multi) |
| `betting_expiration`, `result_expiration` | time_point_sec | future; `result > betting`; ≤ `pm_max_market_duration` |
| `time_penalty_type`, `time_penalty_value`, `penalty_curve_type` | uint8/uint32/uint8 | within bounds |
| `allow_early_resolution`, `allow_cancellation`, `allow_batch` | bool | |
| `allow_instant_bet` | bool | default `true`; when `false` requires `allow_batch=true` (else rejected); ignored / forced `true` for `market_type==1` (multi LMSR has no batch path yet) |
| `endogeneity_tier` | uint8 | 1–3 |
| `dispute_mode` | uint8 | 0 committee / 1 account |
| `dispute_resolver` | account_name_type | required & must exist iff `dispute_mode==1`; ignored for committee |

Charges `pm_market_creation_fee` → committee fund. Locks `liquidity`. Binary: `reserve_a=reserve_b=liquidity/2`, `k`. Multi: `lmsr_b=liquidity/ln(N)`, creates N `pm_outcome_object`. Creator inserted as first `pm_liquidity_object`. Self-oracle → status 1 immediately (insurance check); else status 0.

### 3.5 `pm_oracle_accept_market` (ID 67) — auth: `active` of `oracle`
`{oracle, market_id, accept}`. accept→ status 0→1, transfer `oracle_fixed_fee` creator→oracle, snapshot fee onto market, trigger lazy allocation. reject→ status 0→−1, refund liquidity to creator. Requires oracle insurance ≥ min.

> 🟡 **§3.5 СДЕЛАНО С ОТЛИЧИЯМИ — accept несёт котировку оракула.**
> - **`accept` получил 2 поля `oracle_fee_percent` + `oracle_fixed_fee`** — оракул котирует свои
>   условия (≤ потолка создателя на рынке и ≤ медианного `pm_max_oracle_fee_percent`); они
>   замораживаются в объект, status 0→1, эмитится **виртуальная `pm_market_accepted`** (см. §4).
>   Фактическая выплата оракулу — на сеттлменте из пула (§1.1). 🟢 lazy allocation, проверка страховки.
> - **`reject` — починен консенсус-баг двойного возврата (эмиссия).** Было: креди́т `liquidity_sum`
>   И `return_liquidity()` → эмиссия. Стало: только `return_liquidity()`. Покрыто
>   `oracle_reject_refunds_liquidity_once` (#2).

### 3.6 Betting

**`pm_place_bet` (ID 68)** — auth: `active`

| Field | Type | Notes |
|-------|------|-------|
| `account` | account_name_type | |
| `market_id` | id | status==1, time<betting_expiration |
| `side` / `outcome_index` | int8 / int16 | per market_type |
| `amount` | asset (VIZ) | >0 |
| `min_tokens` | share_type | slippage floor (0 = none) |
| `mode` | uint8 | 0 instant (default), 1 batch |

instant: apply to CPMM/LMSR immediately, mint `weight`, update reserves/q/sums. batch (requires `allow_batch`): enqueue (status 5, `epoch`), lock amount; settled by virtual `pm_batch_settle` (§4).

> **`allow_instant_bet=false` enforcement.** When the market's `allow_instant_bet` flag is `false`, the chain MUST reject `mode=0` with `pm_instant_bet_disabled`. The bettor's only paths are `mode=1` (batch) or `pm_commit_bet` → `pm_reveal_bet`. Conversely the off-chain platform UX must hide the instant option and pre-select batch on these markets.

> 🟡 **§3.6 СДЕЛАНО; гейт `allow_instant_bet` добавлен последним (был пропущен).** Поле хранилось,
> но `pm_place_bet` его НЕ проверял — instant-ставки проходили при выключенном флаге (латентный
> баг). Добавлен консенсус-гейт: `mode 0 ⇒ FC_ASSERT(allow_instant_bet)`, `mode 1 ⇒
> FC_ASSERT(allow_batch)`. Взаимное ограничение `allow_instant_bet || allow_batch` — в `validate()`.
> Покрыто `instant_bet_disabled_gate` (#55). 🟢 commit/reveal, cancel, time-penalty — реализованы.
> Замечание по `batch (mode=1)`: в `pm_place_bet` он исполняется немедленно по CPMM (фронт-ран-защита
> идёт через `commit_bet`/`reveal_bet` и эпохальный `pm_batch_settle`, не через `mode=1`).

**`pm_commit_bet` (ID 69)** — auth: `active`

| Field | Type | Notes |
|-------|------|-------|
| `account`, `market_id` | | requires `allow_batch` & `pm_commit_reveal_enabled` |
| `commitment` | sha256 | binds account+market |
| `escrow_amount` | asset (VIZ) | ≥ `pm_min_batch_bet` |
| `no_reveal_fee_permille` | uint16 | **must equal `pm_commit_no_reveal_penalty_permille`** (consensus check) — the user explicitly agrees to the current chain-voted rate, which is then snapshotted on the commit |

**`pm_reveal_bet` (ID 70)** — auth: `active`. `{commit_id, side/outcome, amount, salt, min_tokens}`; verify hash; refund surplus; enqueue for the next epoch boundary (§4). Unrevealed → virtual `pm_commit_forfeit`.

**`pm_cancel_bet` (ID 71)** — auth: `active`. Requires `allow_cancellation`, status active/queued, betting open. Binary: reverse CPMM; multi: reverse LMSR; refund.

### 3.7 Liquidity

**`pm_add_liquidity` (ID 72)** / **`pm_withdraw_liquidity` (ID 73)** — auth: `active`. Binary: proportional reserve add / principal-safe reverse (block if `reserve<weight`); multi: increase/decrease `lmsr_b` (subsidy floor enforced). Post-betting-expiration withdrawal locked until resolution.

### 3.8 Resolution

**`pm_resolve_market` (ID 74)** — auth: `active` of `oracle`. `{oracle, market_id, winning_outcome, decision_url}`. Preconditions: status 1 (if `allow_early_resolution`) or 2; `time ≤ result_expiration`. Sets status 3, payout_status 1 (pending), starts `pm_dispute_grace`. **Parimutuel settlement** computed and stored on bets' `resolved_amount` (see §settlement below). Actual transfer is deferred to virtual `pm_auto_payout` after grace (so disputes can freeze it).

**`pm_no_contest` (ID 75)** — auth: `active` of `oracle`. Refund all bets+LP (principal), penalty `pm_no_contest_penalty_permille` of dispute fee from insurance distributed to participants. Disputable (3-outcome).

> 🟡 **§3.8 СДЕЛАНО; `pm_no_contest` переработан + починены 2 бага.** Было: штраф считался от
> *всей страховки* (≈50%) и сжигался в forfeit_pool. Стало: `permille × dispute_fee`, распределяется
> pro-rata возвращённым бетторам; no-contest сделан **disputable** — ставит `status=3/payout=1/
> resolved=-1` + grace, а возврат/штраф откладываются в `settle_market` (ветка `win<0`), чтобы диспут
> мог переопределить исход. Покрыто `oracle_no_contest_refund_and_compensate`, `dispute_overrides_no_contest`.
> Parimutuel-сеттлмент 🟢 (zero-sum доказан, [[project_pm_zero_sum]]); починена инверсия сторон в
> batch-сеттлменте (side 0 теперь как в `pm_place_bet`).

**Settlement (both types, computed at resolve):**
```
losers_sum   = Σ amount of non-winning bets
winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee   (fees = permille × losers_sum)  + forfeit_pool
payout_i     = bet_amount_i + floor(winners_pool × weight_i / total_winning_weight) − time_penalty(profit)
LP: principal returned unconditionally + time-weighted share of liq_fee (+ no-winner bonus)
```

### 3.9 Disputes

**`pm_dispute_create` (ID 76)** — auth: `active`. `{disputer, market_id, proposed_outcome}`. Preconditions: disputer has a bet; within `pm_dispute_grace`; no open dispute. Escrow `pm_dispute_fee`. Sets payout_status 3 (frozen). Creates `pm_dispute_object` copying `dispute_mode`. Sets `oracle_response_deadline`, `auto_close_time = now + pm_dispute_auto_close`. Committee mode: `voting_end_time = now + pm_dispute_vote_period`.

**Committee mode (`dispute_mode==0`) — public stake-weighted vote:**

**`pm_dispute_vote` (ID 77)** — auth: `regular` of `voter`. `{voter, market_id, vote_outcome, vote_percent}`. Any SHARES holder. Upsert into `pm_dispute_vote_object`. `vote_outcome` = correct outcome (or −1 to uphold oracle); `vote_percent` ∈ [−10000,10000] = conviction/penalty intensity. Tally is deterministic at `voting_end_time` via virtual `pm_dispute_finalize` (§4): stake-weighted per-outcome rshares, participation threshold `pm_dispute_approve_min_percent`, winning outcome = argmax; **penalty scaled by consensus strength** (`winning_rshares / max_rshares`); bans scaled likewise. (Algorithm = [committee-dao-and-prediction-markets.md](committee-dao-and-prediction-markets.md) §Resolution.)

**Account mode (`dispute_mode==1`) — centralized resolver:**

**`pm_dispute_resolve` (ID 78)** — auth: `active` of `dispute_resolver`. `{resolver, market_id, correct_outcome, penalty_amount, ban_oracle, ban_oracle_until, ban_creator, ban_creator_until}`. Only the market's `dispute_resolver` account may call. Used for **regulated markets** where a named body (e.g. a country's commission multisig) decides. Recalculate payouts / slash insurance / apply bans per fields.

> **Both modes converge** on the same post-verdict recalculation path (delete pending payouts → flip/replace winner → regenerate parimutuel payouts → slash insurance/reward disputer → unfreeze). Only *who decides* differs: the whole SHARES electorate vs one configured account.

> 🟡 **§3.9 СДЕЛАНО ПОЛНОСТЬЮ + починен no-op `ban_creator`.**
> Committee-режим: stake-weighted tally (вес = `effective_vesting_shares` **+ доля в lazy-пуле,
> сконвертированная в vesting-shares** через `get_vesting_share_price()` — чтобы DAO-участники,
> переложившие токены в lazy-пул ради доходности, не теряли право голоса; знаменатель кворума тоже
> включает `pool_NAV→shares`; покрыто `committee_dispute_lazy_pool_voting_weight`),
> порог участия `pm_dispute_approve_min_percent`, winning = argmax по rshares,
> **`consensus_strength = winning_rshares × 100% / max_rshares`** масштабирует слэш; распределение
> fee zero-sum (uphold→оракулу; overturn→disputer fee + carve-out `fee×reward_multiplier/1000` из
> слэша, остаток→forfeit_pool); good-faith (`dispute_penalty_percent<0`) — без слэша, оракулу
> бонус. Account-режим — тот же канон, слэш = `penalty_amount` (без масштабирования). Auto-close
> возвращает fee диспутеру (anti-freeze). Найдены/починены: double-refund emission, time-gate recall.
> - 🔴→🟢 **`ban_creator`/`ban_creator_until` был no-op** (поле в операции есть, кода нет, хранилища
>   нет). Теперь применяется: upsert `pm_creator_ban_object`, проверка в `pm_create_market`.
>   Покрыто `dispute_bans_creator_from_new_markets` (#14). `ban_oracle` 🟢 работал ранее.

### 3.10 `pm_transfer_position` (ID 79) — auth: `active`
`{from, bet_id, to, amount, memo}`. Reassign all/part of a bet's `weight` to `to` (same market/outcome). No market impact. `memo`: plaintext, or `#`-prefixed ECIES-encrypted via VIZ account memo keys.

---

## 4. Virtual Operations (deterministic, block-generated)

Generated during block application by scanning `by_<time>`/`by_epoch` indexes with **bounded per-block work** (cap N per block; defer remainder — same pattern as committee payouts every 200 blocks and vesting withdrawals).

| ID | Virtual op | Trigger | Effect |
|----|-----------|---------|--------|
| 96 | `pm_batch_settle` | Epoch boundary (`block % pm_batch_epoch_blocks == 0`) per market with a queue | Aggregate queued+revealed bets into one CPMM/LMSR transition per side/outcome at a uniform price; `min_tokens` gate; mint weights |
| 97 | `pm_commit_forfeit` | `reveal_deadline` passed, unrevealed | penalty = `escrow × no_reveal_fee_permille/1000` → `market.forfeit_pool`; refund rest |
| 98 | `pm_auto_payout` | Resolved + grace elapsed, no open dispute | Credit `resolved_amount` to winners, fees to oracle/creator, principal+fees to LPs, subsidy return |
| 99 | `pm_dispute_finalize` | Committee `voting_end_time` | Tally stake-weighted votes; apply verdict + scaled penalty/bans; recalc payouts |
| 100 | `pm_dispute_auto_close` | `auto_close_time` reached, unresolved | Full refund + oracle penalty + disputer fee return (anti-freeze) |
| 101 | `pm_oracle_missed_penalty` | `result_expiration` passed, status 2 | Slash `pm_oracle_penalty_percent` of insurance; refund all; distribute bonus |
| 102 | `pm_lazy_recall` | Lazy pool graduated-recall step | Recall idle allocation (phase 2) |

> 🟢 **§4 СДЕЛАНО — единый ограниченный cron, виртуальные операции ЭМИТИРУЮТСЯ.**
> Детерминированная логика собрана в `database::process_pm_markets()` (раз в блок, бюджет
> `pm_processing_cap_per_block`, сканы по `by_<time>`/`by_reveal_deadline`/`by_epoch`). **Каждый шаг
> эмитит свою virtual_operation** (`pm_virtual_operations.hpp`, derive от `virtual_operation` →
> попадают в `account_history`): `pm_batch_settle`, `pm_commit_forfeit`, `pm_auto_payout`,
> `pm_dispute_finalize`, `pm_dispute_auto_close`, `pm_oracle_missed_penalty`, `pm_lazy_recall`, плюс
> leverage `pm_leverage_liquidate`/`pm_leverage_resolve`. Нумерация id 96–102 из спеки не
> используется (op-id = индекс в `operation` variant, append-only). ➕ **Добавлена
> `pm_market_accepted`** — эмитится при accept оракулом и при self-oracle авто-accept (фиксирует
> условия + market_id + флаг self), чтобы парсеры истории видели «рынок запущен».

---

## 5. Chain Properties (consensus parameters)

Set **only** via the existing `versioned_chain_properties_update_operation` (ID 46). Add a new version **index 4 (`chain_properties_pm`)** which **inherits all `chain_properties_hf9` fields** and appends the PM fields below. Validators publish; the network applies the **per-field median** across active validators (same machinery as all other chain properties — no separate transaction, no new op).

| Property | Type | Default | Constraints / notes |
|----------|------|---------|---------------------|
| `pm_oracle_registration_fee` | asset (VIZ) | 10.000 | → committee fund |
| `pm_min_oracle_insurance` | asset (VIZ) | 5000.000 | bond floor |
| `pm_market_creation_fee` | asset (VIZ) | 5.000 | → committee fund |
| `pm_min_liquidity` | asset (VIZ) | 100.000 | binary/multi seed floor |
| `pm_max_outcomes` | uint8 | 10 | 2–10 |
| `pm_max_market_duration` | uint32 (s) | 31536000 | ≤ 1 year |
| `pm_max_oracle_fee_permille` | uint16 | 50 | per-fee cap |
| `pm_max_total_fee_permille` | uint16 | 100 | Σ(oracle+creator+liq) cap |
| `pm_default_time_penalty_percent` | uint16 | 50 | window % of duration |
| `pm_max_time_penalty` | uint32 | 1000000 | 100% of profit (precision 1e6) |
| `pm_dispute_fee` | asset (VIZ) | 1000.000 | to open dispute |
| `pm_dispute_grace_sec` | uint32 | 43200 | 12 h |
| `pm_oracle_dispute_response_sec` | uint32 | 43200 | 12 h |
| `pm_dispute_auto_close_sec` | uint32 | 1209600 | 14 d (anti-freeze) |
| `pm_dispute_vote_period_sec` | uint32 | 259200 | 3 d (committee mode) |
| `pm_dispute_approve_min_percent` | uint16 | 1000 | participation threshold (bp of total SHARES) |
| `pm_oracle_penalty_percent` | uint16 | 500 | insurance slashed on missed deadline (bp) |
| `pm_no_contest_penalty_permille` | uint16 | 500 | of dispute fee |
| `pm_dispute_reward_multiplier` | uint16 | 3000 | ‰ — disputer reward carve-out |
| `pm_batch_epoch_blocks` | uint32 | 20 | ~60 s |
| `pm_reveal_window_blocks` | uint32 | 200 | ~10 min (liveness) |
| `pm_commit_no_reveal_penalty_permille` | uint16 | 200 | **20%** → winners' pool; carried & checked in `pm_commit_bet` |
| `pm_min_batch_bet` | asset (VIZ) | 1.000 | anti-dust |
| `pm_commit_reveal_enabled` | bool | true | global kill-switch |
| `pm_lazy_pool_*` | … | … | phase 2 (allocation %, max %, lock, recall) |

> **Open governance question** (see §7.1): putting ~25 PM params into the validator median set materially enlarges what every validator must publish. Consider a smaller median-voted subset + a dedicated `pm_params_object` updated by a separate mechanism.

> 🟡 **§5 СДЕЛАНО как `chain_properties_pm` (version index 4) + ➕ параметры сверх таблицы.**
> Реализовано через существующий `versioned_chain_properties_update_operation` с per-field median
> (как все прочие свойства). Сверх перечня добавлены:
> - ➕ `pm_lazy_emergency_penalty_permille` (штраф emergency-вывода; раньше ошибочно переиспользовался
>   `pm_no_contest_penalty_permille` — починено).
> - ➕ Параметры leverage: `pm_leverage_enabled` (kill-switch, **false**), `pm_leverage_pool_profit_percent`
>   (R, 10), `pm_leverage_safety_margin_percent`, `pm_leverage_max_slippage_percent`, `pm_leverage_*`
>   (fund_percent, expiration_buffer, max_per_position_bp, max_position_ratio, min_market_liquidity),
>   `pm_conversion_profit_cost_percent`, `pm_lazy_alloc_percent` и др.
> - ➕ `pm_processing_cap_per_block` (бюджет cron из §4/§7.6).
> **Решение по §7.1: пошли путём (a) — median-vote всех** (включая новые), отдельный `pm_params_object`
> НЕ вводили. Поверхность параметров стала ещё больше — вопрос остаётся открытым (см. §7.1).
>
> **Пересмотр фи-параметров (по решению владельца):** (1) **`pm_max_total_fee_permille` УДАЛЁН** —
> агрегатный кэп признан лишним; остаётся только `pm_max_oracle_fee_percent` (на фи оракула),
> creator/liquidity самолимитируются, инвариант платёжеспособности «сумма ≤ 100%» — в `validate()`.
> (2) **Все ‰ → bp (10000 = 100.00%)** как везде в VIZ: `*_fee_percent`, `pm_max_oracle_fee_percent`
> (500), `pm_no_contest_penalty_percent` (5000), `pm_commit_no_reveal_penalty_percent` (2000),
> `pm_lazy_emergency_penalty_percent` (5000); `pm_dispute_reward_multiplier` стал bp-множителем
> (30000 = 3×, пол 10000=1×, потолок 100×). См. секцию фи в `chain-properties-governance.md`.

---

## 6. API Plugin: `prediction_market_api`

Read-only JSON-RPC plugin over the **consensus** objects (mirrors `committee_api`/`database_api`; objects live in `chain`, the plugin only queries indexes). Deps: `json_rpc`, `chain`. Method name = `prediction_market_api.<m>`. All list methods paginated (`from`, `limit ≤ 1000`). Heavy reads cached by the webserver (cleared per applied block).

| Method | Params | Returns / index used |
|--------|--------|----------------------|
| `get_market` | `market_id` | market + outcomes (`by_id`) |
| `list_markets` | `status, from, limit` | markets (`by_status`) |
| `list_markets_by_oracle` / `_by_creator` | `account, from, limit` | (`by_oracle`/`by_creator`) |
| `get_market_bets` | `market_id, from, limit` | bets (`by_market`) |
| `get_account_positions` | `account, from, limit` | bets (`by_account`) **+ computed `expected_payout` & `expected_payout_approx`** (parimutuel estimate; exact for resolved) |
| `get_market_weight_sums` | `market_id` | `a_weight_sum/b_weight_sum` or per-outcome `weight_sum`/`bets_sum` — feeds client `market_math.js` (no server business logic) |
| `get_oracle` | `account` | oracle obj + **computed reliability/trust score** |
| `list_oracles` | `from, limit, sort` | (`by_status`/`by_insurance`) |
| `get_dispute` | `market_id` | dispute obj |
| `get_dispute_votes` | `market_id, from, limit` | committee votes + live tally (`by_market_voter`) |
| `get_liquidity` | `market_id` / `account` | LP positions |
| `get_pm_chain_properties` | — | current median PM params (from `validator_schedule_object.median_props`) |
| `get_lazy_pool` | — | pool + caller deposit (phase 2) |

> The frontend already computes pricing/payouts locally ([market_math.js](../market_math.js)); the API only needs to expose **raw object state** (reserves, `q`, `lmsr_b`, per-side/outcome `bets_sum`+`weight_sum`, fees) so the client can mirror the on-chain math without trusting a server. Settlement itself is consensus (never client/server).

**Real-time:** optional `set_market_applied_callback` via the webserver block callback for live odds.

> 🟡 **§6 СДЕЛАНО + ➕ объединение двух плагинов в один и оффчейн-индекс метаданных.**
> Был отдельный плагин `prediction_market_meta` — **слит в `prediction_market_api`** (один плагин
> владеет markets/oracles/disputes/lazy/leverage/**metadata**; старый каталог удалён). Добавлены:
> ➕ оффчейн-индекс `pm_market_meta_object` + хэндлер `applied_block`, который парсит поле `metadata`
> рынка (категория/подкатегория/теги/`banned_jurisdictions`, неизвестные ключи игнорируются,
> не бросает на не-JSON), TTL-прунинг; ➕ методы `get_market_meta`, `list_markets_by_category`;
> опция `pmm-ttl-days`. Чистые хелперы в `meta_parse.hpp` юнит-тестятся (`tests/pm/meta_parse_test.cpp`).

---

## 7. Review — contentious / hard points to resolve before node implementation

> ⬛ **§7 СТАТУС закрытия пунктов (на момент HF14-реализации):**
> - **§7.2** 🟢 LMSR Q96 реализован в C++ (`pm/lmsr_q96.hpp`), bit-parity к замороженным векторам
>   проверяется в CI (`tests/pm/lmsr_vectors_test.cpp`).
> - **§7.6** 🟢 единый cron с бюджетом `pm_processing_cap_per_block` (см. §4).
> - **§7.9** 🟢 все объекты (вкл. новые `pm_leverage_position`, `pm_creator_ban`) в snapshot
>   export/import/clear, FC_REFLECT-нуты; APPEND-ONLY id стабильны.
> - **§7.10** 🟢 floor-округление, zero-sum-инвариант доказан тестами parimutuel.
> - **§7.12** 🟢 `MAX_PM_*` константы заданы и проверяются в эвалуаторах (`shared_string`).
> - **🔴 Открыто:** §7.1 (поверхность параметров — стала ещё больше), §7.3 (точный in-block ordering
>   batch vs instant — реализовано, но формально не специфицировано), §7.4 (кворум/сибил/vote-buying),
>   §7.5/§7.7/§7.8 — проектные вопросы, кодом не «закрываются».

### 7.1 Chain-property surface (governance design)
~25 PM params in the validator median set is a lot for every validator to track and publish. **Options:** (a) median-vote all (consistent, but heavy + slow to change); (b) median-vote a small core (fees caps, insurance floor, dispute fee/periods) and fix the rest at hardfork; (c) a separate `pm_params_object` updated by committee vote or a 2/3 validator supermajority. **Decision needed.**

### 7.2 Floating-point determinism in consensus — **RESOLVED**
**Status:** resolved by [docs/lmsr-fixed-point-spec.md](lmsr-fixed-point-spec.md). LMSR `exp`/`ln` are now computed in Q96 fixed-point on big integers (PHP `gmp`, JS `BigInt`, future C++ `boost::multiprecision::int256_t`) using a frozen Taylor / atanh series with a fixed iteration count and a single banker's-rounding step in `exp` range reduction. The PHP and JS reference implementations produce **bit-identical** outputs across `tests/lmsr_fixed_vectors.json` (all 71 functional vectors); the C++ plugin author follows §6 of the fixed-point spec and runs the same vector file in CI. Binary CPMM remains integer-only and unchanged.

### 7.3 Batch / commit-reveal as virtual ops
Uniform-price batch settlement (`pm_batch_settle`) and per-epoch processing must be (a) deterministic, (b) bounded per block, (c) ordered vs same-block instant bets and liquidity ops. Need a defined in-block op ordering and an epoch-open vs live-curve decision (see [plan_batch_commit_reveal_betting](plan_batch_commit_reveal_betting.md) §6.1; with parimutuel it can be epoch-open snapshot for full immunity). **Spec the exact ordering + caps.**

### 7.4 Committee dispute voting — sybil, quorum, cost, vote-buying
- **Quorum:** `pm_dispute_approve_min_percent` of *total* SHARES is hard to reach for niche markets → many disputes fall through to auto-close. Tune, or use participating-stake-relative thresholds.
- **Vote storage:** one `pm_dispute_vote_object` per voter per dispute — unbounded; cap or require min stake to vote.
- **Plutocracy / vote-buying:** whales decide outcomes; delegation can concentrate. Acceptable? (Same property as all VIZ governance.)
- **Front-running the tally — DECIDED: votes stay public, NO commit-reveal for disputes (will not be implemented).** A committee dispute is an **open public hearing**. The DAO's whole value proposition is resolving markets as truthfully and transparently as possible; hiding ballots behind a reveal phase would corrode that trust — the platform would lose credibility exactly where credibility matters most. Two consequences are locked in: (1) the live tally is queryable (`get_dispute_votes`); (2) **ballots are revisable** until `voting_end_time` — a repeat `pm_dispute_vote` overwrites the prior one (latest wins), so voters can change their mind as new arguments surface. The KBC/bandwagon worry is weak here because voters are **not paid** for matching the majority (influence is pure stake weight). Implemented in `pm_dispute_vote_evaluator` (modify-or-create); tested by `committee_dispute_flips_outcome`.
- **Penalty scaling formula** (consensus_strength) and **tie-breaking** must be exactly specified (integer math).

### 7.5 Insurance & escrowed funds custody
Where do locked funds live? Options: (a) dedicated `share_type` fields on objects (oracle.insurance, dispute.dispute_fee, bet.amount) decremented from account balance — simplest, but they're invisible to generic balance tooling; (b) reuse the escrow subsystem. Recommend (a) with explicit virtual-op accounting so account_history reflects locks/unlocks.

### 7.6 Time-driven processing cost
Auto-payout, forfeit, dispute-finalize, missed-penalty, lazy-recall all scan time indexes every block. With many markets this is per-block work. **Cap per block + defer** (like committee 200-block cadence). Define caps and a fairness/ordering rule so no market starves.

### 7.7 Bandwidth / energy for high-frequency betting
Betting is `active`-auth and consumes bandwidth (∝ stake). Batch/commit-reveal add 2 ops per bet. Confirm bandwidth model tolerates active markets; consider whether PM ops need a distinct cost class (`data_operations_cost_additional_bandwidth` analog).

### 7.8 Regulated (account) vs committee dispute — legal binding
`dispute_mode==1` points to a `dispute_resolver` account (e.g. a national commission multisig). On-chain it's just an account with `active` auth over `pm_dispute_resolve`. KYC/whitelisting of that account, and which oracles/markets a regulated client surfaces, are **client-layer** concerns — the protocol stays neutral (same op set for both). Confirm this separation is acceptable and that a regulated resolver can also `ban`/slash.

### 7.9 Snapshots & replay
All new objects must be (a) included in `snapshot` plugin serialization, (b) deterministically rebuildable on replay, (c) `FC_REFLECT`-ed. New op/object IDs must be stable across the hardfork. Confirm snapshot format extension + reindex requirement.

### 7.10 Precision & dust
VIZ asset = 3 decimals; fees are permille; LMSR/CPMM produce remainders. Define rounding (floor) and dust routing (→ committee fund) consistently with §settlement. Ensure `Σ payouts ≤ pool` always holds under integer floor (conservation proven for parimutuel — keep it exact).

### 7.11 Multi-outcome cancellation & LMSR reverse
`pm_cancel_bet` for multi reverses LMSR (`lmsr_sell_return`); confirm it can't be gamed (buy/sell churn) and interacts safely with `weight_sum`/`bets_sum` accounting.

### 7.12 Frozen byte-length caps for variable-length fields
Variable-length on-chain strings (`pm_oracle.profile_url`, `pm_market.title`, `pm_outcome.label`, `pm_resolve_market.decision_url`, `pm_dispute.reason`, etc.) MUST have a byte-length cap defined as a chain constant. **This is not an anti-spam concern** — economic gating (`pm_market_creation_fee`, oracle registration cost, `active`-auth bandwidth) already prevents abusive volume. The reason is purely **consensus-mechanical**:

1. **ChainBase allocation determinism.** `chainbase::allocator<char>` strings without a hard cap force the C++ author to invent one; two nodes with different invented caps produce different `fc::raw::pack` byte length → different object hash on the same logical input → fork.
2. **Block-size envelope.** A single op must fit inside `MAX_TRANSACTION_SIZE`/`MAX_BLOCK_SIZE` deterministically across builds.
3. **Snapshot / replay byte-stability** (see §7.9): variable-length fields without a known maximum can disagree on field offsets after a node upgrade.
4. **Operation cost model.** VIZ bandwidth/fee charges by serialized byte size; without a cap the fee ceiling itself is undefined.

**Required output:** a frozen table of constants in §1 / §2 (illustrative starting values, freeze before hardfork):
```
MAX_PM_DECISION_URL_LEN     = 256
MAX_PM_PROFILE_URL_LEN      = 256
MAX_PM_DISPUTE_REASON_LEN   = 1024
MAX_PM_MARKET_TITLE_LEN     = 256
MAX_PM_OUTCOME_LABEL_LEN    = 64
MAX_PM_OUTCOMES_PER_MARKET  = 16
```
Evaluators reject ops exceeding the cap; ChainBase fields use `fc::shared_string` with the cap enforced on insert.

---

## 8. Phasing recommendation

1. **Phase 1 (binary-only, integer-safe):** objects, oracle, create/accept, instant bet, liquidity, resolve, both dispute modes, auto-payout, missed-penalty. No LMSR (avoids §7.2), no batch/commit-reveal. Ships a usable consensus market.
2. **Phase 2:** deterministic LMSR (resolve §7.2) → multi markets; batch + commit-reveal (§7.3); lazy pool.
3. **Phase 3:** shared/category liquidity, automated data oracles, advanced governance of PM params.

> 🟡 **§8 ФАЗИРОВАНИЕ НЕ соблюдено — реализованы Phase 1 + 2 (и часть «расширений») разом.**
> Поскольку HF14 ещё не на мейннете и миграций нет, дробить релиз смысла не было. В одном HF14:
> binary CPMM **и** multi LMSR, instant **и** batch/commit-reveal, полный Lazy Pool, **+ leverage**
> (которого в фазах нет — ближе к Phase 3/«advanced»). Из Phase 3 **не сделано (🔴):** shared/category
> liquidity (есть только оффчейн-категоризация через `metadata`), автоматические data-оракулы.
> Верификация: `consensus_sim` 28 кейсов + `tests/pm/*` — всё зелёное.

---

## 9. ⬛ Сводка отличий реализации от спеки (приложение)

**Изменённые модели (🟡):**
1. Op-id не «с 64», а append-only в `operation` variant; хардфорк = `CHAIN_HARDFORK_14`.
2. Оракульская фи и `oracle_fixed_fee` — модель **offer→quote**: создатель предлагает потолок на
   create, оракул котирует фактическое на accept (≤ потолка, ≤ медианы), замораживается в рынок;
   выплата из пула на сеттлменте (zero-sum). Резолв к медиане не обращается.
3. Виртуальные операции §4 эмитируются (через `process_pm_markets`), но без нумерации id 96–102;
   ➕ добавлена `pm_market_accepted` (announce запуска рынка).
4. `pm_no_contest` штраф = % от dispute_fee (не от всей страховки) и сделан disputable.
5. **Фи-параметры:** удалён `pm_max_total_fee_permille` (агрегатный кэп); все ‰ → bp (10000=100%);
   `pm_dispute_reward_multiplier` — bp-множитель (10000=1×, ≤100×).

**Добавлено сверх спеки (➕):**
5. Подсистема **leverage**: объект `pm_leverage_position` + 3 операции + параметры + kill-switch.
6. Объект **`pm_creator_ban`** (чтобы `ban_creator` перестал быть no-op).
7. Поля рынка **`dispute_penalty_percent`** и **`metadata`** (консенсус-непрозрачный JSON).
8. Параметры: `pm_lazy_emergency_penalty_permille`, `pm_processing_cap_per_block`, весь набор leverage.
9. Объединение `prediction_market_meta` → `prediction_market_api` + оффчейн-индекс метаданных.

**Реализовано вопреки «отложено/phase 2» (🟢):** весь Lazy Pool (§1.9), LMSR multi, batch/commit-reveal.

**Закрытые консенсус-баги (найдены тестами):** double-refund при reject (эмиссия); отсутствие
синглтона Lazy Pool; recall без тайм-гейта; эмиссия pending-награды при lazy-withdraw; инверсия
сторон в batch-сеттлменте; no-op `allow_instant_bet`; no-op `ban_creator`; неверная база/сжигание
штрафа no-contest; emergency-штраф брал не тот параметр.

**Не сделано / отложено (🔴):** отдельные virtual-ops в `account_history`; shared/category on-chain
liquidity; автоматические data-оракулы; формальная спецификация §7.3 ordering; открытые governance-
вопросы §7.1/§7.4/§7.5/§7.7/§7.8.

---

🇷🇺 По запросу сделаю русскую версию (`-ru`) и/или раскрою любой раздел (точные C++ сигнатуры объектов/эвалуаторов, бинарную сериализацию операций, или детальный алгоритм tally для committee-диспута).
