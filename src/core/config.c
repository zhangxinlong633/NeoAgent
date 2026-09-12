#include "config.h"
#include "capability_dir.h"
#include "dag_dir.h"
#include "yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STR 512
#define MAX_PATHS 32
#define MAX_COMMANDS 128
#define MAX_ARGV 16

static char *dup_str(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

static void free_path_list(char **paths, int n) {
  int i;
  if (!paths) return;
  for (i = 0; i < n; i++) free(paths[i]);
  free(paths);
}

static void free_mcp_servers(mcp_server_config_t *srvs, int n) {
  int i, j;
  if (!srvs) return;
  for (i = 0; i < n; i++) {
    free(srvs[i].name);
    free(srvs[i].command);
    free(srvs[i].url);
    if (srvs[i].args) {
      for (j = 0; j < srvs[i].args_count; j++) free(srvs[i].args[j]);
      free(srvs[i].args);
    }
  }
  free(srvs);
}

static void free_tool_commands(tool_command_t *cmds, int n) {
  int i, j;
  if (!cmds) return;
  for (i = 0; i < n; i++) {
    free(cmds[i].name);
    free(cmds[i].description);
    free(cmds[i].when);
    free(cmds[i].when_not);
    free(cmds[i].tags);
    free(cmds[i].outcome);
    free(cmds[i].parameters_json);
    if (cmds[i].argv) {
      for (j = 0; j < cmds[i].argv_count; j++) free(cmds[i].argv[j]);
      free(cmds[i].argv);
    }
  }
  free(cmds);
}

static int tool_command_name_reserved(const char *name) {
  return name && (strcmp(name, "read_file") == 0 || strcmp(name, "write_file") == 0 ||
                  strcmp(name, "list_dir") == 0 || strcmp(name, "http_get") == 0 ||
                  strcmp(name, "grep") == 0 || strcmp(name, "run_command") == 0 ||
                  strcmp(name, "stat") == 0 || strcmp(name, "mkdir") == 0 ||
                  strcmp(name, "append_file") == 0 || strcmp(name, "propose_capability") == 0 ||
                  strcmp(name, "memory_add") == 0);
}

static void free_dags(dag_t *wfs, int n) {
  int i, j, k;
  if (!wfs) return;
  for (i = 0; i < n; i++) {
    free(wfs[i].name);
    free(wfs[i].description);
    free(wfs[i].when);
    free(wfs[i].when_not);
    free(wfs[i].tags);
    free(wfs[i].requires);
    free(wfs[i].outcome);
    if (wfs[i].steps) {
      for (j = 0; j < wfs[i].step_count; j++) {
        dag_step_t *s = &wfs[i].steps[j];
        free(s->id);
        free(s->tool);
        free(s->args_json);
        free(s->prompt);
        if (s->over_ids) {
          for (k = 0; k < s->over_count; k++) free(s->over_ids[k]);
          free(s->over_ids);
        }
        if (s->depends_on) {
          for (k = 0; k < s->depends_count; k++) free(s->depends_on[k]);
          free(s->depends_on);
        }
        free(s->route_on);
        free(s->route_match);
        if (s->route_then) {
          for (k = 0; k < s->route_then_count; k++) free(s->route_then[k]);
          free(s->route_then);
        }
        if (s->route_else) {
          for (k = 0; k < s->route_else_count; k++) free(s->route_else[k]);
          free(s->route_else);
        }
        if (s->route_cases) {
          for (k = 0; k < s->route_case_count; k++) {
            int t;
            free(s->route_cases[k].match);
            if (s->route_cases[k].then_ids) {
              for (t = 0; t < s->route_cases[k].then_count; t++)
                free(s->route_cases[k].then_ids[t]);
              free(s->route_cases[k].then_ids);
            }
          }
          free(s->route_cases);
        }
      }
      free(wfs[i].steps);
    }
  }
  free(wfs);
}

static int dag_id_exists(const dag_t *wf, const char *id) {
  int m;
  if (!id) return 0;
  for (m = 0; m < wf->step_count; m++) {
    if (wf->steps[m].id && strcmp(wf->steps[m].id, id) == 0) return 1;
  }
  return 0;
}

static int validate_dags(agent_config_t *c) {
  int i, j, k, m;
  for (i = 0; i < c->dag_count; i++) {
    dag_t *wf = &c->dags[i];
    if (!wf->name || !wf->name[0]) {
      fprintf(stderr, "neo: dags[%d]: empty name\n", i);
      return -1;
    }
    for (j = 0; j < i; j++) {
      if (c->dags[j].name && strcmp(c->dags[j].name, wf->name) == 0) {
        fprintf(stderr, "neo: dags: duplicate name '%s'\n", wf->name);
        return -1;
      }
    }
    for (j = 0; j < wf->step_count; j++) {
      dag_step_t *s = &wf->steps[j];
      if (!s->id || !s->id[0]) {
        fprintf(stderr, "neo: DAG '%s' step %d: empty id\n", wf->name, j);
        return -1;
      }
      for (k = 0; k < j; k++) {
        if (wf->steps[k].id && strcmp(wf->steps[k].id, s->id) == 0) {
          fprintf(stderr, "neo: DAG '%s': duplicate step id '%s'\n", wf->name, s->id);
          return -1;
        }
      }
      for (k = 0; k < s->depends_count; k++) {
        if (!s->depends_on[k] || !dag_id_exists(wf, s->depends_on[k])) {
          fprintf(stderr, "neo: DAG '%s' step '%s': unknown depends_on '%s'\n",
                  wf->name, s->id, s->depends_on[k] ? s->depends_on[k] : "?");
          return -1;
        }
        if (strcmp(s->depends_on[k], s->id) == 0) {
          fprintf(stderr, "neo: DAG '%s' step '%s': depends_on self\n", wf->name, s->id);
          return -1;
        }
      }
      if (s->type == DAG_STEP_TOOL) {
        /* Planner often omits type: llm and only sets prompt — coerce. */
        if ((!s->tool || !s->tool[0]) && s->prompt && s->prompt[0]) {
          s->type = DAG_STEP_LLM;
        } else if (!s->tool || !s->tool[0]) {
          fprintf(stderr, "neo: DAG '%s' step '%s': tool required\n", wf->name, s->id);
          return -1;
        }
      }
      if (s->type == DAG_STEP_LLM) {
        if (!s->prompt || !s->prompt[0]) {
          fprintf(stderr, "neo: DAG '%s' step '%s': prompt required\n", wf->name, s->id);
          return -1;
        }
      } else if (s->type == DAG_STEP_LOOP) {
        if (s->max_iters < 1) {
          fprintf(stderr, "neo: DAG '%s' step '%s': loop max must be >= 1\n", wf->name, s->id);
          return -1;
        }
        if (s->over_count < 1) {
          fprintf(stderr, "neo: DAG '%s' step '%s': loop over empty\n", wf->name, s->id);
          return -1;
        }
        for (k = 0; k < s->over_count; k++) {
          int found = 0;
          if (!s->over_ids[k]) continue;
          if (strcmp(s->over_ids[k], s->id) == 0) {
            fprintf(stderr, "neo: DAG '%s' step '%s': loop cannot include self\n", wf->name, s->id);
            return -1;
          }
          for (m = 0; m < wf->step_count; m++) {
            if (wf->steps[m].id && strcmp(wf->steps[m].id, s->over_ids[k]) == 0) {
              if (wf->steps[m].type == DAG_STEP_LOOP) {
                fprintf(stderr, "neo: DAG '%s': loop over cannot include loop step '%s'\n",
                        wf->name, s->over_ids[k]);
                return -1;
              }
              found = 1;
              break;
            }
          }
          if (!found) {
            fprintf(stderr, "neo: DAG '%s' step '%s': unknown over id '%s'\n",
                    wf->name, s->id, s->over_ids[k]);
            return -1;
          }
        }
      } else if (s->type == DAG_STEP_ROUTE) {
        if (!s->route_on || !s->route_on[0]) {
          fprintf(stderr, "neo: DAG '%s' step '%s': route on required\n", wf->name, s->id);
          return -1;
        }
        if (s->route_case_count > 0) {
          int defaults = 0;
          for (k = 0; k < s->route_case_count; k++) {
            int t;
            const char *m = s->route_cases[k].match;
            if (!m || !m[0]) defaults++;
            for (t = 0; t < s->route_cases[k].then_count; t++) {
              if (!dag_id_exists(wf, s->route_cases[k].then_ids[t])) {
                fprintf(stderr, "neo: DAG '%s' step '%s': unknown cases[%d] then id\n",
                        wf->name, s->id, k);
                return -1;
              }
            }
          }
          if (defaults > 1) {
            fprintf(stderr, "neo: DAG '%s' step '%s': at most one default route case\n",
                    wf->name, s->id);
            return -1;
          }
        } else if (s->route_then_count < 1 && s->route_else_count < 1) {
          fprintf(stderr, "neo: DAG '%s' step '%s': route then/else empty\n", wf->name, s->id);
          return -1;
        }
        if (s->route_case_count < 1) {
          for (k = 0; k < s->route_then_count; k++) {
            if (!dag_id_exists(wf, s->route_then[k])) {
              fprintf(stderr, "neo: DAG '%s' step '%s': unknown then id\n", wf->name, s->id);
              return -1;
            }
          }
          for (k = 0; k < s->route_else_count; k++) {
            if (!dag_id_exists(wf, s->route_else[k])) {
              fprintf(stderr, "neo: DAG '%s' step '%s': unknown else id\n", wf->name, s->id);
              return -1;
            }
          }
        }
      }
      if (s->retry_max > 0 && s->type != DAG_STEP_TOOL) {
        fprintf(stderr, "neo: DAG '%s' step '%s': retry only allowed on type tool\n",
                wf->name, s->id);
        return -1;
      }
    }
  }
  return 0;
}

const dag_t *config_find_dag(const agent_config_t *c, const char *name) {
  int i;
  if (!c || !name) return NULL;
  for (i = 0; i < c->dag_count; i++) {
    if (c->dags[i].name && strcmp(c->dags[i].name, name) == 0)
      return &c->dags[i];
  }
  return NULL;
}

static int validate_tool_commands(agent_config_t *c) {
  int i, j;
  for (i = 0; i < c->tools.command_count; i++) {
    tool_command_t *cmd = &c->tools.commands[i];
    if (!cmd->name || !cmd->name[0]) {
      fprintf(stderr, "neo: capability_matrix.commands[%d]: empty name\n", i);
      return -1;
    }
    if (tool_command_name_reserved(cmd->name)) {
      fprintf(stderr, "neo: capability_matrix.commands: name '%s' is reserved\n", cmd->name);
      return -1;
    }
    if (cmd->argv_count < 1 || !cmd->argv || !cmd->argv[0] || !cmd->argv[0][0]) {
      fprintf(stderr, "neo: capability_matrix.commands '%s': empty argv\n", cmd->name);
      return -1;
    }
    for (j = 0; j < i; j++) {
      if (c->tools.commands[j].name && strcmp(c->tools.commands[j].name, cmd->name) == 0) {
        fprintf(stderr, "neo: capability_matrix.commands: duplicate name '%s'\n", cmd->name);
        return -1;
      }
    }
    if (cmd->timeout_sec <= 0) cmd->timeout_sec = 30;
    if (cmd->max_output_bytes <= 0) cmd->max_output_bytes = 65536;
  }
  return 0;
}

void config_init(agent_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->soul.max_chars = 8000;
  c->rules.max_chars_per_file = 8000;
  c->memory.max_chars = 4000;
  c->memory.vector_top_k = 5;
  c->memory.vector_dims = 64;
  c->memory.auto_store_max_chars = 500;
  c->tools.max_rounds = 16;
  c->tools.max_read_bytes = 262144;
  c->tools.list_dir_max_entries = 256;
  c->tools.http_fetch_enabled = 0;
  c->tools.http_fetch_max_bytes = 262144;
  c->tools.shell_enabled = 0;
  c->dag_runtime.on_tool_fail_llm = 0;
  c->dag_runtime.on_tool_fail_max_calls = 1;
}

void config_free(agent_config_t *c) {
  free(c->model.provider);
  free(c->model.base_url);
  free(c->model.name);
  free(c->model.api_key);
  c->model.provider = c->model.base_url = c->model.name = c->model.api_key = NULL;
  free_path_list(c->bootstrap.paths, c->bootstrap.path_count);
  c->bootstrap.paths = NULL;
  c->bootstrap.path_count = 0;
  free(c->soul.path);
  c->soul.path = NULL;
  free_path_list(c->rules.paths, c->rules.path_count);
  c->rules.paths = NULL;
  c->rules.path_count = 0;
  free(c->memory.path);
  c->memory.path = NULL;
  free(c->memory.vector_store);
  c->memory.vector_store = NULL;
  free(c->tools.root);
  c->tools.root = NULL;
  free(c->tools.directory);
  c->tools.directory = NULL;
  free(c->dag_directory);
  c->dag_directory = NULL;
  free(c->tools.http_allow_hosts);
  c->tools.http_allow_hosts = NULL;
  free_tool_commands(c->tools.commands, c->tools.command_count);
  c->tools.commands = NULL;
  c->tools.command_count = 0;
  free_mcp_servers(c->tools.mcp_servers, c->tools.mcp_server_count);
  c->tools.mcp_servers = NULL;
  c->tools.mcp_server_count = 0;
  free_dags(c->dags, c->dag_count);
  c->dags = NULL;
  c->dag_count = 0;
}

static int path_has_ext(const char *path, const char *ext) {
  size_t lp, le;
  if (!path || !ext) return 0;
  lp = strlen(path);
  le = strlen(ext);
  if (lp < le) return 0;
  return strcmp(path + lp - le, ext) == 0;
}

static char *yy_dup_str(yyjson_val *v) {
  const char *s;
  size_t n;
  char *o;
  if (!yyjson_is_str(v)) return NULL;
  s = yyjson_get_str(v);
  n = yyjson_get_len(v);
  o = malloc(n + 1);
  if (!o) return NULL;
  memcpy(o, s ? s : "", n);
  o[n] = '\0';
  return o;
}

/* 选型元数据：字符串原样，或数组/对象写成 JSON 文本供 listing。 */
static char *yy_meta_text(yyjson_val *v) {
  if (!v) return NULL;
  if (yyjson_is_str(v)) return yy_dup_str(v);
  if (yyjson_is_arr(v) || yyjson_is_obj(v)) return yyjson_val_write(v, 0, NULL);
  return NULL;
}

static int yy_bool(yyjson_val *v, int *out) {
  if (yyjson_is_bool(v)) {
    *out = yyjson_get_bool(v) ? 1 : 0;
    return 0;
  }
  if (yyjson_is_str(v)) {
    const char *s = yyjson_get_str(v);
    if (!s) return -1;
    if (!strcmp(s, "true") || !strcmp(s, "yes") || !strcmp(s, "on") || !strcmp(s, "1")) {
      *out = 1;
      return 0;
    }
    if (!strcmp(s, "false") || !strcmp(s, "no") || !strcmp(s, "off") || !strcmp(s, "0")) {
      *out = 0;
      return 0;
    }
  }
  return -1;
}

static int yy_string_array(yyjson_val *arr, char ***out, int *out_n, int max_n, const char *jpath) {
  size_t i, n;
  char **argv = NULL;
  int count = 0;
  *out = NULL;
  *out_n = 0;
  if (!yyjson_is_arr(arr)) {
    fprintf(stderr, "neo: config error at %s: expected array\n", jpath);
    return -1;
  }
  n = yyjson_arr_size(arr);
  for (i = 0; i < n; i++) {
    yyjson_val *el = yyjson_arr_get(arr, i);
    char *dup;
    char **np;
    if (!yyjson_is_str(el)) {
      fprintf(stderr, "neo: config error at %s/%zu: expected string\n", jpath, i);
      free_path_list(argv, count);
      return -1;
    }
    if (count >= max_n) {
      fprintf(stderr, "neo: config error at %s: too many entries\n", jpath);
      free_path_list(argv, count);
      return -1;
    }
    dup = yy_dup_str(el);
    if (!dup) {
      free_path_list(argv, count);
      return -1;
    }
    np = realloc(argv, (count + 1) * sizeof(char *));
    if (!np) {
      free(dup);
      free_path_list(argv, count);
      return -1;
    }
    argv = np;
    argv[count++] = dup;
  }
  *out = argv;
  *out_n = count;
  return 0;
}

static void cfg_set_str(char **dst, yyjson_val *obj, const char *key) {
  yyjson_val *v = yyjson_obj_get(obj, key);
  char *s;
  if (!v) return;
  s = yy_dup_str(v);
  if (!s) return;
  free(*dst);
  *dst = s;
}

static int fill_model(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /model: expected object\n");
    return -1;
  }
  cfg_set_str(&c->model.provider, obj, "provider");
  cfg_set_str(&c->model.base_url, obj, "base_url");
  cfg_set_str(&c->model.name, obj, "name");
  cfg_set_str(&c->model.api_key, obj, "api_key");
  v = yyjson_obj_get(obj, "max_tokens");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->model.max_tokens = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "temperature");
  if (yyjson_is_num(v)) c->model.temperature = yyjson_get_real(v);
  return 0;
}

static int fill_memory(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v, *vec;
  int b;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /memory: expected object\n");
    return -1;
  }
  cfg_set_str(&c->memory.path, obj, "path");
  v = yyjson_obj_get(obj, "max_chars");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->memory.max_chars = (int)yyjson_get_sint(v);
  /* vector.*：可选本地检索；缺省关闭，与历史全文截断一致 */
  vec = yyjson_obj_get(obj, "vector");
  if (yyjson_is_obj(vec)) {
    v = yyjson_obj_get(vec, "enabled");
    if (v && yy_bool(v, &b) == 0) c->memory.vector_enabled = b;
    cfg_set_str(&c->memory.vector_store, vec, "store");
    v = yyjson_obj_get(vec, "top_k");
    if (yyjson_is_int(v) || yyjson_is_uint(v)) {
      int k = (int)yyjson_get_sint(v);
      if (k > 0) c->memory.vector_top_k = k;
    }
    v = yyjson_obj_get(vec, "dims");
    if (yyjson_is_int(v) || yyjson_is_uint(v)) {
      int d = (int)yyjson_get_sint(v);
      if (d >= 8 && d <= 256) c->memory.vector_dims = d;
    }
    {
      yyjson_val *as = yyjson_obj_get(vec, "auto_store");
      if (yyjson_is_obj(as)) {
        v = yyjson_obj_get(as, "enabled");
        if (v && yy_bool(v, &b) == 0) c->memory.auto_store_enabled = b;
        v = yyjson_obj_get(as, "max_chars");
        if (yyjson_is_int(v) || yyjson_is_uint(v)) {
          int mc = (int)yyjson_get_sint(v);
          if (mc > 0) c->memory.auto_store_max_chars = mc;
        }
      }
    }
  }
  return 0;
}

static int fill_soul(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /soul: expected object\n");
    return -1;
  }
  cfg_set_str(&c->soul.path, obj, "path");
  v = yyjson_obj_get(obj, "max_chars");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->soul.max_chars = (int)yyjson_get_sint(v);
  return 0;
}

static int fill_workspace(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  int b;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /workspace: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "prompt_cwd");
  if (v && yy_bool(v, &b) == 0) c->workspace.prompt_cwd = b;
  return 0;
}

static int fill_session(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /session: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "max_turns");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->session_max_turns = (int)yyjson_get_sint(v);
  return 0;
}

static int fill_plan(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /plan: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "target_steps");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->plan.target_steps = (int)yyjson_get_sint(v);
  return 0;
}

/* 顶层 "dag"：运行时策略（非 dags 数组）。 */
static int fill_dag_runtime(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *fail, *v;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /dag: expected object\n");
    return -1;
  }
  fail = yyjson_obj_get(obj, "on_tool_fail");
  if (!fail) return 0;
  if (!yyjson_is_obj(fail)) {
    fprintf(stderr, "neo: config error at /dag/on_tool_fail: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(fail, "llm");
  if (yyjson_is_bool(v))
    c->dag_runtime.on_tool_fail_llm = yyjson_get_bool(v) ? 1 : 0;
  else if (yyjson_is_int(v) || yyjson_is_uint(v))
    c->dag_runtime.on_tool_fail_llm = yyjson_get_sint(v) ? 1 : 0;
  v = yyjson_obj_get(fail, "max_calls");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) {
    int m = (int)yyjson_get_sint(v);
    if (m < 0) m = 0;
    if (m > 2) m = 2;
    c->dag_runtime.on_tool_fail_max_calls = m;
  }
  return 0;
}

static int fill_paths_section(yyjson_val *obj, char ***paths, int *count, int *max_chars,
                              const char *section) {
  yyjson_val *v, *arr;
  char pathbuf[64];
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /%s: expected object\n", section);
    return -1;
  }
  v = yyjson_obj_get(obj, "max_chars_per_file");
  if (max_chars && (yyjson_is_int(v) || yyjson_is_uint(v)))
    *max_chars = (int)yyjson_get_sint(v);
  arr = yyjson_obj_get(obj, "paths");
  if (!arr) return 0;
  snprintf(pathbuf, sizeof(pathbuf), "/%s/paths", section);
  return yy_string_array(arr, paths, count, MAX_PATHS, pathbuf);
}

static int fill_tools(agent_config_t *c, yyjson_val *obj) {
  yyjson_val *v, *cmds, *el;
  size_t i, n;
  int b;
  if (!yyjson_is_obj(obj)) {
    fprintf(stderr, "neo: config error at /capability_matrix: expected object\n");
    return -1;
  }
  v = yyjson_obj_get(obj, "enabled");
  if (v && yy_bool(v, &b) == 0) c->tools.enabled = b;
  cfg_set_str(&c->tools.root, obj, "root");
  v = yyjson_obj_get(obj, "max_rounds");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.max_rounds = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "max_read_bytes");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.max_read_bytes = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "list_dir_max_entries");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.list_dir_max_entries = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "http_fetch_enabled");
  if (v && yy_bool(v, &b) == 0) c->tools.http_fetch_enabled = b;
  cfg_set_str(&c->tools.http_allow_hosts, obj, "http_allow_hosts");
  v = yyjson_obj_get(obj, "http_fetch_max_bytes");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) c->tools.http_fetch_max_bytes = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(obj, "shell_enabled");
  if (v && yy_bool(v, &b) == 0) c->tools.shell_enabled = b;
  cfg_set_str(&c->tools.directory, obj, "directory");

  cmds = yyjson_obj_get(obj, "commands");
  if (cmds) {
  if (!yyjson_is_arr(cmds)) {
    fprintf(stderr, "neo: config error at /capability_matrix/commands: expected array\n");
    return -1;
  }
  n = yyjson_arr_size(cmds);
  for (i = 0; i < n; i++) {
    tool_command_t *cmd;
    tool_command_t *np;
    yyjson_val *argv, *pa;
    char pathbuf[64];
    el = yyjson_arr_get(cmds, i);
    if (!yyjson_is_obj(el)) {
      fprintf(stderr, "neo: config error at /capability_matrix/commands/%zu: expected object\n", i);
      return -1;
    }
    if (c->tools.command_count >= MAX_COMMANDS) {
      fprintf(stderr, "neo: capability_matrix.commands: too many entries\n");
      return -1;
    }
    np = realloc(c->tools.commands, (c->tools.command_count + 1) * sizeof(tool_command_t));
    if (!np) return -1;
    c->tools.commands = np;
    cmd = &c->tools.commands[c->tools.command_count];
    memset(cmd, 0, sizeof(*cmd));
    cmd->name = yy_dup_str(yyjson_obj_get(el, "name"));
    cmd->description = yy_dup_str(yyjson_obj_get(el, "description"));
    cmd->when = yy_meta_text(yyjson_obj_get(el, "when"));
    cmd->when_not = yy_meta_text(yyjson_obj_get(el, "when_not"));
    cmd->tags = yy_meta_text(yyjson_obj_get(el, "tags"));
    cmd->outcome = yy_meta_text(yyjson_obj_get(el, "outcome"));
    v = yyjson_obj_get(el, "timeout_sec");
    if (yyjson_is_int(v) || yyjson_is_uint(v)) cmd->timeout_sec = (int)yyjson_get_sint(v);
    v = yyjson_obj_get(el, "max_output_bytes");
    if (yyjson_is_int(v) || yyjson_is_uint(v)) cmd->max_output_bytes = (int)yyjson_get_sint(v);
    pa = yyjson_obj_get(el, "pass_args");
    cmd->pass_args = 0;
    if (yyjson_is_str(pa) && yyjson_get_str(pa) && !strcmp(yyjson_get_str(pa), "env"))
      cmd->pass_args = 1;
    argv = yyjson_obj_get(el, "argv");
    snprintf(pathbuf, sizeof(pathbuf), "/capability_matrix/commands/%zu/argv", i);
    if (argv && yy_string_array(argv, &cmd->argv, &cmd->argv_count, MAX_ARGV, pathbuf) != 0)
      return -1;
    {
      yyjson_val *params = yyjson_obj_get(el, "parameters");
      if (params) {
        if (!yyjson_is_obj(params)) {
          fprintf(stderr, "neo: config error at /capability_matrix/commands/%zu/parameters: expected object\n", i);
          return -1;
        }
        cmd->parameters_json = yyjson_val_write(params, 0, NULL);
        if (!cmd->parameters_json) return -1;
      }
    }
    c->tools.command_count++;
  }
  } /* end commands */

  {
    yyjson_val *ms = yyjson_obj_get(obj, "mcp_servers");
    size_t mi, mn;
    if (!ms) return 0;
    if (!yyjson_is_arr(ms)) {
      fprintf(stderr, "neo: config error at /capability_matrix/mcp_servers: expected array\n");
      return -1;
    }
    mn = yyjson_arr_size(ms);
    for (mi = 0; mi < mn; mi++) {
      mcp_server_config_t *srv;
      mcp_server_config_t *np;
      yyjson_val *el = yyjson_arr_get(ms, mi);
      yyjson_val *args, *en;
      char pathbuf[80];
      int b;
      if (!yyjson_is_obj(el)) {
        fprintf(stderr, "neo: config error at /capability_matrix/mcp_servers/%zu: expected object\n", mi);
        return -1;
      }
      if (c->tools.mcp_server_count >= MAX_COMMANDS) {
        fprintf(stderr, "neo: capability_matrix.mcp_servers: too many entries\n");
        return -1;
      }
      np = realloc(c->tools.mcp_servers, (c->tools.mcp_server_count + 1) * sizeof(mcp_server_config_t));
      if (!np) return -1;
      c->tools.mcp_servers = np;
      srv = &c->tools.mcp_servers[c->tools.mcp_server_count];
      memset(srv, 0, sizeof(*srv));
      srv->enabled = 1;
      srv->name = yy_dup_str(yyjson_obj_get(el, "name"));
      srv->command = yy_dup_str(yyjson_obj_get(el, "command"));
      srv->url = yy_dup_str(yyjson_obj_get(el, "url"));
      en = yyjson_obj_get(el, "enabled");
      if (en && yy_bool(en, &b) == 0) srv->enabled = b;
      args = yyjson_obj_get(el, "args");
      snprintf(pathbuf, sizeof(pathbuf), "/capability_matrix/mcp_servers/%zu/args", mi);
      if (args && yy_string_array(args, &srv->args, &srv->args_count, MAX_ARGV, pathbuf) != 0)
        return -1;
      if (!srv->name || !srv->name[0] || !srv->command || !srv->command[0]) {
        fprintf(stderr, "neo: capability_matrix.mcp_servers/%zu: name and command required\n", mi);
        return -1;
      }
      c->tools.mcp_server_count++;
    }
  }
  return 0;
}

static dag_step_type_t parse_step_type(const char *s) {
  if (!s) return DAG_STEP_TOOL;
  if (!strcmp(s, "llm")) return DAG_STEP_LLM;
  if (!strcmp(s, "loop")) return DAG_STEP_LOOP;
  if (!strcmp(s, "route")) return DAG_STEP_ROUTE;
  return DAG_STEP_TOOL;
}

int config_append_dag_val(agent_config_t *c, yyjson_val *wobj, const char *err_ctx) {
  yyjson_val *steps, *st;
  dag_t *wf;
  dag_t *np;
  size_t si, sn;
  const char *ctx = err_ctx ? err_ctx : "dag";
  if (!c || !yyjson_is_obj(wobj)) {
    fprintf(stderr, "neo: %s: expected object\n", ctx);
    return -1;
  }
  if (c->dag_count >= MAX_COMMANDS) {
    fprintf(stderr, "neo: dags: too many entries\n");
    return -1;
  }
  np = realloc(c->dags, (c->dag_count + 1) * sizeof(dag_t));
  if (!np) return -1;
  c->dags = np;
  wf = &c->dags[c->dag_count];
  memset(wf, 0, sizeof(*wf));
  wf->name = yy_dup_str(yyjson_obj_get(wobj, "name"));
  wf->description = yy_dup_str(yyjson_obj_get(wobj, "description"));
  wf->when = yy_meta_text(yyjson_obj_get(wobj, "when"));
  wf->when_not = yy_meta_text(yyjson_obj_get(wobj, "when_not"));
  wf->tags = yy_meta_text(yyjson_obj_get(wobj, "tags"));
  wf->requires = yy_meta_text(yyjson_obj_get(wobj, "requires"));
  wf->outcome = yy_meta_text(yyjson_obj_get(wobj, "outcome"));
  steps = yyjson_obj_get(wobj, "steps");
  if (!steps) {
    c->dag_count++;
    return 0;
  }
  if (!yyjson_is_arr(steps)) {
    fprintf(stderr, "neo: %s: steps expected array\n", ctx);
    return -1;
  }
  sn = yyjson_arr_size(steps);
  for (si = 0; si < sn; si++) {
    dag_step_t *s;
    dag_step_t *snp;
    yyjson_val *type_v, *tools_v, *args_v, *max_v, *dep, *over, *then_a, *else_a;
    yyjson_val *cases_a, *retry_v;
    char pathbuf[128];
    st = yyjson_arr_get(steps, si);
    if (!yyjson_is_obj(st)) {
      fprintf(stderr, "neo: %s: steps/%zu expected object\n", ctx, si);
      return -1;
    }
    if (wf->step_count >= MAX_PATHS) {
      fprintf(stderr, "neo: DAG steps: too many\n");
      return -1;
    }
    snp = realloc(wf->steps, (wf->step_count + 1) * sizeof(dag_step_t));
    if (!snp) return -1;
    wf->steps = snp;
    s = &wf->steps[wf->step_count];
    memset(s, 0, sizeof(*s));
    s->tools_on = 1;
    s->id = yy_dup_str(yyjson_obj_get(st, "id"));
    type_v = yyjson_obj_get(st, "type");
    if (yyjson_is_str(type_v))
      s->type = parse_step_type(yyjson_get_str(type_v));
    else
      s->type = DAG_STEP_TOOL;
    s->tool = yy_dup_str(yyjson_obj_get(st, "tool"));
    s->prompt = yy_dup_str(yyjson_obj_get(st, "prompt"));
    if (!yyjson_is_str(type_v) && s->prompt && s->prompt[0] && (!s->tool || !s->tool[0]))
      s->type = DAG_STEP_LLM;
    tools_v = yyjson_obj_get(st, "tools");
    if (yyjson_is_str(tools_v)) {
      const char *ts = yyjson_get_str(tools_v);
      if (ts && (!strcmp(ts, "off") || !strcmp(ts, "false") || !strcmp(ts, "0")))
        s->tools_on = 0;
    } else if (yyjson_is_bool(tools_v)) {
      s->tools_on = yyjson_get_bool(tools_v) ? 1 : 0;
    }
    args_v = yyjson_obj_get(st, "args");
    if (args_v) s->args_json = yyjson_val_write(args_v, 0, NULL);
    max_v = yyjson_obj_get(st, "max");
    if (yyjson_is_int(max_v) || yyjson_is_uint(max_v)) s->max_iters = (int)yyjson_get_sint(max_v);
    s->route_on = yy_dup_str(yyjson_obj_get(st, "on"));
    s->route_match = yy_dup_str(yyjson_obj_get(st, "match"));
    dep = yyjson_obj_get(st, "depends_on");
    if (dep) {
      snprintf(pathbuf, sizeof(pathbuf), "%s/steps/%zu/depends_on", ctx, si);
      if (yy_string_array(dep, &s->depends_on, &s->depends_count, MAX_ARGV, pathbuf) != 0)
        return -1;
    }
    over = yyjson_obj_get(st, "over");
    if (over) {
      snprintf(pathbuf, sizeof(pathbuf), "%s/steps/%zu/over", ctx, si);
      if (yy_string_array(over, &s->over_ids, &s->over_count, MAX_ARGV, pathbuf) != 0)
        return -1;
    }
    then_a = yyjson_obj_get(st, "then");
    if (then_a) {
      snprintf(pathbuf, sizeof(pathbuf), "%s/steps/%zu/then", ctx, si);
      if (yy_string_array(then_a, &s->route_then, &s->route_then_count, MAX_ARGV, pathbuf) != 0)
        return -1;
    }
    else_a = yyjson_obj_get(st, "else");
    if (else_a) {
      snprintf(pathbuf, sizeof(pathbuf), "%s/steps/%zu/else", ctx, si);
      if (yy_string_array(else_a, &s->route_else, &s->route_else_count, MAX_ARGV, pathbuf) != 0)
        return -1;
    }
    /* 多路 cases：存在时运行时忽略顶层 match/then/else 的分支语义 */
    cases_a = yyjson_obj_get(st, "cases");
    if (yyjson_is_arr(cases_a)) {
      size_t ci, cn = yyjson_arr_size(cases_a);
      if (cn < 1 || cn > (size_t)DAG_MAX_ROUTE_CASES) {
        fprintf(stderr, "neo: %s/steps/%zu: cases length must be 1..%d\n", ctx, si,
                DAG_MAX_ROUTE_CASES);
        return -1;
      }
      s->route_cases = calloc(cn, sizeof(dag_route_case_t));
      if (!s->route_cases) return -1;
      s->route_case_count = (int)cn;
      for (ci = 0; ci < cn; ci++) {
        yyjson_val *co = yyjson_arr_get(cases_a, ci);
        yyjson_val *th;
        if (!yyjson_is_obj(co)) {
          fprintf(stderr, "neo: %s/steps/%zu/cases/%zu: expected object\n", ctx, si, ci);
          return -1;
        }
        s->route_cases[ci].match = yy_dup_str(yyjson_obj_get(co, "match"));
        th = yyjson_obj_get(co, "then");
        if (th) {
          snprintf(pathbuf, sizeof(pathbuf), "%s/steps/%zu/cases/%zu/then", ctx, si, ci);
          if (yy_string_array(th, &s->route_cases[ci].then_ids, &s->route_cases[ci].then_count,
                              MAX_ARGV, pathbuf) != 0)
            return -1;
        }
      }
    }
    retry_v = yyjson_obj_get(st, "retry");
    if (yyjson_is_obj(retry_v)) {
      yyjson_val *rm = yyjson_obj_get(retry_v, "max");
      if (yyjson_is_int(rm) || yyjson_is_uint(rm)) {
        int m = (int)yyjson_get_sint(rm);
        if (m < 0) m = 0;
        if (m > 3) m = 3;
        s->retry_max = m;
      }
    }
    wf->step_count++;
  }
  c->dag_count++;
  return 0;
}

static int fill_dags(agent_config_t *c, yyjson_val *arr) {
  size_t wi, wn;
  if (!yyjson_is_arr(arr)) {
    fprintf(stderr, "neo: config error at /dags: expected array\n");
    return -1;
  }
  wn = yyjson_arr_size(arr);
  for (wi = 0; wi < wn; wi++) {
    char ctx[64];
    yyjson_val *wobj = yyjson_arr_get(arr, wi);
    snprintf(ctx, sizeof(ctx), "/dags/%zu", wi);
    if (config_append_dag_val(c, wobj, ctx) != 0) return -1;
  }
  return 0;
}

int config_load_file(agent_config_t *c, const char *path) {
  yyjson_read_err err;
  yyjson_doc *doc;
  yyjson_val *root, *sec;

  if (!c || !path) return -1;
  if (path_has_ext(path, ".yaml") || path_has_ext(path, ".yml")) {
    fprintf(stderr,
            "neo: config is JSON5-only; migrate to .json5 (see docs/migrate-json.md)\n");
    return -1;
  }

  c->memory.max_chars = 4000;
  c->memory.vector_enabled = 0;
  c->memory.vector_top_k = 5;
  c->memory.vector_dims = 64;
  c->memory.auto_store_enabled = 0;
  c->memory.auto_store_max_chars = 500;
  c->model.max_tokens = 4096;
  c->model.temperature = 0.7;
  c->bootstrap.max_chars_per_file = 8000;
  c->session_max_turns = 10;

  memset(&err, 0, sizeof(err));
  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: JSON5 parse error in %s: %s (at %zu)\n", path,
            err.msg ? err.msg : "unknown", (size_t)err.pos);
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    fprintf(stderr, "neo: config root must be a JSON object\n");
    yyjson_doc_free(doc);
    return -1;
  }

  if ((sec = yyjson_obj_get(root, "model")) && fill_model(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "memory")) && fill_memory(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "soul")) && fill_soul(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "workspace")) && fill_workspace(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "session")) && fill_session(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "plan")) && fill_plan(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "dag")) && fill_dag_runtime(c, sec) != 0) goto fail;
  if ((sec = yyjson_obj_get(root, "bootstrap")) &&
      fill_paths_section(sec, &c->bootstrap.paths, &c->bootstrap.path_count,
                         &c->bootstrap.max_chars_per_file, "bootstrap") != 0)
    goto fail;
  if ((sec = yyjson_obj_get(root, "rules")) &&
      fill_paths_section(sec, &c->rules.paths, &c->rules.path_count, &c->rules.max_chars_per_file,
                         "rules") != 0)
    goto fail;
  if (yyjson_obj_get(root, "skills"))
    fprintf(stderr,
            "neo: config key 'skills' is removed; migrate to rules/bootstrap and "
            "capability_matrix (see docs/superpowers/specs/2026-09-05-deprecate-skills-design.md)\n");
  /* 硬切：旧键 workflows / workflow_directory 一律拒绝 */
  if (yyjson_obj_get(root, "workflows") || yyjson_obj_get(root, "workflow_directory")) {
    fprintf(stderr,
            "neo: config keys 'workflows' / 'workflow_directory' were removed; "
            "use 'dags' / 'dag_directory' (see docs/migrate-json.md)\n");
    goto fail;
  }
  {
    yyjson_val *mx = yyjson_obj_get(root, "capability_matrix");
    if (!mx) {
      mx = yyjson_obj_get(root, "tools");
      if (mx)
        fprintf(stderr,
                "neo: config key 'tools' is deprecated; rename to 'capability_matrix'\n");
    }
    if (mx && fill_tools(c, mx) != 0) goto fail;
  }
  if (capability_dir_load_into_config(c) != 0) goto fail;
  {
    yyjson_val *wd = yyjson_obj_get(root, "dag_directory");
    if (yyjson_is_str(wd) && yyjson_get_str(wd)) {
      free(c->dag_directory);
      c->dag_directory = dup_str(yyjson_get_str(wd));
    }
  }
  if ((sec = yyjson_obj_get(root, "dags")) && fill_dags(c, sec) != 0) goto fail;
  if (dag_dir_load_into_config(c) != 0) goto fail;

  yyjson_doc_free(doc);

  if (c->session_max_turns <= 0) c->session_max_turns = 10;
  if (c->dag_runtime.on_tool_fail_max_calls < 0) c->dag_runtime.on_tool_fail_max_calls = 0;
  if (c->dag_runtime.on_tool_fail_max_calls > 2) c->dag_runtime.on_tool_fail_max_calls = 2;
  if (c->tools.list_dir_max_entries <= 0) c->tools.list_dir_max_entries = 256;
  if (c->tools.http_fetch_max_bytes <= 0) c->tools.http_fetch_max_bytes = 262144;
  if (c->soul.max_chars <= 0) c->soul.max_chars = 8000;
  if (c->rules.max_chars_per_file <= 0) c->rules.max_chars_per_file = 8000;
  if (validate_tool_commands(c) != 0) return -1;
  if (validate_dags(c) != 0) return -1;


  if (!c->model.base_url) c->model.base_url = dup_str("http://127.0.0.1:11434/v1");
  if (!c->model.name) c->model.name = dup_str("qwen3:8b");
  if (!c->model.provider) c->model.provider = dup_str("ollama");
  if (c->model.max_tokens <= 0) c->model.max_tokens = 4096;
  return 0;

fail:
  yyjson_doc_free(doc);
  return -1;
}

void config_apply_env(agent_config_t *c) {
  const char *v;
  v = getenv("NEO_MODEL");
  if (v && v[0]) {
    free(c->model.name);
    c->model.name = dup_str(v);
  }
  v = getenv("NEO_API_KEY");
  if (v && v[0]) {
    free(c->model.api_key);
    c->model.api_key = dup_str(v);
  }
  v = getenv("NEO_CONFIG");
  (void)v;
}
