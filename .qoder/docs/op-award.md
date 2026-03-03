# VIZ Blockchain — Award Operations

Spec for implementing award operations in PHP/Node.js libraries.

Awards are the primary social reward mechanism in VIZ. An account spends "energy" to award SHARES directly to another account (and optionally to beneficiaries).

---

## `award_operation`

**Type ID:** `47`
**Required authority:** `regular` of `initiator`

Awards SHARES to `receiver` from the reward pool, proportional to the initiator's energy expenditure and SHARES stake.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `initiator` | `account_name_type` | yes | Account giving the award |
| `receiver` | `account_name_type` | yes | Account receiving the award |
| `energy` | `uint16_t` | yes | Energy to spend (basis points: 1–10000) |
| `custom_sequence` | `uint64_t` | yes | Application-defined sequence number |
| `memo` | `string` | yes | Optional message/reason |
| `beneficiaries` | `vector<beneficiary_route_type>` | yes | Optional beneficiaries receiving a share |

### JSON Example

```json
[47, {
  "initiator": "alice",
  "receiver": "bob",
  "energy": 1000,
  "custom_sequence": 0,
  "memo": "great article!",
  "beneficiaries": []
}]
```

### PHP Example

```php
$op = [
    'type' => 'award_operation',
    'value' => [
        'initiator'       => 'alice',
        'receiver'        => 'bob',
        'energy'          => 1000,
        'custom_sequence' => 0,
        'memo'            => 'great article!',
        'beneficiaries'   => [],
    ],
];
```

### Node.js Example

```js
const op = ['award', {
    initiator: 'alice',
    receiver: 'bob',
    energy: 1000,
    custom_sequence: 0,
    memo: 'great article!',
    beneficiaries: [],
}];
```

### Checklist
- [ ] `energy` range: 1–10000 (1 = 0.01%, 10000 = 100%)
- [ ] Energy regenerates at 100%/day (CHAIN_ENERGY_REGENERATION_SECONDS)
- [ ] Beneficiary weights sum must be <= 10000 (100%)
- [ ] Beneficiaries list must be sorted by account name ascending
- [ ] If beneficiaries are present, `receiver` gets `(1 - sum_beneficiary_weights/10000)` share
- [ ] `custom_sequence` is app-defined, can be 0
- [ ] Sign with `initiator`'s regular key
- [ ] Virtual `receive_award_operation` fires for `receiver`
- [ ] Virtual `benefactor_award_operation` fires for each beneficiary

---

## `fixed_award_operation`

**Type ID:** `60`
**Required authority:** `regular` of `initiator`

Awards a **fixed amount** of SHARES to `receiver`, spending energy proportionally based on the desired amount. Added in HF11.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `initiator` | `account_name_type` | yes | Account giving the award |
| `receiver` | `account_name_type` | yes | Account receiving the award |
| `reward_amount` | `asset` (SHARES) | yes | Fixed amount of SHARES to award |
| `max_energy` | `uint16_t` | yes | Maximum energy to spend (0 = no limit) |
| `custom_sequence` | `uint64_t` | yes | Application-defined sequence number |
| `memo` | `string` | yes | Optional message/reason |
| `beneficiaries` | `vector<beneficiary_route_type>` | yes | Optional beneficiaries |

### JSON Example

```json
[60, {
  "initiator": "alice",
  "receiver": "bob",
  "reward_amount": "10.000000 SHARES",
  "max_energy": 5000,
  "custom_sequence": 1,
  "memo": "fixed reward",
  "beneficiaries": [
    {"account": "charlie", "weight": 1000}
  ]
}]
```

### PHP Example

```php
$op = [
    'type' => 'fixed_award_operation',
    'value' => [
        'initiator'       => 'alice',
        'receiver'        => 'bob',
        'reward_amount'   => '10.000000 SHARES',
        'max_energy'      => 5000,
        'custom_sequence' => 1,
        'memo'            => 'fixed reward',
        'beneficiaries'   => [
            ['account' => 'charlie', 'weight' => 1000],
        ],
    ],
];
```

### Node.js Example

```js
const op = ['fixed_award', {
    initiator: 'alice',
    receiver: 'bob',
    reward_amount: '10.000000 SHARES',
    max_energy: 5000,
    custom_sequence: 1,
    memo: 'fixed reward',
    beneficiaries: [
        { account: 'charlie', weight: 1000 },
    ],
}];
```

### Checklist
- [ ] `reward_amount.symbol` must be `SHARES`
- [ ] `reward_amount.amount` must be > 0
- [ ] `max_energy` = 0 means no energy cap (spend as much as needed)
- [ ] `max_energy` = 1–10000 limits maximum energy expenditure
- [ ] Actual energy spent depends on initiator's stake and current reward pool
- [ ] Beneficiary weights sum must be <= 10000 (100%)
- [ ] Beneficiaries list must be sorted by account name ascending
- [ ] Sign with `initiator`'s regular key
- [ ] Virtual `receive_award_operation` fires for `receiver`
- [ ] Virtual `benefactor_award_operation` fires for each beneficiary
