# src/core/

运行时核心模块：JSON5 配置加载（含 `capability_matrix` 与 dag 解析）、daemon 多轮会话状态、HTTPS 门面 `neo_http`（封装 vendor BearHttpsClient），以及 `neo_md_term`（vendored md4c → 终端 Markdown，供 CLI `-R` / `--render`）。Prompt 拼装中的 claw 块（soul / bootstrap / rules / memory）在 `cli/main.c` 与 `daemon.c` 完成。

对外配置键名约定见 [`AGENTS.md`](../../AGENTS.md) §3。本目录无子目录。Skills 子系统已移除。
