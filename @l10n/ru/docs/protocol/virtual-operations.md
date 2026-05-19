# Виртуальные операции

Виртуальные операции генерируются самим блокчейном в процессе обработки блоков. Они **никогда не транслируются пользователями** — они появляются только в истории аккаунта и данных блоков в информационных целях.

Виртуальные операции используют тот же вариант операций, что и обычные, но имеют более высокие type ID. Их можно наблюдать через API `account_history` или потоковую трансляцию блоков.

---

## Выплаты за контент *(устарело)*

### `author_reward_operation` (ID 26)

**Триггер:** Выплата за публикацию контента

Срабатывает, когда автор получает свою долю вознаграждения из выплаты за контент.

| Поле | Тип | Описание |
|------|-----|---------|
| `author` | `account_name_type` | Автор контента |
| `permlink` | `string` | Permlink контента |
| `token_payout` | `asset` (VIZ) | Ликвидная часть VIZ |
| `vesting_payout` | `asset` (SHARES) | Вестинговая часть |

---

### `curation_reward_operation` (ID 27)

**Триггер:** Выплата за публикацию контента

Срабатывает, когда куратор получает вознаграждение за кураторство.

| Поле | Тип | Описание |
|------|-----|---------|
| `curator` | `account_name_type` | Аккаунт куратора |
| `reward` | `asset` (SHARES) | Вознаграждение куратора |
| `content_author` | `account_name_type` | Автор куриуемого контента |
| `content_permlink` | `string` | Permlink куриуемого контента |

---

### `content_reward_operation` (ID 28)

**Триггер:** Выплата за публикацию контента

Срабатывает, когда пост достигает времени выплаты.

| Поле | Тип | Описание |
|------|-----|---------|
| `author` | `account_name_type` | Автор контента |
| `permlink` | `string` | Permlink контента |
| `payout` | `asset` | Общая сумма выплаты |

---

### `content_payout_update_operation` (ID 32)

**Триггер:** Пересчёт выплаты за контент (например, после изменений голосов)

| Поле | Тип | Описание |
|------|-----|---------|
| `author` | `account_name_type` | Автор контента |
| `permlink` | `string` | Permlink контента |

---

### `content_benefactor_reward_operation` (ID 33)

**Триггер:** Выплата за публикацию контента — срабатывает для каждого бенефициара

| Поле | Тип | Описание |
|------|-----|---------|
| `benefactor` | `account_name_type` | Аккаунт бенефициара |
| `author` | `account_name_type` | Автор контента |
| `permlink` | `string` | Permlink контента |
| `reward` | `asset` | Доля вознаграждения бенефициара |

---

## Вывод вестинга

### `fill_vesting_withdraw_operation` (ID 29)

**Триггер:** Каждый интервал вывода вестинга

Срабатывает один раз за интервал для каждого активного маршрута вывода.

| Поле | Тип | Описание |
|------|-----|---------|
| `from_account` | `account_name_type` | Аккаунт, выводящий средства |
| `to_account` | `account_name_type` | Целевой аккаунт (может отличаться через маршрут вывода) |
| `withdrawn` | `asset` (SHARES) | Выведенное количество SHARES за интервал |
| `deposited` | `asset` | Зачислено на `to_account` (VIZ или SHARES при `auto_vest = true`) |

```json
[29, {
  "from_account": "alice",
  "to_account": "alice",
  "withdrawn": "35.714285 SHARES",
  "deposited": "10.000 VIZ"
}]
```

---

### `return_vesting_delegation_operation` (ID 34)

**Триггер:** Окончание 7-дневного периода возврата после `delegate_vesting_shares_operation` с нулевой суммой

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт, получающий возвращённые SHARES |
| `vesting_shares` | `asset` (SHARES) | SHARES, возвращённые из лимба |

---

## Операции валидатора

### `shutdown_witness_operation` (ID 30)

**Триггер:** Валидатор деактивирован из-за недостаточного веса голосов

| Поле | Тип | Описание |
|------|-----|---------|
| `owner` | `account_name_type` | Деактивированный валидатор |

---

### `witness_reward_operation` (ID 42)

**Триггер:** Произведён блок — валидатор получает вознаграждение за блок

| Поле | Тип | Описание |
|------|-----|---------|
| `witness` | `account_name_type` | Аккаунт валидатора |
| `shares` | `asset` (SHARES) | Вознаграждение за блок |

```json
[42, {
  "witness": "alice",
  "shares": "1.234567 SHARES"
}]
```

---

## Сетевые события

### `hardfork_operation` (ID 31)

**Триггер:** Активация хардфорка сети

| Поле | Тип | Описание |
|------|-----|---------|
| `hardfork_id` | `uint32_t` | Номер хардфорка |

---

## Награды

### `receive_award_operation` (ID 48)

**Триггер:** `award_operation` или `fixed_award_operation`

Срабатывает для основного получателя награды.

| Поле | Тип | Описание |
|------|-----|---------|
| `initiator` | `account_name_type` | Аккаунт, выдавший награду |
| `receiver` | `account_name_type` | Аккаунт, получивший награду |
| `custom_sequence` | `uint64_t` | Порядковый номер приложения из операции награды |
| `memo` | `string` | Memo из операции награды |
| `shares` | `asset` (SHARES) | Полученные SHARES |

```json
[48, {
  "initiator": "alice",
  "receiver": "bob",
  "custom_sequence": 0,
  "memo": "great article!",
  "shares": "5.000000 SHARES"
}]
```

---

### `benefactor_award_operation` (ID 49)

**Триггер:** `award_operation` или `fixed_award_operation` с бенефициарами

Срабатывает по одному разу для каждого бенефициара.

| Поле | Тип | Описание |
|------|-----|---------|
| `initiator` | `account_name_type` | Аккаунт, выдавший награду |
| `benefactor` | `account_name_type` | Аккаунт бенефициара |
| `receiver` | `account_name_type` | Основной получатель награды |
| `custom_sequence` | `uint64_t` | Порядковый номер приложения |
| `memo` | `string` | Memo из операции награды |
| `shares` | `asset` (SHARES) | SHARES, полученные бенефициаром |

---

## Комитет

### `committee_cancel_request_operation` (ID 38)

**Триггер:** Запрос на финансирование комитета истекает, не набрав порог одобрения

| Поле | Тип | Описание |
|------|-----|---------|
| `request_id` | `uint32_t` | ID отменённого запроса |

---

### `committee_approve_request_operation` (ID 39)

**Триггер:** Запрос на финансирование комитета достигает требуемого порога одобрения

| Поле | Тип | Описание |
|------|-----|---------|
| `request_id` | `uint32_t` | ID одобренного запроса |

---

### `committee_payout_request_operation` (ID 40)

**Триггер:** Обработка выплаты по запросу комитета

| Поле | Тип | Описание |
|------|-----|---------|
| `request_id` | `uint32_t` | ID выплаченного запроса |

---

### `committee_pay_request_operation` (ID 41)

**Триггер:** Работник получает выплату из фонда комитета

| Поле | Тип | Описание |
|------|-----|---------|
| `worker` | `account_name_type` | Аккаунт работника |
| `request_id` | `uint32_t` | ID запроса комитета |
| `tokens` | `asset` (VIZ) | Выплаченная сумма |

```json
[41, {
  "worker": "alice",
  "request_id": 42,
  "tokens": "250.000 VIZ"
}]
```

---

## Платные подписки

### `paid_subscription_action_operation` (ID 52)

**Триггер:** Исполнена `paid_subscribe_operation` или обработан платёж автопродления

| Поле | Тип | Описание |
|------|-----|---------|
| `subscriber` | `account_name_type` | Аккаунт подписчика |
| `account` | `account_name_type` | Провайдер подписки |
| `level` | `uint16_t` | Уровень подписки |
| `amount` | `asset` (VIZ) | Сумма платежа |
| `period` | `uint16_t` | Количество периодов |
| `summary_duration_sec` | `uint64_t` | Суммарная длительность подписки (секунды) |
| `summary_amount` | `asset` (VIZ) | Общая выплаченная сумма |

---

### `cancel_paid_subscription_operation` (ID 53)

**Триггер:** Подписка истекла или недостаточно баланса для автопродления

| Поле | Тип | Описание |
|------|-----|---------|
| `subscriber` | `account_name_type` | Аккаунт подписчика |
| `account` | `account_name_type` | Провайдер подписки |

---

## Рынок аккаунтов

### `account_sale_operation` (ID 57)

**Триггер:** `buy_account_operation` успешно завершена

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Проданный аккаунт |
| `price` | `asset` (VIZ) | Цена продажи |
| `buyer` | `account_name_type` | Покупатель |
| `seller` | `account_name_type` | Продавец (получатель платежа) |

```json
[57, {
  "account": "alice",
  "price": "1000.000 VIZ",
  "buyer": "bob",
  "seller": "alice"
}]
```

---

### `bid_operation` (ID 62)

**Триггер:** Новая ставка на аккаунт, выставленный на аукцион

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт, на который делается ставка |
| `bidder` | `account_name_type` | Аккаунт, делающий ставку |
| `bid` | `asset` (VIZ) | Сумма ставки |

---

### `outbid_operation` (ID 63)

**Триггер:** Предыдущая ставка перебита более высокой

Срабатывает для перебитого аккаунта; предыдущая сумма ставки возвращается.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт, на который делается ставка |
| `bidder` | `account_name_type` | Перебитый аккаунт |
| `bid` | `asset` (VIZ) | Возвращённая сумма ставки |

---

##托管 (Escrow)

### `expire_escrow_ratification_operation` (ID 59)

**Триггер:** Пропущен дедлайн `ratification_deadline` — ни `to`, ни `agent` не дали одобрения вовремя

Все заблокированные средства возвращаются `from`.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Исходный отправитель эскроу |
| `to` | `account_name_type` | Предполагаемый получатель |
| `agent` | `account_name_type` | Агент эскроу |
| `escrow_id` | `uint32_t` | ID эскроу |
| `token_amount` | `asset` (VIZ) | Возвращённая сумма токенов |
| `fee` | `asset` (VIZ) | Возвращённая комиссия (агент не получает оплату, поскольку эскроу не был ратифицирован) |
| `ratification_deadline` | `time_point_sec` | Пропущенный дедлайн |

---

См. также: [Обзор операций](./operations/overview.md), [Награды](./operations/awards.md), [Комитет](./operations/committee.md).
