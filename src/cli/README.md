# src/cli/

命令行入口：解析全局选项（含 `-v` / `--verbose`、`-D` / `--daemon`），分发单次查询、`daemon`、`dag run`、`plan` / `run`（已知图名走 DAG）等子命令。未知以 `-` 开头的参数会报错（不再当成聊天正文）。`dag run` 为弃用别名。

主文件为 `main.c`。本目录无子目录。
