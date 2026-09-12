# src/

Neo 的 C99 源码树。编译产物（`.o`）输出到仓库根目录 **`build/`**，禁止在本树内写入对象文件。

头文件包含仍使用短名（如 `"config.h"`）；`Makefile` 通过多路径 `-I` 解析。模块职责见 [`AGENTS.md`](../AGENTS.md) §2。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `cli/` | 命令行入口与子命令分发 |
| `core/` | 配置解析、daemon 多轮会话 |
| `llm/` | OpenAI 兼容 HTTP 客户端 |
| `capability/` | 能力矩阵、目录加载、工具循环、MCP stdio |
| `dag/` | DAG 执行、目录加载、plan |
| `memory/` | 本地向量记忆（`vdb.h` 封装；embedding 进程内） |
| `vendor/` | 第三方 amalgamation（yyjson、vdb、BearHttps）；尽量少改 |
