# scripts/tools/

示例命令工具脚本，路径相对仓库根写入 `capability_matrix.commands[].argv` 或 `capabilities/<场景>/*.json5`。

| 文件 | 用途 |
|------|------|
| `echo-args.sh` | 演示 `pass_args` / `NEO_TOOL_ARGS` |
| `unix-exec.sh` | Unix 能力包参数封装（相对路径沙箱） |
| `weather-wttr.sh` | 按城市查 wttr.in 当前天气（供 `weather_wttr`） |
| `gen-unix-pack.py` | 重新生成 `capabilities/unix/` 定义与 `CATALOG.md` |

用于演示与本地试验；生产部署应改为受控可执行文件，并遵守路径沙箱与 Policy。
