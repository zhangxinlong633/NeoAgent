# Neo 工具与能力矩阵（Capability Matrix）

Neo 在配置中启用 `capability_matrix` 后，会构建一张 **Capability Matrix（能力矩阵）**：行是能力（builtin / `commands` / MCP），列是契约（schema、副作用、来源等）。`./neo "…"` 的 tool loop、workflow `type: tool`、以及 `neo plan` 的允许工具名，都共用这张表。顶层键名优先 **`capability_matrix`**；旧键 **`tools`** 仍可读（stderr 提示改名）。

当前内置能力：`read_file`、`write_file`、`append_file`、`list_dir`、`stat`、`grep`、`mkdir`；可选 `http_get`（需 `http_fetch_enabled` + `http_allow_hosts`）、`run_command`（需 `shell_enabled`）。

命令行走查样例见 [`manual.md`](manual.md)、[`examples.md`](examples.md)；能力目录布局见 [`../capabilities/README.md`](../capabilities/README.md)。

---

## 1. 配置示例

在 `config.json5` 中启用（路径相对你运行 `./neo` 时的当前工作目录；一般在仓库根执行则 `root: "."` 即可）：

```json5
{
  capability_matrix: {
    enabled: true,
    root: ".",
    directory: "capabilities",
    max_rounds: 16,
    max_read_bytes: 262144,
    list_dir_max_entries: 256,
    http_fetch_enabled: false,
    http_allow_hosts: "", // 例: "api.github.com,httpbin.org"
    http_fetch_max_bytes: 262144,
    shell_enabled: false,
    commands: [
      {
        name: "echo_args",
        description: "Demo command",
        when: ["Smoke-test tool args"],
        when_not: ["Real domain work"],
        tags: ["demo"],
        outcome: "Args JSON on stdout",
        argv: ["./scripts/tools/echo-args.sh"],
        parameters: {
          type: "object",
          properties: { msg: { type: "string" } },
          required: ["msg"],
        },
      },
    ],
  },
  dag_directory: "dags",
}
```

- **`enabled: false`**（或未写 `capability_matrix:`）：不会发 `tools`，行为与旧版一致。
- **临时关闭工具**：`NEO_DISABLE_TOOLS=1 ./neo "..."`。
- **`commands[].parameters`**：可选 JSON Schema，原样进入 OpenAI `function.parameters`；省略则为 `{"type":"object"}`。
- **选型元数据**（`when` / `when_not` / `tags` / `outcome`）：写入能力文件或 `commands[]`，进入矩阵 listing 与 `function.description`，供 LLM 决定何时调用。见 AGENTS.md §4.1。

完整示例可与仓库内 `config/config.json5.example` 对照。

### 1.1 `list_dir` / `grep` / `stat` / `mkdir` / `append_file` / `http_get`

| 工具 | 说明 |
|------|------|
| `list_dir` | 参数 `path`；非递归列目录。 |
| `grep` | 参数 `pattern`（必填）、可选 `path`（默认 `.`）、可选 `glob`（如 `*.md`）；在 `capability_matrix.root` 下按行做字面量匹配，有匹配数上限。 |
| `stat` | 参数 `path`；返回 `type` / `size` / `mtime`（沙箱内）。 |
| `mkdir` | 参数 `path`；可选 `parents: true` 创建中间目录。 |
| `append_file` | 参数 `path` + `content`；追加写入（文件不存在则创建）。 |
| `run_command` | 仅当 `shell_enabled: true`；参数 `argv` 为字符串数组（**无 shell**）。`argv[0]` 规则与 `commands` 相同（相对 root 或显式绝对路径）。 |
| `http_get` | 仅当 `http_fetch_enabled: true` 且配置了 `http_allow_hosts`；HTTPS、无重定向。 |

### 1.2 MCP stdio（能力矩阵 loader）

在 `capability_matrix.mcp_servers` 声明本地 MCP 子进程（**仅 stdio**；`url` 字段若填写会告警并跳过）：

```json5
capability_matrix: {
  enabled: true,
  root: ".",
  mcp_servers: [
    {
      name: "mock",
      command: "python3",
      args: ["tests/fixtures/mock_mcp_echo.py"],
    },
  ],
}
```

- 启动时：`initialize` → `tools/list`，工具进入矩阵，对外名为 `mcp_<server>_<tool>`（非法字符变为 `_`）。
- 调用：与其它能力相同，经 OpenAI `tool_calls` 或 DAG `type: tool`。
- 失败：单个 server 跳过，不影响 builtin / commands。

### 1.4 能力目录（一文件一能力）

配置 `capability_matrix.directory`（例如 `"capabilities"`）后，Neo 读取该目录下的 **manifest** 与 `load[]` 子目录中的 `*.json5`，合并进矩阵（与内联 `commands` 相同校验）。

```
capabilities/
  manifest.json5       # load: ["local", "git", "unix"]；不递归
  local/*.json5        # 本机轻处理
  git/*.json5          # 仓库探针
  unix/                # Unix 工具集：全量 unix_*.json5 + enabled.json5 白名单 + CATALOG.md
  industry/            # 预留；默认不在 load 中
  proposed/            # propose_capability 草稿；默认不加载
```

- **`propose_capability`**：仅当配置了 `directory` 时出现在矩阵中。把新能力写到 `proposed/`，**当轮不生效**；移到已列入 `load` 的场景子目录（如 `local/`）并重启 `neo` 后可用。
- 场景目录内若存在 **`enabled.json5`**（`{ load: ["cap_name", ...] }`），则只装载名单中的能力定义；文件名茎须与能力 `name` 一致。用于 `unix/` 这类「百余定义、只启用二三十个」的包。
- 内联 `commands` 与目录加载可并存；重名会报错。
- 场景分层与应用定位见 [`applications.md`](applications.md)；目录契约见 [`capabilities/README.md`](../capabilities/README.md)。

### 1.5 `capability_matrix.commands`（自定义命令工具）

在 `capability_matrix:` 下声明 `commands`，无需改 C / 重新 `make`。模型通过 `tool_calls` 调用；Neo **exec 已声明的 `argv`**（不拼 shell）。工作目录为 `capability_matrix.root`。

```json5
{
  capability_matrix: {
    enabled: true,
    root: ".",
    commands: [
      {
        name: "date_iso",
        description: "UTC ISO timestamp",
        when: ["Need current UTC time"],
        argv: ["/bin/date", "-u", "+%Y-%m-%dT%H:%M:%SZ"],
        timeout_sec: 5,
        pass_args: "env",
      },
    ],
  },
}
```

- `argv[0]`：**相对** `capability_matrix.root`（须落在 root 内），或**绝对路径**（须 `realpath` 为可执行普通文件；禁止路径中的 `..` 段）。不支持 PATH 上的裸命令名。
- 读写类 builtin（`read_file` 等）仍只允许相对 root 的路径。
- `pass_args: stdin_json`：arguments JSON 写入子进程 stdin；`pass_args: env`：写入 `NEO_TOOL_ARGS`。`NEO_TOOL_NAME` 始终设置。
- 非 0 退出码：结果前缀 `EXIT:<code>\n`。
- 仓库示例能力包：`capabilities/{local,git,unix}/`（`unix/` 另见 `enabled.json5`）。

---

## 2. 实录：真实命令与输出


| 工具 | 说明 |
|------|------|
| `list_dir` | 参数 `path` 为相对 `capability_matrix.root` 的目录路径；返回该目录下条目名列表（有上限，见 `list_dir_max_entries`）。 |
| `http_get` | 仅当 `http_fetch_enabled: true` 且 `http_allow_hosts` 配置了允许的主机名时出现；参数 `url` 必须为 `https://` 且 URL 的主机名（大小写不敏感）在允许列表中；不跟随 3xx；正文截断至 `http_fetch_max_bytes`。 |

---

## 2. 实录：真实命令与输出

以下输出为在本仓库根目录、已 `make`、已启用 `tools`、使用 OpenRouter `qwen/qwen3-32b` 时**实际截取**的文本。**模型与网关不同则内容会变化**，但 `neo tool:` 行是否出现可用来判断是否走到了本地工具逻辑。

### 2.1 启用 tools：读 `README.md` 并概括

**命令（stderr 与 stdout 合并到同一流，便于复制整段输出）：**

```bash
./neo "请使用 read_file，path 为 README.md；根据返回内容用三句中文概括。" 2>&1
```

**实录（OpenRouter `qwen/qwen3-32b`，仓库根目录，修复 `arguments` JSON 反转义之后）：**

```text
neo tool: read_file
1. **Neo 是一个用 C 语言编写的命令行 AI 助手**，HTTPS 经内置 BearHttpsClient（`neo_http`），通过 OpenAI 兼容 API 提供服务。 
2. **功能通过 YAML 配置注入技能（如南京旅游数据）**，支持单次查询和多轮对话模式，适合本地/边缘部署，资源占用低。  
3. **项目特点包括轻量级设计、多技能扩展、会话模式灵活**，可运行在树莓派等 IoT 设备，适合开发者和需要本地化部署的场景。
```

- 第一行 **`neo tool: read_file`** 表示 Neo 已执行本地读文件；其后均为 **stdout** 上的模型回复（合并 `2>&1` 时顺序即如此）。
- 若将 stderr / stdout **分开重定向**，仍应能在 stderr 中看到 `neo tool: read_file`；模型若发起第二轮带工具的对话，可能出现**多行** `neo tool:`。

**历史问题（已修复）：** 旧版未对 API 返回的 `tool_calls[].function.arguments` 做 JSON **字符串反转义**（线路上常为 `{\"path\":\"README.md\"}`），解析不到 `path` 会导致读失败、模型误报「找不到文件」。当前实现见 `src/agent_tools.c` 中的 `neo_json_unescape_slice` 及对 `id` / `name` / `arguments` 的应用。

---

### 2.1b 小文件：`MEMORY.md`（一轮 tool 的简短验证）

**命令：**

```bash
./neo "只调用一次 read_file，path 用 MEMORY.md。用一句话说明文件里第一行写了什么。" 2>&1
```

**实录：**

```text
neo tool: read_file
文件第一行是标题说明：`# Memory (context for Neo)`，表明这是Neo的记忆上下文配置文件。
```

---

### 2.2 关闭 tools：环境变量 `NEO_DISABLE_TOOLS=1`

**命令：**

```bash
NEO_DISABLE_TOOLS=1 ./neo "只回复一个词：pong" 2>&1
```

**合并输出（stdout + stderr，本次无额外 stderr）：**

```text
pong

```

此时请求中**不会**附带 `tools`，也不会出现 `neo tool:` 行。

---

### 2.3 写后读：`demo-tool.txt`（write_file + read_file）

**命令：**

```bash
./neo "调用 write_file：path 为 demo-tool.txt，content 为一行 Neo tool demo。然后 read_file 读回 demo-tool.txt，只输出工具读到的原文一行。" 2>&1
```

**终端实录（`2>&1`，OpenRouter `qwen/qwen3-32b`，仓库根）** — 拆成两段代码块，避免在围栏内再写 \`\`\`，否则预览会断掉、后面「磁盘文件」一节渲染不出来。

*stderr（Neo）：*

```text
neo tool: write_file
neo tool: read_file
```

*stdout（模型；工具读到的正文即下一行，终端里有时会多包一层 \`\`\`text 围栏）：*

```text
Neo tool demo
```

**仓库根目录下的 `demo-tool.txt`（磁盘上的实际内容，与 `write_file` 写入一致）：**

```text
Neo tool demo
```

本地核对：

```bash
cat demo-tool.txt
```

说明：更长、更绕的指令可能触发**多轮** tool call（stderr 出现多行 `neo tool:`）；若不需要保留演示文件，可在仓库根执行 `rm -f demo-tool.txt`。本仓库 `.gitignore` 已忽略 `demo-tool.txt`，避免误提交生成文件。

---

## 3. 结果形态小结

| 项目 | 说明 |
|------|------|
| 工具是否执行 | 以 stderr 是否出现 `neo tool: <name>` 为准 |
| 最终给用户的话 | 一律在 **stdout** |
| 工具返回给模型的内容 | 纯文本；错误时多为 `ERROR: ...` 前缀 |
| 路径规则 | `read_file` / `write_file` / `append_file` / `list_dir` / `stat` / `mkdir` / `grep`：相对 `capability_matrix.root`；禁止绝对路径、禁止路径段中出现 `..` |

---

## 4. 常见问题

**Q：模型仍只给 `echo`/`cat` 和「我无法执行命令」**  
**A：** 多数是 **`capability_matrix.enabled` 未为 true** 或未写该段。确认 JSON5 已保存，并在仓库根执行 `./neo`。若已启用仍无 `neo tool:`，请换用**明确支持 function calling** 的模型或检查网关是否支持 `tools` / `tool_calls` 字段。

**Q：`path not allowed` / `ERROR: path not allowed`**  
**A：** 路径越出 `capability_matrix.root` 的 realpath 范围，或使用了 `/`、`..`。

**Q：daemon 模式是否支持工具？**  
**A：** 支持；同样依赖 `config.json5` 中 `capability_matrix.enabled`，且可用 `NEO_DISABLE_TOOLS` 关闭。

**Q：出现 `neo tool: read_file` 后立刻 `neo: LLM request failed`、stdout 为空**  
**A：** 多为**第二轮 POST** 失败（网络抖动、网关限流、请求体过大等）。可重试；若只读大文件，可适当降低 `max_read_bytes` 或换用较小测试文件（见 §2.1b）。

---

## 5. 与代码的对应关系

- 工具循环与 HTTP：`src/capability/agent_tools.c`、`src/llm/llm.c`（`llm_post_chat_completions_json`）
- 配置解析：`src/core/config.c`（`capability_matrix:` 段，兼容旧键 `tools`）
- 单次 / daemon 入口：`src/cli/main.c`、`src/core/daemon.c`
- JSON 解析：内置 `src/vendor/yyjson.c` / `src/vendor/yyjson.h`（MIT；LLM 与 tool_calls）
- Capability Matrix：`src/capability/capability_matrix.c`；MCP loader：`src/capability/mcp_stdio.c`

更多整体说明见仓库根目录 `README.md`。
