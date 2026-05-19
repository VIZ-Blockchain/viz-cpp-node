# Операции эскроу

Эскроу удерживает токены VIZ в условном переводе: средства высвобождаются только после одобрения как получателем, так и нейтральным агентом, либо арбитрируются агентом в случае спора.

**Процесс эскроу:**
```
escrow_transfer  →  escrow_approve (обоими "to" и "agent")
                 →  escrow_release (от "from" или "to")
                 →  [escrow_dispute]  →  escrow_release (только от "agent")
                 ↓
           (срок подтверждения пропущен)
                 →  expire_escrow_ratification_operation [виртуальная — средства возвращаются]
```

---

## `escrow_transfer_operation` (ID 15)

**Авторизация:** `active` `from`

Создаёт эскроу. Средства немедленно уходят от `from` на баланс эскроу; для высвобождения необходимо одобрение и `agent`, и `to`.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Отправитель |
| `to` | `account_name_type` | Предполагаемый получатель |
| `agent` | `account_name_type` | Нейтральный агент эскроу (арбитр) |
| `escrow_id` | `uint32_t` | Уникальный ID, выбранный отправителем (по умолч. 30) |
| `token_amount` | `asset` (VIZ) | Сумма для удержания в эскроу |
| `fee` | `asset` (VIZ) | Комиссия агента — выплачивается агенту при одобрении |
| `ratification_deadline` | `time_point_sec` | Срок для одобрения обеими сторонами |
| `escrow_expiration` | `time_point_sec` | Истечение, если эскроу так и не будет высвобождено |
| `json_metadata` | `string` | Опциональные условия / метаданные |

```json
[15, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "escrow_id": 1001,
  "token_amount": "100.000 VIZ",
  "fee": "1.000 VIZ",
  "ratification_deadline": "2024-06-01T00:00:00",
  "escrow_expiration": "2024-07-01T00:00:00",
  "json_metadata": "{\"description\":\"payment for work\"}"
}]
```

- `ratification_deadline` должен быть раньше `escrow_expiration`.
- Обе временные метки должны быть в будущем на момент трансляции.
- `escrow_id` должен быть уникальным для аккаунта `from`.
- Если не одобрено до `ratification_deadline`, срабатывает виртуальная `expire_escrow_ratification_operation`, и средства возвращаются `from`.

---

## `escrow_approve_operation` (ID 18)

**Авторизация:** `active` `who`

Одобряет или отклоняет эскроу. Для активации эскроу необходимо одобрение и `to`, и `agent`.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Исходный отправитель эскроу |
| `to` | `account_name_type` | Исходный получатель эскроу |
| `agent` | `account_name_type` | Агент эскроу |
| `who` | `account_name_type` | Кто одобряет: должен быть `to` или `agent` |
| `escrow_id` | `uint32_t` | ID эскроу |
| `approve` | `bool` | `true` для одобрения, `false` для отклонения |

```json
[18, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "bob",
  "escrow_id": 1001,
  "approve": true
}]
```

- `who` должен быть либо `to`, либо `agent`.
- После одобрения не может быть отозвано.
- Если `approve: false` — эскроу отменяется и средства возвращаются `from`.
- Должно быть транслировано до `ratification_deadline`.

---

## `escrow_dispute_operation` (ID 16)

**Авторизация:** `active` `who`

Открывает спор по одобренному эскроу. После спора только `agent` может высвобождать средства.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Исходный отправитель эскроу |
| `to` | `account_name_type` | Исходный получатель эскроу |
| `agent` | `account_name_type` | Агент эскроу |
| `who` | `account_name_type` | Кто открывает спор: должен быть `from` или `to` |
| `escrow_id` | `uint32_t` | ID эскроу |

```json
[16, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "escrow_id": 1001
}]
```

- Может быть открыт только по **одобренному** эскроу (оба `to` и `agent` одобрили).
- Должен быть открыт до `escrow_expiration`.

---

## `escrow_release_operation` (ID 17)

**Авторизация:** `active` `who`

Высвобождает средства эскроу `receiver`. Частичные высвобождения допускаются.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Исходный отправитель эскроу |
| `to` | `account_name_type` | Исходный получатель эскроу |
| `agent` | `account_name_type` | Агент эскроу |
| `who` | `account_name_type` | Аккаунт, высвобождающий средства |
| `receiver` | `account_name_type` | Аккаунт, получающий средства (должен быть `from` или `to`) |
| `escrow_id` | `uint32_t` | ID эскроу |
| `token_amount` | `asset` (VIZ) | Сумма для высвобождения (может быть частичной) |

```json
[17, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "receiver": "bob",
  "escrow_id": 1001,
  "token_amount": "100.000 VIZ"
}]
```

**Правила разрешения на высвобождение:**

| Состояние | Кто может высвобождать | Кому |
|----------|----------------------|------|
| Без спора, до истечения | `from` или `to` | Другой стороне |
| Без спора, после истечения | `from` или `to` | Любой стороне |
| В споре | Только `agent` | Любой стороне |

- Частичные высвобождения допускаются; остаток остаётся в эскроу.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Виртуальные операции](../virtual-operations.md).
