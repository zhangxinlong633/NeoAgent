/* neo_session：具名会话持久化（.neo/sessions/<id>.json），供 CLI -S 跨进程续聊。
 * 支持多 ID：`-S a,b` 按序拼接历史，本轮只写回第一个 ID。 */
#ifndef NEO_SESSION_H
#define NEO_SESSION_H

#include "llm.h"

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单次 -S 最多混入的会话数（含写回目标）。 */
#define NEO_SESSION_MAX_IDS 8

typedef struct {
  char id[65];
  int n_messages; /* messages 条数（一轮 user+assistant = 2） */
  time_t mtime;
} neo_session_info_t;

/* 会话 id：仅 [A-Za-z0-9_-]，长度 1..64。合法返回 1。 */
int neo_session_id_ok(const char *id);

/*
 * 解析 "a,b,c"（逗号分隔，允许空白）为堆数组。
 * 每个 id 须合法；禁止空段与重复；最多 NEO_SESSION_MAX_IDS 个。
 * 成功 0；*out_ids 用 neo_session_free_ids 释放。
 */
int neo_session_parse_ids(const char *spec, char ***out_ids, int *out_n);

void neo_session_free_ids(char **ids, int n);

/* 解析存储路径到 out（相对 cwd：.neo/sessions/<id>.json）。成功 0。 */
int neo_session_path(const char *id, char *out, size_t out_sz);

/*
 * 加载会话消息。*out_msgs 为堆数组，每条 role/content 均为 strdup；
 * 调用方用 neo_session_free 释放。文件不存在则 *n=0、*out_msgs=NULL，仍返回 0。
 */
int neo_session_load(const char *id, llm_message_t **out_msgs, int *n);

/*
 * 按序加载多个会话并拼接消息（只读注入用）。
 * 写回仍应对 ids[0] 调用 neo_session_append_turn。
 */
int neo_session_load_many(char *const *ids, int n_ids, llm_message_t **out_msgs, int *n);

void neo_session_free(llm_message_t *msgs, int n);

/* 追加一轮 user+assistant，并按 max_turns（轮=一对消息）裁剪后写回。成功 0。 */
int neo_session_append_turn(const char *id, const char *user, const char *assistant, int max_turns);

/* 删除会话文件（不存在也算成功）。 */
int neo_session_clear(const char *id);

/*
 * 列出 .neo/sessions/ 下各 *.json（按 id 字典序）。
 * 目录不存在则 *n=0。成功 0；用 neo_session_list_free 释放。
 */
int neo_session_list(neo_session_info_t **out, int *n);

void neo_session_list_free(neo_session_info_t *list, int n);

#ifdef __cplusplus
}
#endif

#endif /* NEO_SESSION_H */
