#ifndef NEO_DAEMON_H
#define NEO_DAEMON_H

#include "config.h"

/*
 * render：交互式 stdin 下对助手回复做 Markdown 终端渲染。
 * session_ids / n_ids：可选落盘会话挂载（与 chat -S 同语义：多 id 拼接加载，只写回 ids[0]）。
 * n_ids==0：纯内存（今日默认）；不默认挂 default。
 */
int run_daemon_stdin(agent_config_t *conf, int debug, int verbose, int render,
                     char *const *session_ids, int n_ids);
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug, int verbose,
                      char *const *session_ids, int n_ids);

#endif
