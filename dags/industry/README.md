# dags/industry/

行业层 DAG 目录，与 [`capabilities/industry/`](../../capabilities/industry/README.md) 配套，演示「本地执行 + 可选 LLM 整理」在交易场景下的组合。

| 文件 | 图名 | 主要能力 |
|------|------|----------|
| `trade_bot_demo.json5` | `trade_bot_demo` | `trade_bot`（纸面交易）+ LLM 摘要 |

## 说明

- `trade_bot_demo` 用内置合成行情跑一遍纸面交易机器人（变体4），产出 `ledger.csv`，再由 LLM 生成大字号可读摘要。
- 依赖能力 `trade_bot` 须在能力矩阵中存在（见 `capabilities/industry/`）。
- **非目标**：真实下单、实时行情、投资建议。

## 关联入口

- DAG 约定：[`docs/dag.md`](../../docs/dag.md)
- 选型元数据规范：[`AGENTS.md`](../../AGENTS.md) §4.1

本目录暂无其它子目录。
