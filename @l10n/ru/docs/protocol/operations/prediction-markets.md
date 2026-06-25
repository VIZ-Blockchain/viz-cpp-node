# Операции прогнозных рынков (Onix, HF14)

Прогнозный рынок Onix позволяет **оракулу** разрешать **рынок**, на исход которого ставят беттеры. Два движка ценообразования используют **единую модель расчёта** — строго **zero-sum паримутюэль**: ставки проигравших распределяются между победителями пропорционально кривому **весу** (`weight`), принципал всегда возвращается, токены **не эмитируются**.

- **Бинарные рынки** (`market_type = 0`) — ценообразование через **CPMM** (`x·y=k`); `weight = tokens_out`.
- **Мульти-рынки** (`market_type = 1`) — фиксированно-точечный **LMSR** (Q96); `weight = tokens`.

Все суммы — `asset` в VIZ (3 знака). Ссылки на объекты — `int64` id, которые эвалюатор резолвит в chainbase-объекты. Каждая операция гейтится `has_hardfork(HF14)` и валидируется против медианных `chain_properties_pm` (governance v5).

**Жизненный цикл:**

```mermaid
flowchart TD
  REG[pm_oracle_register] --> CRE[pm_create_market]
  CRE --> ACC[pm_oracle_accept_market]
  ACC --> ACT([рынок активен])
  ACT --> BET[pm_place_bet / pm_add_liquidity<br/>pm_commit_bet → pm_reveal_bet]
  BET --> EXP{{betting_expiration}}
  EXP --> RES[pm_resolve_market | pm_no_contest]
  RES -. спор .-> DC[pm_dispute_create]
  DC -->|режим комитета| DV[pm_dispute_vote]
  DC -->|режим аккаунта| DR[pm_dispute_resolve]
  RES --> GR{{окно ожидания}}
  DV --> GR
  DR --> GR
  GR ==> VOP[[ВИРТУАЛЬНАЯ pm_auto_payout<br/>паримутюэль-расчёт · возврат принципала LP]]
```

> **ID операций** ниже — позиция каждой операции в едином variant `operation` цепочки, продолжающая сквозную нумерацию после `stakeholder_reward_operation` (ID 65).

---

## Кастоди и инвариант zero-sum

Ставка / добавление ликвидности / эскроу коммита **списывают** баланс; средства удерживаются на PM-объектах (не трогают `current_supply` — **эмиссии нет**). При расчёте каждый милли-VIZ выплачивается обратно:

```
Σ winner_payout + oracle_take + creator_take + lp_bonus + LP_принципал
    == Σ всех ставок + LP_принципал + forfeit_pool
```

Протокольные комиссии (`pm_market_creation_fee`, `pm_oracle_registration_fee`) направляются в DAO-фонд (`dynamic_global_property_object.committee_fund`) — так же, как `committee_worker_create_request`.

---

## Обычные операции

### `pm_oracle_register_operation` (ID 66)
**Auth:** `active` аккаунта `owner`

Регистрирует оракула со страховым залогом. `pm_oracle_registration_fee` → DAO-фонд; `insurance` блокируется на балансе.

| Поле | Тип | Описание |
|------|-----|----------|
| `owner` | `account_name_type` | Аккаунт оракула |
| `insurance` | `asset` (VIZ) | Залоговый стейк, `≥ pm_min_oracle_insurance` |
| `fee_percent` | `uint16_t` | Стоячая fee за разрешение (bp, 10000 = 100%), `≤ pm_max_oracle_fee_percent`. Advisory прайс-лист — обязывающая fee котируется на рынок при акцепте |
| `fixed_fee` | `asset` (VIZ) | Стоячая фиксированная fee на рынок (`≥ 0`); advisory |
| `rules_url` | `string` | Профиль/правила, `≤ MAX_PM_PROFILE_URL_LEN` |
| `auto_accept` | `bool` | Если `true`, подходящие рынки идут **live при создании** без ручного `pm_oracle_accept` (по умолч. `false`) |
| `auto_accept_creator` | `account_name_type` | Ограничить авто-приём этим создателем; **пусто = любой создатель** |
| `auto_accept_resolver` | `account_name_type` | Требуемый спор-сетап для авто-приёма: **пусто = только комитет** (`dispute_mode == 0`); имя = только рынки `dispute_mode == 1`, у которых `dispute_resolver` равен этому аккаунту |

> **Зачем нужен пин резолвера.** Авто-приём срабатывает лишь когда стоячие условия рынка (`fee_percent`/`fixed_fee`) в пределах прайс-листа оракула и чейн-кэпа, оракул bonded и не забанен, а спор-сетап совпадает с `auto_accept_resolver`. Пин резолвера не даёт мейкеру подсунуть **подставного резолвера** под оракула, который иначе авто-принял бы: оставь пусто, чтобы форсить публичные споры комитета, или назови единственного резолвера, которому доверяешь. Несовпадающий рынок просто откатывается к обычному ручному accept (pending).

### `pm_oracle_update_operation` (ID 67)
**Auth:** `active` аккаунта `owner`

Пополнение/вывод страховки и смена политики. Все поля optional. Вывод ниже `pm_min_oracle_insurance` или при обслуживании активных рынков отклоняется.

| Поле | Тип | Описание |
|------|-----|----------|
| `insurance_delta` | `optional<asset>` | Со знаком: `>0` пополнить, `<0` вывести |
| `fee_percent` | `optional<uint16_t>` | Новая advisory fee (bp) |
| `fixed_fee` | `optional<asset>` | Новая фиксированная fee |
| `rules_url` | `optional<string>` | Новый url правил |
| `auto_accept` | `optional<bool>` | Вкл/выкл авто-приём |
| `auto_accept_creator` | `optional<account_name_type>` | Задать разрешённого создателя (пусто = любой) |
| `auto_accept_resolver` | `optional<account_name_type>` | Задать требуемого резолвера (пусто = только комитет) |

### `pm_create_market_operation` (ID 68)
**Auth:** `active` аккаунта `creator`

Создаёт рынок; создатель вносит первую ликвидность и становится первым LP. `pm_market_creation_fee` → DAO-фонд. Для мульти-рынков `lmsr_b` должен равняться `lmsr_b_from_liquidity(liquidity, N)` узла. При `dispute_mode == 1` `dispute_resolver` должен существовать и **не** равняться `oracle` или `creator` (анти-самосуд).

| Поле | Тип | Описание |
|------|-----|----------|
| `creator` / `oracle` | `account_name_type` | Создатель; зарегистрированный оракул (или создатель при self-oracle) |
| `market_type` | `uint8_t` | 0 binary (CPMM), 1 multi (LMSR) |
| `outcomes` | `vector<string>` | 2 (binary) или 3..`pm_max_outcomes` меток; каждая `≤ MAX_PM_OUTCOME_LABEL_LEN` |
| `url` | `string` | Критерии разрешения, `≤ MAX_PM_MARKET_TITLE_LEN` |
| `oracle_fee_percent` | `uint16_t` | **Потолок-оферта** для % оракула (bp): максимум, который создатель готов платить. Оракул котирует своё фактическое (`≤` этого) при акцепте. Self-oracle: финально, `≤ pm_max_oracle_fee_percent` |
| `oracle_fixed_fee` | `asset` (VIZ) | **Потолок-оферта** для фиксированной fee оракула; оракул котирует `≤` этого при акцепте |
| `creator_fee_percent` / `liquidity_fee_percent` | `uint16_t` | Собственные fee создателя (bp), финальны при создании; без governance-кэпа (само-ограничены) |
| `liquidity` | `asset` (VIZ) | Сид, `≥ pm_min_liquidity` |
| `lmsr_b` | `share_type` | Только мульти: вычисленный клиентом b (сверяется узлом) |
| `betting_expiration` / `result_expiration` | `time_point_sec` | `result > betting`; `≤ now + pm_max_market_duration` |
| `time_penalty_type/value`, `penalty_curve_type` | `uint8/uint32/uint8` | Конфиг штрафа за позднюю ставку (только с прибыли, шкала 1e6) |
| `allow_early_resolution/cancellation/batch/instant_bet` | `bool` | Флаги (мульти форсит `instant_bet=true`; `batch` требует `pm_commit_reveal_enabled`) |
| `endogeneity_tier` | `uint8_t` | 1 эконом-данные / 2 спорт / 3 политика |
| `dispute_mode` | `uint8_t` | 0 комитет / 1 аккаунт |
| `dispute_resolver` | `account_name_type` | Обязателен при `dispute_mode==1`; ≠ oracle/creator |

### `pm_oracle_accept_market_operation` (ID 69)
**Auth:** `active` аккаунта `oracle`

Оракул принимает (`status → active`) или отклоняет (ликвидность возвращается создателю; `status → deleted`) ожидающий рынок. При принятии оракул **котирует свои фактические условия** через `oracle_fee_percent` + `oracle_fixed_fee` — каждое должно быть `≤` оферты создателя на рынке, а `oracle_fee_percent ≤ pm_max_oracle_fee_percent`. Котировка **замораживается в рынок**, эмитится виртуальная `pm_market_accepted` (чтобы парсеры истории видели запуск + условия). Расчёт позже читает только эти замороженные поля — никогда живую медиану.

| Поле | Тип | Описание |
|------|-----|----------|
| `market_id` | `int64` | Ожидающий рынок |
| `accept` | `bool` | Принять (true) или отклонить (false) |
| `oracle_fee_percent` | `uint16_t` | Котируемый % оракула (bp); `≤` оферты & `≤ pm_max_oracle_fee_percent` |
| `oracle_fixed_fee` | `asset` (VIZ) | Котируемая фиксированная fee оракула; `≤` оферты |

### `pm_place_bet_operation` (ID 70)
**Auth:** `active` аккаунта `account`

Мгновенная ставка по живой кривой. `min_tokens` — порог проскальзывания. `weight` задаётся из полученных токенов CPMM/LMSR.

| Поле | Тип | Описание |
|------|-----|----------|
| `market_id` | `int64` | Целевой рынок |
| `side` | `int8_t` | Binary: 0/1; мульти: -1 |
| `outcome_index` | `int16_t` | Мульти: 0..N-1; binary: -1 |
| `amount` | `asset` (VIZ) | Стейк (`> 0`) |
| `min_tokens` | `share_type` | Порог проскальзывания (0 = нет) |
| `mode` | `uint8_t` | 0 instant, 1 batch |

### `pm_commit_bet_operation` (ID 71)
**Auth:** `active` аккаунта `account`

Commit-reveal фаза 1 (нужны `allow_batch` + `pm_commit_reveal_enabled`). Эскроу `≥ pm_min_batch_bet`. `commitment = H(market_id ‖ account ‖ side ‖ outcome_index ‖ amount ‖ min_tokens ‖ salt)`. `no_reveal_fee_percent` **должен равняться** `median(pm_commit_no_reveal_penalty_percent)` (проверяется консенсусом) и снапшотится в коммит.

### `pm_reveal_bet_operation` (ID 72)
**Auth:** `active` аккаунта `account`

Commit-reveal фаза 2: раскрывает ставку и ставит её в очередь следующей batch-эпохи. `amount ≤ escrow_amount` (излишек возвращается). Узел пересчитывает commitment из раскрытых полей + `salt` и отклоняет несовпадение. Закоммиченная, но нераскрытая ставка теряет `no_reveal_fee_percent` эскроу (→ `forfeit_pool`) через `pm_commit_forfeit`.

### `pm_cancel_bet_operation` (ID 73)
**Auth:** `active` аккаунта `account`

Отменяет открытую/очередную ставку (нужен `allow_cancellation`). `min_return` — порог проскальзывания возврата.

### `pm_add_liquidity_operation` (ID 74)
**Auth:** `active` аккаунта `provider`

Добавляет ликвидность в активный рынок. Принципал возвращается безусловно при расчёте плюс pro-rata доля LP-бонуса (liquidity fee + пул штрафов за время + пыль).

### `pm_withdraw_liquidity_operation` (ID 75)
**Auth:** `active` аккаунта `provider`

Выводит ликвидность (принципал-сейф). Заблокировано от `betting_expiration` до разрешения. `amount = 0` выводит всю позицию.

### `pm_resolve_market_operation` (ID 76)
**Auth:** `active` аккаунта `oracle`

Оракул разрешает в `winning_outcome`. Открывает окно ожидания спора (`result_expiration + pm_dispute_grace_sec`); по его истечении `pm_auto_payout` рассчитывает.

### `pm_no_contest_operation` (ID 77)
**Auth:** `active` аккаунта `oracle`

Оракул аннулирует рынок (все ставки возвращены, принципал LP возвращён). Оспоримо. Доля `pm_no_contest_penalty_percent` от dispute fee слешится из страховки и распределяется возвращённым беттерам.

### `pm_dispute_create_operation` (ID 78)
**Auth:** `active` аккаунта `disputer`

Подаёт спор в окне ожидания; эскроу `pm_dispute_fee`. Задаёт дедлайн ответа оракула и (режим комитета) таймеры голосования/авто-закрытия.

### `pm_dispute_vote_operation` (ID 79)
**Auth:** `regular` аккаунта `voter` (зеркалит `committee_vote_request`)

Голос в режиме комитета. `vote_outcome = -1` поддерживает оракула; иначе предлагает верный исход. `vote_percent ∈ [-10000, 10000]` — вес убеждённости, подсчитываемый (по `|vote_percent|`) при finalize.

Спор комитета — **открытые публичные слушания**: **commit-reveal нет** (намеренный, постоянный выбор: ДАО разрешает споры прозрачно, чтобы сохранить доверие к платформе). Так как по ходу открытого голосования всплывают новые доводы, **голос можно пересмотреть**: повторный `pm_dispute_vote` до `voting_end_time` **перезаписывает** прежний (последний бюллетень побеждает). Живой подсчёт виден через `get_dispute_votes`.

### `pm_dispute_resolve_operation` (ID 80)
**Auth:** `active` аккаунта `resolver`

Вердикт в режиме аккаунта от заданного рынком `dispute_resolver`. Может слешить `penalty_amount` страховки и банить оракула/создателя до заданных времён.

### `pm_transfer_position_operation` (ID 81)
**Auth:** `active` аккаунта `from`

Переназначает весь/часть `weight` ставки другому аккаунту (без влияния на рынок). `memo` — открытый текст или `#`-префиксный ECIES (стандартное memo VIZ).

### `pm_lazy_deposit_operation` (ID 82)
**Auth:** `active` аккаунта `account`

Вклад в lazy-пул ликвидности (в HF14 только аллокация, без плеча). Чеканит доли пула (учёт MasterChef, `reward_per_share` со шкалой 1e9).

### `pm_lazy_withdraw_operation` (ID 83)
**Auth:** `active` аккаунта `account`

Сжигает доли пула для вывода принципала + накопленных наград. `emergency = true` до `unlock_time` применяет `pm_lazy_emergency_penalty_percent` к прибыли (штраф остаётся в пуле, добавляется к `reward_per_share`).

---

## Виртуальные операции

Эмитируются логикой консенсуса PM — либо эвалуатором подписанной операции (`pm_market_accepted`, leverage-vops), либо покадровым обработчиком дедлайнов `process_pm_markets()`, когда рынок достигает **экспирации / дедлайна / окна спора / границы эпохи** (кап `pm_processing_cap_per_block`, старейший дедлайн первым). Видны в истории аккаунта, не подписываются.

| ID | Операция | Триггер |
|----|----------|---------|
| 84 | `pm_batch_settle_operation` | Граница эпохи: очередные ставки исполняются по снимку начала эпохи |
| 85 | `pm_commit_forfeit_operation` | `reveal_deadline` без раскрытия: штраф → `forfeit_pool`, остаток возвращён |
| 86 | `pm_auto_payout_operation` | Истекло окно спора: паримутюэль-расчёт + возврат принципала LP |
| 87 | `pm_dispute_finalize_operation` | Голосование завершено: подсчёт решает; штраф оракулу; пере-разрешение/поддержка |
| 88 | `pm_dispute_auto_close_operation` | Оракул не ответил: анти-фриз возврат, слэш страховки → DAO |
| 89 | `pm_oracle_missed_penalty_operation` | Оракул пропустил `result_expiration`: слэш → DAO, возврат всех ставок |
| 90 | `pm_lazy_recall_operation` | Поэтапный отзыв простаивающих аллокаций lazy-пула |
| 94 | `pm_leverage_liquidate_operation` | Эвалуатор — ликвидация плеча в ходе рынка (встречная `0` / отменяющая `1` ставка, каскад) |
| 95 | `pm_leverage_resolve_operation` | Расчёт — плечевая позиция принудительно закрыта: `outcome_index`, `won`, `pool_received`/`bettor_received`, `leverage` |
| 96 | `pm_market_accepted_operation` | Эвалуатор — рынок запущен: оракул принял, self-oracle или авто-приём; замороженные условия + флаг `self_oracle` |
| 97 | `pm_payout_operation` | Расчёт — на каждую активную ставку: `amount` (стейк), `side`/`outcome_index`, `payout` (**0 при проигрыше**) |

---

## Расчёт (паримутюэль, zero-sum)

```
losers_sum   = Σ сумм проигравших ставок
oracle_fee   = floor(losers_sum × oracle_fee_bp   / 10000)     // bp, 10000 = 100%
creator_fee  = floor(losers_sum × creator_fee_bp  / 10000)
liq_fee      = floor(losers_sum × liquidity_fee_bp / 10000)
oracle_fixed = min(oracle_fixed_fee, losers_sum − fees)        // из пула, не эмитируется
winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee − oracle_fixed + forfeit_pool

для каждой выигравшей ставки i (по кривому весу):
    profit_i  = floor(winners_pool × weight_i / Σ weight)
    penalty_i = floor(profit_i × time_penalty_i / 1_000_000)   // только с прибыли → LP
    payout_i  = amount_i + profit_i − penalty_i

LP: принципал возвращается БЕЗУСЛОВНО + pro-rata доля (liq_fee + Σpenalty + остаток округления)
```

**Краевые случаи:** все ставки на победителя → `winners_pool` = только `forfeit_pool` (возврат принципала); нет выигрышных токенов → весь пул в LP-бонус; недействительный исход → полный возврат + принципал LP. Чистый расчёт юнит-тестируется на точное сохранение в `tests/pm/parimutuel_test.cpp`.

---

## Дизайн-решение: только реальная глубина (без виртуальной/фантомной ликвидности)

*Виртуальный* (фантомный) оффсет ликвидности — резервы, добавленные в кривую ценообразования для уплощения влияния на цену, но **не обеспеченные реальным капиталом** и удаляемые при расчёте (опционально через медианное голосование), — сохраняет стоимость в закрытом цикле «ставка → отмена → резолюция» (техника vAMM) и соблазнителен как стабилизатор старта тонких рынков. **Onix сознательно этого не внедряет.** Авто-аллокация «ленивого» пула уже даёт то же сглаживание старта **реальным** капиталом, который зарабатывает комиссии, имеет подотчётного владельца и следует за спросом по каждому рынку.

Фантомная глубина отвергнута, потому что при неосторожном применении вредит структуре рынка и доверию:

1. **Подделываемая глубина** — тонкий или манипулируемый рынок можно нарядить под глубокий и ликвидный, разрушая ценовой сигнал, который несёт реальный дорогой капитал.
2. **Искажённое агрегирование информации** — уплощение кривой весов ослабляет награду за раннюю верную информацию и делает цену невосприимчивой к новостям (устаревший прогноз). Правильная глубина зависит от рынка и объёма; одна константа управления за ней не следует.
3. **Условная платёжеспособность** — платёжеспособна только пока её не выкупают и не используют как залог. Как только она обеспечивает отмену, ранний вывод или ссуду плеча — её приходится исключать везде, иначе она утекает реальными деньгами (например, плечо, рассчитанное/возвращаемое против фейковой глубины → реальный bad debt вкладчикам пула).
4. **Нет владельца, дохода и подотчётности** — не несёт риска и не зарабатывает комиссию никому реальному, удаляя розничный продукт безрисковой доходности на затронутых рынках.

Onix держит **только реальные числа**: каждая единица глубины — реальный капитал (погашаемый, зарабатывающий, подотчётный), предоставляемый через «ленивый» пул (`pm_lazy_deposit` + авто-аллокация). Осознанный компромисс — отказаться от дешёвого виртуального стабилизатора ради целостности ценового сигнала и платёжеспособности каждого реального денежного пути.
