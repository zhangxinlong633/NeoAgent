#ifndef NEO_AGENT_TOOLS_H
#define NEO_AGENT_TOOLS_H

#include "config.h"
#include "llm.h"

/*
 * 反应式 tool loop：把能力矩阵行作为 OpenAI tools 发给模型，并在本地沙箱执行。
 * 路径类能力限制在 capability_matrix.root（内部字段 conf->tools.root）下。
 */

/*
 * 多轮 chat/completions + tool_calls，直到模型不再调工具或达 max_rounds。
 * prefix_messages：daemon 历史；可为 NULL。成功时 out_text 为最终助手文本。
 */
int agent_run_with_tools(
  const agent_config_t *conf,
  const char *system_prompt,
  const llm_message_t *prefix_messages,
  int n_prefix,
  const char *user_message,
  llm_response_t *out_text);

/*
 * 按能力名分发一次调用（builtin / commands / mcp_*）。
 * timeout_override_sec>0 时覆盖 command 墙钟；0 表示用矩阵默认。builtin/MCP 不保证。
 * 即使工具返回 "ERROR: ..." 字符串也返回 0（表示已处理）；调用方 free *out_text。
 */
int neo_dispatch_tool(const agent_config_t *conf, const char *root_real,
                      const char *name, const char *args_json,
                      char **out_text, size_t *out_len,
                      int timeout_override_sec);

#endif
