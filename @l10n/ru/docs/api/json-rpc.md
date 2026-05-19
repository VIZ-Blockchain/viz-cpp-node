# JSON-RPC API

Все API узлов VIZ используют JSON-RPC 2.0 через HTTP POST или WebSocket.

---

## Формат запроса

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "api_name.method_name",
  "params": [arg1, arg2]
}
```

- `id` может быть любым числом или строкой; передаётся в ответе без изменений.
- `params` может быть массивом или объектом в зависимости от метода.
- Поддерживаются HTTP POST и WebSocket. Подписки требуют WebSocket.

---

## Формат ответа

**Успех:**
```json
{ "jsonrpc": "2.0", "id": 1, "result": { ... } }
```

**Ошибка:**
```json
{ "jsonrpc": "2.0", "id": 1, "error": { "code": -32602, "message": "Invalid params" } }
```

### Коды ошибок

| Код | Значение |
|-----|---------|
| `-32700` | Ошибка разбора — некорректный JSON |
| `-32600` | Некорректный запрос |
| `-32601` | Метод не найден |
| `-32602` | Некорректные параметры |
| `-32603` | Внутренняя ошибка |
| от `-32099` до `-32000` | Ошибка сервера (исключение из обработчика) |

---

## Пространства имён плагинов

| Пространство имён | Статус | Описание |
|-----------------|-------|---------|
| `database_api` | Активен | Запросы блоков, аккаунтов, состояния цепочки |
| `network_broadcast_api` | Активен | Трансляция транзакций и блоков |
| `witness_api` | Активен | Запросы валидаторов |
| `account_by_key` | Активен | Обратный поиск по ключу |
| `account_history` | Активен | История операций аккаунта |
| `operation_history` | Активен | Запросы операций блока |
| `committee_api` | Активен | Запросы заявок комитета |
| `invite_api` | Активен | Запросы инвайтов |
| `paid_subscription_api` | Активен | Запросы подписок |
| `custom_protocol_api` | Активен | Метаданные пользовательского протокола |
| `auth_util` | Активен | Проверка авторизации |
| `block_info` | Активен | Расширенная информация о блоке |
| `raw_block` | Активен | Экспорт сырых блоков |
| `follow` | Устарел | Индексы подписок и ленты |
| `tags` | Устарел | Поиск контента по тегу |
| `social_network` | Устарел | Высокоуровневые запросы контента |
| `private_message` | Устарел | Индекс зашифрованных сообщений |
| `debug_node` | Только dev | Тестовые/отладочные операции |

---

## Методы `database_api`

| Метод | Описание |
|-------|---------|
| `get_block_header(block_num)` | Заголовок блока для данной высоты |
| `get_block(block_num)` | Полный подписанный блок |
| `get_irreversible_block_header(block_num)` | Заголовок блока, если он необратимый |
| `get_irreversible_block(block_num)` | Полный блок, если он необратимый |
| `set_block_applied_callback(callback)` | WebSocket: подписка на новые блоки |
| `get_config()` | Компайл-тайм константы цепочки |
| `get_dynamic_global_properties()` | Текущее состояние цепочки |
| `get_chain_properties()` | Медианные параметры цепочки валидаторов |
| `get_hardfork_version()` | Строка текущей версии хардфорка |
| `get_next_scheduled_hardfork()` | Информация о следующем запланированном хардфорке |
| `get_accounts(names[])` | Полные объекты аккаунтов |
| `lookup_account_names(names[])` | Аналог get_accounts, но с поддержкой null |
| `lookup_accounts(lower_bound, limit)` | Постраничный список имён аккаунтов |
| `get_account_count()` | Общее количество зарегистрированных аккаунтов |
| `get_master_history(account)` | История смены мастер-ключей |
| `get_recovery_request(account)` | Ожидающий запрос восстановления аккаунта |
| `get_escrow(from, escrow_id)` | Объект эскроу |
| `get_withdraw_routes(account, type)` | Маршруты вывода из вестинга (`"incoming"` / `"outgoing"` / `"all"`) |
| `get_vesting_delegations(account, from, limit, type)` | Делегирования (`"delegated"` / `"received"`) |
| `get_expiring_vesting_delegations(account, from, limit)` | Делегирования в окне возврата |
| `get_transaction_hex(trx)` | Hex-кодированная сериализованная транзакция |
| `get_required_signatures(trx, available_keys[])` | Минимальный набор ключей для подписания |
| `get_potential_signatures(trx)` | Все ключи, которые могут подписать |
| `verify_authority(trx)` | `true`, если полностью подписана |
| `verify_account_authority(name, keys[])` | `true`, если ключи удовлетворяют авторизации |
| `get_database_info()` | Статистика использования памяти chainbase |
| `get_proposed_transactions(account, from, limit)` | Предложения, требующие одобрения аккаунта |
| `get_accounts_on_sale(from, limit)` | Аккаунты, выставленные на прямую продажу |
| `get_accounts_on_auction(from, limit)` | Аккаунты, выставленные на аукцион |
| `get_subaccounts_on_sale(from, limit)` | Права создания субаккаунтов на продажу |

---

## Методы `network_broadcast_api`

| Метод | Описание |
|-------|---------|
| `broadcast_transaction(trx)` | Трансляция (асинхронная) |
| `broadcast_transaction_synchronous(trx)` | Трансляция с ожиданием включения в блок |
| `broadcast_transaction_with_callback(callback, trx)` | Трансляция с обратным вызовом WebSocket |
| `broadcast_block(block)` | Трансляция подписанного блока (валидаторы) |

---

## Методы `witness_api`

| Метод | Описание |
|-------|---------|
| `get_active_witnesses()` | Текущий активный набор валидаторов (21 аккаунт) |
| `get_witness_schedule()` | Полный объект расписания валидаторов |
| `get_witnesses(ids[])` | Валидаторы по внутренним ID |
| `get_witness_by_account(account)` | Объект валидатора для аккаунта |
| `get_witnesses_by_vote(lower_bound, limit)` | Валидаторы по убыванию веса голосов |
| `get_witnesses_by_counted_vote(lower_bound, limit)` | Валидаторы по числу голосов |
| `get_witness_count()` | Общее количество зарегистрированных валидаторов |
| `lookup_witness_accounts(lower_bound, limit)` | Список имён аккаунтов валидаторов |

---

## Методы `account_history`

### `get_account_history(account, from, limit)`

Возвращает операции, связанные с `account`. `from = -1` начинает с самой последней.

```json
{
  "method": "account_history.get_account_history",
  "params": ["alice", -1, 100]
}
```

Возвращает словарь `{ sequence: { trx_id, block, op: [type_id, data] } }`.

---

## Методы `operation_history`

| Метод | Описание |
|-------|---------|
| `get_ops_in_block(block_num, only_virtual)` | Операции в блоке |
| `get_transaction(trx_id)` | Транзакция по ID |

---

## Методы `committee_api`

| Метод | Описание |
|-------|---------|
| `get_committee_request(request_id)` | Детали и статус заявки |
| `get_committee_request_votes(request_id)` | Голоса по заявке |
| `get_committee_requests_list(from, limit)` | Список ID заявок |

---

## Методы `invite_api`

| Метод | Описание |
|-------|---------|
| `get_invites_list(from, limit)` | Все активные ID инвайтов |
| `get_invite_by_id(id)` | Инвайт по внутреннему ID |
| `get_invite_by_key(public_key)` | Инвайт по публичному ключу |

---

## Методы `paid_subscription_api`

| Метод | Описание |
|-------|---------|
| `get_paid_subscriptions(from, limit)` | Все предложения подписок |
| `get_paid_subscription_options(account)` | Конфигурация подписок для аккаунта |
| `get_paid_subscription_status(subscriber, account)` | Статус подписки |
| `get_active_paid_subscriptions(subscriber, from, limit)` | Активные подписки |
| `get_inactive_paid_subscriptions(subscriber, from, limit)` | Истёкшие подписки |

---

## WebSocket-подписки

Доступны только через постоянное WebSocket-соединение.

| Метод | Данные обратного вызова |
|-------|------------------------|
| `database_api.set_block_applied_callback` | Заголовок блока при каждом применённом блоке |
| `database_api.set_pending_transaction_callback` | Транзакция при входе в пул ожидания |
| `database_api.cancel_all_subscriptions` | Отписаться от всего |

---

## Рекомендуемые наборы плагинов

**Минимальный API-узел:**
```ini
plugin = chain json_rpc webserver p2p
plugin = database_api network_broadcast_api
```

**Полный API-узел (добавить):**
```ini
plugin = witness_api account_by_key account_history operation_history
plugin = committee_api invite_api paid_subscription_api
```

**Узел-валидатор (добавить):**
```ini
plugin = validator snapshot
```

---

См. также: [Database API](../plugins/database-api.md), [Плагин веб-сервера](../plugins/webserver.md), [Обзор операций](../protocol/operations/overview.md).
