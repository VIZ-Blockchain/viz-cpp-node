# Award Operations

Awards are the primary social reward mechanism. An account spends *energy* to award SHARES directly to another account from the reward pool. Award size is proportional to the initiator's energy expenditure and SHARES stake.

**Energy:** stored as basis points 0–10000; regenerates at 100% per 24 hours (`CHAIN_ENERGY_REGENERATION_SECONDS = 86400`).

---

## `award_operation` (ID 47)

**Auth:** `regular` of `initiator`

Awards SHARES to `receiver`, spending a specified percentage of energy. The actual SHARES amount is determined by the initiator's stake and pool depth.

| Field | Type | Description |
|-------|------|-------------|
| `initiator` | `account_name_type` | Account giving the award |
| `receiver` | `account_name_type` | Account receiving the award |
| `energy` | `uint16_t` | Energy to spend in basis points (1–10000) |
| `custom_sequence` | `uint64_t` | Application-defined sequence number (may be 0) |
| `memo` | `string` | Optional message or reason |
| `beneficiaries` | `vector<beneficiary_route_type>` | Optional beneficiaries receiving a share of the award |

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

With beneficiaries:
```json
[47, {
  "initiator": "alice",
  "receiver": "bob",
  "energy": 1000,
  "custom_sequence": 1,
  "memo": "",
  "beneficiaries": [
    {"account": "charlie", "weight": 2000}
  ]
}]
```

- `energy` = 10000 uses 100% of current energy.
- If beneficiaries are present, `receiver` gets `(10000 − sum_of_weights) / 10000` of the award.
- Beneficiary weights must sum to ≤ 10000; beneficiaries must be sorted by account name ascending.
- Virtual `receive_award_operation` fires for `receiver`.
- Virtual `benefactor_award_operation` fires for each beneficiary.

---

## `fixed_award_operation` (ID 60)

**Auth:** `regular` of `initiator`

Awards a **fixed amount** of SHARES to `receiver`. Energy is consumed in proportion to the desired award size; `max_energy` caps the expenditure.

| Field | Type | Description |
|-------|------|-------------|
| `initiator` | `account_name_type` | Account giving the award |
| `receiver` | `account_name_type` | Account receiving the award |
| `reward_amount` | `asset` (SHARES) | Fixed amount of SHARES to award |
| `max_energy` | `uint16_t` | Maximum energy to spend (basis points; 0 = no cap) |
| `custom_sequence` | `uint64_t` | Application-defined sequence number |
| `memo` | `string` | Optional message or reason |
| `beneficiaries` | `vector<beneficiary_route_type>` | Optional beneficiaries |

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

- `reward_amount.symbol` must be `SHARES`.
- `max_energy = 0` means no energy cap — the operation will consume whatever energy is required.
- Actual energy consumed depends on the initiator's stake and the current reward pool depth.
- Beneficiary rules are identical to `award_operation`.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Virtual Operations](../virtual-operations.md).
