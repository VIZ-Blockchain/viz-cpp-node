# 数据库架构

VIZ Ledger 使用 ChainBase——一个基于 Boost.Interprocess 构建的内存映射、多索引持久化存储。所有链状态都在 `shared_memory.bin` 中。每种对象类型都与定义其主索引和次级索引的 Boost.MultiIndex 容器关联。

---

## 对象类型注册表

每个持久化对象在 `chain_object_types.hpp` 中声明了唯一的数字类型 ID。所有跟踪的对象类型完整集合：

| 对象 | 备注 |
|------|------|
| `dynamic_global_property` | 单例：当前链状态、头区块、LIB、通胀 |
| `account` | 所有已注册账户 |
| `account_authority` | master/active/regular 权限集 |
| `witness`（验证者） | 验证者注册信息、签名密钥、票数 |
| `transaction` | 待处理/最近的交易（TAPOS 窗口） |
| `block_summary` | 65536 槽的 TAPOS 区块 ID 缓冲区 |
| `witness_schedule` | 单例：活跃验证者调度 |
| `content` | 帖子和评论（已弃用） |
| `content_type` | 内容标题/正文元数据 |
| `content_vote` | 内容投票 |
| `witness_vote` | 账户对验证者的投票 |
| `hardfork_property` | 单例：当前/下一个硬分叉跟踪 |
| `withdraw_vesting_route` | 提取路由规则 |
| `master_authority_history` | Master 密钥变更历史 |
| `account_recovery_request` | 待处理的账户恢复请求 |
| `change_recovery_account_request` | 待处理的恢复账户变更 |
| `escrow` | 托管转账 |
| `vesting_delegation` | 活跃的 SHARES 委托 |
| `fix_vesting_delegation` | 委托修复记录 |
| `vesting_delegation_expiration` | 处于返还窗口的委托 |
| `account_metadata` | 账户 JSON 元数据 |
| `proposal` | 治理提案 |
| `required_approval` | 提案批准要求 |
| `committee_request` | 委员会资金请求 |
| `committee_vote` | 委员会投票 |
| `invite` | 账户邀请 |
| `award_shares_expire` | 过期的奖励 SHARES |
| `paid_subscription` | 订阅服务 |
| `paid_subscribe` | 活跃订阅 |
| `witness_penalty_expire` | 验证者缺席惩罚到期 |
| `block_post_validation` | 区块后验证记录 |

---

## 账户对象

账户存储余额、锁仓状态、委托指标、带宽、拍卖/出售标志和治理参与情况。

**关键字段：** `name`、`balance`（VIZ）、`vesting_shares`、`delegated_vesting_shares`、`received_vesting_shares`、`energy`、`next_vesting_withdrawal`、`witnesses_voted_for`、`recovery_account`。

**索引：**

| 标签 | 键 | 类型 |
|------|-----|------|
| `by_id` | `id` | 唯一 |
| `by_name` | `name` | 唯一 |
| `by_account_on_sale` | 出售标志 | 非唯一 |
| `by_account_on_auction` | 拍卖标志 | 非唯一 |
| `by_account_on_sale_start_time` | 出售开始时间 | 非唯一 |
| `by_subaccount_on_sale` | 子账户出售标志 | 非唯一 |
| `by_next_vesting_withdrawal` | `(next_vesting_withdrawal, id)` | 复合 |

`by_next_vesting_withdrawal` 复合索引支持以 O(log N) 批量处理即将到来的提取分期付款。

---

## 内容对象

内容对象表示带有投票、结算和嵌套元数据的帖子和评论。**这些对象已弃用**——新应用应改用 `custom_operation`。

**`content` 的索引：**

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_cashout_time` | `(cashout_time, id)` |
| `by_permlink` | `(author, permlink)` |
| `by_root` | `(root_content, id)` |
| `by_parent` | `(parent_author, parent_permlink, id)` |
| `by_last_update` | `(parent_author, last_update, id)` — API 密集 |
| `by_author_last_update` | `(author, last_update, id)` — API 密集 |

**`content_vote` 的索引：**

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_content_voter` | `(content, voter)` — 唯一 |
| `by_voter_content` | `(voter, content)` — 唯一 |
| `by_voter_last_update` | `(voter, last_update, content)` |
| `by_content_weight_voter` | `(content, weight, voter)` — 用于排行榜 |

---

## 验证者（Witness）对象

**`witness_object` 的索引：**

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_name` | `owner` — 唯一 |
| `by_vote_name` | `(votes, owner)` |
| `by_counted_vote_name` | `(counted_votes, owner)` |
| `by_schedule_time` | `(virtual_scheduled_time, id)` — O(log N) 槽位调度 |

**`witness_vote_object` 的索引：**

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_account_witness` | `(account, validator)` — 唯一 |
| `by_witness_account` | `(validator, account)` — 唯一 |

`by_schedule_time` 索引是区块生产调度器以 O(log N) 时间选择下一个验证者的方式。

---

## 提案和必需批准对象

**`proposal_object` 的索引：**

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_account` | `(author, title)` — 唯一 |
| `by_expiration` | `expiration` — 非唯一 |

**`required_approval_object` 的索引：**

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_account` | `(account, proposal)` |

---

## 邀请对象

| 标签 | 键 |
|------|-----|
| `by_id` | `id` |
| `by_invite_key` | 公钥 — 非唯一 |
| `by_status` | 状态 — 非唯一 |
| `by_creator` | 创建者 — 非唯一 |
| `by_receiver` | 接收者 — 非唯一 |

---

## 辅助对象

**`withdraw_vesting_route`：**

| 标签 | 键 |
|------|-----|
| `by_withdraw_route` | `(from_account, to_account)` — 唯一 |
| `by_destination` | `(to_account, id)` |

**`escrow`：**

| 标签 | 键 |
|------|-----|
| `by_from_id` | `(from, escrow_id)` — 唯一 |
| `by_to` | `(to, id)` |
| `by_agent` | `(agent, id)` |
| `by_ratification_deadline` | `(is_approved, ratification_deadline, id)` |

**`vesting_delegation`：**

| 标签 | 键 |
|------|-----|
| `by_delegation` | `(delegator, delegatee)` — 唯一 |

**`vesting_delegation_expiration`：**

| 标签 | 键 |
|------|-----|
| `by_expiration` | `expiration` — 非唯一 |
| `by_account_expiration` | `(delegator, expiration)` |

---

## Fork 数据库

Fork 数据库（`fork_database`）维护一个区块内存树，用于管理链分叉。它独立于持久化的 chainbase 存储运行。

**已链接索引** — 规范链区块，按区块 ID 和区块号索引。  
**未链接索引** — 孤立的或无序的区块，其父区块尚未知晓。

```
推送区块
  ├── 父区块在已链接索引中已知？
  │     是  → 链接区块，插入已链接索引，更新头部
  │     否  → 插入未链接索引
  └── 尝试链接待处理的未链接区块
```

当新区块到达且其 ID 与未链接区块的父区块匹配时，`_push_next()` 级联遍历未链接索引，将这些区块提升到已链接链中。

**分支操作：**
- `fetch_branch_from(first, second)` — 遍历两个分支找到共同祖先。返回 `(first_branch, second_branch)` 用于切换分叉。
- `set_max_size(n)` — 修剪 n 个以前的区块，限制内存使用。
- `walk_main_branch_to_num(n)` — 迭代主链到特定区块号。

**区块有效性：** 标记为无效的区块永远不会被提升。推送超出最大重排窗口的区块会触发断言。

---

## 索引管理

核心索引在 `database::initialize_indexes()` 期间注册。插件通过 `plugin_startup()` 中的 `add_plugin_index<T>()` 注册额外索引。

```cpp
// 核心索引注册（database.cpp）
add_core_index<account_index>();
add_core_index<witness_index>();
// ...

// 插件索引注册（插件启动）
db.add_plugin_index<my_custom_index>();
```

---

## 对象关系

```
account ──(author)──► content ──► content_vote ◄──(voter)── account
account ──(delegator)──► vesting_delegation ──► account (delegatee)
account ──(account)──► witness_vote ──► witness (validator)
account ──(author)──► proposal ──► required_approval ◄──(account)── account
account ──(creator/receiver)──► invite
escrow: from + to + agent → escrow_object
```

---

## 查询优化指南

**快速查找：**
- 按名称查找账户 → `by_name`（唯一，O(log N)）
- 验证者调度 → `by_schedule_time`（按虚拟时间排序）
- 按 author+permlink 查找内容 → `by_permlink`（唯一复合）
- 按 content+weight 查找投票 → `by_content_weight_voter`（排行榜）

**批量处理：**
- 锁仓提取 → 向前迭代 `by_next_vesting_withdrawal`
- 过期委托 → 向前迭代 `by_expiration`
- 过期提案 → 向前迭代 `by_expiration`

**避免全扫描：** 始终使用带索引的标签。复合索引首先按最左边的键排序——将选择性最强或过滤最频繁的字段放在第一位。

---

## 插件的架构扩展

添加自定义对象类型：

1. 定义继承自 `chainbase::object<type_id, MyObject>` 的对象类。
2. 声明带有所需索引的 `chainbase::shared_multi_index_container`。
3. 在 `plugin_startup()` 中通过 `db.add_plugin_index<MyIndex>()` 注册。
4. 添加 `FC_REFLECT` 宏用于序列化。

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

## 架构演进

新硬分叉 → 新字段或对象。指导原则：

- 在硬分叉间保持现有主键语义稳定。
- 将新字段添加为可选或有默认值；永远不要更改现有字段布局。
- 在重放期间用 `has_hardfork()` 检查控制新索引使用。
- 在现有标签旁添加新的 MultiIndex 标签——永远不要删除正在重放的节点可能查询的标签。

另见：[硬分叉管理](./hardfork-management.md)、[共享内存](../storage/shared-memory.md)。

---

参见：[插件开发](../development/plugin-development.md)、[虚拟操作](../protocol/virtual-operations.md)、[硬分叉管理](./hardfork-management.md)。
