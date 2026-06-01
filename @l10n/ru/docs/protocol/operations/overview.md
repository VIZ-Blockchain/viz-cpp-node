# Обзор операций

Операции VIZ Ledger — это атомарные действия изменения состояния, включаемые в транзакции. Каждая операция сериализуется как 2-элементный массив `[type_id, object]` внутри подписанной транзакции.

---

## Обычные операции

Это инициируемые пользователями операции, которые могут быть транслированы в сеть.

| ID | Операция | Уровень авторизации | Справочник |
|----|---------|---------------------|----------|
| 0 | `vote_operation` *(устарела)* | regular | [Контент](./content.md) |
| 1 | `content_operation` *(устарела)* | regular | [Контент](./content.md) |
| 2 | `transfer_operation` | active (VIZ) / master (SHARES) | [Переводы](./transfers.md) |
| 3 | `transfer_to_vesting_operation` | active | [Переводы](./transfers.md) |
| 4 | `withdraw_vesting_operation` | active | [Переводы](./transfers.md) |
| 5 | `account_update_operation` | master / active | [Аккаунты](./accounts.md) |
| 6 | `validator_update_operation` | active | [Валидаторы](./validators.md) |
| 7 | `account_validator_vote_operation` | active | [Валидаторы](./validators.md) |
| 8 | `account_validator_proxy_operation` | active | [Валидаторы](./validators.md) |
| 9 | `delete_content_operation` *(устарела)* | regular | [Контент](./content.md) |
| 10 | `custom_operation` | active / regular | [Контент](./content.md) |
| 11 | `set_withdraw_vesting_route_operation` | active | [Переводы](./transfers.md) |
| 12 | `request_account_recovery_operation` | active | [Восстановление](./recovery.md) |
| 13 | `recover_account_operation` | master (×2) | [Восстановление](./recovery.md) |
| 14 | `change_recovery_account_operation` | master | [Восстановление](./recovery.md) |
| 15 | `escrow_transfer_operation` | active | [Эскроу](./escrow.md) |
| 16 | `escrow_dispute_operation` | active | [Эскроу](./escrow.md) |
| 17 | `escrow_release_operation` | active | [Эскроу](./escrow.md) |
| 18 | `escrow_approve_operation` | active | [Эскроу](./escrow.md) |
| 19 | `delegate_vesting_shares_operation` | active | [Переводы](./transfers.md) |
| 20 | `account_create_operation` | active | [Аккаунты](./accounts.md) |
| 21 | `account_metadata_operation` | regular | [Аккаунты](./accounts.md) |
| 22 | `proposal_create_operation` | active | [Предложения](./proposals.md) |
| 23 | `proposal_update_operation` | varies | [Предложения](./proposals.md) |
| 24 | `proposal_delete_operation` | active | [Предложения](./proposals.md) |
| 25 | `chain_properties_update_operation` | active | [Валидаторы](./validators.md) |
| 35 | `committee_worker_create_request_operation` | regular | [Комитет](./committee.md) |
| 36 | `committee_worker_cancel_request_operation` | regular | [Комитет](./committee.md) |
| 37 | `committee_vote_request_operation` | regular | [Комитет](./committee.md) |
| 43 | `create_invite_operation` | active | [Инвайты](./invites.md) |
| 44 | `claim_invite_balance_operation` | active | [Инвайты](./invites.md) |
| 45 | `invite_registration_operation` | active | [Инвайты](./invites.md) |
| 46 | `versioned_chain_properties_update_operation` | active | [Валидаторы](./validators.md) |
| 47 | `award_operation` | regular | [Награды](./awards.md) |
| 50 | `set_paid_subscription_operation` | active | [Подписки](./subscriptions.md) |
| 51 | `paid_subscribe_operation` | active | [Подписки](./subscriptions.md) |
| 54 | `set_account_price_operation` | master | [Рынок аккаунтов](./account-market.md) |
| 55 | `set_subaccount_price_operation` | master | [Рынок аккаунтов](./account-market.md) |
| 56 | `buy_account_operation` | active | [Рынок аккаунтов](./account-market.md) |
| 58 | `use_invite_balance_operation` | active | [Инвайты](./invites.md) |
| 60 | `fixed_award_operation` | regular | [Награды](./awards.md) |
| 61 | `target_account_sale_operation` | master | [Рынок аккаунтов](./account-market.md) |

---

## Виртуальные операции

Виртуальные операции генерируются самим блокчейном во время обработки блоков. Они никогда не транслируются пользователями — они появляются в истории аккаунта и данных блоков исключительно в информационных целях.

| ID | Операция | Триггер | Справочник |
|----|---------|--------|----------|
| 26 | `author_reward_operation` | Выплата за контент | [Виртуальные операции](../virtual-operations.md) |
| 27 | `curation_reward_operation` | Выплата за контент | [Виртуальные операции](../virtual-operations.md) |
| 28 | `content_reward_operation` | Выплата за контент | [Виртуальные операции](../virtual-operations.md) |
| 29 | `fill_vesting_withdraw_operation` | Срабатывание интервала вывода | [Виртуальные операции](../virtual-operations.md) |
| 30 | `shutdown_validator_operation` | Деактивация валидатора | [Виртуальные операции](../virtual-operations.md) |
| 31 | `hardfork_operation` | Активация хардфорка | [Виртуальные операции](../virtual-operations.md) |
| 32 | `content_payout_update_operation` | Обновление выплаты за контент | [Виртуальные операции](../virtual-operations.md) |
| 33 | `content_benefactor_reward_operation` | Выплата за контент | [Виртуальные операции](../virtual-operations.md) |
| 34 | `return_vesting_delegation_operation` | Завершение периода возврата делегирования | [Виртуальные операции](../virtual-operations.md) |
| 38 | `committee_cancel_request_operation` | Истечение заявки комитета | [Виртуальные операции](../virtual-operations.md) |
| 39 | `committee_approve_request_operation` | Одобрение заявки комитета | [Виртуальные операции](../virtual-operations.md) |
| 40 | `committee_payout_request_operation` | Обработка выплаты комитета | [Виртуальные операции](../virtual-operations.md) |
| 41 | `committee_pay_request_operation` | Оплата работнику комитета | [Виртуальные операции](../virtual-operations.md) |
| 42 | `validator_reward_operation` | Произведён блок | [Виртуальные операции](../virtual-operations.md) |
| 48 | `receive_award_operation` | Получена награда | [Виртуальные операции](../virtual-operations.md) |
| 49 | `benefactor_award_operation` | Награда с бенефициаром | [Виртуальные операции](../virtual-operations.md) |
| 52 | `paid_subscription_action_operation` | Оплата подписки | [Виртуальные операции](../virtual-operations.md) |
| 53 | `cancel_paid_subscription_operation` | Отмена/истечение подписки | [Виртуальные операции](../virtual-operations.md) |
| 57 | `account_sale_operation` | Продан аккаунт | [Виртуальные операции](../virtual-operations.md) |
| 59 | `expire_escrow_ratification_operation` | Истёк дедлайн эскроу | [Виртуальные операции](../virtual-operations.md) |
| 62 | `bid_operation` | Сделана ставка на аукционе | [Виртуальные операции](../virtual-operations.md) |
| 63 | `outbid_operation` | Перебитая ставка на аукционе | [Виртуальные операции](../virtual-operations.md) |

---

## Построение транзакции

```json
{
  "ref_block_num": 12345,
  "ref_block_prefix": 678901234,
  "expiration": "2024-01-15T12:01:00",
  "operations": [
    [2, { "from": "alice", "to": "bob", "amount": "1.000 VIZ", "memo": "" }]
  ],
  "extensions": [],
  "signatures": ["1f2a3b..."]
}
```

- `ref_block_num` = `head_block_number & 0xFFFF`
- `ref_block_prefix` = байты 4–7 `block_id` в виде little-endian `uint32`
- `expiration` = текущее UTC-время + TTL (рекомендуется не более 60 секунд)
- Подпись: `sha256(chain_id || serialized_tx)` → компактная ECDSA-подпись secp256k1

---

См. также: [Типы данных](../data-types.md), [Виртуальные операции](../virtual-operations.md), [JSON-RPC API](../../api/json-rpc.md).
