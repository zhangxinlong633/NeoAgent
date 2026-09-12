#ifndef NEO_DAEMON_H
#define NEO_DAEMON_H

#include "config.h"

int run_daemon_stdin(agent_config_t *conf, int debug, int verbose);
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug, int verbose);

#endif
