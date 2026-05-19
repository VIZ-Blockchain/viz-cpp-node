# Операции с аккаунтами

---

## `account_create_operation` (ID 20)

**Авторизация:** `active` `creator`

Создаёт новый аккаунт в блокчейне. Комиссия конвертируется в SHARES для нового аккаунта.

| Поле | Тип | Описание |
|------|-----|---------|
| `fee` | `asset` (VIZ) | Комиссия за создание ≥ `account_creation_fee` цепочки |
| `delegation` | `asset` (SHARES) | Начальное делегирование SHARES новому аккаунту |
| `creator` | `account_name_type` | Аккаунт, оплачивающий комиссию |
| `new_account_name` | `account_name_type` | Имя нового аккаунта |
| `master` | `authority` | Master authority |
| `active` | `authority` | Active authority |
| `regular` | `authority` | Regular authority |
| `memo_key` | `public_key_type` | Публичный ключ memo |
| `json_metadata` | `string` | JSON-метаданные (может быть `""`) |
| `referrer` | `account_name_type` | Аккаунт-реферер (может быть `""`) |
| `extensions` | `extensions_type` | Всегда `[]` |

```json
[20, {
  "fee": "1.000 VIZ",
  "delegation": "10.000000 SHARES",
  "creator": "alice",
  "new_account_name": "bob",
  "master":  { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "active":  { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "regular": { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "memo_key": "VIZ5...",
  "json_metadata": "",
  "referrer": "",
  "extensions": []
}]
```

- Все три authority обязательны (даже если используются одинаковые ключи).
- `fee.symbol` должен быть `VIZ`; `delegation.symbol` должен быть `SHARES`.

---

## `account_update_operation` (ID 5)

**Авторизация:** `master` `account` (если присутствует поле `master`), иначе `active`

Обновляет ключи и метаданные аккаунта.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт для обновления |
| `master` | `optional<authority>` | Новый master authority (опустить, если не меняется) |
| `active` | `optional<authority>` | Новый active authority |
| `regular` | `optional<authority>` | Новый regular authority |
| `memo_key` | `public_key_type` | Новый ключ memo (обязателен, даже если не меняется) |
| `json_metadata` | `string` | Новые JSON-метаданные |

```json
[5, {
  "account": "alice",
  "active": { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5new...", 1]] },
  "memo_key": "VIZ5new...",
  "json_metadata": "{\"profile\":\"updated\"}"
}]
```

- Если присутствует `master` → подписать текущим ключом **master**.
- Если `master` отсутствует → подписать текущим ключом **active**.
- `memo_key` всегда обязателен.

---

## `account_metadata_operation` (ID 21)

**Авторизация:** `regular` `account`

Обновляет только JSON-метаданные аккаунта. Меньшая стоимость пропускной способности, чем у `account_update`.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт для обновления |
| `json_metadata` | `string` | Новая строка JSON-метаданных |

```json
[21, {
  "account": "alice",
  "json_metadata": "{\"name\":\"Alice\",\"about\":\"Hello!\"}"
}]
```

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md).
