# Операции предложений

Предложения обеспечивают мультиподписное управление: один аккаунт создаёт набор операций, требующий одобрения от заданного набора подписантов перед исполнением. Предложение исполняется автоматически при получении достаточного числа одобрений.

---

## `proposal_create_operation` (ID 22)

**Авторизация:** `active` `author`

Создаёт транзакционное предложение. Предложение идентифицируется парой `author` + `title`.

| Поле | Тип | Описание |
|------|-----|---------|
| `author` | `account_name_type` | Аккаунт, создающий предложение |
| `title` | `string` | Уникальное название для данного автора (используется как ID предложения) |
| `memo` | `string` | Описание в читаемом виде |
| `expiration_time` | `time_point_sec` | Время истечения предложения |
| `proposed_operations` | `vector<operation_wrapper>` | Операции для исполнения при одобрении |
| `review_period_time` | `optional<time_point_sec>` | Опционально: новые одобрения не принимаются после этого времени |
| `extensions` | `extensions_type` | Всегда `[]` |

Каждый элемент `proposed_operations` — это `operation_wrapper`:
```json
{"op": [type_id, operation_object]}
```

```json
[22, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "memo": "Joint transfer to shared fund",
  "expiration_time": "2024-12-31T23:59:59",
  "proposed_operations": [
    {
      "op": [2, {
        "from": "multisig-wallet",
        "to": "fund",
        "amount": "1000.000 VIZ",
        "memo": ""
      }]
    }
  ],
  "review_period_time": null,
  "extensions": []
}]
```

- `title` должен быть уникальным для каждого `author`.
- `expiration_time` должен быть в будущем.
- Если задан `review_period_time`, он должен быть раньше `expiration_time`; новые одобрения после этого момента не принимаются.
- `proposed_operations` может содержать несколько операций любого типа.

---

## `proposal_update_operation` (ID 23)

**Авторизация:** Зависит от изменяемых наборов одобрений

Добавляет или удаляет одобрения. Предложение исполняется автоматически при наборе достаточного количества одобрений.

| Поле | Тип | Описание |
|------|-----|---------|
| `author` | `account_name_type` | Автор предложения |
| `title` | `string` | Название предложения |
| `active_approvals_to_add` | `flat_set<account_name_type>` | Аккаунты, дающие active-одобрение |
| `active_approvals_to_remove` | `flat_set<account_name_type>` | Аккаунты, отзывающие active-одобрение |
| `master_approvals_to_add` | `flat_set<account_name_type>` | Аккаунты, дающие master-одобрение |
| `master_approvals_to_remove` | `flat_set<account_name_type>` | Аккаунты, отзывающие master-одобрение |
| `regular_approvals_to_add` | `flat_set<account_name_type>` | Аккаунты, дающие regular-одобрение |
| `regular_approvals_to_remove` | `flat_set<account_name_type>` | Аккаунты, отзывающие regular-одобрение |
| `key_approvals_to_add` | `flat_set<public_key_type>` | Публичные ключи, дающие одобрение |
| `key_approvals_to_remove` | `flat_set<public_key_type>` | Публичные ключи, отзывающие одобрение |
| `extensions` | `extensions_type` | Всегда `[]` |

```json
[23, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "active_approvals_to_add": ["bob"],
  "active_approvals_to_remove": [],
  "master_approvals_to_add": [],
  "master_approvals_to_remove": [],
  "regular_approvals_to_add": [],
  "regular_approvals_to_remove": [],
  "key_approvals_to_add": [],
  "key_approvals_to_remove": [],
  "extensions": []
}]
```

- Транзакция должна быть подписана ключами, соответствующими добавляемым или удаляемым одобрениям.
- Все поля `*_to_add` и `*_to_remove` по умолчанию равны `[]`, если не нужны.
- После исполнения предложение считается завершённым; дальнейшие обновления отклоняются.

---

## `proposal_delete_operation` (ID 24)

**Авторизация:** `active` `requester`

Безвозвратно удаляет (накладывает вето на) предложение. Может быть вызвана любым требуемым authority в данном предложении.

| Поле | Тип | Описание |
|------|-----|---------|
| `author` | `account_name_type` | Автор предложения |
| `title` | `string` | Название предложения |
| `requester` | `account_name_type` | Аккаунт, запрашивающий удаление |
| `extensions` | `extensions_type` | Всегда `[]` |

```json
[24, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "requester": "bob",
  "extensions": []
}]
```

- `requester` должен быть требуемым authority в данном предложении.
- Удаление необратимо.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Database API — get_proposed_transactions](../../plugins/database-api.md#get_proposed_transactionsaccount-from-limit).
