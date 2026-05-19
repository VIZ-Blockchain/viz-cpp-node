# CLI-кошелёк

Исполняемый файл `cli_wallet` предоставляет интерактивный интерфейс командной строки для управления аккаунтами, подписи и трансляции транзакций, а также запросов к блокчейну.

---

## Подключение

```bash
cli_wallet --server-rpc-endpoint="ws://127.0.0.1:8091"
```

При первом запуске установите пароль:
```
new >>> set_password "yourpassword"
```

Затем разблокируйте:
```
locked >>> unlock "yourpassword"
```

---

## Управление кошельком

| Команда | Описание |
|---------|---------|
| `is_new` | Возвращает `true`, если пароль ещё не установлен |
| `is_locked` | Возвращает `true`, если кошелёк заблокирован |
| `lock` | Заблокировать кошелёк |
| `unlock "password"` | Разблокировать кошелёк |
| `set_password "password"` | Установить или изменить пароль |
| `load_wallet_file "file.json"` | Загрузить файл кошелька (`""` = перезагрузить текущий) |
| `save_wallet_file "file.json"` | Сохранить кошелёк в файл |
| `set_transaction_expiration 60` | Установить TTL транзакции в секундах |
| `quit` | Выйти из кошелька |
| `help` | Список всех команд |
| `gethelp "command"` | Подробная справка по одной команде |

---

## Управление ключами

| Команда | Описание |
|---------|---------|
| `import_key "5K..."` | Импортировать приватный ключ WIF |
| `suggest_brain_key` | Сгенерировать мнемонический ключ с публичной/приватной парой |
| `list_keys` | Список всех приватных ключей (WIF) в кошельке |
| `get_private_key "VIZpubkey..."` | Получить WIF для известного публичного ключа |
| `get_private_key_from_password "account" "role" "password"` | Получить ключ из учётных данных |
| `normalize_brain_key "words..."` | Нормализовать строку мнемонического ключа |

---

## Запросы

| Команда | Описание |
|---------|---------|
| `info` | Текущее состояние блокчейна |
| `database_info` | Статистика объектов базы данных |
| `get_block 1000000` | Данные блока |
| `get_ops_in_block 1000000 false` | Операции в блоке (`true` = только виртуальные) |
| `get_active_validators` | Активный набор валидаторов |
| `get_account "alice"` | Объект аккаунта |
| `list_accounts "" 100` | Постраничный список аккаунтов |
| `list_my_accounts` | Аккаунты с ключами в кошельке |
| `get_account_history "alice" -1 100` | Последние 100 операций alice |
| `get_transaction "txid..."` | Транзакция по ID |
| `get_master_history "alice"` | История смены мастер-ключей |
| `get_withdraw_routes "alice" "all"` | Маршруты вывода (`"incoming"`, `"outgoing"`, `"all"`) |
| `get_proposed_transactions "alice" 0 100` | Предложения, требующие одобрения alice |

---

## Операции с аккаунтами

| Команда | Описание |
|---------|---------|
| `create_account "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" true` | Создать аккаунт с автогенерацией ключей |
| `create_account_with_keys "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" "VIZmaster..." "VIZactive..." "VIZregular..." "VIZmemo..." true` | Создать аккаунт с заданными ключами |
| `update_account "account" "{}" "VIZm..." "VIZa..." "VIZr..." "VIZmemo..." true` | Обновить все ключи и метаданные |
| `update_account_auth_key "account" "active" "VIZnewkey..." 1 true` | Добавить ключ к авторизации (вес 0 = удалить) |
| `update_account_auth_account "account" "active" "guardian" 1 true` | Добавить аккаунт к авторизации |
| `update_account_auth_threshold "account" "active" 2 true` | Установить порог веса авторизации |
| `update_account_meta "account" "{\"key\":\"value\"}" true` | Обновить JSON-метаданные (regular-авторизация) |
| `update_account_memo_key "account" "VIZnewmemo..." true` | Обновить ключ memo |
| `delegate_vesting_shares "alice" "bob" "100.000000 SHARES" true` | Делегировать SHARES (0 = отменить) |

---

## Перевод и вестинг

| Команда | Описание |
|---------|---------|
| `transfer "alice" "bob" "10.000 VIZ" "memo" true` | Перевести VIZ (префикс `#` = зашифрованное memo) |
| `transfer_to_vesting "alice" "alice" "100.000 VIZ" true` | Застейкать VIZ как SHARES |
| `withdraw_vesting "alice" "100.000000 SHARES" true` | Начать анстейкинг (0 = отменить) |
| `set_withdraw_vesting_route "alice" "bob" 5000 false true` | Направить 50% выводов к bob как VIZ |

---

## Операции с валидаторами

| Команда | Описание |
|---------|---------|
| `list_validators "" 100` | Список валидаторов |
| `get_validator "validatorname"` | Объект валидатора |
| `update_validator "myvalidator" "https://url" "VIZsigningkey..." true` | Зарегистрировать/обновить валидатора |
| `update_chain_properties "myvalidator" {...} true` | Проголосовать за параметры цепочки (формат init) |
| `versioned_update_chain_properties "myvalidator" {...} true` | Проголосовать за версионированные параметры (формат hf9) |
| `vote_for_validator "alice" "myvalidator" true true` | Проголосовать за валидатора (`false` = снять голос) |
| `set_voting_proxy "alice" "proxy" true` | Установить прокси голосования (`""` = удалить) |

---

## Операции эскроу

| Команда | Описание |
|---------|---------|
| `escrow_transfer "alice" "bob" "agent" 1 "100.000 VIZ" "1.000 VIZ" "2024-06-01T00:00:00" "2024-07-01T00:00:00" "{}" true` | Создать эскроу |
| `escrow_approve "alice" "bob" "agent" "bob" 1 true true` | Одобрить эскроу (who = `"bob"` или `"agent"`) |
| `escrow_dispute "alice" "bob" "agent" "alice" 1 true` | Открыть спор (who = `"alice"` или `"bob"`) |
| `escrow_release "alice" "bob" "agent" "agent" "bob" 1 "100.000 VIZ" true` | Выпустить средства |

---

## Операции восстановления

| Команда | Описание |
|---------|---------|
| `request_account_recovery "recovery" "victim" {"weight_threshold":1,...} true` | Запросить восстановление как аккаунт восстановления |
| `recover_account "victim" {"recent_master_auth"} {"new_master_auth"} true` | Подтвердить восстановление |
| `change_recovery_account "account" "new_recovery" true` | Изменить аккаунт восстановления (задержка 30 дней) |

---

## Операции комитета

| Команда | Описание |
|---------|---------|
| `committee_worker_create_request "creator" "https://url" "worker" "100.000 VIZ" "500.000 VIZ" 604800 true` | Создать заявку на финансирование |
| `committee_worker_cancel_request "creator" 123 true` | Отменить заявку |
| `committee_vote_request "voter" 123 10000 true` | Голосовать (+10000 = полная поддержка, -10000 = полное несогласие, 0 = снять) |

---

## Операции инвайтов

| Команда | Описание |
|---------|---------|
| `create_invite "creator" "10.000 VIZ" "VIZinvitekey..." true` | Создать инвайт |
| `claim_invite_balance "initiator" "receiver" "5Kinvitesecret..." true` | Получить баланс инвайта |
| `invite_registration "initiator" "newaccount" "5Kinvitesecret..." "VIZnewaccountkey..." true` | Создать аккаунт из инвайта |
| `use_invite_balance "initiator" "receiver" "5Kinvitesecret..." true` | Использовать инвайт (может перевести в SHARES) |

---

## Операции наград

| Команда | Описание |
|---------|---------|
| `award "alice" "bob" 1000 0 "Great work!" [] true` | Наградить с вознаграждением на основе энергии |
| `fixed_award "alice" "bob" "10.000000 SHARES" 5000 0 "Reward" [] true` | Наградить фиксированной суммой SHARES |

Формат бенефициара: `[{"account":"charlie","weight":2000}]`

---

## Операции подписок

| Команда | Описание |
|---------|---------|
| `set_paid_subscription "account" "https://url" 3 "10.000 VIZ" 30 true` | Создать подписку (3 уровня, 10 VIZ/период, 30-дневный период) |
| `paid_subscribe "subscriber" "account" 2 "20.000 VIZ" 1 true true` | Подписаться на уровень 2 |

---

## Рынок аккаунтов

| Команда | Описание |
|---------|---------|
| `set_account_price "account" "account" "100.000 VIZ" true true` | Выставить на продажу |
| `set_subaccount_price "account" "account" "50.000 VIZ" true true` | Выставить создание субаккаунта на продажу |
| `buy_account "buyer" "account" "100.000 VIZ" "VIZnewkey..." "0.000 VIZ" true` | Купить аккаунт |
| `target_account_sale "account" "account" "targetbuyer" "100.000 VIZ" true true` | Целевая продажа |

---

## Пользовательская операция

```bash
custom [] ["alice"] "my_app" "{\"action\":\"follow\",\"target\":\"bob\"}" true
```

Параметры: `required_active_auths` `required_regular_auths` `id` `json` `broadcast`

---

## Конструктор транзакций

Создание и подпись пользовательских транзакций с несколькими операциями:

```bash
begin_builder_transaction           # Возвращает дескриптор (например, 0)
add_operation_to_builder_transaction 0 [2,{"from":"alice","to":"bob","amount":"10.000 VIZ","memo":""}]
sign_builder_transaction 0 true     # Подписать и транслировать
```

| Команда | Описание |
|---------|---------|
| `begin_builder_transaction` | Начать новую транзакцию (возвращает дескриптор) |
| `add_operation_to_builder_transaction handle [type_id, op]` | Добавить операцию |
| `replace_operation_in_builder_transaction handle idx [type_id, op]` | Заменить операцию |
| `preview_builder_transaction handle` | Предварительный просмотр JSON транзакции |
| `sign_builder_transaction handle broadcast` | Подписать (и опционально транслировать) |
| `propose_builder_transaction handle author title memo expiry review broadcast` | Обернуть в предложение |
| `remove_builder_transaction handle` | Отменить |
| `get_prototype_operation "transfer_operation"` | Получить пустой шаблон операции |
| `serialize_transaction {trx}` | Получить hex-сериализацию |
| `sign_transaction {trx} broadcast` | Подписать произвольную транзакцию |

---

## DNS-помощники NS

Хранить DNS-записи в метаданных аккаунта:

```bash
ns_set_records "myaccount" {"a_records":["188.120.231.153"],"ssl_hash":"4a4613...","ttl":28800} true
ns_get_summary "myaccount"
ns_extract_a_records "myaccount"
ns_remove_records "myaccount" true
```

Хелперы валидации: `ns_validate_ipv4`, `ns_validate_sha256_hash`, `ns_validate_ttl`, `ns_validate_ssl_txt_record`, `ns_validate_metadata`, `ns_create_metadata`.

---

## Личные сообщения

```bash
get_encrypted_memo "alice" "bob" "#secret message"
decrypt_memo "#encrypteddata..."
get_inbox "myaccount" "2024-01-15T00:00:00" 100 0
get_outbox "myaccount" "2024-01-15T00:00:00" 100 0
```

---

См. также: [JSON-RPC API](./json-rpc.md), [Обзор операций](../protocol/operations/overview.md), [Типы данных](../protocol/data-types.md).
