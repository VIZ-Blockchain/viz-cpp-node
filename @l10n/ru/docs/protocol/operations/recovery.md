# Операции восстановления аккаунта

Механизм восстановления позволяет заранее назначенному доверенному аккаунту (*аккаунту восстановления*) помочь восстановить доступ к скомпрометированному аккаунту, используя недавнюю действительную master authority.

**Процесс восстановления:**
```
request_account_recovery  →  recover_account  (в течение 24 часов)
change_recovery_account   (задержка 30 дней перед вступлением в силу)
```

---

## `request_account_recovery_operation` (ID 12)

**Авторизация:** `active` `recovery_account`

Инициирует запрос восстановления аккаунта. Аккаунт восстановления предлагает новую master authority для скомпрометированного аккаунта; у владельца аккаунта есть 24 часа для подтверждения через `recover_account_operation`.

| Поле | Тип | Описание |
|------|-----|---------|
| `recovery_account` | `account_name_type` | Доверенный аккаунт восстановления |
| `account_to_recover` | `account_name_type` | Скомпрометированный аккаунт для восстановления |
| `new_master_authority` | `authority` | Новая master authority для назначения после подтверждения |
| `extensions` | `extensions_type` | Всегда `[]` |

```json
[12, {
  "recovery_account": "recover-service",
  "account_to_recover": "alice",
  "new_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5newkey...", 1]]
  },
  "extensions": []
}]
```

- Только назначенный аккаунт восстановления `account_to_recover` может отправить это.
- Разрешён только один активный запрос на восстановление на аккаунт; повторная отправка обновляет запрос и сбрасывает 24-часовое окно.
- Для отмены: установите `new_master_authority.weight_threshold` в `0`.

---

## `recover_account_operation` (ID 13)

**Авторизация:** Подписи, удовлетворяющие **и** `new_master_authority`, **и** `recent_master_authority`

Подтверждает восстановление, доказывая предыдущее владение. Должна быть транслирована в течение 24 часов после запроса восстановления.

| Поле | Тип | Описание |
|------|-----|---------|
| `account_to_recover` | `account_name_type` | Восстанавливаемый аккаунт |
| `new_master_authority` | `authority` | Новая master authority (должна точно совпадать с запросом на восстановление) |
| `recent_master_authority` | `authority` | Master authority, действовавшая в течение последних 30 дней |
| `extensions` | `extensions_type` | Всегда `[]` |

```json
[13, {
  "account_to_recover": "alice",
  "new_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5newkey...", 1]]
  },
  "recent_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5oldkey...", 1]]
  },
  "extensions": []
}]
```

- Транзакция должна быть подписана ключами, одновременно удовлетворяющими **обеим** authority — новой и недавней.
- `new_master_authority` должна точно совпадать с указанной в ожидающем запросе на восстановление.
- После восстановления старый master ключ становится недействительным.

---

## `change_recovery_account_operation` (ID 14)

**Авторизация:** `master` `account_to_recover`

Изменяет аккаунт восстановления. Изменение вступает в силу после **30-дневной задержки**, чтобы предотвратить подмену аккаунта восстановления злоумышленником во время активной атаки.

| Поле | Тип | Описание |
|------|-----|---------|
| `account_to_recover` | `account_name_type` | Аккаунт, меняющий свой аккаунт восстановления |
| `new_recovery_account` | `account_name_type` | Новое имя аккаунта восстановления |
| `extensions` | `extensions_type` | Всегда `[]` |

```json
[14, {
  "account_to_recover": "alice",
  "new_recovery_account": "new-recovery-service",
  "extensions": []
}]
```

- `new_recovery_account` должен быть существующим аккаунтом.
- Если `new_recovery_account` равен `""`, аккаунтом восстановления становится валидатор с наибольшим количеством голосов.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Аккаунты](./accounts.md).
