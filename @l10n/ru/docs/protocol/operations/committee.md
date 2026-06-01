# Операции комитета

Система комитета (рабочие предложения) позволяет членам сообщества запрашивать финансирование из фонда комитета. Держатели SHARES голосуют за одобрение или отклонение запросов; одобренные запросы получают выплату из фонда.

---

## `committee_worker_create_request_operation` (ID 35)

**Авторизация:** `regular` `creator`

Создаёт новый запрос на финансирование. При подаче с создателя взимается `committee_create_request_fee`.

| Поле | Тип | Описание |
|------|-----|---------|
| `creator` | `account_name_type` | Аккаунт, создающий запрос |
| `url` | `string` | URL с описанием предложения (непустой, макс. 255 байт) |
| `worker` | `account_name_type` | Аккаунт, который получит выплату |
| `required_amount_min` | `asset` (VIZ) | Минимально приемлемая выплата |
| `required_amount_max` | `asset` (VIZ) | Максимально приемлемая выплата |
| `duration` | `uint32_t` | Длительность запроса в секундах |

```json
[35, {
  "creator": "alice",
  "url": "https://alice.example.com/proposal",
  "worker": "alice",
  "required_amount_min": "100.000 VIZ",
  "required_amount_max": "500.000 VIZ",
  "duration": 604800
}]
```

**Ограничения:**

| Параметр | Значение |
|---------|---------|
| Минимальная длительность | 5 дней (432000 с) |
| Максимальная длительность | 30 дней (2592000 с) |
| `required_amount_max` | Должен быть > `required_amount_min` |

- `required_amount_min` ≥ 0; `required_amount_max` > `required_amount_min`.
- `worker` может отличаться от `creator`.

---

## `committee_worker_cancel_request_operation` (ID 36)

**Авторизация:** `regular` `creator`

Отменяет существующий запрос на финансирование до его истечения.

| Поле | Тип | Описание |
|------|-----|---------|
| `creator` | `account_name_type` | Создатель запроса |
| `request_id` | `uint32_t` | ID запроса для отмены |

```json
[36, {
  "creator": "alice",
  "request_id": 42
}]
```

- Только `creator` запроса может его отменить.
- `request_id` должен ссылаться на существующий активный запрос.

---

## `committee_vote_request_operation` (ID 37)

**Авторизация:** `regular` `voter`

Голосует за запрос на финансирование. Голосовая сила пропорциональна стейку SHARES голосующего.

| Поле | Тип | Описание |
|------|-----|---------|
| `voter` | `account_name_type` | Аккаунт, отдающий голос |
| `request_id` | `uint32_t` | ID запроса |
| `vote_percent` | `int16_t` | Вес голоса в базисных пунктах (−10000 до 10000) |

```json
[37, {
  "voter": "bob",
  "request_id": 42,
  "vote_percent": 10000
}]
```

- `vote_percent` > 0 → поддержка; `vote_percent` < 0 → возражение; `vote_percent` = 0 → снять голос.
- Запрос одобряется, когда взвешенный нетто-процент голосов ≥ свойству цепочки `committee_request_approve_min_percent`.

**Виртуальные операции, вызываемые жизненным циклом комитета:**

| Виртуальная операция | Триггер |
|--------------------|---------|
| `committee_cancel_request_operation` | Запрос истекает без одобрения |
| `committee_approve_request_operation` | Запрос достигает порога одобрения |
| `committee_payout_request_operation` | Обрабатывается выплата |
| `committee_pay_request_operation` | Работник получает оплату |

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Виртуальные операции](../virtual-operations.md), [Управление комитетом](../../governance/committee.md).
