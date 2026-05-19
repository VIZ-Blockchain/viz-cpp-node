# Операции рынка аккаунтов

Рынок аккаунтов позволяет выставлять аккаунты и пространства субаккаунтов на продажу и покупать их в блокчейне.

---

## `set_account_price_operation` (ID 54)

**Авторизация:** `master` `account`

Выставляет аккаунт на публичную продажу или обновляет объявление. При листинге взимается `account_on_sale_fee`.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Выставляемый аккаунт |
| `account_seller` | `account_name_type` | Аккаунт, получающий оплату (может отличаться от `account`) |
| `account_offer_price` | `asset` (VIZ) | Запрашиваемая цена |
| `account_on_sale` | `bool` | `true` — выставить; `false` — снять с продажи |

```json
[54, {
  "account": "alice",
  "account_seller": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_on_sale": true
}]
```

- `account_on_sale: false` снимает аккаунт с продажи без возврата комиссии.
- `account_seller` может быть любым аккаунтом — полезно при брокерских продажах.

---

## `set_subaccount_price_operation` (ID 55)

**Авторизация:** `master` `account`

Выставляет на продажу право создания субаккаунтов (например, `account.childname`). При листинге взимается `subaccount_on_sale_fee`.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Родительский аккаунт |
| `subaccount_seller` | `account_name_type` | Аккаунт, получающий оплату |
| `subaccount_offer_price` | `asset` (VIZ) | Цена за право создания одного субаккаунта |
| `subaccount_on_sale` | `bool` | `true` — выставить; `false` — снять с продажи |

```json
[55, {
  "account": "alice",
  "subaccount_seller": "alice",
  "subaccount_offer_price": "50.000 VIZ",
  "subaccount_on_sale": true
}]
```

- Покупатели приобретают право создать один субаккаунт в пространстве имён `account` за транзакцию.

---

## `buy_account_operation` (ID 56)

**Авторизация:** `active` `buyer`

Покупает аккаунт, выставленный на продажу. Все права передаются покупателю.

| Поле | Тип | Описание |
|------|-----|---------|
| `buyer` | `account_name_type` | Покупающий аккаунт |
| `account` | `account_name_type` | Покупаемый аккаунт |
| `account_offer_price` | `asset` (VIZ) | Цена покупки (должна точно совпадать с объявлением) |
| `account_authorities_key` | `public_key_type` | Новый ключ, устанавливаемый как master, active, regular и memo купленного аккаунта |
| `tokens_to_shares` | `asset` (VIZ) | Дополнительный VIZ для конвертации в SHARES для купленного аккаунта (может быть `"0.000 VIZ"`) |

```json
[56, {
  "buyer": "bob",
  "account": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_authorities_key": "VIZ5newowner...",
  "tokens_to_shares": "0.000 VIZ"
}]
```

- `account_offer_price` должна точно совпадать с ценой в `set_account_price_operation`.
- `account_authorities_key` применяется ко всем четырём слотам authority одновременно.
- Оплата отправляется `account_seller`, указанному в объявлении.
- При успешной покупке срабатывает виртуальная операция `account_sale_operation`.

---

## `target_account_sale_operation` (ID 61)

**Авторизация:** `master` `account`

Выставляет аккаунт на приватную (адресную) продажу конкретному покупателю. Купить это объявление может только `target_buyer`.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Выставляемый аккаунт |
| `account_seller` | `account_name_type` | Аккаунт, получающий оплату |
| `target_buyer` | `account_name_type` | Единственный допустимый покупатель |
| `account_offer_price` | `asset` (VIZ) | Запрашиваемая цена |
| `account_on_sale` | `bool` | `true` — выставить; `false` — снять с продажи |

```json
[61, {
  "account": "alice",
  "account_seller": "alice",
  "target_buyer": "charlie",
  "account_offer_price": "500.000 VIZ",
  "account_on_sale": true
}]
```

- `account_on_sale: false` отменяет адресное объявление.
- Покупатель использует стандартную `buy_account_operation` для завершения покупки.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Виртуальные операции](../virtual-operations.md), [Database API — рынок аккаунтов](../../plugins/database-api.md#account-market).
