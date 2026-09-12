#ifndef NEO_MEMORY_H
#define NEO_MEMORY_H

#include "config.h"

/*
 * 本地向量记忆门面：store 直写 vdb；embedding 仅本地。
 * 其它模块勿直接 include vdb.h。
 */

typedef struct neo_memory neo_memory_t;

neo_memory_t *neo_memory_open(const agent_config_t *conf);
void neo_memory_close(neo_memory_t *m);

/* 按 query 召回可注入文本（调用方 free *out）。失败或无内容返回 -1。 */
int neo_memory_recall(neo_memory_t *m, const char *query, char **out);

/* 将文本写入向量库并持久化（不写 MEMORY.md）。需 vector.enabled。 */
int neo_memory_store(neo_memory_t *m, const char *text);

/*
 * 启发式自动写入：需 vector.enabled 且 auto_store.enabled。
 * 命中触发词则 store 用户句（截断）；verbose 时 stderr 一行。
 * 返回 1=已写入，0=未写，-1=错误。
 */
int neo_memory_auto_store(const agent_config_t *conf, const char *user_text, int verbose);

/* 从磁盘重新装入索引（不从 MEMORY.md 导入）。 */
int neo_memory_reindex(neo_memory_t *m);

int neo_memory_count(const neo_memory_t *m);

#endif
