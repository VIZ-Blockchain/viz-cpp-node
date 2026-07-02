# `prediction_market_api` 插件

对 HF14 预测市场状态的只读 JSON-RPC 访问（市场、下注、预言机、流动性、争议、懒惰池、链属性 v5）。插件直接返回原始共识 `pm_*` 对象，外加少量计算型 DTO。

**启用：** 将 `prediction_market_api` 加入节点插件列表（`vizd` 默认注册）。依赖 `chain` + `json_rpc`。所有列表方法通过 `from`（跳过）与 `limit`（`≤ 1000`）分页。

## 方法

### 市场

| 方法 | 参数 | 返回 |
|------|------|------|
| `get_market` | `market_id` | `pm_market_object` |
| `list_markets` | `status, from, limit, [show_risky]` | `pm_market_object[]` |
| `list_markets_by_oracle` | `oracle, from, limit` | `pm_market_object[]` |
| `list_markets_by_creator` | `creator, from, limit` | `pm_market_object[]` |
| `get_market_outcomes` | `market_id` | `pm_outcome_object[]` |
| `get_market_weight_sums` | `market_id` | `pm_market_weight_sums`（计算型） |
| `get_market_bets` | `market_id, from, limit` | `pm_bet_object[]` |
| `get_market_liquidity` | `market_id, from, limit` | `pm_liquidity_object[]` |
| `get_market_full` | `market_id, [account]` | `pm_market_full`（计算型） |

`list_markets` 的 `status`：`-1` 已删除、`0` 等待、`1` 活跃、`2` 关闭、`3` 已裁定。默认情况下
`list_markets` 会隐藏保证金不足的市场（预言机保险 < 下注量的 **2.5×**）；`show_risky = true` 可显示它们
（仅隐藏，链上始终允许下注）。

`get_market_full` 是市场详情页的**单次调用富集视图**：返回市场 + 结果 + 权重合计 + 预言机（含可靠度）+ 已解析元数据，且——当提供可选 `account` 时——返回该账户**在本市场上**的下注、杠杆头寸与 LP。为瘦客户端省去多次往返。

### 市场元数据（链下解析）

每个市场携带一个自由格式、共识不透明的 `metadata` JSON 字符串。插件将其索引的键（类别 / 子类别 / 标签 /
受禁司法辖区）解析为 `pm_market_meta_object`——**仅用于展示/索引，非共识**。

| 方法 | 参数 | 返回 |
|------|------|------|
| `get_market_meta` | `market_id` | `pm_market_meta_object`（无则报错） |
| `list_markets_by_category` | `category, from, limit, [jurisdiction], [subcategory], [tag], [sort]` | `pm_market_meta_object[]` |
| `get_market_categories` | — | `pm_market_categories`（计算型） |

`list_markets_by_category` 会排除其 `banned_jurisdictions` 含可选 ISO 代码 `jurisdiction` 的市场（受监管
客户端传入自身辖区即可只获取可列出的市场）。可选的 `subcategory`（精确）与 `tag`（CSV 成员）进一步收窄集合；
`sort` ∈ `newest`（市场 id 降序，默认）· `oldest` · `volume`（`bets_sum` 降序）· `expiration`（`betting_expiration`
升序）。`get_market_categories` 返回实时分类法——每类别 / 每子类别计数，外加前 20 个热门标签（排除 jurisdiction-*）——
在当前已索引市场上聚合，使浏览 UI 无需硬编码分类法即可构建其筛选标签。对象：`market`、`category`、`subcategory`、`tags`（逗号分隔）、
`banned_jurisdictions`（逗号分隔 ISO；为空 = 全球允许）、`expiry`（争议窗口关闭 + TTL 后清理）。

### 持仓与预言机

| 方法 | 参数 | 返回 |
|------|------|------|
| `get_account_positions` | `account, from, limit` | `pm_position[]`（下注 + `expected_payout`） |
| `get_account_leverage_positions` | `account, from, limit` | `pm_leverage_position_object[]` |
| `get_market_leverage_positions` | `market_id, from, limit` | `pm_leverage_position_object[]` |
| `get_creator_ban` | `account` | `pm_creator_ban_object`（无则报错） |
| `get_oracle` | `owner` | `pm_oracle`（对象 + `reliability_score`） |
| `list_oracles` | `from, limit` | `pm_oracle_object[]` |

> 每位下注者的结算以 `pm_payout` 虚拟操作发出（本金、side/outcome、结果；输则为 `0`）；杠杆头寸的结算为
> `pm_leverage_resolve`（`outcome_index`、`won`、`leverage`）。两者均见于 `account_history`；头寸对象本身可经上述方法查询。

### 杠杆预览（Boost）

只读报价，调用评估器所用的**同一套节点内保证金数学**，因此预览与对应 `pm_leverage_*` 操作在头区块的计算结果一致。它们是非共识估算（读取与广播之间储备会变动——务必发送链上滑点保护）。

| 方法 | 参数 | 返回 |
|------|------|------|
| `get_leverage_quote` | `market_id, outcome_index, collateral` | `pm_leverage_quote`（计算型） |
| `get_leverage_close_preview` | `position_id` | `pm_leverage_close_preview`（计算型） |
| `get_leverage_convert_preview` | `position_id` | `pm_leverage_convert_preview`（计算型） |

`get_leverage_quote` 镜像 `pm_leverage_open`：返回最大偿付贷款与由此得到的最大杠杆、池/头寸上限、至多 12 个滑块档位（每档含代币、阈值、当前及最坏情形取消价值），且——当无法杠杆时——返回 `available = false` 并附 `failed_constraints[]` 列表。`get_leverage_close_preview` / `get_leverage_convert_preview` 在当前储备下镜像 `pm_leverage_close` / `pm_leverage_convert`（取消价值、池义务、下注者所得、是否可平仓/可转换，以及按当前中位数 `pm_conversion_profit_cost_percent` 的转换费）。

### 争议、懒惰池、治理

| 方法 | 参数 | 返回 |
|------|------|------|
| `get_dispute` | `market_id` | `pm_dispute_object` |
| `get_dispute_votes` | `market_id` | `pm_dispute_votes`（投票 + 实时计票） |
| `get_lazy_pool` | — | `pm_lazy_pool_object` |
| `get_lazy_deposit` | `account` | `pm_lazy_deposit_object` |
| `get_lazy_allocations` | `from, limit` | `pm_lazy_allocation_object[]` |
| `get_market_lazy_allocation` | `market_id` | `pm_lazy_allocation_object`（无则报错） |
| `get_pm_chain_properties` | — | `chain_properties_pm`（中位数, v5） |

`get_lazy_allocations` 列出懒惰池的每市场分配记录（用于池仪表盘）；`get_market_lazy_allocation` 获取给定市场的那一条。预言机罚分戳记无需单独方法——它们随 `pm_oracle_object`（`penalty_stamps`、`last_penalty_stamp_time`）经 `get_oracle` 一并返回。

### 图表 —— kline / 权重历史

用于绘制每个结果权重随时间变化的时间序列。**每当市场的各结果权重发生变化时**（下注、取消、清算、批次结算、杠杆开仓或杠杆结算），插件追加一个点 —— 即各结果同注分彩权重（下注额）的带时间戳快照。这是**非共识**插件状态（存于 chainbase，undo/redo 安全，不计入状态哈希）；历史从插件在节点上首次启用时开始累积。

**保留：** kline 历史与市场元数据**一同清理**，遵循同一时间表 —— `result_expiration` + 争议宽限 + `pmm-ttl-days`（默认 7）。市场的完整图表在其存续期间及结算后的保留窗口内均可用，随后两个索引被清理（极长历史会分多个区块逐步清空），以使节点存储保持有界。

| 方法 | 参数 | 返回 |
|------|------|------|
| `get_market_kline` | `market_id, [from], [limit]` | `pm_kline[]`（按 `seq` 升序） |

分页为**从最新偏移**（为瘦客户端刻意保持简单）：`from` 为跳过的**最新**点数量，`limit ≤ 1000` 为页大小。
- `(market_id, 0, 1000)` → 最新 ≤ 1000 个变化。
- `(market_id, 1000, 1000)` → 再往前一页的 1000 个 —— 以 `from += 1000` 重复以惰性加载更早历史。

绘图：x = `timestamp`（unix 秒），每个结果 `i` 一条线，y = `weights[i]`（或归一化 `weights[i] / Σweights`，即隐含概率）。

## 计算型 DTO

- **`pm_position`** —— 下注 + `expected_payout`（若该方获胜的赔付，或结算后已实现值；与 `settle_market` 逐字节一致）、`market_status`、`resolved_outcome`。
- **`pm_oracle`** —— 预言机对象 + `reliability_score`（bp `[0..10000]`，非共识启发式：裁定成功率与争议胜率的混合，再减去封禁罚分）。
- **`pm_market_weight_sums`** —— 各方/各结果的 `bets_sum`/`weight_sum`（权重通过扫描下注计算，因其不存储）。
- **`pm_kline`** —— 一个图表点：`seq`（uint32，0 起、按市场单调递增的变化索引）、`timestamp`（unix 秒，x）、`reason`（uint8：0 下注、1 取消、2 清算、3 批次结算、4 杠杆开仓、5 杠杆结算）、`bets_sum`（总下注额）、`weights[]`（每个结果的权重，y；索引 = outcome_index）。
- **`pm_dispute_votes`** —— 投票 + finalize 计票。旧字段（权重 = `|vote_percent|`，非质押）：`uphold_weight`/`challenge_weight`/`total_weight`、`challenger_leads`（≥ `pm_dispute_approve_min_percent`）、`proposed_outcome`。**精确的按质押加权投影（镜像 `pm_dispute_finalize`；所有 `*_shares` 为 vesting-shares = `effective_vesting_shares` + 懒惰池质押→shares）：** `participation_shares`（已投票者权重之和）、`electorate_shares`（`total_vesting_shares` + 池 NAV→shares）、`quorum_required_shares`、`quorum_percent_bp`（法定人数，bp，10000 = 100.00%）、`quorum_reached`（bool）、`oracle_defense_shares`/`change_shares`、`outcome_change_shares[]`（按结果）、`expected_uphold`（预言机裁决是否维持）、`expected_outcome`（当前若裁决将设定的结果）、`expected_consensus_strength_bp`。该投影与定时任务在 `voting_end_time` 按当前投票应用的结果一致（在此之前投票可更改）。

**`pm_market_full`** —— 单次调用的富集市场视图（`oracle`/`meta` 缺失时为 `null`；除非提供 `account` 参数，否则 `my_*` 数组为空）：
```
{ market: pm_market_object,
  outcomes: pm_outcome_object[],            // 二元市场为空
  weight_sums: pm_market_weight_sums,
  oracle: pm_oracle | null,
  meta: pm_market_meta_object | null,
  my_positions: pm_position[],              // 账户在本市场的下注
  my_leverage_positions: pm_leverage_position_object[],
  my_liquidity: pm_liquidity_object[] }
```

**`pm_leverage_quote`** —— 杠杆开仓预览（来自 `pm::leverage::*`，即评估器运行的同一套数学）：
```
{ available: bool, outcome_index, collateral,
  max_loan, max_leverage_x100,              // 100 = 1.00×
  pool_free_amount, fund_available, per_position_cap, market_position_cap,
  pool_profit_percent, safety_margin_percent, max_slippage_percent, m_factor_percent,
  expiration_buffer_sec, auto_close_time,   // betting_expiration − buffer
  stops: [ { leverage_x100, loan, total_bet, expected_tokens, pool_profit,
             liquidation_threshold, current_cancel_value, worst_case_cancel_value } ],
  failed_constraints: [ { constraint, reason } ] }   // !available 时填充
```
**`pm_leverage_close_preview`** —— `{ position_id, outcome_index, cancel_value, pool_obligation, bettor_receives, collateral, loan, pool_profit_charge, closeable: bool, loss_vs_collateral, loss_percent_bp }`。
**`pm_leverage_convert_preview`** —— `{ position_id, outcome_index, cancel_value, pool_obligation, current_profit, conversion_profit_cost_percent, conversion_fee, total_user_payment, convertible: bool }`。

**`pm_market_categories`** —— 带实时计数的浏览分类法：
```
{ categories: [ { category, count, subcategories: [ { subcategory, count } ] } ],  // 按 count 降序
  hot_tags:   [ { tag, count } ] }                                                 // 前 20（排除 jurisdiction-*）
```

## 示例

市场 `42` 最新 1000 个图表点，再取前 1000 个：
```bash
# 最新一页
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_market_kline",[42,0,1000]]}' http://127.0.0.1:8090
# 往前一页
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_market_kline",[42,1000,1000]]}' http://127.0.0.1:8090
```

瘦客户端（向后滚动时惰性加载更早历史）—— 将每个点按结果转成 `{ x: unixtime, y: weight }` 序列：
```js
async function call(method, params) {
  const r = await fetch('http://127.0.0.1:8090', { method: 'POST',
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'call',
      params: ['prediction_market_api', method, params] }) });
  return (await r.json()).result;
}

// 从最新向后按每页 1000 拉取，直到凑够 `want` 个点（或历史耗尽）。
async function loadKline(marketId, want = 3000) {
  const points = [];
  for (let from = 0; points.length < want; from += 1000) {
    const page = await call('get_market_kline', [marketId, from, 1000]);
    if (!page.length) break;            // 到达历史起点
    points.unshift(...page);            // 页内升序；更早的页前置
    if (page.length < 1000) break;
  }
  return points;
}

// 每个结果一条 {x,y} 序列 —— 直接喂给任意图表库。
function toSeries(points, outcomeCount) {
  const series = Array.from({ length: outcomeCount }, () => []);
  for (const p of points)
    for (let i = 0; i < outcomeCount; i++)
      series[i].push({ x: p.timestamp, y: Number(p.weights[i]) });
  return series;
}
```

参见 [预测市场操作](../protocol/operations/prediction-markets.md) 与 [链属性](../governance/chain-properties.md#pm-parameters)。
