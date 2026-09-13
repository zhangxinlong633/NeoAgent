#ifndef NEO_COMMAND_TOOLS_H
#define NEO_COMMAND_TOOLS_H

#include "config.h"
#include <stddef.h>

/* 白名单命令工具：只 exec 配置/目录声明过的 argv，不拼 shell。 */

/* 按 name 查找 command 下标，找不到返回 -1。 */
int command_tool_find(const agent_config_t *conf, const char *name);

/*
 * 在 root_real 下执行已声明命令；stdin_json 或 env 传参。
 * timeout_override_sec>0 时覆盖本趟墙钟（不改 cmd 结构体）；否则用 cmd->timeout_sec。
 * 进程非零退出仍返回 0，但输出带 EXIT:<code> 前缀（workflow 会当失败）。
 * 致命准备错误返回 -1。调用方 free *out_text。
 */
int command_tool_run(const agent_config_t *conf, const char *root_real, const tool_command_t *cmd,
                     const char *args_json, char **out_text, size_t *out_len,
                     int timeout_override_sec);

#endif
