# capabilities/unix/

Unix 常用工具能力包：约 **127** 个命令定义（用法、选型元数据、样例输出）。

## 加载策略

| 文件 | 作用 |
|------|------|
| `enabled.json5` | 白名单：仅 `load` 中的能力进入矩阵（含本机诊断：`unix_ps` / `unix_sysctl_hw` / `unix_vm_stat` / `unix_ping`） |
| `unix_*.json5` | 全量定义；未列入白名单者不进矩阵 |
| `CATALOG.md` | 用法与样例输出总表 |

存在 `enabled.json5` 时只装载名单；否则装载全部（受命令数上限约束，不推荐）。

## 执行约定

- 参数型：`./scripts/tools/unix-exec.sh` + 绝对二进制；`parameters.argv` → `NEO_TOOL_ARGS`。
- 无参探针：直接 exec 绝对路径。
- 危险命令默认不在白名单；与 builtin 重叠时优先 builtin。

编辑 `enabled.json5` 后须重启 `neo`。本目录无子目录。关联：[`docs/applications.md`](../../docs/applications.md)、[`docs/tool.md`](../../docs/tool.md)。
