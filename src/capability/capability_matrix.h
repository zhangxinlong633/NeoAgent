#ifndef NEO_CAPABILITY_MATRIX_H
#define NEO_CAPABILITY_MATRIX_H

/*
 * 能力矩阵（Capability Matrix）：可发现的能力行表。
 * builtin / commands / MCP 共用同一套 name，供 tool loop、DAG type:tool、neo plan 使用。
 * MCP 只是往本表灌行的 loader，不是平行工具系统。产品三角：DAG ∥ 矩阵 ∥ Policy — 见 AGENTS.md。
 *
 * Capability Matrix: discoverable rows shared by agent loop, DAG, and neo plan.
 */

#include "config.h"
#include <stddef.h>
#include <stdio.h>

typedef enum {
  CAP_SRC_BUILTIN = 0,
  CAP_SRC_COMMAND = 1,
  CAP_SRC_MCP = 2
} cap_source_t;

typedef enum {
  CAP_EFFECT_READ = 0,
  CAP_EFFECT_WRITE = 1,
  CAP_EFFECT_EXEC = 2,
  CAP_EFFECT_NETWORK = 3
} cap_effect_t;

/* builtin_id values for path tools (0 = none / not a builtin binding) */
enum {
  CAP_BUILTIN_NONE = 0,
  CAP_BUILTIN_READ_FILE = 1,
  CAP_BUILTIN_WRITE_FILE = 2,
  CAP_BUILTIN_LIST_DIR = 3,
  CAP_BUILTIN_HTTP_GET = 4,
  CAP_BUILTIN_GREP = 5,
  CAP_BUILTIN_RUN_COMMAND = 6,
  CAP_BUILTIN_STAT = 7,
  CAP_BUILTIN_MKDIR = 8,
  CAP_BUILTIN_APPEND_FILE = 9,
  CAP_BUILTIN_PROPOSE_CAPABILITY = 10,
  CAP_BUILTIN_MEMORY_ADD = 11
};

typedef struct {
  char *name;
  cap_source_t source;
  char *source_detail; /* e.g. mcp server name; may be NULL */
  char *description;
  char *when;     /* 选型提示：何时用 */
  char *when_not; /* 选型提示：何时不用 */
  char *tags;
  char *parameters_json; /* JSON Schema object text */
  cap_effect_t effect;
  int timeout_sec;
  int max_output_bytes;
  int enabled;
  int builtin_id;
  const tool_command_t *command; /* non-owning; NULL if not command */
  char *mcp_server;
  char *mcp_tool;
} cap_row_t;

typedef struct {
  cap_row_t *rows;
  int count;
  int cap;
} capability_matrix_t;

void capability_matrix_init(capability_matrix_t *m);
void capability_matrix_free(capability_matrix_t *m);
const cap_row_t *capability_matrix_find(const capability_matrix_t *m, const char *name);

/* Heap JSON array of OpenAI tool objects for enabled rows; caller frees. */
char *capability_matrix_tools_json(const capability_matrix_t *m);

/* Human/model listing of enabled capabilities; caller frees. */
char *capability_matrix_prompt_listing(const capability_matrix_t *m);

/* 从 conf 物化矩阵：先清表，再按 Policy 注册 builtin / commands，并触发 MCP 加载。
 * 调用前须 capability_matrix_init（或传入已 free 过的矩阵）。成功返回 0。 */
int capability_matrix_build_from_config(capability_matrix_t *m, const agent_config_t *conf);

/* 单轮 tool_calls 过多时打 stderr 截断警告；返回是否已警告。 */
int capability_warn_tool_truncation(size_t n_calls, int max_per_turn, FILE *err);

/* 追加一行 MCP 来源能力；name 须已规范化（如 mcp_server_tool）。重名则跳过。 */
int capability_matrix_add_mcp(capability_matrix_t *m, const char *name, const char *description,
                              const char *parameters_json, const char *mcp_server,
                              const char *mcp_tool);

#endif
