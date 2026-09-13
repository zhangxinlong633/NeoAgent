# Neo 近端进化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不偏离「瑞士军刀 / DAG ∥ Capability Matrix ∥ Policy」的前提下，夯实规划、会话、可观测与专业软件套餐，抬高受控执行完成度。

**Architecture:** 分四条可独立交付的轨：（1）收紧 `plan`/`run` 选型与校验；（2）统一 daemon 与 `-S` 会话叙事；（3）结构化运行事件；（4）Blender（及同类）意图层能力 + DAG。各轨只扩展现有模块，不引入第二套工具系统、不做 IDE agent。

**Tech Stack:** C99、现有 `plan.c` / `neo_session` / `daemon.c` / capability 目录 / DAG 目录、JSON5、`make test` / CLI 冒烟。

**依据:** [`docs/evaluations/2026-09-12.md`](../../evaluations/2026-09-12.md)；产品约束 [`AGENTS.md`](../../../AGENTS.md)；架构近端 [`docs/architecture.md`](../../architecture.md) §8.1。

## Global Constraints

- 顶层配置键继续用 `capability_matrix`；步骤开关字段仍叫 `"tools"`
- 禁止默认打开 `shell_enabled`；危险 unix / 任意代码执行须白名单或人审
- 不做 Temporal / hooks 总线 / Cursor 级多文件编码 UX / 分布式漂移（本期）
- 复杂逻辑补中文注释；目录变更更新正式 `README.md`
- `make test` 通过；commit **无** Cursor/`Co-authored-by` 等 agent trailer
- 用户可见行为变了再改 `docs/manual.md` / `README*.md`

## File map（按轨）

| 轨 | 主要文件 |
|----|----------|
| A plan | `src/dag/plan.c`、`src/dag/dag_dir.c`、`tests/*plan*`、`docs/dag.md` |
| B session | `src/core/daemon.c`、`src/core/neo_session.*`、`src/cli/main.c`、`docs/manual.md` |
| C observe | `src/cli/main.c`、`src/dag/dag.c`、`src/capability/agent_tools.c`（或新建薄 `neo_events.*`） |
| D blender packs | `capabilities/local/`、`dags/baseline/`、`scripts/tools/`、可选 MCP 配置样例 |

**刻意不做（本期）:** 全仓索引、真云端 embedding 默认开、WASM/容器隔离、自动把 tool loop 编译成 DAG。

---

## 轨 A — 收紧 plan / run（P0）

### Task A1: 选型失败可解释

**Files:** `src/dag/plan.c`、相关测试 / CLI 冒烟

- [x] 当模型输出 `{"use":[...]}` 但名字非 catalog：stderr 已有部分提示；统一为稳定错误码文案（含「可用 catalog 名」摘要，长度有上限）
- [x] 当 `use` 与 capability 名混淆：保持拒绝，并提示应改为 `type:tool` 现编或正确 catalog
- [x] 增加/扩展 fixture：错误 `use` → 非 0 退出且 stderr 含固定关键字
- [x] `make test` / 相关 CLI 冒烟

### Task A2: requires 预检（catalog 与现编）

**Files:** `src/dag/plan.c`、`src/dag/dag_dir.c`（或 materialize 路径）、DAG 文件元数据

- [x] 对将执行/写出的 DAG：读取 `requires[]`（若有），检查矩阵中能力均存在且 enabled
- [x] 缺失时：plan 校验失败；run 不执行并打印缺哪些名
- [x] 单测或 CLI：故意缺 requires → 失败
- [x] 文档：`docs/dag.md` 一小节说明 requires 预检

### Task A3: 鼓励 catalog、限制胡编（软→半硬）

**Files:** `src/dag/plan.c`、planner prompt 字符串

- [x] 配置项或环境变量（择一，默认偏安全）：例如 `plan.prefer_catalog_only` / `NEO_PLAN_CATALOG_ONLY=1` 时，禁止输出完整 `dags` 数组（仅允许 `use`）
- [x] 默认保持现行为（可现编），但 `-v` 时统计「use vs invent」
- [x] 手册补充开关说明
- [x] Commit（无 agent trailer）

---

## 轨 B — 统一会话叙事（P0）

### Task B1: 产品决策写死（文档先行）

**Files:** `docs/manual.md`、`docs/examples.md`、`example/usage/daemon.md`、`README*.md`

- [x] 明确两种模式对照表：`-S` 落盘 vs daemon 内存（今日行为）
- [x] 写清「推荐路径」：多轮续聊优先 `-S` / `-D` 是否即将支持挂载（见 B2）
- [x] 若 B2 延期：文档写死「daemon 不写 `-S`」，避免模型/用户再猜

### Task B2: daemon 可选挂载具名会话（实现）

**Files:** `src/core/daemon.c`、`src/cli/main.c`、`src/core/neo_session.*`

- [x] CLI：`neo -D -S id` 或 `neo daemon -S id`：启动时 `neo_session_load` 注入内存历史；每轮结束后 `neo_session_append_turn` 写回（与 chat 同 max_turns）
- [x] 无 `-S`：保持今日纯内存行为
- [x] `-S a,b`：与 chat 一致（加载多段、只写第一个）；若过复杂可 v1 仅支持单 id
- [x] 交互提示符旁可显示 session id（stderr）
- [x] CLI 冒烟：`printf 'exit\n' | ./neo -D -S ...` 不炸；有写入则检查 json
- [x] 更新手册；Commit

---

## 轨 C — 结构化可观测（P1）

### Task C1: 事件 JSONL 最小集

**Files:** 新建 `src/core/neo_events.h` / `.c`（或等价薄封装）、`main.c` / `dag.c` / `agent_tools.c` 挂钩

- [x] 环境变量或 `-v` 扩展：例如 `NEO_EVENTS=1` 时向 stderr（或 `.neo/events.jsonl`）写一行一事件
- [x] 最小事件：`session_start` / `tool_call` / `tool_result` / `dag_step` / `llm_done` / `error`（字段：ts、name、ok、ms、简短 detail）
- [x] 默认关闭，避免吵；手册一节
- [x] 冒烟：开开关跑 `dag run show_time` 可见至少 1 条 JSON
- [x] Commit

---

## 轨 D — 专业软件套餐：Blender（P1）

### Task D1: 意图层能力清单（已有 showcase/cup/watermelon 之上）

**Files:** `capabilities/local/`、`dags/baseline/`、`scripts/tools/`

- [x] 盘点已有：`blender_showcase` / `blender_cup` / `blender_watermelon`
- [x] 新增 1～2 个高信号套餐（择需）：例如 `blender_product_turntable`（转盘静帧）或 `blender_export_glb`（导出）
- [x] 每个能力：`when` / `when_not` / `outcome` 齐全；DAG `requires` 对齐
- [x] README 目录表更新；本机有 Blender 时 `neo dag run ...` 出图
- [x] Commit

### Task D2: Blender MCP 接入样例（可选，不默认开）

**Files:** `config/config.json5.example` 注释块、`docs/manual.md` 或 `example/usage/`

- [x] 文档化 stdio MCP 登记片段（官方/社区 bridge）；注明须本机 Blender + add-on
- [x] Policy：提示裁剪 `execute_code` 类高危 tool（若 bridge 暴露）
- [x] 不强制 CI 依赖 Blender
- [x] Commit

---

## 轨 E — 卫生与模板（P2，可穿插）

### Task E1: unix 白名单模板

**Files:** `capabilities/unix/`（如 `enabled.json5` + `enabled.readonly.json5.example`）

- [x] 提供「只读诊断」「含 curl」「全量（不推荐）」三份样例或文档表
- [x] README 指向命令数上限 256 与危险命令清单
- [x] Commit

### Task E2: architecture §8 路径对齐

**Files:** `docs/architecture.md`

- [x] 对照表改为现行 `dag.c` / 模块路径；去掉过时 `workflow.c` 等
- [x] Commit

---

## 建议实施顺序

1. **A1 → A2**（plan 可解释 + requires）  
2. **B1 → B2**（先文档后 daemon `-S`）  
3. **C1**（事件）  
4. **D1**（Blender 套餐）；D2 按需  
5. **E1 / E2** 穿插  

每完成一轨：更新 [`docs/evaluations/`](../../evaluations/) 可另开一篇日期评价，或在本计划顶部勾选进度。

## 验收总览

| 轨 | 验收 |
|----|------|
| A | 错误 `use` / 缺 `requires` 必失败且可读；可选 catalog-only 开关可用 |
| B | 手册无歧义；`-D -S id` 多轮写回落盘（若实现 B2） |
| C | `NEO_EVENTS=1` 可见 JSONL 事件 |
| D | 至少再增一个可 `dag run` 的 Blender 套餐或 MCP 文档样例 |
| E | unix 模板与架构 §8 无过时路径 |

## 执行方式（落地时）

1. **Subagent-Driven** — 每 Task 新开子代理，任务间复查  
2. **Inline** — 本会话按勾选逐步做  

选定后从 **Task A1** 开始；未勾选完成前不要宣称整轨完成。
