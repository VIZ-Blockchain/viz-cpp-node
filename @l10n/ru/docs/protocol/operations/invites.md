# Операции инвайтов

Инвайты позволяют существующим пользователям VIZ привлекать новые аккаунты без необходимости иметь предварительно существующий аккаунт у получателя. Инвайт — это одноразовая пара ключей, финансируемая токенами VIZ; приватный ключ служит секретом инвайта.

**Процесс инвайта:**
```
create_invite  →  invite_registration_operation  (создать новый аккаунт)
               →  claim_invite_balance_operation  (зачислить баланс существующему аккаунту)
               →  use_invite_balance_operation    (альтернативное получение)
```

---

## `create_invite_operation` (ID 43)

**Авторизация:** `active` `creator`

Создаёт инвайт, генерируя ключ и блокируя токены VIZ в нём.

| Поле | Тип | Описание |
|------|-----|---------|
| `creator` | `account_name_type` | Аккаунт, создающий инвайт |
| `balance` | `asset` (VIZ) | VIZ для блокировки в инвайте |
| `invite_key` | `public_key_type` | Публичный ключ пары ключей инвайта |

```json
[43, {
  "creator": "alice",
  "balance": "5.000 VIZ",
  "invite_key": "VIZ5invite..."
}]
```

- Сгенерируйте случайную пару ключей secp256k1. **Публичный ключ** идёт в `invite_key`; **приватный ключ** (WIF) становится секретом инвайта для передачи.
- `balance` должен быть ≥ свойству цепочки `create_invite_min_balance`.

---

## `claim_invite_balance_operation` (ID 44)

**Авторизация:** `active` `initiator`

Получает баланс VIZ из инвайта, переводя его `receiver`. Инвайт используется и не может быть применён повторно.

| Поле | Тип | Описание |
|------|-----|---------|
| `initiator` | `account_name_type` | Существующий аккаунт, получающий инвайт |
| `receiver` | `account_name_type` | Аккаунт, получающий баланс VIZ |
| `invite_secret` | `string` | WIF-приватный ключ инвайта |

```json
[44, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

- `receiver` может отличаться от `initiator` — баланс может быть перенаправлен.
- `invite_secret` — приватный ключ пары ключей инвайта в кодировке WIF.

---

## `invite_registration_operation` (ID 45)

**Авторизация:** `active` `initiator`

Использует инвайт для создания нового аккаунта в блокчейне. Баланс инвайта конвертируется в SHARES и назначается новому аккаунту.

| Поле | Тип | Описание |
|------|-----|---------|
| `initiator` | `account_name_type` | Существующий аккаунт, инициирующий регистрацию |
| `new_account_name` | `account_name_type` | Имя нового аккаунта |
| `invite_secret` | `string` | WIF-приватный ключ инвайта |
| `new_account_key` | `public_key_type` | Ключ, устанавливаемый как master, active, regular и memo для нового аккаунта |

```json
[45, {
  "initiator": "bob",
  "new_account_name": "carol",
  "invite_secret": "5Ky1MXn...",
  "new_account_key": "VIZ5newacct..."
}]
```

- `new_account_key` применяется ко всем четырём слотам authority (master, active, regular, memo).
- Баланс инвайта конвертируется в SHARES (не ликвидные VIZ) для нового аккаунта.
- Инвайт используется после применения.

---

## `use_invite_balance_operation` (ID 58)

**Авторизация:** `active` `initiator`

Альтернативное получение инвайта, которое может конвертировать баланс в SHARES для получателя вместо ликвидных VIZ.

| Поле | Тип | Описание |
|------|-----|---------|
| `initiator` | `account_name_type` | Аккаунт, использующий инвайт |
| `receiver` | `account_name_type` | Существующий аккаунт, получающий баланс |
| `invite_secret` | `string` | WIF-приватный ключ инвайта |

```json
[58, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

- `receiver` должен быть существующим аккаунтом.
- Инвайт используется после применения.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Аккаунты](./accounts.md).
