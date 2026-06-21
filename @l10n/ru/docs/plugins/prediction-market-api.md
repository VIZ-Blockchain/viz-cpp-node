# Плагин `prediction_market_api`

Read-only JSON-RPC доступ к состоянию прогнозных рынков HF14 (рынки, ставки, оракулы, ликвидность, споры, lazy-пул, chain properties v5). Плагин возвращает сырые консенсус-объекты `pm_*` напрямую плюс несколько вычисляемых DTO.

**Включение:** добавить `prediction_market_api` в список плагинов узла (в `vizd` зарегистрирован по умолчанию). Зависит от `chain` + `json_rpc`. Все list-методы пагинируются через `from` (пропуск) и `limit` (`≤ 1000`).

## Методы

### Рынки

| Метод | Аргументы | Возврат |
|-------|-----------|---------|
| `get_market` | `market_id` | `pm_market_object` |
| `list_markets` | `status, from, limit, [show_risky]` | `pm_market_object[]` |
| `list_markets_by_oracle` | `oracle, from, limit` | `pm_market_object[]` |
| `list_markets_by_creator` | `creator, from, limit` | `pm_market_object[]` |
| `get_market_outcomes` | `market_id` | `pm_outcome_object[]` |
| `get_market_weight_sums` | `market_id` | `pm_market_weight_sums` (вычисляемый) |
| `get_market_bets` | `market_id, from, limit` | `pm_bet_object[]` |
| `get_market_liquidity` | `market_id, from, limit` | `pm_liquidity_object[]` |

`status` для `list_markets`: `-1` удалён, `0` ожидание, `1` активен, `2` закрыт, `3` разрешён. По
умолчанию `list_markets` скрывает недострахованные рынки (страховка оракула < **2.5×** объёма ставок);
`show_risky = true` показывает их (рынки только скрываются, ставки on-chain всегда разрешены).

### Метаданные рынка (парсятся off-chain)

Каждый рынок несёт свободную, консенсус-непрозрачную JSON-строку `metadata`. Плагин парсит индексируемые
ключи (категория / подкатегория / теги / запрещённые юрисдикции) в `pm_market_meta_object` — **только для
отображения/индексации, не консенсус**.

| Метод | Аргументы | Возврат |
|-------|-----------|---------|
| `get_market_meta` | `market_id` | `pm_market_meta_object` (или ошибка, если нет) |
| `list_markets_by_category` | `category, from, limit, [jurisdiction]` | `pm_market_meta_object[]` |

`list_markets_by_category` исключает рынки, чьи `banned_jurisdictions` содержат необязательный ISO-код
`jurisdiction` (регулируемый клиент передаёт свою юрисдикцию, чтобы получить только допустимые рынки).
Объект: `market`, `category`, `subcategory`, `tags` (через запятую), `banned_jurisdictions` (ISO через
запятую; пусто = разрешено везде), `expiry` (пруна после закрытия окна спора + TTL).

### Позиции и оракулы

| Метод | Аргументы | Возврат |
|-------|-----------|---------|
| `get_account_positions` | `account, from, limit` | `pm_position[]` (ставка + `expected_payout`) |
| `get_account_leverage_positions` | `account, from, limit` | `pm_leverage_position_object[]` |
| `get_market_leverage_positions` | `market_id, from, limit` | `pm_leverage_position_object[]` |
| `get_creator_ban` | `account` | `pm_creator_ban_object` (или ошибка, если нет) |
| `get_oracle` | `owner` | `pm_oracle` (объект + `reliability_score`) |
| `list_oracles` | `from, limit` | `pm_oracle_object[]` |

> Выплата каждому беттору — виртуальная операция `pm_payout` (стейк, side/outcome, итог; `0` при
> проигрыше); закрытие плечевой позиции — `pm_leverage_resolve` (`outcome_index`, `won`, `leverage`).
> Обе видны в `account_history`; сами объекты позиций — через методы выше.

### Споры, lazy-пул, governance

| Метод | Аргументы | Возврат |
|-------|-----------|---------|
| `get_dispute` | `market_id` | `pm_dispute_object` |
| `get_dispute_votes` | `market_id` | `pm_dispute_votes` (голоса + живой подсчёт) |
| `get_lazy_pool` | — | `pm_lazy_pool_object` |
| `get_lazy_deposit` | `account` | `pm_lazy_deposit_object` |
| `get_pm_chain_properties` | — | `chain_properties_pm` (медиана, v5) |

### Графики — kline / история весов

Тайм-серия для построения графика изменения веса каждого исхода. Плагин добавляет точку **каждый раз, когда веса исходов рынка меняются** — ставка, отмена, ликвидация, batch-settle, открытие плеча или расчёт плеча — как таймстампированный снимок паримутюэль-веса (сумма ставок) по каждому исходу. Это **неконсенсусное** состояние плагина (хранится в chainbase, undo/redo-безопасно, не входит в хеш состояния); история копится с момента первого включения плагина на узле.

**Хранение:** kline-история пруна́ется **вместе с метаданными рынка**, по тому же расписанию — `result_expiration` + grace спора + `pmm-ttl-days` (по умолч. 7). Полный график рынка доступен на протяжении его жизни и в окне хранения после расчёта, затем оба индекса очищаются (для очень длинной истории — частями за несколько блоков), чтобы хранилище узла оставалось ограниченным.

| Метод | Аргументы | Возврат |
|-------|-----------|---------|
| `get_market_kline` | `market_id, [from], [limit]` | `pm_kline[]` (по возрастанию `seq`) |

Пагинация — **отступ от новейших** (намеренно простая для тонких клиентов): `from` — сколько **новейших** точек пропустить, `limit ≤ 1000` — размер страницы.
- `(market_id, 0, 1000)` → последние ≤ 1000 изменений.
- `(market_id, 1000, 1000)` → предыдущие 1000 (страницей раньше) — повторяй с `from += 1000`, чтобы дозагружать более старую историю.

График: x = `timestamp` (unix-секунды), по одной линии на исход `i` с y = `weights[i]` (или нормированно `weights[i] / Σweights` — вменённая вероятность).

## Вычисляемые DTO

- **`pm_position`** — ставка + `expected_payout` (выплата, если сторона победит, или реализованная после расчёта; байт-в-байт повторяет `settle_market`), `market_status`, `resolved_outcome`.
- **`pm_oracle`** — объект оракула + `reliability_score` (bp `[0..10000]`, неконсенсусная эвристика: смесь доли успешных разрешений и доли выигранных споров минус штраф за баны).
- **`pm_market_weight_sums`** — `bets_sum`/`weight_sum` по сторонам/исходам (веса считаются сканом ставок, т.к. не хранятся).
- **`pm_kline`** — одна точка графика: `seq` (uint32, 0-based, монотонный индекс изменения по рынку), `timestamp` (unix-секунды, x), `reason` (uint8: 0 ставка, 1 отмена, 2 ликвидация, 3 batch settle, 4 открытие плеча, 5 расчёт плеча), `bets_sum` (всего поставлено), `weights[]` (вес по каждому исходу, y; индекс = outcome_index).
- **`pm_dispute_votes`** — голоса + подсчёт finalize. Старые поля (вес = `|vote_percent|`, не стейк): `uphold_weight`/`challenge_weight`/`total_weight`, `challenger_leads` (≥ `pm_dispute_approve_min_percent`), `proposed_outcome`. **Точная stake-взвешенная проекция (зеркалит `pm_dispute_finalize`; все `*_shares` в vesting-shares = `effective_vesting_shares` + стейк lazy-пула→shares):** `participation_shares` (Σ веса проголосовавших), `electorate_shares` (`total_vesting_shares` + NAV пула→shares), `quorum_required_shares`, `quorum_percent_bp` (кворум в bp, 10000 = 100.00%), `quorum_reached` (bool), `oracle_defense_shares`/`change_shares`, `outcome_change_shares[]` (по исходам), `expected_uphold` (останется ли решение оракула), `expected_outcome` (какой исход будет выставлен при резолюции сейчас), `expected_consensus_strength_bp`. Проекция совпадает с тем, что крон применит на `voting_end_time` при текущих голосах (голоса изменяемы до этого момента).

## Пример

Последние 1000 точек графика для рынка `42`, затем предыдущие 1000:
```bash
# новейшая страница
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_market_kline",[42,0,1000]]}' http://127.0.0.1:8090
# страницей старше
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_market_kline",[42,1000,1000]]}' http://127.0.0.1:8090
```

Тонкий клиент (дозагрузка старой истории при прокрутке назад) — каждая точка в серию `{ x: unixtime, y: weight }` по исходам:
```js
async function call(method, params) {
  const r = await fetch('http://127.0.0.1:8090', { method: 'POST',
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'call',
      params: ['prediction_market_api', method, params] }) });
  return (await r.json()).result;
}

// Страницы по 1000 от новейших назад, пока не наберём `want` точек (или не кончится история).
async function loadKline(marketId, want = 3000) {
  const points = [];
  for (let from = 0; points.length < want; from += 1000) {
    const page = await call('get_market_kline', [marketId, from, 1000]);
    if (!page.length) break;            // дошли до начала истории
    points.unshift(...page);            // страницы по возрастанию; старые — в начало
    if (page.length < 1000) break;
  }
  return points;
}

// По одной серии {x,y} на исход — напрямую в любую библиотеку графиков.
function toSeries(points, outcomeCount) {
  const series = Array.from({ length: outcomeCount }, () => []);
  for (const p of points)
    for (let i = 0; i < outcomeCount; i++)
      series[i].push({ x: p.timestamp, y: Number(p.weights[i]) });
  return series;
}
```

См. [Операции прогнозных рынков](../protocol/operations/prediction-markets.md) и [Chain Properties](../governance/chain-properties.md#pm-parameters).
