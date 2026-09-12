# Neo 开发约束（CLAUDE.md）

给在本仓库改代码的人与 AI agent。**与 `AGENTS.md` 保持同步。**

产品定位见 `README.md`（英文）/ `README_zh.md`（中文）：瑞士军刀、可移植、轻量——**不是**最强 IDE agent。

> 注意：`example/AGENTS.md` 是给 Neo **运行时** bootstrap 注入的示例身份文件，与本文无关。

---

## 1. 产品三角（改功能前先对齐）

**DAG ∥ Capability Matrix ∥ Policy**

| 层 | 管什么 | 配置 / 代码入口 |
|----|--------|-----------------|
| **DAG** | 编排怎么走（确定性拓扑） | `dags`；`src/dag/dag.c`；`neo plan` / `neo run` → `src/dag/plan.c` |
| **Capability Matrix** | 能调用什么（可发现能力表） | 顶层键 **`capability_matrix`**；`src/capability/capability_matrix.c` |
| **Policy** | 许不许、贵不贵 | `shell_enabled`、`http_*`、轮次/字节上限；加载进矩阵时裁剪 `enabled` |

一句话：**DAG 编排；矩阵定义刀刃；Policy 守门。** MCP 只是矩阵的 **loader**（`src/capability/mcp_stdio.c`），不是第二套工具系统。

---

## 2. 源码地图（改哪里）

布局：`src/{cli,core,llm,capability,dag,memory,vendor}/`；编译产物在 **`build/`**（不要往 `src/` 写 `.o`）。

| 模块 | 职责 |
|------|------|
| `src/cli/main.c` | CLI 入口、`-v` / 子命令分发 |
| `src/core/config.c` / `config.h` | JSON5 解析；内部结构体字段可叫 `tools`，**对外键名**见下节 |
| `src/core/neo_http.c` / `neo_http.h` | HTTPS 门面（BearHttpsClient）；`llm` / `http_get` 只经此调用 |
| `src/core/config.c` / `daemon.c` | 配置解析、daemon 多轮会话 |
| `src/llm/llm.c` | OpenAI 兼容 HTTP |
| `src/capability/capability_matrix.c` | 建表、OpenAI `tools` JSON、prompt 列表 |
| `src/capability/capability_dir.c` | 能力目录加载器 + `propose_capability` |
| `src/capability/agent_tools.c` | 反应式 tool loop + `neo_dispatch_tool` |
| `src/capability/command_tools.c` | `capability_matrix.commands` 白名单 exec |
| `src/capability/mcp_stdio.c` | stdio MCP → 矩阵行；`tools/call` |
| `src/dag/dag.c` | DAG 执行（`tool` / `llm` / `loop` / `route`） |
| `src/dag/dag_dir.c` | DAG 目录加载 + catalog 列表 |
| `src/dag/plan.c` | 规划提示（catalog `use` 优先）、抽出 dags JSON、materialize |
| `src/memory/neo_memory.c` | 本地向量记忆（分块 / recall；仅此封装 `vdb.h`） |
| `src/vendor/yyjson.c` | 第三方 amalgamation；少改 |

`#include` 仍用短名（如 `"config.h"`）；Makefile 用多路径 `-I`。

用户文档：`docs/architecture.md`（目标架构）、`docs/applications.md`（应用场景与定位）、`docs/examples.md`（使用样例）、`docs/tool.md`（矩阵）、`docs/dag.md`（DAG）、`docs/claw.md`（prompt 块）、`docs/migrate-json.md`（迁移）。设计稿：`docs/superpowers/specs/`。

---

## 3. 配置命名（易混，硬约束）

1. **顶层对象**：优先 **`capability_matrix`**。旧键 **`tools`** 仅兼容读取，并 stderr 弃用提示；新示例 / fixture / materialize **只写** `capability_matrix`。
2. **Workflow 步骤字段** `"tools": "on"|"off"`：表示该 **LLM 步骤**是否带工具，**禁止**改成 `capability_matrix`。
3. **C 内部**：`agent_config_t.tools` / `tools_config_t` 可保留（历史命名）；对外文档与错误路径用 `/capability_matrix/...`。
4. **MCP JSON-RPC** 里的 `"tools"`（协议字段）不要改名。
5. **配置格式**：JSON5 only（`YYJSON_READ_JSON5`）；拒 yaml，指引 `docs/migrate-json.md`。
6. **路径沙箱**：读写类 builtin 相对 `capability_matrix.root`，禁止绝对路径与 `..` 段。`capability_matrix.commands` / 能力目录的 `argv[0]` 可为相对 root 的路径，或**显式声明的绝对路径**（能力文件即白名单；禁止 `..`；不查 PATH 裸名）。

```json5
capability_matrix: {
  enabled: true,
  root: ".",
  shell_enabled: false,   // Policy：才暴露 run_command
  commands: [ /* ... */ ],
  mcp_servers: [ /* stdio */ ],
}
```

---

## 4. 能力矩阵约定

- **一行一能力**：稳定 `name` 供模型与 DAG `type: tool` 共用。
- **来源**：`builtin` | `command` | `mcp:<server>`；MCP 对外名：`mcp_<server>_<tool>`。
- **消费者必须同源**：`./neo` loop、`workflow` 的 `tool` / `llm+tools:on`、`neo plan` 允许名，都读同一张矩阵快照。
- **新能力默认路径**：加 builtin → 矩阵行 + dispatch；或加 `commands` / `mcp_servers` / 能力目录文件。**不要**在 `agent_tools` 旁再开一套平行注册表。
- **Policy 在物化/注册时生效**：关 `shell_enabled` 就不要出现 `run_command`；MCP 失败只让该 server 行不可用，其它行继续。

---

## 4.1 选型元数据（能力目录 ∥ DAG 目录，硬约束）

能力文件与 DAG 文件不能只有 `name` + 一句空泛 `description`。必须带上足够信息，让 LLM **判断何时用 / 何时不用**：

| 字段 | 能力（`capabilities/<场景>/` 等） | DAG（`dags/<场景>/` 等） | 说明 |
|------|-----------------------------------|--------------------------|------|
| `description` | 必填 | 必填 | 做什么、产出什么 |
| `when` | **强烈建议** | **强烈建议** | 适用场景（字符串或字符串数组） |
| `when_not` | 建议 | 建议 | 不适用 / 易误用场景 |
| `tags` | 可选 | 可选 | 粗分类，便于扫描 |
| `requires` | — | 建议 | 依赖的能力名（矩阵里须存在） |
| `outcome` | 可选 | 建议 | 成功时用户可见结果长什么样 |

这些字段会进入：

- 能力矩阵 **prompt listing** 与（尽可能）OpenAI `function.description`
- planner 的 **DAG catalog**（鼓励 `{"use":[...]}`）

写能力/DAG 时自检：去掉 `name` 后，模型能否只靠元数据决定选不选？若不能，补 `when` / `when_not`。

---

## 5. 明确不要做的事

- 不做 Temporal / 重插件宿主 / 通用 hooks 总线。
- 不做 Cursor 级 IDE agent（多文件 apply_patch UX、全仓索引等）作为本仓库目标。
- 不把 MCP resources/prompts 做成与矩阵并列的一等运行时（非目标）。
- v1 不做 MCP HTTP/SSE 运行时（配置可留 `url` 字段并跳过）。
- 不自动把 tool loop「编译」成 DAG（升级路径非目标）。
- 不为「演示方便」默认打开 `shell_enabled`。
- 不引入与矩阵并行的第二套 tool 名字空间。

---

## 6. 目录须有 README（硬约束）

**每个仓库目录**（新建或长期保留的）都要有 **`README.md`**。README 是目录契约，不是随手备注。

### 6.1 必备内容

1. **本目录职责**：用完整陈述句说明用途与边界（禁止只写目录名或空口号）。
2. **子目录说明**：若有子目录，用表格或列表逐条写清各自职责。
3. **关联入口**：指向权威文档（如 `docs/tool.md`、`docs/dag.md`、本文相关节），避免在 README 中复述大段实现细节。

特别适用：`capabilities/`、`dags/`、`rules/`、`src/` 各模块、`config/`、`tests/`、`docs/`、`scripts/` 及其子目录。

### 6.2 文风（正式）

- 使用规范术语与当前对外键名（Capability Matrix、DAG、Policy、`capability_matrix`、JSON5）；勿残留已弃用的顶层 `tools` / YAML 等误导表述。
- 标题层级清晰；优先表格与短列表，避免口语、网络梗、未解释缩写堆砌。
- 禁止「随便看看」「临时放这里」「差不多就行」等敷衍表述；未实现能力须标明「预留 / 未实现」。
- 示例路径、命令与配置键须与仓库现状一致，并可被读者按文档复现。

### 6.3 例外与维护

例外（可不写 README）：纯生成物目录（如 `build/`）、git 元数据。仅含瞬时/忽略产物的空壳若保留，仍须简短说明「仅产物 / 已 gitignore」。

若目录已有约定入口文档，`README.md` 可短，但必须存在，并明确指向该入口。

新增目录时：**先写 README，再堆文件。** 目录职责或对外约定变更时，**同一变更内**更新对应 README。

---

## 7. 复杂代码须有中文注释（硬约束）

**偏复杂**的逻辑必须用**中文注释**写清意图与边界，便于后人与 agent 阅读。包括但不限于：

- 非显然的控制流（DAG 调度、route/skip、plan 选型 vs 现编）
- 能力矩阵物化、目录加载、MCP 协议握手、路径沙箱 / Policy 门闩
- 易混命名（如顶层 `capability_matrix` vs 步骤字段 `"tools"`）
- 错误路径与「故意不热加载」等产品决策（如 `propose_capability`）

要求：

1. **文件头或模块头**：一两句说明本文件职责（中文即可，可与英文并存）。
2. **非平凡函数**：注释「做什么 / 不做什么 / 关键前置条件」。
3. **巧妙或脆弱处**：说明为什么这样写（例如 realpath 越界检查、proposed 不进矩阵）。
4. **不要**：给 `i++`、显而易见的 getter 堆注释；不要大段复述代码。
5. **第三方**（`src/vendor/`）：保持上游风格，不强制中文化。

改复杂逻辑时：**先补/改中文注释，再改代码**（或同一提交内完成）。

---

## 8. 改动习惯

- **语言 / 风格**：C99 风格、与邻文件一致；少加依赖（内置 yyjson + 内置 BearHttpsClient，经 `neo_http` 调用）。
- **测试**：相关改动后跑 `make test`（含单元测试 + `tests/cli_capability_matrix.sh` CLI 冒烟；勿对 `make test` 盲目 `| tail` 以致看起来挂死）。仅 CLI：`make test-cli`。fixture 用 `tests/fixtures/*.json5`；旧键兼容可放 `tools_legacy_key.json5`。
- **文档**：用户可见行为变了再改 `docs/*.md` / 根 `README.md`（及中文 `README_zh.md`）；超长设计放 `docs/superpowers/`。目录职责变了须按 §6 **正式更新**该目录 `README.md`。
- **Plan materialize**：写出的临时配置顶层键用 `capability_matrix`，并带上需要的 policy 字段。
- **Verbose**：`-v` / `--verbose` 必须真正解析；步骤与 capability 日志走 stderr。
- **Git 提交（硬约束）**：提交记录须像普通人手写——**禁止**在 message / trailer / author / committer 中留下各类 agent 痕迹。包括但不限于：`Co-authored-by: Cursor <cursoragent@cursor.com>`、`Co-authored-by: *agent*`、Copilot / Claude / Codex / Devin 等自动署名、`Made-with:` / `Generated-by:` 之类 trailer。若环境自动追加此类行，提交前须去掉后再 `commit`（可用编辑 message 或重新提交；已推送的历史不要乱改写，除非维护者明确要求）。Author 使用维护者本机已配置的用户名与邮箱，不要改成 agent 身份。

---

## 9. 快速自检清单

改工具 / 配置 / DAG / plan 时：

- [ ] 新能力是否进入 **Capability Matrix**，而非旁路？
- [ ] JSON 示例是否用 **`capability_matrix`**？步骤开关是否仍叫 **`tools`**？
- [ ] Policy 默认是否偏安全（shell / http 默认关或白名单）？
- [ ] 新建/调整的目录是否有 **正式 `README.md`**（职责清晰、术语正确；含 `capabilities/`、`dags/` 等）？
- [ ] 能力/DAG 文件是否有足够的 **when / when_not**（及 DAG 的 requires/outcome）供 LLM 选型？
- [ ] 复杂逻辑是否有足够的**中文注释**？
- [ ] `make test` 是否通过？
- [ ] 本次 commit **无** Cursor / 其它 agent 的 `Co-authored-by` 等 trailer？
- [ ] 是否误改了 `example/AGENTS.md`（那是运行时示例，不是本约束文件）？
