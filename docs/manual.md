# Neo Agent 使用手册

面向使用者的完整操作说明。安装与命令以本仓库现行实现为准；设计愿景见 [`applications.md`](applications.md) / [`architecture.md`](architecture.md)。

- 可复制短命令：[`../example/usage/`](../example/usage/)
- 更长样例与排错：[`examples.md`](examples.md)
- 矩阵 / DAG / Claw 细则：[`tool.md`](tool.md)、[`dag.md`](dag.md)、[`claw.md`](claw.md)

---

## 1. 它是什么

Neo Agent 是面向**明确目标**的可移植 Agent 运行时：在可控边界内完成核查、整理、SOP 运维、仓库旁路执行等一类工作。不是闲聊机器人，也不是最强编码 IDE。

产品三角：

1. **DAG** — 编排怎么走（确定性拓扑）
2. **Capability Matrix** — 能调用什么（白名单能力表）
3. **Policy** — 许不许、贵不贵（shell / HTTPS / 路径沙箱 / 轮次上限等）

LLM 做决策与规划；Memory 在本机留存与召回。当前交付以本机 CLI 为主。

---

## 2. 安装与配置

### 2.1 构建

需要 macOS 或 Linux，以及可达的 OpenAI 兼容大模型 API。

```bash
cd /path/to/neoclaw   # 或 NeoAgent 仓库根
make
cp config/config.json5.example config/config.json5
# 填写 model.api_key / model.name，或 export NEO_API_KEY=...
./neo "你是谁"
```

测试：`make test`（不含可选的 `make test-prompts*`）。

### 2.2 配置要点

配置为 **JSON5 only**（见 [`migrate-json.md`](migrate-json.md)）。默认路径：`config/config.json5`，也可用 `-c` / `NEO_CONFIG`。

常用顶层键：

1. `model` — `base_url` / `name` / `api_key` / `max_tokens` / `temperature`
2. `capability_matrix` — 能力矩阵（勿再写新配置的顶层 `tools`）
3. `dag_directory` — DAG catalog，如 `"dags"`
4. `session.max_turns` — 会话保留轮数（默认 10）
5. `roles` — 可选，供 `--role` 使用
6. `soul` / `bootstrap` / `rules` / `memory` — Claw 拼装（见 [`claw.md`](claw.md)）

开箱能力包与 DAG 包说明见 [`examples.md`](examples.md)「开箱组合」。

---

## 3. 命令行总览

```text
./neo [OPTIONS] "your message"
./neo [OPTIONS] daemon|-D|--daemon [--socket PATH]
./neo [OPTIONS] dag run NAME
./neo [OPTIONS] plan [--steps N] [-o FILE] "task"
./neo [OPTIONS] run NAME|"task" [--steps N] [-o FILE]
./neo [OPTIONS] memory recall|store "…"
```

常用选项：

1. `-c` / `--config` — 配置文件
2. `-p` / `--profile` — 切换到 `config/profiles/NAME/`
3. `-m` / `--model` — 覆盖模型名
4. `-v` / `-d` — 详细 / 调试（stderr）；**`-D` 是 daemon**，不是 debug
5. `-R` / `--render` — Markdown 渲染（**默认已开**）
6. `--no-render` — 输出原文
7. `-S` / `--session` — 会话 id（可 `a,b` 混合）；省略则用 `default`
8. `-N` / `--session-new` — 归档 `default` 后新开
9. `--session-list` / `--session-clear`
10. `--role NAME` — 本轮角色（需配置 `roles`）
11. `-j` / `--json` — OpenAI `chat.completion` JSON
12. `-o` / `--output` — 聊天写回复/JSON；`plan`/`run` 写规划 DAG
13. `-D` / `--daemon` — 同子命令 `daemon`（stdin 多轮）

诊断与进度在 **stderr**；程序对接请用 stdout 或 `-o` 文件。

---

## 4. 对话与会话

Neo 有两套多轮机制。**默认彼此独立**；可选在 daemon 上挂载 `-S` 落盘会话（见下表与 §4.5）。

| | 具名会话 `-S`（chat） | Daemon `-D`（无 `-S`） | Daemon `-D -S id` |
|--|---------------------|------------------------|-------------------|
| 入口 | `./neo [-S id] "…"`（省略 ≡ `-S default`） | `./neo -D` | `./neo -D -S id`（或 `-S a,b`） |
| 存储 | 落盘 `.neo/sessions/<id>.json` | **仅进程内存**；退出即丢 | 启动灌入内存；每轮写回 **第一个** id |
| 跨进程续聊 | 可以 | 不可以 | 可以（经落盘文件） |
| 轮次上限 | `session.max_turns`（默认 10） | 同左（内存 trim） | 同左（内存 + 落盘 trim） |
| 典型场景 | 隔天续聊、脚本多次调用 | 本机 REPL、不关心落盘 | 交互 REPL **且**要可恢复 |

**推荐路径**

1. **脚本 / 非交互可恢复多轮** → `./neo -S id "…"`（或默认 `default`）。
2. **本机 REPL、不在乎落盘** → `./neo -D`。
3. **本机 REPL 且要写回落盘** → `./neo -D -S id`（多 id 时与 chat 相同：按序加载，只写第一个）。

`-N` / `--session-list` / `--session-clear` 不能与 daemon 同用。

### 4.1 单次与续聊

```bash
./neo "用三句话介绍产品三角"
./neo "根据上面继续展开第二点"    # 默认写入会话 default
```

历史文件：`.neo/sessions/default.json`。

### 4.2 新开默认会话

```bash
./neo -N "换个话题"              # 将 default 归档为 YYYYMMDD-HHMMSS，再聊
./neo -N                         # 只归档
```

### 4.3 具名与混合

```bash
./neo -S ship "飞船怎么做"
./neo -S ship,cook "结合两个会话继续"   # 按序注入；本轮只写回第一个 id
./neo --session-list
./neo --session-clear ship
```

### 4.4 同会话角色

配置示例：

```json5
roles: {
  researcher: { prompt: "本轮只做调研，不写终稿。" },
  writer: { prompt: "本轮只写终稿，沿用历史调研。" },
}
```

```bash
./neo -S demo --role researcher "先调研…"
./neo -S demo --role writer "写成短文"
```

落盘 assistant 会带 `[researcher] ` 等前缀。细则设计见 `docs/superpowers/specs/2026-09-12-session-roles-design.md`。

### 4.5 Daemon 多轮

```bash
./neo daemon
./neo -D                    # 同 daemon（注意：-d 是 debug）
./neo -D -S ship            # 挂载落盘会话 ship：启动加载，每轮写回
./neo -D -S a,b             # 按序注入 a+b 历史，只写回 a
./neo --daemon --socket /tmp/neo.sock
./neo --daemon -S ship --socket /tmp/neo.sock
```

无 `-S` 时**仅内存轮次**，进程结束历史消失。带 `-S` 时 stderr 会打印 `neo daemon: session=…`；交互提示符为 `User[id]>`。管道 / `--socket` 仍为无前缀原文。交互输入会打开终端 `IUTF8`，退格按 UTF-8 字符删除（而非按字节）。

---

## 5. 机读输出（对接程序）

### 5.1 OpenAI `chat.completion` JSON

```bash
./neo -j "用一句话介绍 Neo"
./neo -j "…" | jq -r '.choices[0].message.content'
```

成功时包含：`id`、`object`（`chat.completion`）、`created`、`model`、`choices[0].message.content`、`finish_reason`、`usage`（当前为占位 0）、扩展字段 `neo.session` / `neo.role`。

失败时：

```json
{ "error": { "message": "…", "type": "neo_error", "code": "llm_request_failed", "param": null } }
```

进程 exit 非 0。

### 5.2 写文件

```bash
./neo -o /tmp/reply.txt "原文写入文件，不刷终端"
./neo -j -o /tmp/reply.json "chat.completion JSON 写入文件"
```

`neo plan -o FILE` 仍表示保存**规划 DAG**，与聊天模式按子命令区分。

### 5.3 结构化运行事件（JSONL）

默认关闭。设置 `NEO_EVENTS=1`（或 `true`/`yes`/`on`）后，向 **stderr** 写一行一事件；若设 `NEO_EVENTS_PATH=文件` 则追加到该文件。

```bash
NEO_EVENTS=1 ./neo dag run show_time
NEO_EVENTS=1 NEO_EVENTS_PATH=.neo/events.jsonl ./neo dag run cli_count
```

| 字段 | 含义 |
|------|------|
| `v` | schema 版本（当前 **1**；破坏性改字段再 bump） |
| `ts` | unix 秒 |
| `run_id` | 本进程内关联 id（首次 emit 懒生成，不跨进程） |
| `name` | 事件名 |
| `ok` | `0` / `1` |
| `ms` | 耗时毫秒（未知可为 0） |
| `detail` | 短摘要（截断并 JSON 转义） |

事件名：`session_start`、`tool_call`、`tool_result`、`dag_step`、`llm_done`、`error`。`-v` 仍是人类可读步骤日志，可与事件流同时开。

---

## 6. 能力矩阵（能调用什么）

启用 `capability_matrix` 后，对话 tool loop、`dag` 的 `type: tool`、`neo plan` 允许名共用一张表。

1. **Builtin** — 如 `read_file`、`write_file`、`grep`、`http_get`（需打开 Policy）
2. **commands / 能力目录** — `capabilities/` 下 JSON5 + 脚本（如 `train_query`、`weather_wttr`）
3. **MCP stdio** — `mcp_servers` → 对外名 `mcp_<server>_<tool>`

Policy 示例：`shell_enabled` 默认关；`http_fetch_enabled` + `http_allow_hosts` 才暴露 `http_get`；路径相对 `capability_matrix.root`。

权威说明：[`tool.md`](tool.md)。目录契约：[`../capabilities/README.md`](../capabilities/README.md)。

高铁查询示例（站名可用中文）：

```bash
./neo "看一下明天南京到北京的高铁信息"
```

Blender：**优先** headless 套餐（`./neo dag run blender_cup` 等，见 `capabilities/local/`）。可选交互式 MCP 登记样例（须本机 Blender + addon，**不默认开**，含 `execute_code` 风险说明）见 [`example/usage/blender-mcp.md`](../example/usage/blender-mcp.md)。

---

## 7. DAG 与规划（怎么编排）

```bash
./neo dag run show_time
./neo dag run workspace_brief
./neo run show_time                 # 名命中 catalog 则直接跑图
./neo run "查看系统时间"             # 否则 plan + 执行
./neo plan "梳理仓库近期重点"         # 只出 DAG JSON
./neo plan -o /tmp/plan.json5 "…"
```

节点类型概要：`tool` / `llm`（步骤字段 `"tools": "on"|"off"`）/ `loop` / `route`。

可选收紧规划（默认仍允许现编 `dags`）：

```json5
{ plan: { catalog_only: true } }
```

或环境变量 `NEO_PLAN_CATALOG_ONLY=1`（`0` 强制关闭）。开启后模型只能输出 `{"use":["catalog_name"]}`，禁止现编完整 `dags`。`-v` 时 stderr 会打印 `plan_path=use|invent`。

权威说明：[`dag.md`](dag.md)。目录：[`../dags/README.md`](../dags/README.md)。

---

## 8. 记忆与 Claw

### 8.1 本机向量记忆

配置 `memory.vector.enabled` 后可召回注入、启发式自动写入、`memory_add`：

```bash
./neo memory store "用户偏好深色模式"
./neo memory recall "深色模式"
./neo -v "请记住我喜欢大号字体"
```

### 8.2 Claw 拼装顺序

system prompt 大致顺序：开场 → UTC 时间 →（可选 cwd）→ Soul → Bootstrap → Rules → Memory → 能力矩阵说明。

演示配置：[`../example/`](../example/)。全文：[`claw.md`](claw.md)。

---

## 9. Profile 与脚本入口

```bash
./neo -p demo dag run demo_loop
./scripts/neo-ask -p demo --dag demo_loop
```

见 [`../config/profiles/`](../config/profiles/)、[`../scripts/README.md`](../scripts/README.md)。

---

## 10. 排错速查

1. **从不调工具** — 检查 `capability_matrix.enabled`；是否设了 `NEO_DISABLE_TOOLS=1`
2. **unknown DAG** — 配置 `dag_directory: "dags"`，在仓库根执行
3. **高铁/天气空结果** — 看 stderr 是否有 `ERROR:`；网络/限流；`train_query` 已支持中文站名
4. **zsh：`no matches found`** — 问句含 `?` 时请加引号：`./neo "……？"`
5. **想看完整 system** — `./neo -d "…"`（stderr）
6. **JSON5 / 旧 YAML** — [`migrate-json.md`](migrate-json.md)

更多现象表：[`examples.md`](examples.md) 末节。

---

## 11. 文档地图

1. **本手册** — 日常怎么用
2. [`examples.md`](examples.md) — 可复现样例与开箱组合
3. [`../example/usage/`](../example/usage/) — 更短的命令清单
4. [`tool.md`](tool.md) / [`dag.md`](dag.md) / [`claw.md`](claw.md) — 子系统细则
5. [`applications.md`](applications.md) / [`architecture.md`](architecture.md) — 定位与目标架构
6. [`superpowers/`](superpowers/) — 设计稿与实施计划（非用户手册）
7. [`../AGENTS.md`](../AGENTS.md) — 给改代码的人 / agent 的仓库约束
