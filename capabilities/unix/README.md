# capabilities/unix/

Unix 常用工具能力包：约 **127** 个命令定义（用法、选型元数据、样例输出）。

## 白名单模板（三选一）

| 模板 | 文件 | 说明 |
|------|------|------|
| 只读诊断 | [`enabled.readonly.json5.example`](enabled.readonly.json5.example) | 无 `unix_curl` / `unix_ping`；本机探针与文本工具 |
| 含 curl | [`enabled.with_curl.json5.example`](enabled.with_curl.json5.example) | 只读诊断 + `unix_curl`（及可选 `unix_ping`）；与当前默认 `enabled.json5` 接近 |
| 全量（不推荐） | 见 [`enabled.full.md`](enabled.full.md) | **删除** `enabled.json5` 即装载全部 `unix_*.json5` |

```bash
cp capabilities/unix/enabled.readonly.json5.example capabilities/unix/enabled.json5
# 或
cp capabilities/unix/enabled.with_curl.json5.example capabilities/unix/enabled.json5
```

编辑 `enabled.json5` 后须重启 `neo`。

## 加载策略

| 文件 | 作用 |
|------|------|
| `enabled.json5` | 白名单：仅 `load` 中的能力进入矩阵 |
| `unix_*.json5` | 全量定义；未列入白名单者不进矩阵 |
| `CATALOG.md` | 用法与样例输出总表（含「危险/默认禁用」列） |

存在 `enabled.json5` 时只装载名单；否则装载全部。

## 上限与危险命令

- 运行时 `MAX_COMMANDS` / `CAP_DIR_MAX_COMMANDS` = **256**（含 `capabilities/local`、`git`、配置内联 `commands`）。unix 全量约 127，可装但会挤占配额；默认仍宜精简白名单。
- **危险命令**（`CATALOG.md` 中标记为危险者）默认不在白名单，例如：`unix_rm`、`unix_kill`、`unix_pkill`、`unix_chmod`、`unix_chown`、`unix_dd`、`unix_wget` 等。经 Policy 审查后再加入 `load`。
- 与 builtin 重叠时（如 `list_dir` vs `unix_ls`）优先用 builtin。

## 执行约定

- 参数型：`./scripts/tools/unix-exec.sh` + 绝对二进制；`parameters.argv` → `NEO_TOOL_ARGS`。
- 无参探针：直接 exec 绝对路径。

本目录无子目录。关联：[`docs/applications.md`](../../docs/applications.md)、[`docs/tool.md`](../../docs/tool.md)。
