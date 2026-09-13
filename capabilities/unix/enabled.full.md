# 全量 unix 装载（不推荐）

**不要**维护一份列出全部 ~127 名的 `enabled.json5`。

| 做法 | 效果 |
|------|------|
| **删除**（或移走）本目录的 `enabled.json5` | 装载全部 `unix_*.json5` |
| 保留精简 `enabled.json5` | 仅白名单进矩阵（推荐） |

## 风险

1. **命令数上限**：运行时 `MAX_COMMANDS` / `CAP_DIR_MAX_COMMANDS` = **256**（含 `local` / `git` / 配置内联 `commands`）。unix 全量约 127，通常能装下，但会挤占其它目录配额。
2. **危险命令**会一并进入矩阵（见 [`CATALOG.md`](CATALOG.md) 中「危险/默认禁用」列为 `yes` 者），例如：
   - 文件系统：`unix_rm` / `unix_rmdir` / `unix_mv` / `unix_cp` / `unix_dd` / …
   - 进程：`unix_kill` / `unix_pkill` / …
   - 权限：`unix_chmod` / `unix_chown` / `unix_chgrp`
   - 网络：`unix_curl` / `unix_wget` / `unix_nc` / …
3. Policy 不会因「全量」自动变严；矩阵里出现即可能被模型点名调用。

推荐路径：从 [`enabled.readonly.json5.example`](enabled.readonly.json5.example) 或 [`enabled.with_curl.json5.example`](enabled.with_curl.json5.example) 复制为 `enabled.json5`，按需逐项加名并审查。
