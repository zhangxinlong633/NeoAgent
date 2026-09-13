# example/usage/

日常用法样例（可复制命令）。假定当前目录为**仓库根**，且已 `make`、已配置 `config/config.json5`。

使用手册见 [`docs/manual.md`](../../docs/manual.md)。权威长样例见 [`docs/examples.md`](../../docs/examples.md)。本目录只收「怎么敲命令」的短清单。

## 样例一览

1. [`chat.md`](chat.md) — 单次对话、Markdown、默认会话
2. [`session.md`](session.md) — `-S` / `-N` / 多会话混合 / 列表与清除
3. [`json-output.md`](json-output.md) — `-j` OpenAI JSON、`-o` 写文件
4. [`dag.md`](dag.md) — `dag run` / `plan` / `run`
5. [`roles.md`](roles.md) — 同会话 `--role` 切换
6. [`tools.md`](tools.md) — 能力矩阵与高铁查询等
7. [`daemon.md`](daemon.md) — `-D` / `daemon` / `--socket`；`User>` / `neo>`
8. [`blender-mcp.md`](blender-mcp.md) — 可选 Blender stdio MCP（不默认开；优先 headless `blender_*`）

在仓库根按文件中的命令执行即可。zsh 中带 `?` / `*` 的句子请加引号。
