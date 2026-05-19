# Общие типы данных

Все общие типы данных, используемые в операциях и виртуальных операциях протокола VIZ Ledger.

---

## Примитивные типы

| Тип C++ | JSON-представление | Описание |
|---------|-------------------|---------|
| `string` | `string` | UTF-8 строка |
| `bool` | `boolean` | `true` / `false` |
| `uint8_t` | `integer` | Беззнаковое 8-битное целое |
| `uint16_t` | `integer` | Беззнаковое 16-битное целое (0–65535) |
| `int16_t` | `integer` | Знаковое 16-битное целое (−32768–32767) |
| `uint32_t` | `integer` | Беззнаковое 32-битное целое |
| `int32_t` | `integer` | Знаковое 32-битное целое |
| `uint64_t` | `string` или `integer` | Беззнаковое 64-битное целое — в JavaScript используйте строку во избежание переполнения |
| `int64_t` | `string` или `integer` | Знаковое 64-битное целое |
| `share_type` | `integer` | Псевдоним для `safe<int64_t>` — количество токенов в единицах сатоши |
| `time_point_sec` | `string` | Дата и время UTC в формате ISO 8601: `"2024-01-15T12:00:00"` (без суффикса часового пояса) |

---

## `account_name_type`

Строка фиксированной длины (максимум 16 байт), идентифицирующая аккаунт. Правила:

- Метки, разделённые точками; каждая метка — не менее 3 символов.
- Начинается с буквы, оканчивается буквой или цифрой.
- Только строчные латинские буквы (`a`–`z`), цифры (`0`–`9`), дефисы (`-`).
- Минимальная длина: 2 символа (`CHAIN_MIN_ACCOUNT_NAME_LENGTH`).
- Максимальная длина: 16 символов (`CHAIN_MAX_ACCOUNT_NAME_LENGTH`).

**JSON:** обычная строка — `"alice"`, `"alice.bob"`

---

## `public_key_type`

Сжатый публичный ключ secp256k1, закодированный в base58check с префиксом `VIZ`.

**JSON:** строка — `"VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"`

- Префикс должен быть `VIZ` (не `STM`, `GLS` или любой другой).
- Кодируется из 33-байтного сжатого публичного ключа + 4-байтной контрольной суммы = 37 байт всего, затем base58-кодирование.

---

## `asset`

Представляет количество токенов с символом. В ответах JSON API и параметрах операций сериализуется в виде человекочитаемой строки:

```
"10.000 VIZ"
"5.000000 SHARES"
```

### Символы токенов

| Символ | Строка | Знаки после запятой | Описание |
|--------|-------|---------------------|---------|
| `TOKEN_SYMBOL` | `VIZ` | 3 | Основной ликвидный токен |
| `SHARES_SYMBOL` | `SHARES` | 6 | Вестинговые доли (застейканный VIZ) |

При построении операций всегда используйте строковый формат. Разбирайте разделением по пробелу: слева — количество, справа — символ. VIZ использует 3 знака после запятой; SHARES — 6.

---

## `authority`

Структура мультиподписной авторизации, управляющая уровнем разрешений аккаунта.

```json
{
  "weight_threshold": 1,
  "account_auths": [
    ["alice", 1]
  ],
  "key_auths": [
    ["VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9", 1]
  ]
}
```

| Поле | Тип | Описание |
|------|-----|---------|
| `weight_threshold` | `uint32_t` | Минимальный суммарный вес для выполнения авторизации |
| `account_auths` | `[[account_name, weight], ...]` | Подписывающие на основе аккаунта |
| `key_auths` | `[[public_key, weight], ...]` | Подписывающие на основе ключа |

Сумма весов выполненных подписей должна быть ≥ `weight_threshold`. Пустая авторизация: `{ "weight_threshold": 0, "account_auths": [], "key_auths": [] }`.

### Уровни авторизации

| Уровень | Используется для |
|---------|-----------------|
| `master` | Наивысшая безопасность — смена ключей, восстановление аккаунта |
| `active` | Операции с токенами — перевод, вестинг, голосование за валидаторов |
| `regular` | Социальные операции — контент, награды, голосование в комитете |

---

## `beneficiary_route_type`

Указывает бенефициара и его долю вознаграждения при выплатах за контент.

```json
{ "account": "alice", "weight": 2500 }
```

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт бенефициара |
| `weight` | `uint16_t` | Доля в базисных пунктах (10000 = 100%) |

- Сумма весов всех бенефициаров не должна превышать 10000.
- Бенефициары должны быть отсортированы по имени аккаунта (по возрастанию) в массиве.
- Каждый аккаунт бенефициара должен существовать в блокчейне.

---

## `extensions_type`

В настоящее время не используется — всегда сериализуется как пустой массив.

```json
"extensions": []
```

---

## `versioned_chain_properties`

Статический вариант, содержащий одну из версий параметров цепочки. Сериализуется как 2-элементный массив `[type_index, object]`.

| Индекс | Тип |
|--------|-----|
| 0 | `chain_properties_init` |
| 1 | `chain_properties_hf4` |
| 2 | `chain_properties_hf6` |
| 3 | `chain_properties_hf9` (текущая) |

Полный справочник полей по версиям — в разделе [Параметры цепочки](../governance/chain-properties.md).

---

## `operation` (статический вариант)

Каждая операция сериализуется как 2-элементный массив: `[type_id, operation_object]`.

### Обычные операции (транслируемые пользователями)

| ID | Операция |
|----|---------|
| 0 | `vote_operation` *(устарела)* |
| 1 | `content_operation` *(устарела)* |
| 2 | `transfer_operation` |
| 3 | `transfer_to_vesting_operation` |
| 4 | `withdraw_vesting_operation` |
| 5 | `account_update_operation` |
| 6 | `witness_update_operation` |
| 7 | `account_witness_vote_operation` |
| 8 | `account_witness_proxy_operation` |
| 9 | `delete_content_operation` *(устарела)* |
| 10 | `custom_operation` |
| 11 | `set_withdraw_vesting_route_operation` |
| 12 | `request_account_recovery_operation` |
| 13 | `recover_account_operation` |
| 14 | `change_recovery_account_operation` |
| 15 | `escrow_transfer_operation` |
| 16 | `escrow_dispute_operation` |
| 17 | `escrow_release_operation` |
| 18 | `escrow_approve_operation` |
| 19 | `delegate_vesting_shares_operation` |
| 20 | `account_create_operation` |
| 21 | `account_metadata_operation` |
| 22 | `proposal_create_operation` |
| 23 | `proposal_update_operation` |
| 24 | `proposal_delete_operation` |
| 25 | `chain_properties_update_operation` |
| 35 | `committee_worker_create_request_operation` |
| 36 | `committee_worker_cancel_request_operation` |
| 37 | `committee_vote_request_operation` |
| 43 | `create_invite_operation` |
| 44 | `claim_invite_balance_operation` |
| 45 | `invite_registration_operation` |
| 46 | `versioned_chain_properties_update_operation` |
| 47 | `award_operation` |
| 50 | `set_paid_subscription_operation` |
| 51 | `paid_subscribe_operation` |
| 54 | `set_account_price_operation` |
| 55 | `set_subaccount_price_operation` |
| 56 | `buy_account_operation` |
| 58 | `use_invite_balance_operation` |
| 60 | `fixed_award_operation` |
| 61 | `target_account_sale_operation` |

### Виртуальные операции (генерируются блокчейном, не транслируются)

| ID | Операция |
|----|---------|
| 26 | `author_reward_operation` |
| 27 | `curation_reward_operation` |
| 28 | `content_reward_operation` |
| 29 | `fill_vesting_withdraw_operation` |
| 30 | `shutdown_witness_operation` |
| 31 | `hardfork_operation` |
| 32 | `content_payout_update_operation` |
| 33 | `content_benefactor_reward_operation` |
| 34 | `return_vesting_delegation_operation` |
| 38 | `committee_cancel_request_operation` |
| 39 | `committee_approve_request_operation` |
| 40 | `committee_payout_request_operation` |
| 41 | `committee_pay_request_operation` |
| 42 | `witness_reward_operation` |
| 48 | `receive_award_operation` |
| 49 | `benefactor_award_operation` |
| 52 | `paid_subscription_action_operation` |
| 53 | `cancel_paid_subscription_operation` |
| 57 | `account_sale_operation` |
| 59 | `expire_escrow_ratification_operation` |
| 62 | `bid_operation` |
| 63 | `outbid_operation` |

---

## Построение транзакции

Подписанная транзакция содержит:

| Поле | Значение |
|------|---------|
| `ref_block_num` | `head_block_number & 0xFFFF` |
| `ref_block_prefix` | байты 4–7 `block_id` в виде little-endian `uint32` |
| `expiration` | UTC время в виде строки; рекомендуется не более ~60 с от времени трансляции |
| `operations` | Массив пар `[type_id, object]` |
| `extensions` | Всегда `[]` |
| `signatures` | Массив компактных подписей ECDSA в hex-кодировке |

**Подпись:** `sha256(chain_id + serialized_transaction_body)` → компактная ECDSA-подпись над secp256k1.

**Приватные ключи:** формат WIF (base58check, байт версии `0x80`).

---

## Система энергии

Энергия используется операциями типа «награда».

- Хранится в базисных пунктах: 0–10000 (0%–100%).
- Восстанавливается со скоростью 100% за 24 часа (`CHAIN_ENERGY_REGENERATION_SECONDS = 86400`).
- Текущая энергия: `min(10000, last_energy + elapsed_seconds × 10000 / 86400)`.

---

См. также: [Обзор операций](./operations/overview.md), [Виртуальные операции](./virtual-operations.md), [Параметры цепочки](../governance/chain-properties.md).
