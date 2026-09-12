# dags/

本目录为确定性 **DAG（workflow）** 的文件化库：一图对应一个 JSON5 文件。顶层配置 `dag_directory` 指向本目录后，按 `manifest.json5` 的 `load[]` **逐项**扫描子目录加载。

Planner（`neo plan` / `neo run`）生成 catalog（含 `when` / `when_not` / `requires` / `outcome`），并优先输出 `{"use":["name"]}`；无合适条目时再现编 `{"dags":[...]}`。

权威说明：[`docs/dag.md`](../docs/dag.md)、[`docs/manual.md`](../docs/manual.md)、[`docs/examples.md`](../docs/examples.md)（含 **开箱组合** 与 `workspace_brief`）、[`docs/applications.md`](../docs/applications.md)、[`docs/architecture.md`](../docs/architecture.md)、[`AGENTS.md`](../AGENTS.md) §4.1。

## 职责边界

- 定义可复现的执行拓扑（`tool` / `llm` / `loop` / `route`），供调度层确定性推进。
- 不替代 Capability Matrix；`requires` 中的能力名须已在矩阵中存在（builtin 或目录命令）。
- 加载器**不递归**：图文件放在场景子目录内，且该目录名须出现在 `manifest.load` 中。

## 与应用场景的对应

| 场景层次（applications） | 本目录落点 | 是否默认加载 |
|--------------------------|------------|--------------|
| 基础层：复杂任务稳定执行 | `baseline/` | 是 |
| 基础层：工作区旁路助手 | `workspace/` | 是 |
| 行业层：边缘自治 / 联锁 | `edge/` | 否（预留） |
| 未来层：Agent-as-OS 等 | 非现行交付 | — |

## 子目录

| 子目录 | 职责 |
|--------|------|
| `baseline/` | 多步 tool→LLM 稳定编排示范 |
| `workspace/` | 仓库旁路：读览、搜索、备忘；旗舰 SOP 为 `workspace_brief`（列目录 → LLM 简报 → 落盘 `WORKSPACE_BRIEF.md`） |
| `edge/` | 边缘 / 工业联锁图预留；**不**列入 `load` |
| `proposed/` | 草稿区；当前默认不加载 |

根目录 `manifest.json5` 声明 `load` 与 `proposed`。
