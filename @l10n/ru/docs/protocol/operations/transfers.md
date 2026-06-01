# Операции переводов

---

## `transfer_operation` (ID 2)

**Авторизация:** `active` `from` (VIZ) / `master` `from` (SHARES)

Переводит токены VIZ или SHARES между аккаунтами.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Отправляющий аккаунт |
| `to` | `account_name_type` | Получающий аккаунт |
| `amount` | `asset` | Сумма перевода (VIZ или SHARES) |
| `memo` | `string` | Текст memo (открытый или зашифрованный; может быть `""`) |

```json
[2, {
  "from": "alice",
  "to": "bob",
  "amount": "10.000 VIZ",
  "memo": "payment for services"
}]
```

- `amount.symbol` должен быть `VIZ` или `SHARES`.
- Переводы VIZ требуют полномочия **active**; переводы SHARES требуют полномочия **master**.
- Формат зашифрованного memo: `#`, за которым следует ciphertext в кодировке base58.

---

## `transfer_to_vesting_operation` (ID 3)

**Авторизация:** `active` `from`

Конвертирует ликвидные VIZ в SHARES (стейкинг). SHARES могут быть начислены другому аккаунту.

| Поле | Тип | Описание |
|------|-----|---------|
| `from` | `account_name_type` | Аккаунт, предоставляющий VIZ |
| `to` | `account_name_type` | Аккаунт, получающий SHARES (может совпадать с `from`) |
| `amount` | `asset` (VIZ) | Количество VIZ для стейкинга |

```json
[3, {
  "from": "alice",
  "to": "alice",
  "amount": "100.000 VIZ"
}]
```

- `amount.symbol` должен быть `VIZ`.
- `to` может быть любым существующим аккаунтом — полезно для подарка застейканных SHARES.

---

## `withdraw_vesting_operation` (ID 4)

**Авторизация:** `active` `account`

Инициирует постепенный вывод SHARES обратно в ликвидные VIZ через несколько интервалов.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт, инициирующий вывод |
| `vesting_shares` | `asset` (SHARES) | Суммарные SHARES для вывода; `0.000000 SHARES` отменяет |

```json
[4, {
  "account": "alice",
  "vesting_shares": "1000.000000 SHARES"
}]
```

- Вывод распределяется на `withdraw_intervals` интервалов (свойство цепочки, по умолч. 28).
- Каждый интервал: конвертируется `vesting_shares / withdraw_intervals` SHARES.
- Установите `"0.000000 SHARES"` для отмены активного вывода.

---

## `set_withdraw_vesting_route_operation` (ID 11)

**Авторизация:** `active` `from_account`

Направляет процент выводимых vesting средств на указанный аккаунт с опциональным повторным вестингом направленной части.

| Поле | Тип | Описание |
|------|-----|---------|
| `from_account` | `account_name_type` | Аккаунт, выводы которого перенаправляются |
| `to_account` | `account_name_type` | Аккаунт-получатель |
| `percent` | `uint16_t` | Процент для маршрутизации (0–10000 базисных пунктов) |
| `auto_vest` | `bool` | Если `true`, направленные токены немедленно снова вестируются в `to_account` |

```json
[11, {
  "from_account": "alice",
  "to_account": "bob",
  "percent": 5000,
  "auto_vest": false
}]
```

- `percent` = 0 удаляет этот маршрут к `to_account`.
- Сумма всех маршрутов от `from_account` не должна превышать 10000.
- Допускаются несколько маршрутов к разным аккаунтам.

---

## `delegate_vesting_shares_operation` (ID 19)

**Авторизация:** `active` `delegator`

Делегирует SHARES другому аккаунту. Делегат получает пропускную способность и голосовую силу; владение остаётся у делегирующего.

| Поле | Тип | Описание |
|------|-----|---------|
| `delegator` | `account_name_type` | Аккаунт, делегирующий SHARES |
| `delegatee` | `account_name_type` | Аккаунт, получающий делегирование |
| `vesting_shares` | `asset` (SHARES) | Сумма делегирования; `0.000000 SHARES` удаляет делегирование |

```json
[19, {
  "delegator": "alice",
  "delegatee": "bob",
  "vesting_shares": "500.000000 SHARES"
}]
```

- `vesting_shares` должен быть ≥ свойства цепочки `min_delegation`, или ровно `0.000000 SHARES` для удаления.
- При удалении делегирования SHARES входят в 7-дневное окно возврата перед зачислением обратно.
- Виртуальная `return_vesting_delegation_operation` срабатывает по окончании окна возврата.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md).
