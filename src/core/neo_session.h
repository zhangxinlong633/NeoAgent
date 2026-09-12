/* neo_session：具名会话持久化（.neo/sessions/<id>.json），供 CLI -S 跨进程续聊。 */
#ifndef NEO_SESSION_H
#define NEO_SESSION_H

#include "llm.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 会话 id：仅 [A-Za-z0-9_-]，长度 1..64。合法返回 1。 */
int neo_session_id_ok(const char *id);

/* 解析存储路径到 out（相对 cwd：.neo/sessions/<id>.json）。成功 0。 */
int neo_session_path(const char *id, char *out, size_t out_sz);

/*
 * 加载会话消息。*out_msgs 为堆数组，每条 role/content 均为 strdup；
 * 调用方用 neo_session_free 释放。文件不存在则 *n=0、*out_msgs=NULL，仍返回 0。
 */
int neo_session_load(const char *id, llm_message_t **out_msgs, int *n);

void neo_session_free(llm_message_t *msgs, int n);

/* 追加一轮 user+assistant，并按 max_turns（轮=一对消息）裁剪后写回。成功 0。 */
int neo_session_append_turn(const char *id, const char *user, const char *assistant, int max_turns);

/* 删除会话文件（不存在也算成功）。 */
int neo_session_clear(const char *id);

#ifdef __cplusplus
}
#endif

#endif /* NEO_SESSION_H */
