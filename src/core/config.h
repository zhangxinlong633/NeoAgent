#ifndef NEO_CONFIG_H
#define NEO_CONFIG_H

typedef struct yyjson_val yyjson_val;

typedef struct {
  char *provider;
  char *base_url;
  char *name;
  char *api_key;
  int max_tokens;
  double temperature;
} model_config_t;

typedef struct {
  char **paths;
  int path_count;
  int max_chars_per_file;
} bootstrap_config_t;

typedef struct {
  char *path;
  int max_chars;
  /* vector.*：本地检索增强；默认关闭，行为与仅截断 path 一致 */
  int vector_enabled;
  char *vector_store;
  int vector_top_k;
  int vector_dims;
  /* vector.auto_store：对话启发式写入；默认关 */
  int auto_store_enabled;
  int auto_store_max_chars;
} memory_config_t;

/* OpenClaw-style persona file (injected before bootstrap as ## Soul) */
typedef struct {
  char *path;
  int max_chars;
} soul_config_t;

/* Project rules / constraints (like Cursor rules; injected after bootstrap) */
typedef struct {
  char **paths;
  int path_count;
  int max_chars_per_file;
} rules_config_t;

/* 会话内角色：--role 切换；prompt 注入 ## Role */
typedef struct {
  char *name;
  char *description; /* 可选 */
  char *prompt;      /* 必填 */
} neo_role_t;

/* Anchor prompts to the process working directory (run neo from repo root). */
typedef struct {
  int prompt_cwd; /* 1: append cwd to system prompt */
} workspace_config_t;

/* Declared external command (capability_matrix.commands). pass_args: 0=stdin_json, 1=env */
typedef struct {
  char *name;
  char *description;
  char *when;     /* LLM 选型：适用场景（字符串或 JSON 数组文本） */
  char *when_not; /* LLM 选型：不适用场景 */
  char *tags;     /* 可选标签 */
  char *outcome;  /* 可选：成功时可见结果 */
  char **argv;
  int argv_count;
  int timeout_sec;
  int max_output_bytes;
  int pass_args;
  char *parameters_json; /* optional JSON Schema object; NULL → default {"type":"object"} */
} tool_command_t;

typedef struct {
  char *name;
  char *command;
  char **args;
  int args_count;
  char *url; /* reserved; nonempty → warn and skip in v1 */
  int enabled; /* default 1 */
} mcp_server_config_t;

/*
 * Capability Matrix + Policy 旋钮，来自 JSON 键 "capability_matrix"（旧别名 "tools"）。
 * 结构体字段仍叫 tools 以保持历史命名；对外文档与错误路径用 /capability_matrix/...。
 * 见 AGENTS.md。
 */
typedef struct {
  int enabled;       /* 0 关，1 开（root 下路径类 builtin） */
  char *root;        /* 沙箱根目录（默认 "."） */
  int max_rounds;    /* tool API 最大轮次（默认 16） */
  int max_read_bytes; /* 单次 read_file 上限（默认 262144） */
  int list_dir_max_entries; /* list_dir 返回名数量上限 */
  int http_fetch_enabled;   /* 0：不注册 http_get */
  char *http_allow_hosts;   /* 逗号分隔主机名白名单 */
  int http_fetch_max_bytes; /* 响应体上限 */
  int shell_enabled;        /* 0：不注册 run_command */
  char *directory;          /* 可选：能力文件包目录（一文件一能力） */
  tool_command_t *commands;
  int command_count;
  mcp_server_config_t *mcp_servers;
  int mcp_server_count;
} tools_config_t;

typedef enum {
  DAG_STEP_TOOL = 0,
  DAG_STEP_LLM = 1,
  DAG_STEP_LOOP = 2,
  DAG_STEP_ROUTE = 3
} dag_step_type_t;

#define DAG_MAX_ROUTE_CASES 16

/* 多路 route 的一支；match 为空表示默认支。 */
typedef struct {
  char *match;
  char **then_ids;
  int then_count;
} dag_route_case_t;

typedef struct {
  char *id;
  dag_step_type_t type;
  char *tool;
  char *args_json;
  char *prompt;
  int tools_on; /* llm step JSON "tools":"on"|"off" — NOT the capability_matrix key */
  char **over_ids;
  int over_count;
  int max_iters;
  char **depends_on;
  int depends_count;
  /* route: expand `on`；无 cases 时用 match/then/else；有 cases 时忽略三者分支语义 */
  char *route_on;
  char *route_match;
  char **route_then;
  int route_then_count;
  char **route_else;
  int route_else_count;
  dag_route_case_t *route_cases;
  int route_case_count;
  int retry_max; /* 仅 tool：首次失败后的额外尝试次数，0..3 */
  int timeout_sec; /* 仅 tool：0=不覆盖矩阵；1..600 覆盖本次墙钟 */
} dag_step_t;

typedef struct {
  char *name;
  char *description;
  char *when;     /* LLM 选型：适用场景 */
  char *when_not; /* LLM 选型：不适用场景 */
  char *tags;
  char *requires; /* 依赖的能力名；执行/materialize 前预检矩阵 */
  char *outcome;  /* 成功时可见结果 */
  dag_step_t *steps;
  int step_count;
} dag_t;

typedef struct {
  int target_steps; /* 0 = unset; soft preference for planner */
  int catalog_only; /* 1：仅允许 {"use":...}，禁止现编 dags；默认 0 */
} plan_config_t;

/* DAG 运行时策略（顶层键 "dag"，与 "dags" 数组不同）。 */
typedef struct {
  int on_tool_fail_llm;       /* 0：关（默认）；1：本地 retry 耗尽后问 LLM */
  int on_tool_fail_max_calls; /* 每步最多问几次；0..2，默认 1 */
} dag_runtime_config_t;

typedef struct {
  model_config_t model;
  bootstrap_config_t bootstrap;
  soul_config_t soul;
  rules_config_t rules;
  workspace_config_t workspace;
  memory_config_t memory;
  tools_config_t tools;
  plan_config_t plan;
  dag_runtime_config_t dag_runtime;
  char *dag_directory; /* optional DAG pack dir (one workflow per file) */
  dag_t *dags;
  int dag_count;
  int session_max_turns;
  neo_role_t *roles;
  int role_count;
} agent_config_t;

void config_init(agent_config_t *c);
void config_free(agent_config_t *c);
int config_load_file(agent_config_t *c, const char *path);
void config_apply_env(agent_config_t *c);
const dag_t *config_find_dag(const agent_config_t *c, const char *name);
const neo_role_t *config_find_role(const agent_config_t *c, const char *name);

/* 追加单个 workflow 对象（内联数组或目录文件共用）。err_ctx 用于错误路径文案。 */
int config_append_dag_val(agent_config_t *c, yyjson_val *wobj, const char *err_ctx);

#endif
