# src/memory/

本目录封装 Neo 的**本地向量记忆**：基于 vendored `vdb.h` 做分块检索，embedding 在进程内自算，不调用外部 embedding API。

CLI / daemon 只通过 `neo_memory.h` 注入 `## Memory`；**禁止**在其它模块直接 `#include "vdb.h"`。

## 职责边界

- 从 `memory.vector.store` 读写本地向量库；`neo memory store` 直写 DB（sidecar 存文本），**不写** `MEMORY.md`。
- `memory.vector.enabled` 为 false 时 recall 回退为 `memory.path` 全文截断。
- vector 开启时 recall **只查库**，忽略 `MEMORY.md`。
- 不负责 Capability Matrix 工具、不负责自动把对话写入笔记。

## 文件

| 文件 | 职责 |
|------|------|
| `neo_embed.h` / `neo_embed.c` | 本地文本 → 定长 float 向量 |
| `neo_memory.h` / `neo_memory.c` | 索引 / recall / 与 vdb 交互 |
| `README.md` | 本目录契约 |

权威说明见 `docs/claw.md` 与 `docs/superpowers/specs/2026-09-12-memory-vdb-design.md`。
