# DAG tool 超时 + 事件 v1 字段 设计

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-13 |
| 状态 | 已批准（brainstorming） |
| 依据 | [`../../evaluations/2026-09-13.md`](../../evaluations/2026-09-13.md) P1 薄切片；产品三角 [`AGENTS.md`](../../../AGENTS.md) |
| 前序 | tool `retry.max`：[`2026-09-06-flexible-dag-f1-design.md`](2026-09-06-flexible-dag-f1-design.md)；事件初版：近端演进计划轨 C |

## 1. 目标与非目标

**目标（本期两轨薄切片）：**

| 轨 | 交付 |
|----|------|
| **F** | DAG `type:tool` 可选 `timeout_sec`；超时算失败并进入现有 `retry.max` /（若开）`on_tool_fail.llm`；文档对齐 retry 语义 |
| **G** | `NEO_EVENTS` JSONL 增加稳定字段 `v`、`run_id`；手册字段表 |

**非目标：**

- 无依赖边最小并行 / 图级重试引擎
- 事件中的 token / usage / 成本字段
- 图级默认超时、第二套日志系统
- 保证 builtin / MCP 的墙钟超时（仅 **command** 类硬保证；其余 override 可忽略并在文档标明）

## 2. 轨 F — tool 步 `timeout_sec`

### 2.1 配置

仅 `type: tool`：

```json5
{
  id: "slow",
  type: "tool",
  tool: "some_command",
  timeout_sec: 5,
  retry: { max: 1 },
  args: {},
}
```

| 规则 | 说明 |
|------|------|
| 缺省 / `0` | **不覆盖**；command 仍用矩阵行 / `commands[].timeout_sec`（缺省通常 30） |
| 合法范围 | **1..600**；越界 → 配置加载失败 |
| 非 tool 出现 | 配置失败（与 `retry` 同风格） |
| 与 retry | 每次 attempt **重新**计时；超时输出沿用 `ERROR: timeout`，DAG 现有逻辑当失败 |

### 2.2 分发路径

1. `dag_step_t` 增加 `int timeout_sec`（0 = 不覆盖）。
2. `config.c` 解析与校验。
3. 扩展：
   - `neo_dispatch_tool(..., int timeout_override_sec)`
   - `command_tool_run(..., int timeout_override_sec)`  
   `timeout_override_sec > 0` 时覆盖本次 `cmd->timeout_sec`；否则行为不变。
4. `dag_run_tool_step` 传入 `st->timeout_sec`。
5. 反应式 tool loop 调用处传 `0`（不改变对话路径默认）。

### 2.3 与现有 retry / LLM 热线

不变：总尝试 = `1 + retry.max`；耗尽后可选 `dag.on_tool_fail.llm`。超时只是失败原因之一，不新增失败码枚举。

### 2.4 文档

`docs/dag.md`：在「tool 步可选 retry」旁增加 `timeout_sec` 小节，并写明与 retry / 热线的关系。  
`docs/architecture.md` §8.1：一行注明「tool 步可选超时」若与表冲突则回写。

## 3. 轨 G — 事件 v1 字段

### 3.1 行格式（破坏性小扩展）

```json
{"v":1,"ts":1726200000,"run_id":"a1b2c3d4","name":"dag_step","ok":1,"ms":12,"detail":"cli_count:run"}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `v` | int | schema 版本；本期固定 **1**；破坏性改字段再 bump |
| `ts` | int | unix 秒（已有） |
| `run_id` | string | 进程内首次 `neo_events_emit` 时生成（8–16 位 hex），之后复用；**不**跨进程持久化 |
| `name` | string | 已有事件名集合不变 |
| `ok` | 0\|1 | 已有 |
| `ms` | int | 已有 |
| `detail` | string | 已有；截断与转义规则不变 |

### 3.2 API

保持 `neo_events_emit(name, ok, ms, detail)`；内部写入 `v`/`run_id`。不新增并行 emit API。

### 3.3 文档

`docs/manual.md` §5.3：用字段表替换「形如 …」一句；说明 `NEO_EVENTS` / `NEO_EVENTS_PATH` 行为不变。

## 4. 错误与边界

| 情况 | 行为 |
|------|------|
| command 超时 | `ERROR: timeout`；dispatch 返回 0 且正文带 ERROR（现状）；DAG 当失败 |
| builtin/MCP + 步级 override | **不保证**墙钟杀掉；实现可忽略 override；文档写明优先测 command |
| `NEO_EVENTS` 关 | 早退，无 I/O |
| `NEO_EVENTS_PATH` 打不开 | 警告 + 回退 stderr（现状） |

## 5. 测试与验收

- 单元 / CLI：短 `timeout_sec` + 慢 command → 出现 `ERROR: timeout`；带 `retry.max: 1` 可见第二次 attempt（`-v` 或 stderr retry 行）。
- 事件：`NEO_EVENTS=1` 下两行 emit 含相同 `run_id` 与 `"v":1`。
- `make test` 通过。

## 6. 实现落点（文件）

| 文件 | 职责 |
|------|------|
| `src/core/config.h` / `config.c` | `dag_step_t.timeout_sec` 解析校验 |
| `src/capability/agent_tools.h` / `.c` | `neo_dispatch_tool` override 参数 |
| `src/capability/command_tools.h` / `.c` | 执行时应用 override |
| `src/dag/dag.c` | tool 步传入 override |
| `src/core/neo_events.c` / `.h` | `v`、`run_id` |
| `tests/*`、fixture | 超时 + 事件断言 |
| `docs/dag.md`、`docs/manual.md` | 用户可见行为 |

## 7. 一句话

给 DAG tool 步一把可选秒表（失败进现有 retry），给事件流一个可关联的 `run_id` 与 schema 版本——不加并行、不加计费。
