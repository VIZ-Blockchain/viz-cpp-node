# Параметры цепочки

Параметры цепочки — это управляемые параметры сети: комиссии, размер блока, уровни инфляции, правила штрафов и другое. Центрального органа, устанавливающего их, нет — каждый активный валидатор публикует свои предпочтительные значения, а блокчейн применяет **медиану** по всем активным валидаторам.

---

## Как это работает

### 1. Валидаторы публикуют предпочтения

Каждый валидатор отправляет предпочтительные параметры через `versioned_chain_properties_update_operation`:

```json
[46, {
  "owner": "alice",
  "props": [3, {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 131072,
    ...
  }]
}]
```

`[3, {...}]` указывает версию 3 (`chain_properties_hf9`, текущий формат).

### 2. Расчёт медианы

При каждом обновлении расписания валидаторов блокчейн вызывает `update_median_witness_props()`. Для **каждого параметра независимо** он:
1. Собирает значения от каждого активного валидатора.
2. Сортирует их.
3. Выбирает **медиану** (индекс `active.size() / 2`).

```
Пример — 5 валидаторов голосуют за account_creation_fee:
  0.5, 1.0, 1.0, 2.0, 5.0 VIZ
              ↑
         медиана = 1.0 VIZ
```

Медиана устойчива к экстремумам: один валидатор не может вызвать внезапный крупный сдвиг; для значительного изменения любого параметра необходимо согласие большинства.

### 3. Применение

Результирующий объект `median_props` сохраняется в `validator_schedule_object` и применяется при обработке всех блоков.

---

## Все управляемые параметры

### Аккаунт и делегирование

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `account_creation_fee` | asset (VIZ) | 1.000 VIZ | Минимальная комиссия за создание нового аккаунта |
| `create_account_delegation_ratio` | uint32 | 10 | Необходимое делегирование = ratio × fee |
| `create_account_delegation_time` | uint32 (с) | 2592000 (30д) | Время блокировки делегирования при создании |
| `min_delegation` | asset (VIZ) | 1.000 VIZ | Минимальная сумма для любого делегирования SHARES |

### Размер блока и пропускная способность

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `maximum_block_size` | uint32 (байт) | 131072 | Максимальный размер блока; управляет пропускной способностью |
| `bandwidth_reserve_percent` | uint16 (bp) | 1000 (10%) | Дополнительная пропускная способность для малых аккаунтов |
| `bandwidth_reserve_below` | asset (SHARES) | 500.000000 | Порог для получения резерва пропускной способности |
| `data_operations_cost_additional_bandwidth` | uint32 (%) | 0 | Множитель дополнительной пропускной способности для операций с данными (custom_operation) |

### Инфляция и экономика

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `inflation_validator_percent` | uint16 (bp) | 2000 (20%) | Доля валидаторов от блочной инфляции |
| `inflation_ratio_committee_vs_reward_fund` | uint16 (bp) | 5000 (50%) | Разделение оставшейся инфляции: фонд комитета vs фонд вознаграждений |
| `inflation_recalc_period` | uint32 (блоки) | 806400 (~28д) | Как часто пересчитывается инфляция |

Поток инфляции: `block_reward × inflation_validator_percent` → валидатор. Остаток делится: `inflation_ratio_committee_vs_reward_fund` → фонд комитета; остальное → фонд вознаграждений за награды.

### Система вознаграждений

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `min_curation_percent` | uint16 (bp) | 500 (5%) | Минимальная доля кураторского вознаграждения из выплат за контент |
| `max_curation_percent` | uint16 (bp) | 500 (5%) | Максимальная доля кураторского вознаграждения |
| `vote_accounting_min_rshares` | uint32 | 5000000 | Минимальные rshares для ненулевого вознаграждения за награду |
| `flag_energy_additional_cost` | uint16 (bp) | 0 | Дополнительная стоимость энергии для даунвотов/флагов |

### Ответственность валидаторов

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `validator_miss_penalty_percent` | uint16 (bp) | 100 (1%) | Снижение веса голосов при пропуске блока |
| `validator_miss_penalty_duration` | uint32 (с) | 86400 (1д) | Продолжительность штрафа за пропуск |

### Комиссии

Все комиссии поступают в фонд комитета (казна DAO).

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `committee_create_request_fee` | asset (VIZ) | 100.000 VIZ | Комиссия за создание заявки на финансирование комитета |
| `create_paid_subscription_fee` | asset (VIZ) | 100.000 VIZ | Комиссия за создание платной подписки |
| `account_on_sale_fee` | asset (VIZ) | 10.000 VIZ | Комиссия за выставление аккаунта на продажу |
| `subaccount_on_sale_fee` | asset (VIZ) | 100.000 VIZ | Комиссия за выставление прав создания субаккаунта на продажу |
| `validator_declaration_fee` | asset (VIZ) | 10.000 VIZ | Единовременная комиссия за регистрацию валидатора |
| `create_invite_min_balance` | asset (VIZ) | 10.000 VIZ | Минимальный баланс инвайта |

### Вывод из вестинга

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `withdraw_intervals` | uint16 | 28 | Количество ежедневных платежей при анстейкинге SHARES |

### Голосование комитета (HF14)

Антиспам-ограничения на бюллетени заявок DAO-комитета, действуют с HF14. Эти два поля живут в **PM-структуре** (`chain_properties_pm`, версия 5), а не в базовой `chain_properties_hf9` — чтобы не трогать уже действующий wire-формат hf9 и не ломать позиционную раскладку голосов валидаторов до HF14.

| Параметр | Тип | По умолчанию | Описание |
|---------|-----|------------|---------|
| `committee_votes_per_request` | uint32 | 100 000 | Максимум бюллетеней, которые может накопить одна заявка комитета, прежде чем новые голоса отклоняются |
| `committee_vote_min_vesting` | asset (VIZ) | 1000.000 VIZ | Минимальный эффективный вестинг (конвертируется в SHARES на момент голосования), необходимый для подачи или пересмотра бюллетеня комитета |

---

## Версии параметров

Параметры вводились поэтапно при хардфорках:

| Версия | Индекс | Хардфорк | Добавленные поля |
|--------|--------|---------|----------------|
| `chain_properties_init` | 0 | Генезис | account_creation_fee, maximum_block_size, параметры делегирования, курация, пропускная способность, стоимость флага, минимальные rshares, порог комитета |
| `chain_properties_hf4` | 1 | HF4 | inflation_validator_percent, inflation_ratio_committee_vs_reward_fund, inflation_recalc_period |
| `chain_properties_hf6` | 2 | HF6 | data_operations_cost_additional_bandwidth, validator_miss_penalty_percent, validator_miss_penalty_duration |
| `chain_properties_hf9` | 3 | HF9 | create_invite_min_balance, committee_create_request_fee, create_paid_subscription_fee, account_on_sale_fee, subaccount_on_sale_fee, validator_declaration_fee, withdraw_intervals |
| `chain_properties_hf13` | 4 | HF13 | distribution_epoch_length |
| `chain_properties_pm` | 5 | HF14 | ~30 параметров прогнозных рынков + kill-switch `pm_commit_reveal_enabled`, `pm_lazy_pool_enabled` + голосовые капсы `committee_votes_per_request`, `committee_vote_min_vesting`, `pm_dispute_votes_per_market`, `pm_dispute_vote_min_vesting` |

Для всех новых публикаций параметров валидатора используйте индекс версии **5** (`chain_properties_pm`). Индекс 4 — `chain_properties_hf13` (`distribution_epoch_length`).

### Параметры прогнозных рынков (v5, HF14) {#pm-parameters}

Все медиан-голосуемые; см. [Операции прогнозных рынков](../protocol/operations/prediction-markets.md).

Все проценты PM — в bp (10000 = 100.00%), как прочие `*_percent`; промилле (‰) нигде нет.

- **Оракул:** `pm_min_oracle_insurance`, `pm_max_oracle_fee_percent` (**единственный** governance-кэп на фи — на % оракула), `pm_oracle_registration_fee`, `pm_oracle_penalty_percent`, `pm_oracle_dispute_response_sec`, `pm_oracle_accept_window_sec` (по умолчанию 3600 = 1 ч — названный оракул должен принять или отклонить пендинг-рынок в течение этого окна; по истечении крон возвращает создателю сид-ликвидность, но **не** комиссию за создание, и аннулирует рынок → `pm_market_expired`).
- **Риск / покрытие** *(процент от объёма ставок рынка, 100 = 1.0×):* `pm_listing_min_coverage_percent` (250 = 2.5×) — рынки, чья страховка оракула покрывает меньше этой доли их объёма, скрыты из каталога `list_markets` по умолчанию (показываются через `show_risky`); `pm_betting_min_coverage_percent` (150 = 1.5×) — рекомендательный порог, публикуемый для клиентов, чтобы требовать явного подтверждения риска перед ставкой (не форсится on-chain; должен быть `≤ pm_listing_min_coverage_percent`).
- **Рынок:** `pm_min_liquidity`, `pm_market_creation_fee`, `pm_max_outcomes`, `pm_max_market_duration`. *(Агрегатного кэпа фи нет; creator/liquidity-фи без кэпа, самолимитируются; статический инвариант `сумма ≤ 100%`.)*
- **Batch / commit-reveal:** `pm_batch_epoch_blocks`, `pm_reveal_window_blocks`, `pm_min_batch_bet`, `pm_commit_no_reveal_penalty_percent`, `pm_commit_reveal_enabled`.
- **Споры:** `pm_dispute_fee`, `pm_dispute_grace_sec`, `pm_dispute_vote_period_sec`, `pm_dispute_auto_close_sec`, `pm_dispute_approve_min_percent`, `pm_no_contest_penalty_percent`, `pm_dispute_reward_multiplier` (bp-множитель, 10000 = 1×), `pm_dispute_votes_per_market` (по умолчанию 100 000), `pm_dispute_vote_min_vesting` (по умолчанию 1000.000 VIZ).
- **Time penalty:** `pm_default_time_penalty_percent`, `pm_max_time_penalty`.
- **Lazy-пул:** `pm_lazy_pool_enabled`, `pm_lazy_alloc_percent`, `pm_lazy_max_total_alloc_percent`, `pm_lazy_recall_step_percent`, `pm_lazy_lock_sec`, `pm_lazy_emergency_penalty_percent`, `pm_lazy_min_liquidity_fee_percent` (по умолчанию 200 = 2% — пул отказывается со-предоставлять ликвидность рынку, чей `liquidity_fee_percent` ниже этого порога вознаграждения).
- **Плечо (опц.):** `pm_leverage_enabled`, `pm_leverage_fund_percent`, `pm_leverage_max_per_position_bp`, `pm_leverage_max_position_ratio_percent`, `pm_leverage_min_market_liquidity`, `pm_leverage_safety_margin_percent`, `pm_leverage_max_slippage_percent`, `pm_leverage_m_factor_percent`, `pm_leverage_pool_profit_percent`, `pm_leverage_expiration_buffer_sec`, `pm_conversion_profit_cost_percent`.
- **Справедливость:** `pm_processing_cap_per_block`.

Три флага `*_enabled` (`pm_commit_reveal_enabled`, `pm_lazy_pool_enabled`, `pm_leverage_enabled`) — живые kill-switch: медиана валидаторов может отключить commit-reveal, lazy-пул или плечо без нового хардфорка.

---

## Цикл управления

```
Держатели SHARES → голосуют за валидаторов
Валидаторы → публикуют предпочтительные значения параметров
Блокчейн → берёт медиану активного набора
Медиана → применяется как живые правила сети
```

Изменение параметра требует, чтобы **большинство активных валидаторов** опубликовали новое значение. Процесс:
1. Сообщество обсуждает желаемое изменение (например, снижение комиссий).
2. Валидаторы обновляют публикуемые параметры.
3. Пользователи переносят голоса к валидаторам, публикующим желаемые значения.
4. Как только большинство активных валидаторов публикует новое значение, медиана смещается.
5. Новое значение вступает в силу автоматически — хардфорк или голосование за управление не требуются.

---

## Чтение текущих параметров

```json
{ "method": "database_api.get_chain_properties", "params": [] }
```

Возвращает текущие действующие медианные параметры. См. [Database API](../plugins/database-api.md#get_chain_properties).

---

См. также: [Валидаторы](../protocol/operations/validators.md), [Database API](../plugins/database-api.md), [Стейкинг и DAO](./staking-and-dao.md).
