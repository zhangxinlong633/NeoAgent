# Neo 使用样例

| 属性 | 内容 |
|------|------|
| 文档版本 | 1.2 |
| 发布日期 | 2026-09-05 |
| 文档状态 | 正式说明 |
| 前置条件 | 已在仓库根执行 `make`；已配置 `config/config.json5`（可自 `config/config.json5.example` 复制） |
| 精简入口 | 仓库根 [`README.md`](../README.md) / [`README_zh.md`](../README_zh.md) 上手样例 |

### 文档目的

汇集可复现的命令行样例，覆盖对话、能力矩阵、确定性 DAG、规划执行与常见排错。字段与行为细则见 [`tool.md`](tool.md)、[`workflow.md`](workflow.md)、[`claw.md`](claw.md)。

下文命令均假定**当前工作目录为仓库根**。模型回复因网关与模型而异；带 `neo tool:` 的 stderr 行可用来判断是否走到本地能力。

---

## 1. 最小配置核对

建议配置至少包含：

```json5
{
  model: {
    base_url: "https://api.deepseek.com/v1", // 或其它 OpenAI 兼容端点
    name: "deepseek-chat",
    api_key: "YOUR_KEY",
  },
  capability_matrix: {
    enabled: true,
    root: ".",
    directory: "capabilities",
    shell_enabled: false,
  },
  dag_directory: "dags",
}
```

| 项 | 作用 |
|----|------|
| `capability_matrix.directory` | 加载 `capabilities/{local,git,unix}/` 等能力包 |
| `dag_directory` | 加载 `dags/{baseline,workspace}/` 的 DAG catalog |
| `unix/enabled.json5` | 控制 Unix 工具集实际装入矩阵的子集 |

未设置 `dag_directory` 时，`neo dag run` / `neo run`（名命中图）的 catalog 选型不可用。

---

## 1.1 开箱组合（默认包）

复制 `config/config.json5.example` 且不改 `directory` / `dag_directory` / 各 `manifest.load` 时，默认组合如下。细则见 [`../capabilities/README.md`](../capabilities/README.md)、[`../dags/README.md`](../dags/README.md)。

| 场景 | capabilities `load` | dags `load` | 推荐试跑 | Policy 注意 |
|------|---------------------|-------------|----------|-------------|
| 默认旁路助手 | `local` + `git` + `unix`（仅 `enabled.json5`） | `baseline` + `workspace` | `show_time`、`repo_pulse`、**`workspace_brief`** | `shell_enabled: false`；Unix 危险命令默认不在白名单 |
| 行业 / 边缘预留 | `industry/` 存在但不在 `load` | `edge/` 同理 | — | 勿当作现行交付 |

旗舰 SOP（列目录 → LLM 简报 → 落盘）：

```bash
./neo dag run workspace_brief
```

成功后工作区根会出现或追加 `WORKSPACE_BRIEF.md`（可复查；与 `MEMORY.md` 分离）。

---

## 2. 单次对话

### 2.1 纯问答（可不依赖工具）

```bash
./neo "用三句话介绍 Neo 的产品三角"
```

### 2.2 指定配置与模型

```bash
./neo -c config/config.json5 -m deepseek-chat "南京有哪些必去的地方？"
```

答法以当前 `rules/`（如 `identity.md`、`response-playbook.md`）与矩阵能力为准。见 [`claw.md`](claw.md)。查实时天气用矩阵能力 `weather_wttr`（见 §3）。

### 2.3 临时关闭工具

即使配置启用了矩阵，也可单次关闭：

```bash
NEO_DISABLE_TOOLS=1 ./neo "不要调用任何工具，只解释 capability_matrix 是什么"
```

### 2.4 查看完整 system prompt（调试）

```bash
./neo -d "你是谁" 2>&1 | less
```

`-d` / `--debug` 会在 stderr 打印装配后的 system prompt（含 tools listing 等）。

---

## 3. 能力矩阵：让模型调用本地工具

前置：`capability_matrix.enabled: true`，且未设置 `NEO_DISABLE_TOOLS=1`。

### 3.1 读文件并概括

```bash
./neo "请使用 read_file，path 为 README.md；根据返回内容用三句中文概括。" 2>&1
```

期望 stderr 出现类似：

```text
neo tool: read_file
```

### 3.2 列目录

```bash
./neo "调用 list_dir，path 为 docs，列出主要文档文件名并各用半句话说明用途。" 2>&1
```

### 3.3 本机时间（目录能力 `date_iso`）

```bash
./neo "调用 date_iso，告诉我现在的 UTC 时间。" 2>&1
```

### 3.3.1 城市天气（目录能力 `weather_wttr`）

```bash
./neo "看下南京今天的天气"
# 或显式：./neo "调用 weather_wttr，city 为 南京"
```

依赖本机 `curl` 与外网访问 wttr.in；城市名经脚本校验，不拼 shell。

### 3.4 Unix 白名单能力（需 `unix` 在 `manifest.load` 且列入 `enabled.json5`）

```bash
./neo "调用 unix_wc，参数 argv 为 [\"-l\",\"README.md\"]，报告行数。" 2>&1
```

本机诊断（白名单已含）：`unix_ps`（进程/`%cpu`）、`unix_sysctl_hw`（如 `hw.ncpu`）、`unix_vm_stat`、`unix_ping`（连通性）。HTTPS 探测用 builtin `http_get`（需配置 `http_fetch_enabled` + `http_allow_hosts`）。

危险命令（如 `unix_rm`）默认不在白名单，调用应失败或不可见。调整见 [`capabilities/unix/README.md`](../capabilities/unix/README.md)。

### 3.5 具名会话（跨进程上下文）

普通聊天默认写入会话 `default`（`.neo/sessions/default.json`），无需 `-S` 即可续聊。

```bash
./neo "飞船怎么做"
./neo "展开第 4 种：真正的载人/货运飞船"
./neo -N "换个话题从头聊"   # 将 default 归档为 YYYYMMDD-HHMMSS，再写新的 default
./neo --session-list
./neo -S ship "只用名为 ship 的会话"
./neo -S demo,other "结合两个会话继续"
./neo --session-clear ship
```

`-S` / `--session ID[,ID...]` 按序加载多个会话历史并注入本次请求；**本轮只写回第一个 ID**。省略 `-S` 时等价于 `-S default`。id 仅 `[A-Za-z0-9_-]`，最多 8 个、不可重复。`-N` / `--session-new` 与 `-S` 不能同用。`--session-list` 列出已有会话；`--session-clear` 仅清除单个 id。轮数上限为 `session.max_turns`（默认 10）。与 `neo daemon` 内存会话独立。

同会话切换角色（配置顶层 `roles`，见 `config/config.json5.example`）：

```bash
./neo -S demo --role researcher "先调研"
./neo -S demo --role writer "写成短文"
```

`--role NAME` 向 system 注入 `## Role`；落盘时 assistant 前缀 `[NAME] `。无 `--role` 时行为不变。

### 3.6 终端 Markdown 与提示词评测

```bash
./neo "用表格对比 DAG 与 Capability Matrix 的职责"
./neo --no-render "同上，原始 Markdown"
make test-prompts-dry
```

助手回复**默认**经 md4c 渲染到终端；`--no-render` 关闭。`-R` / `--render` 仍可显式打开（与默认相同）。领域提示词评测见 [`tests/prompts/README.md`](../tests/prompts/README.md)（**不**进默认 `make test`）。

### 3.7 机读输出（OpenAI chat.completion JSON / 文件）

便于管道与其它程序对接（诊断仍走 stderr）。`-j` 输出与 OpenAI Chat Completions 响应同形：

```bash
./neo -j "用一句话介绍 Neo"
# → stdout:
# {
#   "id": "chatcmpl-neo-…",
#   "object": "chat.completion",
#   "created": 1690000000,
#   "model": "…",
#   "choices": [{"index":0,"message":{"role":"assistant","content":"…"},"finish_reason":"stop"}],
#   "usage": {"prompt_tokens":0,"completion_tokens":0,"total_tokens":0},
#   "neo": {"session":"default","role":null}
# }

./neo -o /tmp/neo-reply.txt "只把原文写入文件"
./neo -j -o /tmp/neo-reply.json "chat.completion JSON 写入文件"
```

取正文：`choices[0].message.content`（与官方 SDK 一致）。`neo.session` / `neo.role` 为扩展字段。失败时为 `{"error":{"message","type","code","param"}}`。

| 组合 | 行为 |
|------|------|
| （默认） | 终端渲染到 stdout |
| `-j` / `--json` | stdout：OpenAI `chat.completion` JSON |
| `-o FILE` | 原文写入 FILE，不打印到 stdout |
| `-j -o FILE` | `chat.completion` JSON 写入 FILE |
| `plan`/`run` 的 `-o` | 仍表示保存规划出的 dags JSON |

## 4. 确定性 DAG：`dag run`

> `neo dag run` 已移除；请用 `neo dag run`。已知图名也可用 `neo run <name>`（不经 planner）。

不经过规划器，直接执行已声明的图。

### 4.1 查看系统时间（`show_time`）

```bash
./neo dag run show_time
```

典型输出（stdout）：

```text
2026-09-05T13:55:20Z
```

stderr 可含：

```text
neo tool: date_iso
neo: run done workflow=show_time status=ok
```

### 4.2 仓库脉搏（`repo_pulse`）

```bash
./neo dag run repo_pulse
```

步骤：`git_status_short` → `git_log_five` → LLM 一段话汇总。需本机有 `git`，且矩阵中已加载对应能力。

### 4.3 工作区旗舰 SOP（`workspace_brief`）

```bash
./neo dag run workspace_brief
```

步骤：`list_dir` → LLM 简报 → `append_file` 写入 `WORKSPACE_BRIEF.md`。适合演示「取数 → 整理 → 落盘」；不要用它改 `MEMORY.md`（长期记忆用 `append_memo`）。

### 4.4 工作区概览（`list_overview`）

```bash
./neo dag run list_overview
```

步骤：`list_dir` → LLM 说明顶层布局（不落盘）。

### 4.5 单文件检视（`inspect_path`）

默认图内路径为 `README.md`：

```bash
./neo dag run inspect_path
```

### 4.6 详细步骤日志

```bash
./neo -v dag run show_time
```

---

## 5. 规划后执行：`neo plan` / `neo run`

### 5.1 仅规划（查看 JSON）

```bash
./neo plan "看下系统时间"
```

可能输出之一（选型 catalog）：

```json
{
  "use": ["show_time"]
}
```

或现编单步 tool 图：

```json
{
  "dags": [
    {
      "name": "planned",
      "steps": [
        { "id": "time", "type": "tool", "tool": "date_iso" }
      ]
    }
  ]
}
```

### 5.2 规划并执行

```bash
./neo run "看下系统时间"
```

stdout 一般为时间戳或最终用户可见结果；规划细节在 stderr。

### 5.3 知识类问答（多步 LLM 流水线）

```bash
./neo run "对比 Capability Matrix 与 DAG 各自管什么，用条目列出"
```

Planner 倾向约 4 步编辑流水线（理解 → 起草 → 自检 → 终稿），步骤内通常 `tools: "off"`。

### 5.4 限制规划软步数

```bash
./neo run --steps 1 "用一句话说明 Policy 是什么"
./neo plan --steps 4 "解释 neo plan 与 neo dag run 的区别"
```

### 5.5 保存规划结果

```bash
./neo plan -o /tmp/neo-plan.json5 "检查一下当前仓库是否干净"
```

---

## 6. 多轮会话（daemon）

### 6.1 交互式 stdin

```bash
./neo daemon
```

输入问题后回车；`exit` 或 EOF 结束。轮次上限见 `session.max_turns`。

### 6.2 Unix socket

```bash
./neo daemon --socket /tmp/neo.sock
# 另一终端：
echo "现在几点（UTC）？请用工具" | nc -U /tmp/neo.sock
```

---

## 7. Profile 与脚本入口

### 7.1 Profile

```bash
./neo -p demo dag run demo_loop
```

会切换到 `config/profiles/demo/` 下的配置与工作目录（以该 profile 的 README / `neo.json5` 为准）。

### 7.2 管道 / cron 辅助

```bash
./scripts/neo-ask -p demo --dag demo_loop
```

见 [`scripts/`](../scripts/) 目录说明。

---

## 8. 常见问题（样例向）

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| `unknown catalog DAG 'date_iso'` | 模型把**能力名**写进了 `{"use":[...]}` | 已支持降级为 ad-hoc tool 图；更稳妥是配置 `dag_directory` 并使用 `show_time` 等 catalog 名。`use` **只能**是 DAG 名 |
| `unknown DAG 'show_time'` | 未配置 `dag_directory: "dags"` | 写入配置后重启命令；在仓库根执行 |
| 模型从不调用工具 | 矩阵未启用，或 `NEO_DISABLE_TOOLS=1` | 检查 `capability_matrix.enabled` |
| `unix_rm` 不可用 | 未列入 `capabilities/unix/enabled.json5` | 故意默认禁用；勿轻易加入白名单 |
| Unix 参数被拒绝 | `unix-exec` 禁止绝对路径与 `..` | `argv` 仅用相对 `capability_matrix.root` 的路径 |

---

## 9. 推荐练习路径

1. `./neo "你是谁"` — 确认模型与 claw 注入  
2. `./neo dag run show_time` — 确认矩阵 + DAG 目录  
3. `./neo run "看下系统时间"` — 确认 planner 选型  
4. `./neo "read_file README.md 并概括"` — 确认反应式 tool loop  
5. `./neo dag run repo_pulse` — 确认多步 tool→LLM  

---

## 附录　相关文档

| 文档 | 用途 |
|------|------|
| [`README.md`](../README.md) / [`README_zh.md`](../README_zh.md) | 产品概述与快速开始（英 / 中） |
| [`tool.md`](tool.md) | 能力矩阵、commands、Unix 包、MCP |
| [`workflow.md`](workflow.md) | DAG、plan/run |
| [`applications.md`](applications.md) | 场景与生态位 |
| [`architecture.md`](architecture.md) | 目标架构 |
| [`capabilities/README.md`](../capabilities/README.md) | 能力目录契约 |
| [`dags/README.md`](../dags/README.md) | DAG 目录契约 |

---

*样例随仓库能力包演进；若命令失败，优先核对配置键名与是否在仓库根执行。*
