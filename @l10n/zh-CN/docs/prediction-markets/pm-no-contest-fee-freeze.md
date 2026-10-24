# HF16：no-contest 费用冻结

状态：已实现，计划在 **2026-10-31 00:00:00 UTC** 随 4.2.0 激活，还需硬分叉投票门槛。参见 [HF16](../consensus/hf16.md)。

## 费用何时确定

HF16 之前，`pm_no_contest` 只声明无结果并开启争议宽限期，费用在结算时使用当时的中位数参数计算。参数在窗口内改变会追溯改变账单。

HF16 下，声明区块的中位数决定 `floor(pm_dispute_fee × pm_no_contest_penalty_percent / 10000)`，保存到市场的 `no_contest_fee`。结算不重新读取当前中位数；实际罚款仍为 `min(frozen_fee, oracle.insurance)`，不会超过保险余额。有无下注的处理不变；没有 stake 的市场记录冻结零费用且不收费。

## 两种状态

- `no_contest_fee_frozen=true`：HF16 下声明，使用保存的金额，包括合法的零值。
- `false`：激活前声明，或来自旧快照；保留旧的结算时中位数算法。

不能用 `fee == 0` 代替标志。冻结为零必须保持零，不能因后来的投票涨价。激活时不扫描或回填宽限期中的旧市场，也不猜测历史费用。

## 统计与快照

`no_contest_slashed_total` 是预言机累计已收取的 no-contest 罚款，在增加 `total_insurance_slashed` 的同一次 modify 中累加。HF16 后结算的旧声明也计入；HF16 之前已经收取的罚款不回填。它仅用于展示/归因，不参与共识判定，不增加虚拟操作。

市场金额及冻结标志、预言机累计值必须由当前版本快照保存/恢复；旧数据默认未冻结。对象布局变化要求使用当前 schema 和匹配二进制，不手工修改 shared_memory。更新前保存数据备份。

## 验证要点

1. 确认实际生效的 hardfork 为 16，而不只是二进制版本 4.2.0。
2. 新声明记录 `no_contest_fee_frozen=true`；在宽限期改变参数后结算仍使用冻结金额。
3. 验证冻结零费用、无下注、保险不足和激活前声明的旧路径。
4. 快照往返后字段保持一致；累计值与实际保险扣除对应，不把冻结的名义费用当作已经收取的罚款。

回归入口：`tests/consensus_sim/scenarios/test_pm_no_contest_fee.cpp`。[英文完整检查表](/prediction-markets/pm-no-contest-fee-freeze)包含底层字段和快照验证步骤。
