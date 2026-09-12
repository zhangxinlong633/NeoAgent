# capabilities/industry/

行业层能力目录，对应 [`docs/applications.md`](../../docs/applications.md) 的行业应用场景（工业边缘、具身、物联网执行器，以及本目录新增的量化交易类工具）。

## 现状

| 文件 | 能力名 | 说明 |
|------|--------|------|
| `trade_bot.json5` | `trade_bot` | 纸面交易机器人（变体4：均值回归 + 分层风控 + 纸面撮合）的能力声明 |
| `trade_bot.py` | — | 上述能力的实现脚本（Python 3，仅标准库） |
| `trade_bot_v29.py` | — | 变体29 实现脚本（布林带 + RSI 双确认均值回归 + 凯利分数仓位 + 分层风控 + 纸面撮合）；能力声明见 [`../proposed/trade_bot_v29.json5`](../proposed/trade_bot_v29.json5) |

> **重要边界**：`trade_bot` 与 `trade_bot_v29` **只做纸面交易（paper trading）**，不连接任何真实券商/交易所，不发起网络请求，不构成投资建议。行情来源为本地 CSV 或内置合成序列。

## 变体一览

| 变体 | 策略要点 | 能力声明位置 | 状态 |
|------|----------|--------------|------|
| 变体4 | 均值回归（z-score）+ 波动率缩放仓位 + 回撤熔断 | `trade_bot.json5`（本目录） | 已就位 |
| 变体9 | 动量突破 + 波动率目标仓位 + 回撤熔断 | [`../proposed/trade_bot_v9.json5`](../proposed/trade_bot_v9.json5) | 草稿 |
| 变体24 | 双均线趋势 + ATR 波动率目标仓位 + 回撤熔断 | [`../proposed/trade_bot_v24.json5`](../proposed/trade_bot_v24.json5) | 草稿 |
| 变体29 | 布林带 + RSI 双确认均值回归 + 凯利分数仓位 + 回撤熔断 | [`../proposed/trade_bot_v29.json5`](../proposed/trade_bot_v29.json5) | 草稿 |

## 启用方式

1. 本目录能力文件已就位；在 `capabilities/manifest.json5` 的 `load` 中增加 `"industry"`。
2. 重启 `neo`；确认 Policy（路径沙箱、超时、输出上限）满足约束。
3. 直接运行脚本自检：
   - 变体4：`python3 capabilities/industry/trade_bot.py --demo`
   - 变体29：`python3 capabilities/industry/trade_bot_v29.py --demo`
4. 变体9 / 24 / 29 的能力声明位于 `proposed/`，审核通过后移入本目录并重启方可进入矩阵。

## 关联入口

- 能力矩阵约定：[`docs/tool.md`](../../docs/tool.md)
- 选型元数据规范：[`AGENTS.md`](../../AGENTS.md) §4.1
- 配套 DAG：[`dags/industry/`](../../dags/industry/README.md)、[`dags/proposed/`](../../dags/proposed/README.md)

本目录暂无其它子目录。
