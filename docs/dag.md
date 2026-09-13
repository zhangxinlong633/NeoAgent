# Neo 确定性 DAG Workflow

编排路径主张：**拓扑在配置（JSON5）里声明，LLM 只当 Worker**（不参与「下一步走哪」）。  
自然语言 `./neo "..."` 仍可自由对话；`neo dag run`（或 `neo run <已知名>`）走确定性图。

不引入 Temporal / Airflow；引擎就是 `neo` 进程内的小 DAG runner。  
配置迁移见 [migrate-json.md](migrate-json.md)。使用手册见 [manual.md](manual.md)；命令样例见 [examples.md](examples.md)。

## 节点类型

| type | 角色 |
|------|------|
| `tool` | 确定性 / 本地命令算子 |
| `llm` | LLM Worker（步骤字段 `"tools": "on"|"off"`） |
| `loop` | 按 `over`×`max` 重复（兼容旧写法） |
| `route` | **确定性**路由：多路 `cases`，或二路 `match` → `then` / `else` |

## DAG：`depends_on`

存在任一 `depends_on` 或 `type: route` 时进入 **DAG 模式**（拓扑排序执行）。  
否则保持旧的线性 + loop 行为。

```json5
{
  dags: [
    {
      name: "diamond",
      steps: [
        { id: "a", type: "tool", tool: "count_run" },
        { id: "b", type: "tool", tool: "count_run", depends_on: ["a"] },
        { id: "c", type: "tool", tool: "count_run", depends_on: ["a"] },
        { id: "d", type: "tool", tool: "count_run", depends_on: ["b", "c"] },
      ],
    },
  ],
}
```

就绪节点按配置顺序**串行**执行（轻量；依赖正确即可）。

## 确定性 route

二路（兼容旧写法）：

```json5
{
  id: "branch",
  type: "route",
  depends_on: ["classify"],
  on: "{{steps.classify}}",
  match: "pdf",
  then: ["as_pdf"],
  else: ["as_text"],
}
```

多路 `cases`（有序，**首个**子串命中胜出；无 `match` 或空串为默认臂，最多一个）。当 `cases` 非空时，**忽略**顶层 `match` / `then` / `else`：

```json5
{
  id: "branch",
  type: "route",
  depends_on: ["classify"],
  on: "{{steps.classify}}",
  cases: [
    { match: "pdf", then: ["as_pdf"] },
    { match: "docx", then: ["as_docx"] },
    { then: ["as_other"] },
  ],
}
```

未选中分支会 `skip`；**仅当某步的全部 `depends_on` 都被 skip 时**，该步才连带 skip（route 之后的汇合/总结步在另一侧分支被 skip 时仍可执行）。

## tool 步可选 retry

仅 `type: tool` 可写 `retry.max`（首次失败后的额外尝试次数，钳制到 **0..3**；非 tool 配置报错）：

```json5
{
  id: "flaky",
  type: "tool",
  tool: "flaky_count",
  retry: { max: 1 },
  args: {},
}
```

## tool 失败自动 LLM 热线（可选）

默认**关闭**。打开后：本地 `retry.max` 耗尽仍失败时，引擎自动问一次模型（无 tools）只答 `RETRY` / `ABORT`；`RETRY` 则用**同一 args** 再跑该 tool。模型不改图、不改参数、不选下一步。

```json5
dag: {
  on_tool_fail: {
    llm: true,
    max_calls: 1, // 每步最多问几次，0..2
  },
},
```

与顶层 `dags: [...]` / `dag_directory` 不同：`dag` 是运行时策略对象。

## 运行

```bash
./neo dag run show_time
./neo dag run repo_pulse
./neo dag run workspace_brief
./neo dag run list_overview
./neo -p demo dag run demo_loop
./neo -v dag run show_time   # stderr：步骤 type / tool / status
./scripts/neo-ask --dag diamond
```

常用 catalog 图（需 `dag_directory: "dags"`）：`show_time`、`repo_pulse`、**`workspace_brief`**、`list_overview`、`inspect_path`、`search_context`、`append_memo`。详见 [`../dags/README.md`](../dags/README.md)。

### 可观测（stderr）

| 开关 | 行为 |
|------|------|
| 默认 | 整图结束 `neo: run done … status=ok`；**任一步失败**另打 `neo: step end … type=… status=fail` |
| `-v` / `--verbose` | 另打每步 `step start` / `step end`，含 `type=tool tool=…` 或 `type=llm tools=on\|off` |

约定：`type:tool` 走 Capability Matrix 本地能力；`type:llm` 才调用远端模型（完整「计算漂移」仍见架构远期路线图）。

## Plan then Run

| 命令 | 行为 |
|------|------|
| `neo run "task"` | 规划 → 校验 → **执行**（stdout 只有执行结果） |
| `neo plan "task"` | 规划 → 校验（stdout 为 `use` 或 dags **JSON**） |
| `neo dag run NAME` | 跑配置 / `dag_directory` 里已声明的图 |
| `neo run NAME` | 若 NAME 为已加载图名则直接跑图；否则 plan+execute |

### DAG 目录（一图一文件）

```json5
dag_directory: "dags",
```

```
dags/
  manifest.json5          # load: ["baseline", "workspace"]；不递归
  baseline/*.json5        # 基础层多步编排示范
  workspace/*.json5       # 工作区旁路助手
  edge/                   # 预留；默认不在 load 中
  proposed/               # 预留；默认不加载
```

每个 DAG 文件建议写：`description`、`when`、`when_not`、`requires`、`outcome`（见 AGENTS.md §4.1）。这些字段会出现在 planner 的 catalog listing。场景分层见 [`applications.md`](applications.md) 与 [`dags/README.md`](../dags/README.md)。

### `requires` 预检

`requires` 不只是给 planner 看的元数据。在以下路径会**硬校验**：

| 路径 | 行为 |
|------|------|
| `neo dag run` / `dag_run` | 执行前：每个名须在 Capability Matrix 中存在且 enabled |
| `neo plan` / `neo run` 选型 `{"use":[...]}` | 选中 catalog 图后同样预检（即使只 plan 不执行） |
| `neo plan` / `neo run` 现编 `dags` | `plan_materialize_config` 加载后对图内 `requires` 预检 |

缺失时 stderr 形如：

```text
neo run: DAG 'foo' missing required capability 'bar' (not in Capability Matrix or not enabled)
```

进程非 0 退出，不执行该图。无 `requires` 或空数组则跳过。能力名须与矩阵行 `name` 完全一致（builtin / command / `mcp_*`）。

Planner **优先**输出 `{"use":["catalog_name"]}` 选用目录中的图；没有合适的再现编 `{"dags":[...]}`。执行期仍是确定性 runner。

由 LLM **一次性**选型或生成冻结的 DAG；执行期不重规划。

### 默认：按任务类型规划

Planner 先区分任务类型（prompt 约定，软目标默认仍为 **10**）：

| 类型 | 典型问法 | DAG |
|------|----------|-----|
| **知识 / 问答** | 说明、对比、解释、定义 | 约 **4** 步编辑流水线（全 `llm` + `tools: "off"`）：理解 → 起草 → 自检 → 终稿；禁止虚构写文件/实现。`--steps 1` 时才单步 |
| **工程** | 实现、改代码、多工具流水线 | 约 N 步「研发团队」流水线（可用 `--steps` 缩放） |

知识类角色：

1. **understand** — 复述问题与对比维度  
2. **draft** — 写出完整回答  
3. **self-check** — 挑错；输出 `PASS`/`FAIL`（可选 `route` 回 draft 至多 1 次）  
4. **summarize** — 面向用户的终稿（stdout）

工程流水线角色（合并/拆分以贴合 N，保留阶段顺序）：

1. 理解业务需求  
2–3. 分解 / 设计  
4–7. 实现  
8–9. 验证（失败可用 `route`/`loop` **最多回跳 2 次**到实现或分解）  
10. 总结输出  

验证信号建议用子串如 `PASS` / `FAIL` 供 `route` 匹配。不整图重 plan。

软目标步数（工程按 N 缩放；知识默认 ~4，勿用假工程角色注水到 10）：

```bash
./neo run "对比一下 VxWorks 与 Linux"          # 知识：约 4 步编辑流
./neo run --steps 1 "对比一下 VxWorks 与 Linux" # 强制单步
./neo run --steps 12 "把任务拆成多步流水线"   # 工程：放大团队 DAG
./neo plan --steps 3 "尽量短的团队 DAG"
```

配置可选：

```json5
{ plan: { target_steps: 10, catalog_only: false } }
```

优先级：`--steps` > `plan.target_steps` > 默认 10（上限 32）。

### catalog-only（半硬）

默认允许「catalog `use` 优先，否则现编 `dags`」。若希望强制只选型：

| 开关 | 效果 |
|------|------|
| `plan.catalog_only: true` | 仅允许 `{"use":[...]}`；现编 / 能力名降级 invent 均失败 |
| `NEO_PLAN_CATALOG_ONLY=1` | 同左（覆盖配置）；`=0` 强制关闭 |

`-v` / `--verbose` 时 stderr 增加：`plan_path=use` 或 `plan_path=invent`（及当前 `catalog_only` 标志）。

```bash
./neo run "用 echo_args 打出 hello，再让 llm 总结一句"
./neo plan -o /tmp/planned.json5 "列出当前目录再写一句说明"
./neo run -o /tmp/planned.json5 "调用 allowlist 里的脚本做两步流水线"
```

Planner 只能引用配置里已声明的 `tools.commands` 以及 builtin `read_file` / `write_file` / `list_dir`。

（旧写法 `neo plan --run` 已移除，请改用 `neo run`。）

## cron

```text
0 * * * * cd /path/to/neoclaw && ./scripts/neo-ask -p demo --dag demo_loop >>/tmp/neo-cron.log 2>&1
```
