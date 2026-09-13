#ifndef NEO_WORKFLOW_H
#define NEO_WORKFLOW_H

#include "config.h"

/*
 * 确定性 DAG 执行器。
 * 拓扑在配置里冻结；LLM 只做 worker（及可选的 tool 失败热线闸门），不在执行期改图。
 */

/* 展开 {{prev}} / {{steps.<id>}}；未知变量返回 NULL。调用方 free。 */
char *dag_expand_template(const char *tmpl, const char *prev,
                               const char **ids, const char **texts, int n_maps);

/*
 * 预检 DAG.requires：每个能力名须在 Capability Matrix 中存在且 enabled。
 * requires 为空 / 缺省：通过。cmd 用于 stderr 前缀（如 "plan" / "run" / "dag"）。
 * 返回 0 通过；>0 为缺失个数；-1 参数/内部错误。
 */
int dag_check_requires(const agent_config_t *conf, const char *cmd, const dag_t *dag);

/* 按名运行 DAG；成功时 *out_text 为最终输出（调用方 free）。verbose：stderr 步骤日志。 */
int dag_run(const agent_config_t *conf, const char *dag_name, char **out_text,
                 int verbose);

/* 解析 on_tool_fail 热线回复：1=RETRY，0=ABORT（含不清/空）。 */
int dag_parse_fail_llm_reply(const char *text);

#endif
