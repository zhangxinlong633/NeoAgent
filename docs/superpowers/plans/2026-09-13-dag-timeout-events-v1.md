# DAG tool timeout + events v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DAG `type:tool` 可选 `timeout_sec`（超时进现有 retry）；`NEO_EVENTS` JSONL 增加 `v` 与 `run_id`；文档对齐。

**Architecture:** 超时只覆盖 **command** 执行墙钟（`command_tool_run`）；经 `neo_dispatch_tool(..., timeout_override_sec)` 从 `dag_run_tool_step` 传入。事件仍单函数 `neo_events_emit`，内部懒生成 `run_id`、写死 `v:1`。不做并行、不做 usage。

**Tech Stack:** C99、现有 `config` / `dag` / `command_tools` / `neo_events`、`make test`。

**Spec:** [`../specs/2026-09-13-dag-timeout-events-v1-design.md`](../specs/2026-09-13-dag-timeout-events-v1-design.md)

## Global Constraints

- 顶层键 `capability_matrix`；步骤开关仍叫 `"tools"`
- 不默认打开 `shell_enabled`；不加并行 / token 字段
- builtin/MCP：步级 `timeout_sec` **可忽略**（文档写明）；command 必须生效
- `make test` 通过；commit **无** Cursor/`Co-authored-by` 等 agent trailer
- 复杂逻辑补中文注释

## File map

| 文件 | 职责 |
|------|------|
| `src/core/neo_events.c` / `.h` | `v`、`run_id` |
| `tests/test_neo_events.c` | 断言新字段 + 同进程 `run_id` 稳定 |
| `src/capability/command_tools.h` / `.c` | `timeout_override_sec` |
| `tests/fixtures/bin/sleep_n.sh` | 慢命令 fixture |
| `tests/test_command_exec.c` | override 超时单测 |
| `src/capability/agent_tools.h` / `.c` | `neo_dispatch_tool` / `run_one_tool` 透传 |
| `tests/test_capability_matrix.c` | 所有 `neo_dispatch_tool` 调用补 `0` |
| `src/core/config.h` / `config.c` | `dag_step_t.timeout_sec` 解析校验 |
| `src/dag/dag.c` | 传入 `st->timeout_sec` |
| `tests/fixtures/dag_runner.json5` + `test_dag_runner.c` | 步级超时 + retry |
| `docs/dag.md` / `docs/manual.md` / 可选 `architecture.md` §8.1 | 用户可见 |

**刻意不做：** 最小并行、图级默认超时、usage、保证 MCP/builtin 墙钟。

---

## Task 1: 事件 `v` + `run_id`

**Files:**
- Modify: `src/core/neo_events.h`、`src/core/neo_events.c`
- Test: `tests/test_neo_events.c`

**Interfaces:**
- Consumes: 现有 `neo_events_emit` / `neo_events_enabled`
- Produces: 每行含 `"v":1` 与 `"run_id":"<hex>"`；同进程多次 emit 的 `run_id` 相同

- [ ] **Step 1: 扩展失败断言（先改测试）**

在 `tests/test_neo_events.c` 的 payload 检查中增加：

```c
  if (!strstr(buf, "\"v\":1")) {
    fprintf(stderr, "missing v:1:\n%s\n", buf);
    return 1;
  }
  {
    const char *p1 = strstr(buf, "\"run_id\":\"");
    const char *p2;
    char id1[32];
    size_t i = 0;
    if (!p1) {
      fprintf(stderr, "missing run_id:\n%s\n", buf);
      return 1;
    }
    p1 += strlen("\"run_id\":\"");
    while (p1[i] && p1[i] != '"' && i + 1 < sizeof(id1)) {
      id1[i] = p1[i];
      i++;
    }
    id1[i] = '\0';
    if (i < 8) {
      fprintf(stderr, "run_id too short: %s\n", id1);
      return 1;
    }
    p2 = strstr(p1, "\"run_id\":\"");
    if (!p2 || strncmp(p2 + strlen("\"run_id\":\""), id1, i) != 0) {
      fprintf(stderr, "run_id not stable across lines:\n%s\n", buf);
      return 1;
    }
  }
```

- [ ] **Step 2: 跑测确认失败**

```bash
make tests/test_neo_events && ./tests/test_neo_events
```

Expected: FAIL（缺 `v` / `run_id`）

- [ ] **Step 3: 实现**

`neo_events.h` 注释更新为含 `v`、`run_id`。

`neo_events.c`（要点）：

```c
#define NEO_EVENTS_SCHEMA_V 1
static char g_run_id[17]; /* 16 hex + NUL */

static void ensure_run_id(void) {
  unsigned char b[8];
  size_t i;
  FILE *f;
  if (g_run_id[0]) return;
  f = fopen("/dev/urandom", "rb");
  if (f && fread(b, 1, sizeof(b), f) == sizeof(b)) {
    fclose(f);
  } else {
    if (f) fclose(f);
    /* 退化：用时间混合，仅测试兜底 */
    {
      long t = (long)time(NULL) ^ (long)neo_events_now_ms();
      memcpy(b, &t, sizeof(t) < sizeof(b) ? sizeof(t) : sizeof(b));
    }
  }
  for (i = 0; i < sizeof(b); i++)
    snprintf(g_run_id + i * 2, 3, "%02x", b[i]);
}

/* emit 内：ensure_run_id(); snprintf 加入 "v":%d,"run_id":"%s" */
```

中文注释：说明 `run_id` 进程内懒生成、不持久化。

- [ ] **Step 4: 跑测通过**

```bash
make tests/test_neo_events && ./tests/test_neo_events
```

Expected: `ok`

- [ ] **Step 5: Commit**

```bash
git add src/core/neo_events.c src/core/neo_events.h tests/test_neo_events.c
git commit -m "$(cat <<'EOF'
feat: add schema version and run_id to NEO_EVENTS

Make JSONL lines correlatable within a process without a second logger.
EOF
)"
```

（提交后检查 message 无 `Co-authored-by`；若有则用 `commit-tree` 去掉。）

---

## Task 2: `command_tool_run` 超时覆盖

**Files:**
- Modify: `src/capability/command_tools.h`、`src/capability/command_tools.c`
- Create: `tests/fixtures/bin/sleep_n.sh`
- Modify: `tests/test_command_exec.c`

**Interfaces:**
- Consumes: 现有 `tool_command_t.timeout_sec`
- Produces: `command_tool_run(..., int timeout_override_sec)`；`>0` 覆盖本次墙钟

- [ ] **Step 1: 慢命令 fixture**

`tests/fixtures/bin/sleep_n.sh`：

```sh
#!/bin/sh
# 读 stdin JSON 忽略；睡眠后打印 ok（供超时单测）
sleep 5
echo "slept_ok"
```

```bash
chmod +x tests/fixtures/bin/sleep_n.sh
```

- [ ] **Step 2: 失败测试 — override=1 应对 sleep 5 超时**

在 `tests/test_command_exec.c` 末尾 `printf("ok\n")` 前增加：

```c
  {
    tool_command_t slow;
    char *argv_slow[2];
    memset(&slow, 0, sizeof(slow));
    slow.name = (char *)"sleep_n";
    argv_slow[0] = (char *)"./tests/fixtures/bin/sleep_n.sh";
    argv_slow[1] = NULL;
    slow.argv = argv_slow;
    slow.argv_count = 1;
    slow.timeout_sec = 30; /* 矩阵默认长，靠 override 压到 1 */
    slow.max_output_bytes = 256;
    slow.pass_args = 0;
    free(out);
    out = NULL;
    rc = command_tool_run(&c, root_real, &slow, "{}", &out, &out_len, 1);
    if (rc != 0 || !out || strncmp(out, "ERROR: timeout", 14) != 0) {
      fprintf(stderr, "expected ERROR: timeout, rc=%d out=%s\n", rc, out ? out : "(null)");
      free(out);
      config_free(&c);
      return 1;
    }
    free(out);
    out = NULL;
  }
```

（若签名尚未改，先改头文件再编译。）

- [ ] **Step 3: 改签名与实现**

`command_tools.h`：

```c
int command_tool_run(const agent_config_t *conf, const char *root_real, const tool_command_t *cmd,
                     const char *args_json, char **out_text, size_t *out_len,
                     int timeout_override_sec);
```

`command_tools.c` 两处定义（平台分支若有）同步；应用：

```c
  timeout_sec = cmd->timeout_sec > 0 ? cmd->timeout_sec : 30;
  if (timeout_override_sec > 0) timeout_sec = timeout_override_sec;
  if (timeout_sec > 600) timeout_sec = 600;
```

中文注释：override 仅本趟有效，不改 `cmd` 结构体。

把文件内其它 `command_tool_run(...)` 调用（仅测试与 agent_tools，Task 3 改）暂时能编过：本 task 先修 `test_command_exec` 旧调用为末参 `0`。

- [ ] **Step 4: 跑测**

```bash
make tests/test_command_exec && ./tests/test_command_exec
```

Expected: `ok`（约 ≥1s，因超时等待）

- [ ] **Step 5: Commit**

```bash
git add src/capability/command_tools.h src/capability/command_tools.c \
  tests/fixtures/bin/sleep_n.sh tests/test_command_exec.c
git commit -m "$(cat <<'EOF'
feat: allow per-call timeout override for command tools

DAG steps can tighten the wall clock without mutating matrix defaults.
EOF
)"
```

---

## Task 3: `neo_dispatch_tool` 透传 override

**Files:**
- Modify: `src/capability/agent_tools.h`、`src/capability/agent_tools.c`
- Modify: `src/dag/dag.c`（先传 `0`，Task 4 再改）
- Modify: `tests/test_capability_matrix.c`（全部调用补 `, 0`）

**Interfaces:**
- Consumes: Task 2 的 `command_tool_run(..., override)`
- Produces: `neo_dispatch_tool(..., int timeout_override_sec)`；loop 内传 `0`

- [ ] **Step 1: 改头文件与 `run_one_tool`**

```c
int neo_dispatch_tool(const agent_config_t *conf, const char *root_real,
                      const char *name, const char *args_json,
                      char **out_text, size_t *out_len,
                      int timeout_override_sec);
```

`run_one_tool` 增加同名参数；command 分支：

```c
    if (command_tool_run(conf, root_real, &conf->tools.commands[idx],
                         tc->arguments ? tc->arguments : "{}", &out, &out_len,
                         timeout_override_sec) != 0) {
```

`tool_run_command` 里构造的临时 `tmp.timeout_sec`：若 `timeout_override_sec > 0` 则写入 tmp（可选一致性）。builtin/MCP：**忽略** override（可 `(void)timeout_override_sec` 在非 command 路径，或仅 command 使用）。

agent loop 内直接调 `run_one_tool` 处传 `0`。

- [ ] **Step 2: 全局补调用方**

```bash
rg -n 'neo_dispatch_tool\(|command_tool_run\(' src tests
```

凡签名不匹配处补 `0`（或最终的 `st->timeout_sec`）。

- [ ] **Step 3: 编译测试**

```bash
make test
```

Expected: 全部通过（行为与改签名前一致）

- [ ] **Step 4: Commit**

```bash
git add src/capability/agent_tools.h src/capability/agent_tools.c src/dag/dag.c \
  tests/test_capability_matrix.c
git commit -m "$(cat <<'EOF'
refactor: thread timeout override through neo_dispatch_tool

Keep reactive tool-loop callers at override 0; DAG will pass step values next.
EOF
)"
```

---

## Task 4: 配置解析 + DAG 传入 + 集成测

**Files:**
- Modify: `src/core/config.h`、`src/core/config.c`
- Modify: `src/dag/dag.c`
- Modify: `tests/fixtures/dag_runner.json5`、`tests/test_dag_runner.c`
- Optional: `tests/fixtures/dag_bad_timeout.json5` + `tests/test_parse_dags.c`

**Interfaces:**
- Consumes: Task 3 签名；`dag_step_t.timeout_sec`
- Produces: 合法 1..600；非 tool 报错；DAG 调用 `neo_dispatch_tool(..., st->timeout_sec)`

- [ ] **Step 1: `dag_step_t` 字段**

`config.h` 在 `retry_max` 旁：

```c
  int retry_max;
  int timeout_sec; /* 仅 tool：0=不覆盖矩阵；1..600 覆盖本次 */
```

- [ ] **Step 2: 解析与校验**

在 `config.c` 解析 `retry` 之后：

```c
    {
      yyjson_val *to = yyjson_obj_get(st, "timeout_sec");
      if (yyjson_is_int(to) || yyjson_is_uint(to)) {
        int t = (int)yyjson_get_sint(to);
        if (t < 0) t = 0;
        s->timeout_sec = t;
      }
    }
```

在已有 `retry_max > 0 && type != TOOL` 校验旁增加：

```c
      if (s->timeout_sec != 0 && s->type != DAG_STEP_TOOL) {
        fprintf(stderr, "neo: DAG '%s' step '%s': timeout_sec only allowed on type tool\n",
                wf->name, s->id);
        return -1;
      }
      if (s->timeout_sec < 0 || s->timeout_sec > 600) {
        fprintf(stderr, "neo: DAG '%s' step '%s': timeout_sec out of range 0..600\n",
                wf->name, s->id);
        return -1;
      }
      /* 注：0 合法；1..600 为覆盖。若解析后仍要拒绝「仅允许 0 或 1..600」已覆盖。 */
```

若 `timeout_sec` 为 0 跳过范围错误；若 >600 失败。Spec：缺省/0 不覆盖；非法 >600 失败。若输入 `timeout_sec: 0` 显式，保持 0。

- [ ] **Step 3: DAG 调用**

`dag_run_tool_step` 两处 `neo_dispatch_tool`：

```c
    if (neo_dispatch_tool(conf, root_real, st->tool, args, &out, &out_len,
                          st->timeout_sec) != 0) {
```

- [ ] **Step 4: fixture 图**

在 `dag_runner.json5` 的 `commands` 增加：

```json5
      {
        "name": "sleep_n",
        "description": "Sleep then ok",
        "argv": ["./tests/fixtures/bin/sleep_n.sh"],
        "timeout_sec": 30,
        "max_output_bytes": 4096,
        "pass_args": "stdin_json"
      }
```

在 `dags` 增加（预期**失败**退出，用于断言超时路径；或单独 fixture）：

更稳妥：在 `test_dag_runner.c` 增加加载后构造/或专用小 fixture `tests/fixtures/dag_timeout.json5`：

```json5
{
  "model": { "base_url": "http://127.0.0.1:9", "name": "test", "api_key": "x" },
  "capability_matrix": {
    "enabled": true,
    "root": ".",
    "commands": [
      {
        "name": "sleep_n",
        "description": "slow",
        "argv": ["./tests/fixtures/bin/sleep_n.sh"],
        "timeout_sec": 30,
        "max_output_bytes": 4096,
        "pass_args": "stdin_json"
      }
    ]
  },
  "dags": [
    {
      "name": "tool_timeout",
      "steps": [
        {
          "id": "t",
          "type": "tool",
          "tool": "sleep_n",
          "timeout_sec": 1,
          "retry": { "max": 1 },
          "args": {}
        }
      ]
    }
  ]
}
```

测试：`dag_run` 该图应非 0；stderr 含 `ERROR: timeout` 与 `retry attempt`（第二次）。

参考现有 `test_dag_runner.c` 的加载/运行模式编写断言。

- [ ] **Step 5: `make test`**

```bash
make test
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/core/config.h src/core/config.c src/dag/dag.c \
  tests/fixtures/dag_timeout.json5 tests/fixtures/bin/sleep_n.sh \
  tests/test_dag_runner.c
git commit -m "$(cat <<'EOF'
feat: honor DAG tool step timeout_sec with retry

Fail wall-clock overruns as ERROR: timeout and reuse existing retry.max.
EOF
)"
```

---

## Task 5: 文档

**Files:**
- Modify: `docs/dag.md`、`docs/manual.md` §5.3
- Modify: `docs/architecture.md` §8.1（一行）
- Modify: `docs/superpowers/plans/README.md`（指向本计划）

- [ ] **Step 1: `docs/dag.md`**

在「tool 步可选 retry」后增加「tool 步可选 timeout_sec」：

```markdown
## tool 步可选 timeout_sec

仅 `type: tool`。正整数秒 **1..600** 时覆盖该次 command 墙钟超时；`0` 或缺省不覆盖矩阵/`commands[].timeout_sec`。
超时输出为 `ERROR: timeout`，计入失败，进入上方 `retry.max` 与（若开启）`on_tool_fail.llm`。
builtin / MCP **不保证**步级超时生效。
```

- [ ] **Step 2: `docs/manual.md` §5.3**

用字段表替换「每行形如」：

| 字段 | 含义 |
|------|------|
| `v` | schema 版本（当前 1） |
| `ts` | unix 秒 |
| `run_id` | 本进程运行关联 id |
| `name` | 事件名 |
| `ok` | 0/1 |
| `ms` | 耗时毫秒 |
| `detail` | 短摘要 |

- [ ] **Step 3: architecture §8.1**

「规划选型稳健」旁或「步骤可观测」行注明 tool 步可选 `timeout_sec`。

- [ ] **Step 4: Commit + 勾选本计划复选框**

```bash
git add docs/dag.md docs/manual.md docs/architecture.md \
  docs/superpowers/plans/2026-09-13-dag-timeout-events-v1.md \
  docs/superpowers/plans/README.md
git commit -m "$(cat <<'EOF'
docs: document tool timeout_sec and NEO_EVENTS v1 fields

EOF
)"
```

---

## Self-review（对照 spec）

| Spec 要求 | Task |
|-----------|------|
| `timeout_sec` 1..600、0 不覆盖 | Task 4 |
| 超时 → retry / 热线 | Task 4（复用现逻辑） |
| command override 路径 | Task 2–3 |
| builtin/MCP 不保证 | Task 3/5 文档 |
| `v` + `run_id` | Task 1 |
| 手册字段表 + dag.md | Task 5 |
| 无并行 / usage | 全局约束 |

无 TBD 占位；`neo_dispatch_tool` 全仓补参已写入 Task 3。
