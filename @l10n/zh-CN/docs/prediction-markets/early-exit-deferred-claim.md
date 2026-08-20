# 提前退出的递延权益（F1 / #300）

状态：设计已锁定（owner 2026-08-08），实现正在 `pm` 分支上进行。

## 问题

市场是一个混合体：入口和提前退出用 CPMM（二元）/ LMSR（多元）**曲线**，
而持有到裁定的仓位用**帕里姆图（pari-mutuel）**结算。任何穿越曲线的往返（先买后卖、在结算前）
都会把一笔交易盈亏兑现到曲线深度——即 LP——之上，与 Uniswap 的无常损失完全一样。但设计向 LP
承诺**本金保障**（只收费用、无无常损失）。这两者是矛盾的。

在二元市场上，有两条代码路径会逆着曲线退出：

- **杠杆**（`liquidate_position` / `pm_leverage_close`）：总是如此；在结算时被强平；被池贷款放大。
- **普通撤单**（`cancel_bet`，F2 按曲线定价退还）：发生在下注窗口内。

两者都把 `residual = stake − curve_refund` 路由到 `forfeit_pool`（带符号）。当一次提前退出
*盈利*时（`curve_refund > stake`），`forfeit_pool` 变为**负值**。结算时
`winners_pool = losers_sum − fees + forfeit_pool`；如果杠杆/提前退出的利润超过了输家的押注，
则 `winners_pool < 0`，被下限到 0，缺口（`uncovered`，F1）向 LP 本金收取——或者当 LP 本金耗尽时被
铸币。这是可达成的：用一个遵守门槛的 CPMM 模拟证明了这一点（单向拉升，`uncovered = 7316`）；
Babin 的重放语料在 1163/1988 对中命中它。

根本原因：**按曲线定价的退出支付的是一个不受输家池约束的 bonding-curve 价值**，而结算是帕里姆图。
这个缺口落在了 LP 头上。

## 模型（已锁定）

提前退出不再从 LP 身上抽取曲线价值。相反，退出记录一份**结果相关的递延权益**，在结算时由
**输家池的有界份额**提供资金。

### 退出时记录
`{ position_id, kind (bet|leverage), chosen_outcome, claim_amount, exit_time }`。

### 普通撤单
- **本金立即、无条件返还**：`refund = min(curve_refund, stake)`（自己的钱，不是借来的）。撤单可以
  止损或保本，但永远不会在撤单时刻兑现曲线利润。
- **利润尾巴** `max(curve_refund − stake, 0)` → 对所选结果的一份递延权益。
- **按深度归一化定价（审计 #1-C）：** cap/tail 的拆分是按该下注*入场时*的曲线深度重新定价的，而不是
  当前深度。`pm_bet_object.entry_liquidity` 记录 CPMM 下注击中曲线那一刻（即时 `place_bet` 和
  批量填充）的 `liquidity_sum`。撤单时两个储备都按 `entry_liquidity / liquidity_sum` 缩放（在缩放后的
  储备上做 mirror-of-buy）。因为下注保持 `k` 不变，而流动性操作把 `k` 缩放 `f²`、把 `liquidity_sum`
  缩放 `f`，所以 `sqrt(k_entry / k_now) == L_entry / L_now` **精确**成立 → 确定性、无需开方（无整数
  开方）。结果被**钳制到真实的 `curve_refund`**，因此归一化只能*降低*赔付，永远不会抬高。这扼杀了
  自我流动性通胀向量（下注 → 自己的 `add_liquidity` 吹大深度 → 更大的 `curve_refund` → 撤单铸出更大
  的尾巴 → 提现整体拿回流动性 = 现金中性的保证利润），又不打开收缩侧的漏洞。当 `entry_liquidity` 缺失
  （该字段出现之前的旧下注）时回退到旧定价。

### 杠杆平仓 / 清算
- **抵押品不单独返还**——它是池子的 first-loss margin。池子先从 `cv` 里收回自己的义务
  （`loan·(1+R) + funding`）；如果 `cv < obligation`，抵押品补上缺口。
- **剩余** `max(cv − obligation, 0)` → 对所选结果的一份递延权益。
- 因此杠杆是一笔**带杠杆的方向性下注**，而不是波动率收割：只有当你的结果赢了、而且桶里有空间时才
  盈利；押错结果就损失抵押品。
- 注意：现在有两个不同的谓词：**偿付能力**（`cv ≥ obligation`，决定贷款回收）vs
  **结果胜出**（决定索取权益的权利）。一个仓位可能偿付能力充足却押在输的结果上 → 池子被补全，
  claim = 0。

### 结算
1. `bucket = pm_early_exit_reward_cap_percent × losers_sum / 10000`（默认 33%）。
2. 只收集**胜出结果**上的递延权益（输的结果上的权益 → 0）。
3. 按 `exit_time` **FIFO**（先出先付）支付它们，直到桶被耗尽；没有 per-position 上限（owner
   2026-08-08：FIFO 顺序 + 总桶就是边界）。一份剩余桶无法全额支付的权益会被部分支付；其余部分不付
   （haircut）。
4. **未使用的桶返回赢家池**——持有的胜出下注按帕里姆图分享它。

### 保证
```
paid_claims ≤ bucket = cap · losers_sum
winners_pool = losers_sum − fees − paid_claims + honest_forfeits
             ≥ (1 − cap) · losers_sum − fees  ≥ 0     (cap < 100%)
```
- `uncovered` **在构造上不可能**；无铸币；**LP 本金永不被触及**；lazy pool 不承担杠杆无常损失。
- **输的结果永远不盈利**（owner 要求）。
- 持有的赢家获得输家池的 `≥ (1 − cap)`，外加任何未使用的桶。
- 提前退出是一份**有界的、相关的折扣**（≤ cap，FIFO），相对于持有到裁定（全额帕里姆图份额）→
  不存在针对持有的套利；这是一份刻意的流动性折扣。

## 链参数

`pm_early_exit_reward_cap_percent`（uint16，bp，默认 **3300** = `losers_sum` 的 33%）。
验证者中位数投票参数；`validate()` 限定 `≤ 10000`。已加入
`chain_properties_pm` + FC_REFLECT + `calc_median`（DONE，single-TU 验证）。

## 实现触点（节点）

- [x] 链参数 `pm_early_exit_reward_cap_percent`（struct/validate/reflect/median）。
- [ ] 对象 `pm_deferred_claim_object`（+ 按 market、按 exit_time 的索引）——space 30，追加到
      `object_type` 枚举末尾（snapshot-safe，如同 `pm_lazy_withdraw_request`）。
- [ ] `cancel_bet`：返还 `min(curve_refund, stake)`，记录利润尾巴权益；停止把负 residual 路由到
      `forfeit_pool`。
- [ ] `liquidate_position` / `pm_leverage_close`：池子取走 obligation，记录 `cv − obligation`
      权益，标记所选结果；去掉立即的 `bettor_received`；停止负 `forfeit_pool`。
- [ ] 结算（`settle_market`）：强平之后计算 `bucket`，按 exit_time FIFO 支付胜出结果的权益，剩余 →
      赢家池；移除 `uncovered`/F1 的 `settle_liquidity` 收取路径（LP 不再吸收它）。
- [ ] 快照：把 `pm_deferred_claim_object` 加入 allowlist（+ import handler）。
- [x] 虚拟操作 `pm_early_exit_claim_paid`（account, market, kind, outcome, `claimed`, `paid`）——
      追加到 `operation` 变体末尾（op-id 保持稳定），做了 FC_REFLECT，在结算的分配循环里、紧挨着
      `adjust_balance` 发射，并路由到提前退出者的账户历史（`account_history` 的 impacted-accounts
      visitor）。`claimed` vs `paid` 暴露任何桶耗尽导致的 haircut。单独的 `adjust_balance` 不留历史
      痕迹——这补上了缺口。
- [x] 读 API：`get_deferred_claims(market, [from=0], [limit=100])` —— 通过 `by_claim_market`
      的 FIFO 退出顺序；在已结算市场上为空（权益已消费）。仅插件（客户端通过 rawApi/JSON-RPC 调用），
      与 `get_lazy_withdraw_requests` 一致；不接钱包。

## 客户端 / 库 / 文档后续
- viz-js-lib / viz-php-lib / viz-python-lib：v5 chain_properties_pm 里的新链参数（序列化
  lock-step，字节校验），任何新的读方法 / vop。
- Forecaster：提示 + 操作说明（杠杆 = 方向性，提前退出 = 有界折扣），在仓位上展示待定的递延权益。
- WebVIZWallet：同样的操作说明更新（如果展示）。
- 学术文章：带数学的 `early-exit choice`（regular vs leverage-from-lazy-pool，验证者集合奖励上限）。

## 被否决的替代方案（原因）
- 把 `uncovered` 摊到所有 LP / 铸币（status quo）——破坏 LP 本金承诺。
- 只本地化到 lazy pool —— 池子可能被耗尽；仍然只是近似；撤单会泄漏。
- 在退出时封顶收益 —— 退出时刻你不知道 `losers_sum`；递延消除了这一点。
- 完全 AMM 结算 —— 放弃 VIZ 的帕里姆图论点。
结果相关的递延权益是唯一能在保持帕里姆图的同时给出 LP **硬性**保证的选项。
