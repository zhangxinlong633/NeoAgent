# Neo 本地向量 Memory（vdb）设计


| 属性  | 内容                                                        |
| --- | --------------------------------------------------------- |
| 日期  | 2026-09-12                                                |
| 状态  | 已批准                                                       |
| 范围  | `src/memory/` 封装 + CLI/daemon 检索注入；embedding **仅本地**；默认关闭 |


## 1. 目标与非目标

**目标**

- 在现有 `MEMORY.md`（`memory.path`）之上，增加可选的**检索增强注入**：按用户问题召回 top_k 片段写入 `## Memory`。
- 向量库用 vendored `src/vendor/vdb.h`（`vdb_save` / `vdb_load`）。
- **先封装再引入**：仅 `src/memory/` 接触 `vdb.h`；CLI/daemon 只调 `neo_memory_`*。
- Embedding **进程内自实现**，不依赖任何外部 embedding API。

**非目标**

- HTTP / OpenAI `/embeddings`

- Capability Matrix 工具 `memory_search` / `memory_add`（可后做）
- 自动把对话写入向量库（仍改 Markdown；索引惰性重建）
- 多租户、重排序、ANN 近似索引

## 2. 模块


| 路径                               | 职责                                                                            |
| -------------------------------- | ----------------------------------------------------------------------------- |
| `src/memory/neo_embed.h` / `.c`  | `neo_embed_text(text, dims, out_float[])`：本地 hashing bag-of-features + L2 归一化 |
| `src/memory/neo_memory.h` / `.c` | 分块、建索引、recall、store 读写；唯一 `#include "vdb.h"`                                  |
| `src/memory/README.md`           | 目录契约                                                                          |


公开 API（示意）：

```c
typedef struct neo_memory neo_memory_t;

neo_memory_t *neo_memory_open(const agent_config_t *conf);
void neo_memory_close(neo_memory_t *m);

/* 按 query 召回；*out 为可注入的 Markdown 文本（调用方 free）。失败返回 -1。 */
int neo_memory_recall(neo_memory_t *m, const char *query, char **out);

/* 强制从 memory.path 重建并保存 store（测试/运维）。 */
int neo_memory_reindex(neo_memory_t *m);
```

当 `memory.vector.enabled == 0` 或未配置：`recall` 行为等价于读文件截断到 `max_chars`（与今日一致）。

## 3. 配置

扩展现有顶层 `memory`（旧 `path` / `max_chars` 兼容）：

```json5
memory: {
  path: "MEMORY.md",
  max_chars: 4000,
  vector: {
    enabled: false,
    store: ".neo/memory.vdb",
    top_k: 5,
    dims: 64,          // 8..256，默认 64
  },
}
```

C：`memory_config_t` 增加 `vector_enabled`、`vector_store`、`vector_top_k`、`vector_dims`。

## 4. 分块与索引

- 按空行或行组切块；单块过长则按字符硬切（保留 UTF-8 边界）。
- 每块：`id` 稳定（如 `chunk-%u` 或内容 hash 前缀）；`metadata` 指向堆上原文副本（`vdb` 的 `void *metadata`）。
- 启动 `open`：若 `enabled` 且 store 存在则 `vdb_load`；否则 `reindex` 后 `vdb_save`。
- `MEMORY.md` mtime 新于 store 时重建（能取 mtime 则用之；否则每次 open 重建——YAGNI 可先「每次 open 若 enabled 则从 path 重建」以保证正确，store 仅加速可选）。

**本期简化**：`enabled` 时每次 `open`/`reindex` 从 `path` 全量重建并 `vdb_save`（正确优先；文件大时再优化增量）。

## 5. Embedding（本地）

`neo_embed_text`：

1. 将文本转为小写 ASCII 字母数字 run（非 ASCII 字节参与 hash）。
2. 滑动 token / 字符 n-gram 映射到 `dims` 桶累加。
3. L2 归一化；全零则均匀微小噪声或单位首维，避免除零。

同文高相似、无关文较低即可，不追求 SOTA。

## 6. Prompt 接入

`main.c` / `daemon.c` 现「读 `memory.path`」处改为：

1. `neo_memory_open`
2. `neo_memory_recall(user_message 或本轮输入)`
3. 非空则 `## Memory` + 文本
4. `neo_memory_close`

Daemon 多轮：每轮用当前 user 行 recall（或首轮索引、每轮只 search）。

## 7. 测试

- `tests/test_neo_embed.c`：同文距离小、明显不同文更大。
- `tests/test_neo_memory.c`：fixture `MEMORY.md` 含可区分段落；query 命中期望关键词。
- `vector.enabled: false` 时现有 claw 行为不变（冒烟不强制）。



## 9. 修订：store 直写 DB（2026-09-12）

- `neo memory store` 写入 `vector.store` + `.txts` sidecar，**不追加 MEMORY.md**。
- `vector.enabled` 时 `open` 从磁盘装库；`recall` 只查库。
- `vector.enabled` 关时行为不变（截断 `memory.path`）。

