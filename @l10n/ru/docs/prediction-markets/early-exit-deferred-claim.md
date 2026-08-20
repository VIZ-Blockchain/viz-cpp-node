# Deferred claim при раннем выходе (F1 / #300)

Статус: дизайн залочен (владелец 2026-08-08), реализация в работе на ветке `pm`.

## Проблема

Рынок — гибрид: **кривая** CPMM (binary) / LMSR (multi) для входа и раннего выхода,
и **pari-mutuel** сеттлмент для позиций, додержанных до резолва. Любой round-trip через
кривую (купить, потом продать до сеттлмента) реализует торговый P&L против глубины кривой —
против LP — ровно как impermanent loss в Uniswap. Но дизайн обещает LP **защиту принципала**
(только комиссии, без IL). Эти два требования в конфликте.

Два code-пути выходят против кривой на бинарном рынке:

- **Плечо** (`liquidate_position` / `pm_leverage_close`): всегда; force-закрывается на
  сеттлменте; усилено займом пула.
- **Обычный cancel ставки** (`cancel_bet`, F2 curve-priced refund): в окне ставок.

Оба роутят `residual = stake − curve_refund` в `forfeit_pool` (знаковый). Когда ранний выход
*прибылен* (`curve_refund > stake`), `forfeit_pool` уходит в **минус**. На сеттле
`winners_pool = losers_sum − fees + forfeit_pool`; если прибыль плеча/раннего выхода
перекрывает проигравшие стейки, `winners_pool < 0`, полом кладётся в 0, а недостача
(`uncovered`, F1) чарджится с принципала LP — либо минтится, когда принципал LP исчерпан.
Достижимо: доказано gate-respecting CPMM-симуляцией (односторонняя накачка, `uncovered = 7316`);
replay-корпус Бабина попал в это в 1163/1988 пар.

Корень: **curve-priced выход платит значение bonding-кривой, не ограниченное проигравшим
пулом**, а сеттл платит pari-mutuel. Разрыв падает на LP.

## Модель (залочена)

Ранние выходы больше не извлекают curve-стоимость из LP. Вместо этого выход записывает
**outcome-contingent deferred claim**, финансируемый на сеттле из **ограниченной доли
проигравшего пула**.

### Записывается при выходе
`{ position_id, kind (bet|leverage), chosen_outcome, claim_amount, exit_time }`.

### Обычный cancel ставки
- **Принципал возвращается сразу, безусловно**: `refund = min(curve_refund, stake)`
  (свои деньги, не заёмные). Cancel может срезать убыток или выйти в ноль, но никогда не
  реализует curve-прибыль в момент cancel'а.
- **Хвост прибыли** `max(curve_refund − stake, 0)` → deferred claim на выбранный исход.
- **Depth-normalized pricing (аудит #1-C):** сплит cap/tail переоценивается по глубине кривой
  *на входе* ставки, а не по текущей. `pm_bet_object.entry_liquidity` записывает
  `liquidity_sum` в момент, когда CPMM-ставка попала в кривую (instant `place_bet` и batch fill).
  На cancel оба резерва масштабируются на `entry_liquidity / liquidity_sum` (mirror-of-buy на
  отмасштабированных резервах). Поскольку ставки держат `k` инвариантным, а liquidity-опы
  масштабируют `k` на `f²` и `liquidity_sum` на `f`, `sqrt(k_entry / k_now) == L_entry / L_now`
  **точно** → детерминированно, без sqrt (нет целочисленного sqrt). Результат **клампится к
  реальному `curve_refund`**, так что нормализация может только *уменьшить* выплату, никогда не
  поднять. Это убивает вектор self-liquidity-инфляции (ставка → собственный `add_liquidity`
  раздувает глубину → больший `curve_refund` → cancel минтит больший хвост → вывод возвращает
  ликвидность целиком = гарантированная cash-neutral прибыль), не открывая shrink-сторону.
  Фолбэк на легаси-ценообразование, когда `entry_liquidity` отсутствует (ставки до поля).

### Leverage close / liquidate
- **Залог НЕ возвращается отдельно** — это first-loss margin для пула. Пул возвращает своё
  обязательство (`loan·(1+R) + funding`) из `cv` первым; если `cv < obligation`, залог закрывает
  разрыв.
- **Остаток** `max(cv − obligation, 0)` → deferred claim на выбранный исход.
- Плечо, таким образом, — **leveraged directional bet**, а не harvest волатильности: вы в плюсе
  только если ваш исход выиграл и в ведре есть место; неверный исход теряет залог.
- NB: теперь два разных предиката: **solvency** (`cv ≥ obligation`, управляет возвратом займа) vs
  **outcome-win** (управляет правом на claim). Позиция может быть solvent, но на проигравшем
  исходе → пул сделан целым, claim = 0.

### Сеттлмент
1. `bucket = pm_early_exit_reward_cap_percent × losers_sum / 10000` (default 33%).
2. Собрать deferred claims **только на выигравшем исходе** (claims на проигравшем → 0).
3. Заплатить их **FIFO по `exit_time`** (первым вышел — первым оплачен), пока ведро не опустеет;
   без per-position cap (владелец 2026-08-08: FIFO-порядок + общее ведро = граница). Claim,
   который остаток ведра не может покрыть полностью, оплачивается частично; остаток не платится
   (haircut).
4. **Неиспользованное ведро возвращается в winners' pool** — удержанные выигравшие ставки делят
   его pari-mutuel.

### Гарантии
```
paid_claims ≤ bucket = cap · losers_sum
winners_pool = losers_sum − fees − paid_claims + honest_forfeits
             ≥ (1 − cap) · losers_sum − fees  ≥ 0     (cap < 100%)
```
- `uncovered` **невозможен by construction**; без минта; **принципал LP не трогается**; lazy pool
  не несёт leverage IL.
- **Проигравший исход никогда не в плюсе** (требование владельца).
- Удержанные победители получают `≥ (1 − cap)` проигравшего пула плюс любое неиспользованное
  ведро.
- Ранний выход — это **ограниченная, contingent скидка** (≤ cap, FIFO) против додержания до
  резолва (полная pari-mutuel доля) → нет арбитража против додержания; осознанная скидка за
  ликвидность.

## Chain-параметр

`pm_early_exit_reward_cap_percent` (uint16, bp, default **3300** = 33% от `losers_sum`).
Median-voted параметр валидатора; `validate()` баундит `≤ 10000`. Добавлен в
`chain_properties_pm` + FC_REFLECT + `calc_median` (DONE, single-TU verified).

## Touchpoints реализации (нода)

- [x] chain-параметр `pm_early_exit_reward_cap_percent` (struct/validate/reflect/median).
- [ ] объект `pm_deferred_claim_object` (+ индекс по market, по exit_time) — space 30, дописать
      в конец enum `object_type` (snapshot-safe, как `pm_lazy_withdraw_request`).
- [ ] `cancel_bet`: вернуть `min(curve_refund, stake)`, записать profit-tail claim; перестать
      роутить отрицательный residual в `forfeit_pool`.
- [ ] `liquidate_position` / `pm_leverage_close`: пул забирает obligation, записать
      `cv − obligation` claim, пометить выбранный исход; убрать immediate `bettor_received`;
      прекратить отрицательный `forfeit_pool`.
- [ ] сеттлмент (`settle_market`): после force-close посчитать `bucket`, оплатить claims
      выигравшего исхода FIFO по exit_time, остаток → winners' pool; убрать путь charge
      `uncovered`/F1 в `settle_liquidity` (LP больше это не поглощает).
- [ ] снапшот: включить `pm_deferred_claim_object` в allowlist (+ import handler).
- [x] virtual op `pm_early_exit_claim_paid` (account, market, kind, outcome, `claimed`, `paid`) —
      дописана в конец варианта `operation` (op-id стабильны), FC_REFLECT'нута, эмитится в
      distribution-loop сеттлмента рядом с `adjust_balance`, и роутится в account_history
      раннего выходящего (visitor impacted-accounts в `account_history`). `claimed` vs `paid`
      показывает любой haircut от исчерпания ведра. `adjust_balance` один не оставляет следа в
      истории — это закрывает разрыв.
- [x] read API: `get_deferred_claims(market, [from=0], [limit=100])` — FIFO-порядок выхода через
      `by_claim_market`; пусто на settled рынке (claims consumed). Plugin-only (клиенты зовут через
      rawApi/JSON-RPC), как `get_lazy_withdraw_requests`; без wallet-wiring.

## Client / lib / docs follow-ups
- viz-js-lib / viz-php-lib / viz-python-lib: новый chain-параметр в v5 chain_properties_pm
  (serialization lock-step, byte-verify), любой новый read-метод / vop.
- Forecaster: notices + описания операций (плечо = directional, ранний выход = ограниченная
  скидка), показывать pending deferred claim на позициях.
- WebVIZWallet: те же обновления описаний операций, если показываются.
- Научная статья: `early-exit choice` с математикой (regular vs leverage-from-lazy-pool,
  validator-set reward cap).

## Отвергнутые альтернативы (почему)
- Размазать `uncovered` по всем LP / минтить (status quo) — ломает обещание принципала LP.
- Локализовать только в lazy pool — пул может быть исчерпан; всё равно аппроксимация; cancel'ы
  протекают.
- Капнуть выигрыш на выходе — на момент выхода не знаешь `losers_sum`; deferral это убирает.
- Full-AMM сеттлмент — отказывается от pari-mutuel тезиса VIZ.
Deferred outcome-contingent claim — единственный вариант, дающий **жёсткую** гарантию LP, оставаясь
pari-mutuel.
