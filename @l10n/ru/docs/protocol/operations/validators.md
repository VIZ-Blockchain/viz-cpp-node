# Операции валидаторов

---

## `witness_update_operation` (ID 6)

**Авторизация:** `active` `owner`

Регистрирует или обновляет узел валидатора. Установка `block_signing_key` в нулевой ключ удаляет валидатора из производства блоков.

| Поле | Тип | Описание |
|------|-----|---------|
| `owner` | `account_name_type` | Имя аккаунта валидатора |
| `url` | `string` | Сайт или информационный URL валидатора (непустой, макс. 256 байт) |
| `block_signing_key` | `public_key_type` | Ключ, используемый для подписи произведённых блоков |

```json
[6, {
  "owner": "alice",
  "url": "https://alice.example.com",
  "block_signing_key": "VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"
}]
```

- **Нулевой ключ** (деактивация): `"VIZ1111111111111111111111111111111114T1Anm"` — удаляет из производства блоков без удаления записи валидатора.
- Трансляция этой операции требует `witness_declaration_fee` (выплачивается в фонд комитета).

---

## `chain_properties_update_operation` (ID 25)

**Авторизация:** `active` `owner`

Голосует за базовые свойства цепочки (формат `chain_properties_init`). Значение on-chain — медиана среди всех активных валидаторов.

| Поле | Тип | Описание |
|------|-----|---------|
| `owner` | `account_name_type` | Валидатор, отдающий голос |
| `props` | `chain_properties_init` | Предлагаемые параметры цепочки |

```json
[25, {
  "owner": "alice",
  "props": {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 65536,
    "create_account_delegation_ratio": 10,
    "create_account_delegation_time": 2592000,
    "min_delegation": "1.000 VIZ",
    "min_curation_percent": 0,
    "max_curation_percent": 10000,
    "bandwidth_reserve_percent": 1000,
    "bandwidth_reserve_below": "1.000000 SHARES",
    "flag_energy_additional_cost": 1000,
    "vote_accounting_min_rshares": 0,
    "committee_request_approve_min_percent": 1000
  }
}]
```

- Все процентные поля в базисных пунктах (0–10000).
- `min_curation_percent` должен быть ≤ `max_curation_percent`.
- Используйте `versioned_chain_properties_update_operation` (ID 46) для расширенных свойств HF9+.

---

## `versioned_chain_properties_update_operation` (ID 46)

**Авторизация:** `active` `owner`

Голосует за версионированные свойства цепочки, поддерживающие все расширения харфорков. Предпочтительнее `chain_properties_update_operation` для текущих узлов.

| Поле | Тип | Описание |
|------|-----|---------|
| `owner` | `account_name_type` | Валидатор, отдающий голос |
| `props` | `versioned_chain_properties` | Версионированные параметры, сериализованные как `[index, object]` |

```json
[46, {
  "owner": "alice",
  "props": [3, {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 65536,
    "create_account_delegation_ratio": 10,
    "create_account_delegation_time": 2592000,
    "min_delegation": "1.000 VIZ",
    "min_curation_percent": 0,
    "max_curation_percent": 10000,
    "bandwidth_reserve_percent": 1000,
    "bandwidth_reserve_below": "1.000000 SHARES",
    "flag_energy_additional_cost": 1000,
    "vote_accounting_min_rshares": 0,
    "committee_request_approve_min_percent": 1000,
    "inflation_witness_percent": 2000,
    "inflation_ratio_committee_vs_reward_fund": 1000,
    "inflation_recalc_period": 28800,
    "data_operations_cost_additional_bandwidth": 0,
    "witness_miss_penalty_percent": 100,
    "witness_miss_penalty_duration": 86400,
    "create_invite_min_balance": "1.000 VIZ",
    "committee_create_request_fee": "1.000 VIZ",
    "create_paid_subscription_fee": "1.000 VIZ",
    "account_on_sale_fee": "10.000 VIZ",
    "subaccount_on_sale_fee": "1.000 VIZ",
    "witness_declaration_fee": "1.000 VIZ",
    "withdraw_intervals": 28
  }]
}]
```

- `props` — статический вариант: используйте индекс `3` для `chain_properties_hf9` (текущий).
- Полный список полей по индексу версии см. в [Типах данных](../data-types.md#versioned_chain_properties).

---

## `account_witness_vote_operation` (ID 7)

**Авторизация:** `active` `account`

Голосует за валидатора или снимает голос. Топ-21 валидаторов по кумулятивному весу голосов производят блоки.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Голосующий аккаунт |
| `witness` | `account_name_type` | Валидатор, за которого голосуют |
| `approve` | `bool` | `true` для добавления голоса, `false` для его снятия |

```json
[7, {
  "account": "alice",
  "witness": "bob",
  "approve": true
}]
```

- Вес голоса пропорционален стейку SHARES голосующего.
- `approve: false` снимает ранее поданный голос.

---

## `account_witness_proxy_operation` (ID 8)

**Авторизация:** `active` `account`

Делегирует все голоса за валидаторов аккаунту-прокси. Все существующие прямые голоса удаляются при установке прокси.

| Поле | Тип | Описание |
|------|-----|---------|
| `account` | `account_name_type` | Аккаунт, устанавливающий прокси |
| `proxy` | `account_name_type` | Аккаунт-прокси; `""` удаляет прокси |

```json
[8, {
  "account": "alice",
  "proxy": "bob"
}]
```

- `proxy: ""` (пустая строка) удаляет прокси и восстанавливает прямое голосование.
- Нельзя установить прокси на себя.
- Цепочки прокси разрешаются транзитивно (A→B→C); максимальная глубина цепочки ограничена.
- Установка прокси удаляет все прямые голоса за валидаторов.

---

См. также: [Типы данных](../data-types.md), [Обзор операций](./overview.md), [Свойства цепочки](../../governance/chain-properties.md).
