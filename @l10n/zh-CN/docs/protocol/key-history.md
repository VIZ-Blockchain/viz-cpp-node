# 密钥历史（HF15）

DLT 节点从快照启动，未必保留旧区块。HF15 起，共识状态永久记录账户已停止使用的旧权限成员，使历史签名可以用当前状态证明。

## 记录内容

某角色的权限实际改变时，为旧权限的每个成员记录一行：

| 字段 | 含义 |
|---|---|
| `account` | 账户 |
| `role` | `master`、`active`、`regular` 或 `memo` |
| `key` | 旧公钥；账户成员使用空公钥 |
| `auth_account` | 旧 `account_auths` 成员；公钥成员为空 |
| `weight` | 成员权重，memo 为 0 |
| `weight_threshold` | 角色阈值，memo 为 0 |
| `valid_until_block` | 旧权限最后有效的区块（含），下一区块发生改变 |
| `valid_until_time` | 该区块时间 |

空旧权限也保存标记行：空公钥、空账户、权重 0，用于空→非空的冷却限制；空公钥查询也能返回标记。起始时间是同一角色上一行结束后的区块；HF15 激活前已存在的密钥起点可能未知。当前权限仍在账户对象中。

`account_update`、恢复、直接出售和拍卖成交均记录实际改变的旧权限。未改变的角色不新增行；历史行永久保留。它独立于[账户恢复](operations/recovery.md)使用的仅保留 30 天的 master 历史。

## 修改频率与 API

HF15 起通过 `account_update` 实际修改 active、regular 或 memo 每个角色最多每小时一次，与已有 master 限制一致。不同角色分别计时；账户恢复和出售不受此限制。

- `database_api.get_key_history(account, from, limit)`：账户时间线。
- `database_api.get_key_history_by_key(key, limit)`：谁曾持有密钥、何时结束。
