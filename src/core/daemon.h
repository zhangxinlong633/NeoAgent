#ifndef NEO_DAEMON_H
#define NEO_DAEMON_H

#include "config.h"

/* render：交互式 stdin 下对助手回复做 Markdown 终端渲染（与 CLI 默认一致）。 */
int run_daemon_stdin(agent_config_t *conf, int debug, int verbose, int render);
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug, int verbose);

#endif
