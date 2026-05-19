# 奖励操作

奖励是主要的社交激励机制。账户消耗*能量*，从奖励池中直接向另一账户授予 SHARES。奖励大小与发起者的能量消耗和 SHARES 质押量成比例。

**能量：** 以基点 0–10000 存储；每 24 小时恢复至 100%（`CHAIN_ENERGY_REGENERATION_SECONDS = 86400`）。

---

## `award_operation`（ID 47）

**授权：** `initiator` 的 `regular`

向 `receiver` 授予 SHARES，消耗指定百分比的能量。实际 SHARES 数量由发起者的质押量和奖励池深度决定。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 给予奖励的账户 |
| `receiver` | `account_name_type` | 接收奖励的账户 |
| `energy` | `uint16_t` | 消耗的能量（基点，1–10000） |
| `custom_sequence` | `uint64_t` | 应用程序定义的序列号（可为 0） |
| `memo` | `string` | 可选消息或原因 |
| `beneficiaries` | `vector<beneficiary_route_type>` | 可选受益人，获得奖励的一部分 |

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

含受益人的示例：
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

- `energy` = 10000 消耗当前 100% 的能量。
- 若存在受益人，`receiver` 获得奖励的 `(10000 − 权重之和) / 10000`。
- 受益人权重之和必须 ≤ 10000；受益人必须按账户名升序排列。
- 虚拟操作 `receive_award_operation` 为 `receiver` 触发。
- 虚拟操作 `benefactor_award_operation` 为每个受益人触发。

---

## `fixed_award_operation`（ID 60）

**授权：** `initiator` 的 `regular`

向 `receiver` 授予**固定数量**的 SHARES。能量按所需奖励大小比例消耗；`max_energy` 限制最大消耗。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 给予奖励的账户 |
| `receiver` | `account_name_type` | 接收奖励的账户 |
| `reward_amount` | `asset`（SHARES） | 授予的固定 SHARES 数量 |
| `max_energy` | `uint16_t` | 最大消耗能量（基点；0 = 无限制） |
| `custom_sequence` | `uint64_t` | 应用程序定义的序列号 |
| `memo` | `string` | 可选消息或原因 |
| `beneficiaries` | `vector<beneficiary_route_type>` | 可选受益人 |

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

- `reward_amount.symbol` 必须为 `SHARES`。
- `max_energy = 0` 表示无能量限制 — 操作将消耗所需的任意能量。
- 实际消耗的能量取决于发起者的质押量和当前奖励池深度。
- 受益人规则与 `award_operation` 相同。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[虚拟操作](../virtual-operations.md)。
