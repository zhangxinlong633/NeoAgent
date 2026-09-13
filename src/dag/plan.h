#ifndef NEO_PLAN_H
#define NEO_PLAN_H

#include "config.h"
#include <stddef.h>

#define PLAN_DEFAULT_TARGET_STEPS 10
#define PLAN_MAX_TARGET_STEPS 32

/*
 * neo plan / neo run：让 LLM 一次性规划，再确定性执行。
 * 优先解析 {"use":[...]} 选用 dag_directory 中的图；否则解析完整 dags JSON 现编。
 */

/* 软步数目标：CLI > conf->plan.target_steps > 默认 10；夹紧到 1..PLAN_MAX_TARGET_STEPS。 */
int plan_resolve_target_steps(const agent_config_t *conf, int cli_steps);

/*
 * 是否仅允许 catalog use（禁止现编 dags）。
 * 环境变量 NEO_PLAN_CATALOG_ONLY=1/true/yes 开启；=0/false/no 关闭；未设则读 plan.catalog_only。
 */
int plan_catalog_only_enabled(const agent_config_t *conf);

/* catalog_only 时拒绝 invent：stderr 说明后返回 -1；否则返回 0。 */
int plan_ensure_invent_allowed(const agent_config_t *conf, const char *cmd);

/* 从模型输出抽出含 "dags" 的 JSON（支持 fenced 代码块）。调用方 free *out_json。 */
int plan_extract_dags_json(const char *llm_text, char **out_json);

/* 抽出 {"use":"name"} 或 {"use":["a","b"]}。调用方 plan_free_use。 */
int plan_extract_use(const char *llm_text, char ***out_names, int *out_n);
void plan_free_use(char **names, int n);

/*
 * 把误写入 use 的能力名合成单图多 tool 步的 dags JSON（name=adhoc_tools）。
 * 供 plan_run 降级路径与单测；调用方 free 返回值。
 */
char *plan_dags_json_for_tools(char **names, int n);

/*
 * 生成 DAG catalog 短名单（逗号分隔），供未知 use 错误提示。
 * max_names：最多列出几个；max_chars：总长度上限（含省略号）。
 * 无图时返回 "(none)"。调用方 free。
 */
char *plan_format_catalog_hint(const agent_config_t *conf, int max_names, size_t max_chars);

/*
 * 对 use 中既非 catalog 也非 capability 的名字打印 stderr：
 *   neo <cmd>: unknown use 'X' (not a catalog DAG or Capability Matrix name)
 * 并附带一行 catalog 摘要。返回未知名个数。
 */
int plan_report_unknown_use_names(const agent_config_t *conf, const char *cmd,
                                  char *const *use_names, int use_n);

/* 拼 planner system prompt：DAG catalog + 能力矩阵名单 + 知识/工程路由规则。调用方 free。 */
char *plan_build_system_prompt(const agent_config_t *conf, int target_steps);

/* 把规划出的 dags 与 base 的 model/capability_matrix 合成临时配置并 load 校验。 */
int plan_materialize_config(const agent_config_t *base, const char *dags_json,
                            agent_config_t *out_conf, char *out_path, size_t out_path_sz);

/*
 * 调 LLM → 优先 use 选型跑目录 DAG，否则 materialize 现编图并可选执行。
 * quiet_plan=0 时把 use/dags JSON 打到 stdout。
 */
int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_plan,
             int target_steps, const char *save_path, int debug, int verbose);

#endif
