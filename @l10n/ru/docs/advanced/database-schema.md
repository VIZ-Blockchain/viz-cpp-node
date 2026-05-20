# Схема базы данных

VIZ Ledger использует ChainBase — персистентное хранилище с отображением в память и несколькими индексами, построенное на Boost.Interprocess. Всё состояние цепочки находится в `shared_memory.bin`. Каждый тип объекта ассоциирован с контейнером Boost.MultiIndex, определяющим его первичные и вторичные индексы.

---

## Реестр типов объектов

Каждый персистентный объект имеет уникальный числовой type ID, объявленный в `chain_object_types.hpp`. Полный набор отслеживаемых типов объектов:

| Объект | Примечания |
|--------|-----------|
| `dynamic_global_property` | Синглтон: текущее состояние цепочки, head-блок, LIB, инфляция |
| `account` | Все зарегистрированные аккаунты |
| `account_authority` | Наборы authority master/active/regular |
| `witness` (валидатор) | Регистрации валидаторов, ключи подписи, счётчики голосов |
| `transaction` | Ожидающие/недавние транзакции (окно TAPOS) |
| `block_summary` | 65536-слотовый TAPOS-буфер ID блоков |
| `witness_schedule` | Синглтон: расписание активных валидаторов |
| `content` | Посты и комментарии (устарело) |
| `content_type` | Метаданные заголовка/тела контента |
| `content_vote` | Голоса за контент |
| `witness_vote` | Голоса за валидаторов от аккаунтов |
| `hardfork_property` | Синглтон: отслеживание текущего/следующего хардфорка |
| `withdraw_vesting_route` | Правила маршрутизации вывода |
| `master_authority_history` | История изменений master-ключей |
| `account_recovery_request` | Ожидающие запросы восстановления аккаунта |
| `change_recovery_account_request` | Ожидающие изменения аккаунта восстановления |
| `escrow` | Эскроу-переводы |
| `vesting_delegation` | Активные делегирования SHARES |
| `fix_vesting_delegation` | Записи исправления делегирований |
| `vesting_delegation_expiration` | Делегирования в окне возврата |
| `account_metadata` | JSON-метаданные аккаунта |
| `proposal` | Управленческие предложения |
| `required_approval` | Требования одобрения предложений |
| `committee_request` | Запросы на финансирование комитета |
| `committee_vote` | Голоса комитета |
| `invite` | Инвайты аккаунтов |
| `award_shares_expire` | Истекающие наградные SHARES |
| `paid_subscription` | Предложения подписок |
| `paid_subscribe` | Активные подписки |
| `witness_penalty_expire` | Истечения штрафов за пропуски валидатора |
| `block_post_validation` | Записи поствалидации блоков |

---

## Объект аккаунта

Аккаунты хранят балансы, состояние вестинга, метрики делегирования, пропускную способность, флаги аукциона/продажи и участие в управлении.

**Ключевые поля:** `name`, `balance` (VIZ), `vesting_shares`, `delegated_vesting_shares`, `received_vesting_shares`, `energy`, `next_vesting_withdrawal`, `witnesses_voted_for`, `recovery_account`.

**Индексы:**

| Тег | Ключ | Тип |
|-----|------|-----|
| `by_id` | `id` | уникальный |
| `by_name` | `name` | уникальный |
| `by_account_on_sale` | флаг продажи | неуникальный |
| `by_account_on_auction` | флаг аукциона | неуникальный |
| `by_account_on_sale_start_time` | время начала продажи | неуникальный |
| `by_subaccount_on_sale` | флаг продажи субаккаунта | неуникальный |
| `by_next_vesting_withdrawal` | `(next_vesting_withdrawal, id)` | составной |

Составной индекс `by_next_vesting_withdrawal` обеспечивает пакетную обработку предстоящих выплат вывода за O(log N).

---

## Объект контента

Объекты контента представляют посты и комментарии с метаданными голосования, выплат и вложенности. **Эти объекты устарели** — новые приложения должны использовать `custom_operation`.

**Индексы на `content`:**

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_cashout_time` | `(cashout_time, id)` |
| `by_permlink` | `(author, permlink)` |
| `by_root` | `(root_content, id)` |
| `by_parent` | `(parent_author, parent_permlink, id)` |
| `by_last_update` | `(parent_author, last_update, id)` — нагружает API |
| `by_author_last_update` | `(author, last_update, id)` — нагружает API |

**Индексы на `content_vote`:**

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_content_voter` | `(content, voter)` — уникальный |
| `by_voter_content` | `(voter, content)` — уникальный |
| `by_voter_last_update` | `(voter, last_update, content)` |
| `by_content_weight_voter` | `(content, weight, voter)` — для лидербордов |

---

## Объекты валидатора

**Индексы `validator_object`:**

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_name` | `owner` — уникальный |
| `by_vote_name` | `(votes, owner)` |
| `by_counted_vote_name` | `(counted_votes, owner)` |
| `by_schedule_time` | `(virtual_scheduled_time, id)` — O(log N) планирование слотов |

**Индексы `witness_vote_object`:**

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_account_witness` | `(account, validator)` — уникальный |
| `by_witness_account` | `(validator, account)` — уникальный |

Индекс `by_schedule_time` используется планировщиком производства блоков для выбора следующего валидатора за O(log N).

---

## Объекты предложений и требуемых одобрений

**Индексы `proposal_object`:**

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_account` | `(author, title)` — уникальный |
| `by_expiration` | `expiration` — неуникальный |

**Индексы `required_approval_object`:**

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_account` | `(account, proposal)` |

---

## Объект инвайта

| Тег | Ключ |
|-----|------|
| `by_id` | `id` |
| `by_invite_key` | публичный ключ — неуникальный |
| `by_status` | статус — неуникальный |
| `by_creator` | создатель — неуникальный |
| `by_receiver` | получатель — неуникальный |

---

## Вспомогательные объекты

**`withdraw_vesting_route`:**

| Тег | Ключ |
|-----|------|
| `by_withdraw_route` | `(from_account, to_account)` — уникальный |
| `by_destination` | `(to_account, id)` |

**`escrow`:**

| Тег | Ключ |
|-----|------|
| `by_from_id` | `(from, escrow_id)` — уникальный |
| `by_to` | `(to, id)` |
| `by_agent` | `(agent, id)` |
| `by_ratification_deadline` | `(is_approved, ratification_deadline, id)` |

**`vesting_delegation`:**

| Тег | Ключ |
|-----|------|
| `by_delegation` | `(delegator, delegatee)` — уникальный |

**`vesting_delegation_expiration`:**

| Тег | Ключ |
|-----|------|
| `by_expiration` | `expiration` — неуникальный |
| `by_account_expiration` | `(delegator, expiration)` |

---

## Fork-база данных

Fork-база данных (`fork_database`) поддерживает дерево блоков в памяти для управления форками цепочки. Работает отдельно от персистентного хранилища chainbase.

**Связанный индекс** — блоки канонической цепочки, индексированные по ID и номеру блока.  
**Несвязанный индекс** — осиротевшие или неупорядоченные блоки, родитель которых ещё не известен.

```
Добавление блока
  ├── Родитель известен в связанном индексе?
  │     ДА  → связать блок, вставить в связанный индекс, обновить head
  │     НЕТ → вставить в несвязанный индекс
  └── Попытаться связать ожидающие несвязанные блоки
```

Когда поступает новый блок, ID которого совпадает с родителем несвязанного блока, `_push_next()` каскадно обходит несвязанный индекс и продвигает эти блоки в связанную цепочку.

**Операции с ветками:**
- `fetch_branch_from(first, second)` — обходит обе ветки для поиска общего предка. Возвращает `(first_branch, second_branch)` для переключения форков.
- `set_max_size(n)` — усекает блоки старше n, ограничивает потребление памяти.
- `walk_main_branch_to_num(n)` — итерирует главную цепочку до определённого номера блока.

**Валидность блока:** Блоки, помеченные как невалидные, никогда не продвигаются. Добавление блока за пределами максимального окна переупорядочивания вызывает assert.

---

## Управление индексами

Основные индексы регистрируются в `database::initialize_indexes()`. Плагины регистрируют дополнительные индексы через `add_plugin_index<T>()` в `plugin_startup()`.

```cpp
// Регистрация основных индексов (database.cpp)
add_core_index<account_index>();
add_core_index<witness_index>();
// ...

// Регистрация индексов плагинов (запуск плагина)
db.add_plugin_index<my_custom_index>();
```

---

## Связи объектов

```
account ──(author)──► content ──► content_vote ◄──(voter)── account
account ──(delegator)──► vesting_delegation ──► account (delegatee)
account ──(account)──► witness_vote ──► witness (validator)
account ──(author)──► proposal ──► required_approval ◄──(account)── account
account ──(creator/receiver)──► invite
escrow: from + to + agent → escrow_object
```

---

## Руководство по оптимизации запросов

**Быстрые поиски:**
- Аккаунт по имени → `by_name` (уникальный, O(log N))
- Расписание валидаторов → `by_schedule_time` (упорядочен по виртуальному времени)
- Контент по author+permlink → `by_permlink` (уникальный составной)
- Голоса по content+weight → `by_content_weight_voter` (лидерборды)

**Пакетная обработка:**
- Выводы вестинга → итерировать `by_next_vesting_withdrawal` вперёд
- Истекающие делегирования → итерировать `by_expiration` вперёд
- Истекающие предложения → итерировать `by_expiration` вперёд

**Избегайте полного сканирования:** всегда используйте тег с индексом. Составные индексы упорядочены прежде всего по крайнему левому ключу — ставьте наиболее селективное или часто фильтруемое поле первым.

---

## Расширение схемы для плагинов

Для добавления пользовательского типа объекта:

1. Определить класс объекта, наследующий от `chainbase::object<type_id, MyObject>`.
2. Объявить `chainbase::shared_multi_index_container` с нужными индексами.
3. Зарегистрировать через `db.add_plugin_index<MyIndex>()` в `plugin_startup()`.
4. Добавить макросы `FC_REFLECT` для сериализации.

```cpp
class my_object : public chainbase::object<my_object_type, my_object> {
    id_type          id;
    account_name_type account;
    uint64_t          value;
};

using my_index = chainbase::shared_multi_index_container<
    my_object,
    indexed_by<
        ordered_unique<tag<by_id>,
            member<my_object, my_object::id_type, &my_object::id>>,
        ordered_unique<tag<by_account>,
            member<my_object, account_name_type, &my_object::account>>
    >
>;
```

---

## Эволюция схемы

Новый хардфорк → новые поля или объекты. Руководящие принципы:

- Сохранять семантику первичных ключей стабильной между хардфорками.
- Добавлять новые поля как опциональные или с умолчаниями; никогда не менять существующий порядок полей.
- Ограждать использование новых индексов проверками `has_hardfork()` при реплее.
- Добавлять новые теги MultiIndex рядом с существующими — никогда не удалять тег, который могут запрашивать реплеирующие узлы.

---

См. также: [Разработка плагинов](../development/plugin-development.md), [Виртуальные операции](../protocol/virtual-operations.md), [Управление хардфорками](./hardfork-management.md).
