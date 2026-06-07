# Database API

Плагин `database_api` предоставляет JSON-RPC методы только для чтения, позволяющие запрашивать состояние блокчейна: блоки, аккаунты, свойства цепочки, делегирование, проверку авторизации и управление.

**Исходник:** [plugins/database_api/](../../plugins/database_api/)

---

## Зависимости

```
json_rpc::plugin, chain::plugin
```

---

## Формат запроса

Все методы используют JSON-RPC 2.0 через HTTP POST или WebSocket:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "database_api.<method_name>",
  "params": [<arg1>, <arg2>, ...]
}
```

---

## Блоки и транзакции

### `get_block_header(block_num)`

Возвращает заголовок блока для указанного номера блока или `null`, если не найден.

```json
{ "method": "database_api.get_block_header", "params": [12345678] }
```

**Возвращает:** `block_header` — `previous`, `timestamp`, `witness`, `transaction_merkle_root`, `extensions`.

---

### `get_block(block_num)`

Возвращает полный подписанный блок, включая все транзакции.

```json
{ "method": "database_api.get_block", "params": [12345678] }
```

**Возвращает:** `signed_block` — все поля заголовка плюс `transactions[]` с полными данными операций, а также `block_id`, `signing_key`, `transaction_ids[]`.

---

### `get_irreversible_block_header(block_num)`

То же, что `get_block_header`, но возвращает блок только если он достиг LIB (необратим).

---

### `get_irreversible_block(block_num)`

То же, что `get_block`, но возвращает блок только если он достиг LIB.

---

### `set_block_applied_callback(callback)`

Зарегистрировать WebSocket-колбек для уведомления о каждом применённом блоке. Колбек получает заголовок блока в виде JSON-варианта.

**Только WebSocket.** Отписаться через `cancel_all_subscriptions`.

---

## Глобальные параметры цепочки

### `get_config()`

Возвращает константы времени компиляции: chain ID, символы токенов, интервал блоков, максимальный размер блока, периоды голосования и все константы `CHAIN_*`.

```json
{ "method": "database_api.get_config", "params": [] }
```

---

### `get_dynamic_global_properties()`

Возвращает текущее состояние цепочки: номер и ID текущего head блока, время, текущий валидатор, суммарные vesting shares, уровень участия, балансы фондов DPO и прочее.

```json
{ "method": "database_api.get_dynamic_global_properties", "params": [] }
```

Ключевые поля: `head_block_number`, `head_block_id`, `time`, `current_validator`, `total_vesting_shares`, `total_vesting_fund_viz`, `committee_fund`, `last_irreversible_block_num`, `participation_count`, `earliest_available_block_num`.

`earliest_available_block_num` — нодо-локальное поле (не консенсусное): самый ранний блок, для которого эта конкретная API-нода может отдать полные данные. После импорта снапшота или при использовании rolling DLT block log более старая история обрезается, поэтому клиентам следует начинать парсинг с этого блока, а не запрашивать более ранние блоки, упираясь в отсутствие данных.

---

### `get_chain_properties()`

Возвращает текущие параметры управления on-chain (задаются через `chain_properties_update_operation`): минимальную комиссию за создание аккаунта, максимальный размер блока, комиссию за создание аккаунта в VIZ, процент резерва пропускной способности и параметры вознаграждений.

```json
{ "method": "database_api.get_chain_properties", "params": [] }
```

---

### `get_hardfork_version()`

Возвращает строку версии текущего активного харфорка (например, `"1.0.0"`).

```json
{ "method": "database_api.get_hardfork_version", "params": [] }
```

---

### `get_next_scheduled_hardfork()`

Возвращает версию и запланированное время активации следующего ожидающего харфорка.

```json
{ "method": "database_api.get_next_scheduled_hardfork", "params": [] }
```

**Возвращает:** `{ "hf_version": "1.0.0", "live_time": "2025-01-01T00:00:00" }`

---

## Аккаунты

### `get_accounts(account_names)`

Возвращает полные объекты аккаунтов для заданного списка имён аккаунтов.

```json
{ "method": "database_api.get_accounts", "params": [["alice", "bob"]] }
```

**Возвращает:** Массив `account_api_object` — имя, баланс, vesting shares, полученные vesting, делегированные vesting, ключи, аккаунт восстановления, дата создания, количество постов, голосовая сила и прочее.

---

### `lookup_account_names(account_names)`

То же, что `get_accounts`, но возвращает `null` для аккаунтов, которые не существуют.

```json
{ "method": "database_api.lookup_account_names", "params": [["alice", "nonexistent"]] }
```

**Возвращает:** Массив `optional<account_api_object>` — `null` для отсутствующих аккаунтов.

---

### `lookup_accounts(lower_bound_name, limit)`

Возвращает набор имён аккаунтов начиная с `lower_bound_name`, до `limit` результатов (макс. 1000). Полезно для постраничного перечисления аккаунтов.

```json
{ "method": "database_api.lookup_accounts", "params": ["alice", 100] }
```

**Возвращает:** Набор строк с именами аккаунтов.

---

### `get_account_count()`

Возвращает общее количество зарегистрированных аккаунтов.

```json
{ "method": "database_api.get_account_count", "params": [] }
```

---

## Состояние аккаунта

### `get_master_history(account)`

Возвращает историю изменений master authority для указанного аккаунта.

```json
{ "method": "database_api.get_master_history", "params": ["alice"] }
```

**Возвращает:** Массив `master_authority_history_api_object` — `account`, `previous_master_authority`, `last_valid_time`.

---

### `get_recovery_request(account)`

Возвращает ожидающий запрос восстановления аккаунта для указанного аккаунта, если он есть.

```json
{ "method": "database_api.get_recovery_request", "params": ["alice"] }
```

**Возвращает:** `optional<account_recovery_request_api_object>` — `account_to_recover`, `new_master_authority`, `expires`.

---

### `get_escrow(from, escrow_id)`

Возвращает объект эскроу-перевода для указанного отправителя и ID эскроу.

```json
{ "method": "database_api.get_escrow", "params": ["alice", 1] }
```

**Возвращает:** `optional<escrow_api_object>` — все поля эскроу, включая `from`, `to`, `agent`, `ratification_deadline`, `escrow_expiration`, суммы и статус подтверждения.

---

### `get_withdraw_routes(account, type)`

Возвращает маршруты вывода vesting power для аккаунта. `type` — одно из `"incoming"`, `"outgoing"` или `"all"`.

```json
{ "method": "database_api.get_withdraw_routes", "params": ["alice", "outgoing"] }
```

**Возвращает:** Массив `{ "from_account", "to_account", "percent", "auto_vest" }`.

---

### `get_vesting_delegations(account, from, limit, type)`

Возвращает vesting-делегирования для аккаунта. `type` — `"delegated"` (делегирования этого аккаунта) или `"received"` (полученные делегирования).

```json
{ "method": "database_api.get_vesting_delegations", "params": ["alice", "", 100, "delegated"] }
```

**Возвращает:** Массив `vesting_delegation_api_object` — `delegator`, `delegatee`, `vesting_shares`, `min_delegation_time`.

---

### `get_expiring_vesting_delegations(account, from, limit)`

Возвращает записи об истечении vesting-делегирований для аккаунта — делегирования, которые были отозваны и ожидают возврата.

```json
{ "method": "database_api.get_expiring_vesting_delegations", "params": ["alice", "1970-01-01T00:00:00", 100] }
```

**Возвращает:** Массив `vesting_delegation_expiration_api_object` — `delegator`, `vesting_shares`, `expiration`.

---

## Авторизация и проверка транзакций

### `get_transaction_hex(trx)`

Возвращает hex-encoded сериализованную двоичную форму транзакции. Полезно для подписи и трансляции.

```json
{ "method": "database_api.get_transaction_hex", "params": [{ ...объект транзакции... }] }
```

**Возвращает:** Hex-строку.

---

### `get_required_signatures(trx, available_keys)`

По частично подписанной транзакции и набору доступных подписывающему публичных ключей возвращает минимальное подмножество ключей, которые должны подписать для авторизации транзакции.

```json
{
  "method": "database_api.get_required_signatures",
  "params": [{ ...trx... }, ["VIZ5...", "VIZ6..."]]
}
```

**Возвращает:** Набор строк публичных ключей.

---

### `get_potential_signatures(trx)`

Возвращает все публичные ключи, которые потенциально могут подписать транзакцию (по всем задействованным аккаунтам и уровням authority). Используйте это для предварительной фильтрации набора ключей кошелька перед вызовом `get_required_signatures`.

```json
{ "method": "database_api.get_potential_signatures", "params": [{ ...trx... }] }
```

**Возвращает:** Набор строк публичных ключей.

---

### `verify_authority(trx)`

Возвращает `true`, если транзакция имеет все необходимые подписи; иначе выбрасывает ошибку с описанием того, чего не хватает.

```json
{ "method": "database_api.verify_authority", "params": [{ ...signed_trx... }] }
```

---

### `verify_account_authority(name, signers)`

Возвращает `true`, если заданный набор публичных ключей имеет достаточные полномочия для действий от имени `name`.

```json
{ "method": "database_api.verify_account_authority", "params": ["alice", ["VIZ5..."]] }
```

---

## Информация о базе данных

### `get_database_info()`

Возвращает статистику использования разделяемой памяти chainbase.

```json
{ "method": "database_api.get_database_info", "params": [] }
```

**Возвращает:**
```json
{
  "total_size": 4294967296,
  "free_size": 1073741824,
  "reserved_size": 0,
  "used_size": 3221225472,
  "index_list": [
    { "name": "account_object", "record_count": 52341 },
    ...
  ]
}
```

---

## Управление

### `get_proposed_transactions(account, from, limit)`

Возвращает предложения по управлению, требующие подтверждения от `account`.

```json
{ "method": "database_api.get_proposed_transactions", "params": ["alice", 0, 100] }
```

**Возвращает:** Массив `proposal_api_object` — полные детали предложения, включая необходимые подтверждения, срок действия и список операций.

---

## Рынок аккаунтов

### `get_accounts_on_sale(from, limit)`

Возвращает аккаунты, выставленные на продажу (прямая продажа, не аукцион).

```json
{ "method": "database_api.get_accounts_on_sale", "params": [0, 100] }
```

**Возвращает:** Массив `account_on_sale_api_object` — `account`, `account_seller`, `account_offer_price`, `account_on_sale_start_time`, `target_buyer`.

---

### `get_accounts_on_auction(from, limit)`

Возвращает аккаунты, выставленные на аукцион.

```json
{ "method": "database_api.get_accounts_on_auction", "params": [0, 100] }
```

**Возвращает:** Массив `account_on_sale_api_object` — то же плюс `current_bid`, `current_bidder`, `current_bidder_key`, `last_bid`.

---

### `get_subaccounts_on_sale(from, limit)`

Возвращает регистрации пространства имён аккаунтов, доступные для продажи (права на создание субаккаунтов).

```json
{ "method": "database_api.get_subaccounts_on_sale", "params": [0, 100] }
```

**Возвращает:** Массив `subaccount_on_sale_api_object` — `account`, `subaccount_seller`, `subaccount_offer_price`.

---

## Коды ошибок

| Код | Значение |
|-----|---------|
| `-32700` | Ошибка разбора — недействительный JSON |
| `-32600` | Недействительный запрос — отсутствуют обязательные поля |
| `-32601` | Метод не найден |
| `-32602` | Недействительные параметры |
| `-32603` | Внутренняя ошибка |
| `-32099` – `-32000` | Ошибка сервера (исключение из обработчика метода) |

---

См. также: [Обзор плагинов](./overview.md), [Методы validator_api](./overview.md#validator_api), [JSON-RPC API](../api/json-rpc.md).
