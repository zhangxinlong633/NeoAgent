/*
 * neo plan / run：LLM 一次性规划；执行期不重规划。
 * 先尝试 {"use":[...]} 选型目录 DAG，失败再抽完整 dags JSON。
 */
#include "plan.h"
#include "capability_matrix.h"
#include "llm.h"
#include "dag.h"
#include "dag_dir.h"
#include "yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

static yyjson_mut_val *mut_str(yyjson_mut_doc *doc, const char *s) {
  return yyjson_mut_str(doc, s ? s : "");
}

/* 名称是否在能力矩阵中（builtin / command / MCP），用于纠正模型把 tool 名写进 use 的情况。 */
static int plan_name_is_capability(const agent_config_t *conf, const char *name) {
  capability_matrix_t mx;
  const cap_row_t *row;
  int ok = 0;
  if (!conf || !name || !name[0] || !conf->tools.enabled) return 0;
  capability_matrix_init(&mx);
  if (capability_matrix_build_from_config(&mx, conf) != 0) {
    capability_matrix_free(&mx);
    return 0;
  }
  row = capability_matrix_find(&mx, name);
  ok = row != NULL;
  capability_matrix_free(&mx);
  return ok;
}

/* 把误放入 use 的能力名降级成单图多 tool 步，供 materialize / 执行。调用方 free。 */
char *plan_dags_json_for_tools(char **names, int n) {
  yyjson_mut_doc *doc;
  yyjson_mut_val *root, *wfs, *wf, *steps, *st, *deps;
  char *out;
  int i;
  char idbuf[32];
  if (!names || n < 1) return NULL;
  doc = yyjson_mut_doc_new(NULL);
  if (!doc) return NULL;
  root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  wfs = yyjson_mut_arr(doc);
  yyjson_mut_obj_add_val(doc, root, "dags", wfs);
  wf = yyjson_mut_obj(doc);
  yyjson_mut_arr_add_val(wfs, wf);
  yyjson_mut_obj_add_strcpy(doc, wf, "name", "adhoc_tools");
  yyjson_mut_obj_add_strcpy(doc, wf, "description", "Ad-hoc tool steps synthesized from capability names");
  steps = yyjson_mut_arr(doc);
  yyjson_mut_obj_add_val(doc, wf, "steps", steps);
  for (i = 0; i < n; i++) {
    st = yyjson_mut_obj(doc);
    yyjson_mut_arr_add_val(steps, st);
    snprintf(idbuf, sizeof(idbuf), "t%d", i);
    yyjson_mut_obj_add_strcpy(doc, st, "id", idbuf);
    yyjson_mut_obj_add_strcpy(doc, st, "type", "tool");
    yyjson_mut_obj_add_strcpy(doc, st, "tool", names[i] ? names[i] : "");
    yyjson_mut_obj_add_val(doc, st, "args", yyjson_mut_obj(doc));
    if (i > 0) {
      char prev[32];
      snprintf(prev, sizeof(prev), "t%d", i - 1);
      deps = yyjson_mut_arr(doc);
      yyjson_mut_arr_add_strcpy(doc, deps, prev);
      yyjson_mut_obj_add_val(doc, st, "depends_on", deps);
    }
  }
  out = yyjson_mut_write(doc, 0, NULL);
  yyjson_mut_doc_free(doc);
  return out;
}

int plan_extract_dags_json(const char *llm_text, char **out_json) {
  const char *p, *start = NULL, *end = NULL;
  char *slice = NULL;
  size_t len;
  yyjson_doc *doc = NULL;
  yyjson_val *root, *wfs;
  char *written = NULL;
  size_t wlen = 0;

  if (out_json) *out_json = NULL;
  if (!llm_text || !out_json) return -1;

  p = strstr(llm_text, "```json5");
  if (!p) p = strstr(llm_text, "```json");
  if (!p) p = strstr(llm_text, "```JSON");
  if (!p) p = strstr(llm_text, "```");
  if (p) {
    p = strchr(p + 3, '\n');
    if (p) {
      start = p + 1;
      end = strstr(start, "```");
    }
  }
  if (start && end && end > start) {
    len = (size_t)(end - start);
    slice = malloc(len + 1);
    if (!slice) return -1;
    memcpy(slice, start, len);
    slice[len] = '\0';
  } else {
    start = strchr(llm_text, '{');
    if (!start) return -1;
    slice = strdup(start);
    if (!slice) return -1;
  }

  doc = yyjson_read(slice, strlen(slice), YYJSON_READ_JSON5);
  free(slice);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  wfs = yyjson_obj_get(root, "dags");
  if (!yyjson_is_arr(wfs)) {
    yyjson_doc_free(doc);
    return -1;
  }

  {
    yyjson_mut_doc *mdoc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *mroot, *mwfs;
    if (!mdoc) {
      yyjson_doc_free(doc);
      return -1;
    }
    mroot = yyjson_mut_obj(mdoc);
    yyjson_mut_doc_set_root(mdoc, mroot);
    mwfs = yyjson_val_mut_copy(mdoc, wfs);
    if (!mwfs || !yyjson_mut_obj_add_val(mdoc, mroot, "dags", mwfs)) {
      yyjson_mut_doc_free(mdoc);
      yyjson_doc_free(doc);
      return -1;
    }
    written = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, &wlen);
    yyjson_mut_doc_free(mdoc);
  }
  yyjson_doc_free(doc);
  if (!written) return -1;
  *out_json = written;
  return 0;
}

static int slice_json_object(const char *llm_text, char **out_slice) {
  const char *p, *start = NULL, *end = NULL;
  char *slice = NULL;
  size_t len;
  if (out_slice) *out_slice = NULL;
  if (!llm_text || !out_slice) return -1;
  p = strstr(llm_text, "```json5");
  if (!p) p = strstr(llm_text, "```json");
  if (!p) p = strstr(llm_text, "```JSON");
  if (!p) p = strstr(llm_text, "```");
  if (p) {
    p = strchr(p + 3, '\n');
    if (p) {
      start = p + 1;
      end = strstr(start, "```");
    }
  }
  if (start && end && end > start) {
    len = (size_t)(end - start);
    slice = malloc(len + 1);
    if (!slice) return -1;
    memcpy(slice, start, len);
    slice[len] = '\0';
  } else {
    start = strchr(llm_text, '{');
    if (!start) return -1;
    slice = strdup(start);
    if (!slice) return -1;
  }
  *out_slice = slice;
  return 0;
}

void plan_free_use(char **names, int n) {
  int i;
  if (!names) return;
  for (i = 0; i < n; i++) free(names[i]);
  free(names);
}

int plan_extract_use(const char *llm_text, char ***out_names, int *out_n) {
  /* 解析选型短格式；与完整 dags 数组互斥优先（由 plan_run 先调本函数）。 */
  char *slice = NULL;
  yyjson_doc *doc = NULL;
  yyjson_val *root, *use;
  char **names = NULL;
  int n = 0;

  if (out_names) *out_names = NULL;
  if (out_n) *out_n = 0;
  if (!llm_text || !out_names || !out_n) return -1;
  if (slice_json_object(llm_text, &slice) != 0) return -1;
  doc = yyjson_read(slice, strlen(slice), YYJSON_READ_JSON5);
  free(slice);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  use = yyjson_obj_get(root, "use");
  if (yyjson_is_str(use) && yyjson_get_str(use) && yyjson_get_str(use)[0]) {
    names = calloc(1, sizeof(char *));
    if (!names) {
      yyjson_doc_free(doc);
      return -1;
    }
    names[0] = strdup(yyjson_get_str(use));
    if (!names[0]) {
      free(names);
      yyjson_doc_free(doc);
      return -1;
    }
    n = 1;
  } else if (yyjson_is_arr(use) && yyjson_arr_size(use) > 0) {
    size_t i, sz = yyjson_arr_size(use);
    names = calloc(sz, sizeof(char *));
    if (!names) {
      yyjson_doc_free(doc);
      return -1;
    }
    for (i = 0; i < sz; i++) {
      yyjson_val *el = yyjson_arr_get(use, i);
      if (!yyjson_is_str(el) || !yyjson_get_str(el)[0]) {
        plan_free_use(names, (int)i);
        yyjson_doc_free(doc);
        return -1;
      }
      names[i] = strdup(yyjson_get_str(el));
      if (!names[i]) {
        plan_free_use(names, (int)i);
        yyjson_doc_free(doc);
        return -1;
      }
    }
    n = (int)sz;
  } else {
    yyjson_doc_free(doc);
    return -1;
  }
  yyjson_doc_free(doc);
  *out_names = names;
  *out_n = n;
  return 0;
}

int plan_resolve_target_steps(const agent_config_t *conf, int cli_steps) {
  int n = cli_steps;
  if (n <= 0 && conf && conf->plan.target_steps > 0)
    n = conf->plan.target_steps;
  if (n <= 0) n = PLAN_DEFAULT_TARGET_STEPS;
  if (n < 1) n = 1;
  if (n > PLAN_MAX_TARGET_STEPS) n = PLAN_MAX_TARGET_STEPS;
  return n;
}

char *plan_build_system_prompt(const agent_config_t *conf, int target_steps) {
  /* 拼 planner 提示：先 DAG catalog（鼓励 use），再知识/工程现编规则，再能力矩阵名单。 */
  size_t cap = 12288;
  char *s = malloc(cap);
  size_t n = 0;
  int i, j;
  if (!s) return NULL;
  if (target_steps < 1) target_steps = PLAN_DEFAULT_TARGET_STEPS;
  if (target_steps > PLAN_MAX_TARGET_STEPS) target_steps = PLAN_MAX_TARGET_STEPS;
  n += (size_t)snprintf(
      s + n, cap - n,
      "You are Neo's DAG planner. Given a user task, choose how to run it.\n"
      "Rules:\n"
      "- Prefer selecting an existing DAG from the catalog below when it fits.\n"
      "- Selection output (preferred):\n```json\n{\"use\":[\"catalog_name\"]}\n```\n"
      "  (\"use\" may also be a single string.)\n"
      "- CRITICAL: names in \"use\" MUST be DAG catalog names only "
      "(listed below as \"DAG: <name> — ...\"). "
      "Never put Capability Matrix tool names (e.g. date_iso, read_file, unix_wc) in \"use\". "
      "Those belong only in invented steps with \"type\":\"tool\" and a \"tool\" field.\n"
      "- Only invent a full dags array if no catalog entry fits.\n"
      "- Topology is fixed after you emit it; workers do not replan.\n"
      "- When inventing: use steps with id, type, depends_on, and type-specific fields.\n"
      "- Allowed types: tool, llm, loop, route.\n"
      "- Every invented step MUST set type explicitly (tool|llm|loop|route).\n"
      "- First classify the task as knowledge or engineering.\n"
      "- knowledge tasks (explain, compare, define, summarize, Q&A, "
      "\"说明一下\" / \"对比一下\" style): use a short editorial pipeline of "
      "about 4 llm steps (all tools \"off\"), chained with depends_on:\n"
      "  1) understand — restate the question and outline comparison axes\n"
      "  2) draft — write the full answer in the user's language\n"
      "  3) self-check — critique gaps/errors; emit PASS or FAIL\n"
      "  4) summarize — final polished answer for the user (stdout deliverable); "
      "if prior step failed, revise using {{steps.draft}} and {{steps.self_check}}\n"
      "  Optional: a route after self-check back to draft at most once on FAIL.\n"
      "  Do not invent implement/write_file/coding work. Do not pad knowledge "
      "graphs to %d with fake engineering roles. If soft target is 1, emit a "
      "single llm step only.\n"
      "- For tiny factual probes answerable by one allowlisted tool (time, hostname, "
      "pwd, short git status): invent a 1-step {\"type\":\"tool\"} DAG "
      "(or pick a matching catalog DAG). Do not emit {\"use\":[\"tool_name\"]}.\n"
      "- engineering tasks (implement, fix bugs, write/edit files, scripts, "
      "multi-tool pipelines, build features): use a small software R&D team "
      "pipeline scaled to about %d steps.\n"
      "  Role map (merge/split roles if N differs, but keep the phase order):\n"
      "  1) understand requirements (llm)\n"
      "  2-3) decompose / design (llm; optional tool)\n"
      "  4-7) implement (llm and/or tool)\n"
      "  8-9) verify (llm/tool); on failure route/loop back to implement or decompose "
      "at most 2 times (use match PASS/FAIL or similar; no unbounded goto)\n"
      "  10) summarize deliverable (llm; this should be the final useful output)\n"
      "- Soft step target %d sizes engineering; knowledge stays ~4 unless "
      "--steps/target is 1.\n"
      "- tool: only use tool names from the allowlist below (or read_file, "
      "write_file, append_file, list_dir, stat, mkdir, grep). type tool requires a tool field.\n"
      "- llm: set prompt; tools on or off as a step field string "
      "(\"on\"|\"off\"). Later llm prompts should reference prior outputs via "
      "{{steps.<id>}} or {{prev}} when useful.\n"
      "- tool args: JSON object; templates {{steps.<id>}} / {{prev}} expand like prompts.\n"
      "- route: \"on\" template like \"{{steps.verify_id}}\"; match substring; "
      "then/else MUST be JSON arrays of step ids.\n"
      "- Use depends_on arrays for ordering when there is more than one step.\n"
      "- Invented output format:\n```json\n{\"dags\":[{\"name\":\"planned\",\"steps\":["
      "{\"id\":\"...\",\"type\":\"llm\",\"prompt\":\"...\",\"tools\":\"off\"}]}]}\n```\n\n"
      "DAG catalog (prefer {\"use\":[\"name\"]}):\n",
      target_steps, target_steps, target_steps);
  {
    char *cat = dag_dir_catalog_listing(conf);
    if (cat) {
      size_t ln = strlen(cat);
      if (n + ln + 8 > cap) {
        char *ns = realloc(s, n + ln + 4096);
        if (ns) {
          s = ns;
          cap = n + ln + 4096;
        }
      }
      if (n + ln < cap) {
        memcpy(s + n, cat, ln + 1);
        n += ln;
      }
      free(cat);
    }
  }
  n += (size_t)snprintf(s + n, cap - n,
                        "\nAllowed capabilities (Capability Matrix — NEVER put these names in \"use\"; "
                        "only in type:tool steps as the \"tool\" field):\n");
  if (conf && conf->tools.enabled) {
    capability_matrix_t mx;
    char *listing;
    capability_matrix_init(&mx);
    if (capability_matrix_build_from_config(&mx, conf) == 0) {
      listing = capability_matrix_prompt_listing(&mx);
      if (listing) {
        size_t ln = strlen(listing);
        if (n + ln + 8 > cap) {
          char *ns = realloc(s, (n + ln + 4096));
          if (ns) {
            s = ns;
            cap = n + ln + 4096;
          }
        }
        if (n + ln < cap) {
          memcpy(s + n, listing, ln + 1);
          n += ln;
        }
        free(listing);
      }
    }
    capability_matrix_free(&mx);
  } else {
    n += (size_t)snprintf(s + n, cap - n, "(tools disabled — use llm steps only)\n");
  }
  if (conf) {
    for (i = 0; i < conf->tools.command_count; i++) {
      const tool_command_t *cmd = &conf->tools.commands[i];
      if (!cmd->name) continue;
      if (n + 256 > cap) {
        char *ns = realloc(s, cap * 2);
        if (!ns) {
          free(s);
          return NULL;
        }
        s = ns;
        cap *= 2;
      }
      n += (size_t)snprintf(s + n, cap - n, "Command detail %s argv:", cmd->name);
      for (j = 0; j < cmd->argv_count; j++)
        n += (size_t)snprintf(s + n, cap - n, " %s", cmd->argv[j] ? cmd->argv[j] : "");
      n += (size_t)snprintf(s + n, cap - n, "\n");
    }
  }
  (void)n;
  return s;
}

int plan_materialize_config(const agent_config_t *base, const char *dags_json,
                            agent_config_t *out_conf, char *out_path, size_t out_path_sz) {
  char path[] = "/tmp/neo-plan-XXXXXX";
  int fd;
  yyjson_doc *wf_doc = NULL;
  yyjson_val *wf_root, *wfs;
  yyjson_mut_doc *mdoc = NULL;
  yyjson_mut_val *root, *model, *tools, *mem, *sess, *cmds, *mwfs;
  char *written = NULL;
  size_t wlen = 0;
  FILE *f;
  int i, j;

  if (!base || !dags_json || !out_conf) return -1;
  fd = mkstemp(path);
  if (fd < 0) return -1;

  wf_doc = yyjson_read(dags_json, strlen(dags_json), YYJSON_READ_JSON5);
  if (!wf_doc) {
    close(fd);
    unlink(path);
    return -1;
  }
  wf_root = yyjson_doc_get_root(wf_doc);
  wfs = yyjson_is_obj(wf_root) ? yyjson_obj_get(wf_root, "dags") : NULL;
  if (!yyjson_is_arr(wfs)) {
    yyjson_doc_free(wf_doc);
    close(fd);
    unlink(path);
    return -1;
  }

  mdoc = yyjson_mut_doc_new(NULL);
  if (!mdoc) {
    yyjson_doc_free(wf_doc);
    close(fd);
    unlink(path);
    return -1;
  }
  root = yyjson_mut_obj(mdoc);
  yyjson_mut_doc_set_root(mdoc, root);

  model = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "model", model);
  yyjson_mut_obj_add_val(mdoc, model, "base_url", mut_str(mdoc, base->model.base_url));
  yyjson_mut_obj_add_val(mdoc, model, "name", mut_str(mdoc, base->model.name));
  yyjson_mut_obj_add_val(mdoc, model, "api_key",
                        mut_str(mdoc, base->model.api_key ? base->model.api_key : ""));
  yyjson_mut_obj_add_int(mdoc, model, "max_tokens",
                         base->model.max_tokens > 0 ? base->model.max_tokens : 4096);
  yyjson_mut_obj_add_real(mdoc, model, "temperature", base->model.temperature);

  tools = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "capability_matrix", tools);
  yyjson_mut_obj_add_bool(mdoc, tools, "enabled", base->tools.enabled ? true : false);
  yyjson_mut_obj_add_val(mdoc, tools, "root",
                         mut_str(mdoc, base->tools.root && base->tools.root[0] ? base->tools.root : "."));
  yyjson_mut_obj_add_int(mdoc, tools, "max_rounds",
                         base->tools.max_rounds > 0 ? base->tools.max_rounds : 16);
  if (base->tools.command_count > 0) {
    cmds = yyjson_mut_arr(mdoc);
    yyjson_mut_obj_add_val(mdoc, tools, "commands", cmds);
    for (i = 0; i < base->tools.command_count; i++) {
      const tool_command_t *cmd = &base->tools.commands[i];
      yyjson_mut_val *co = yyjson_mut_obj(mdoc);
      yyjson_mut_val *argv = yyjson_mut_arr(mdoc);
      yyjson_mut_arr_append(cmds, co);
      yyjson_mut_obj_add_val(mdoc, co, "name", mut_str(mdoc, cmd->name));
      yyjson_mut_obj_add_val(mdoc, co, "description",
                             mut_str(mdoc, cmd->description ? cmd->description : ""));
      yyjson_mut_obj_add_val(mdoc, co, "argv", argv);
      for (j = 0; j < cmd->argv_count; j++)
        yyjson_mut_arr_append(argv, mut_str(mdoc, cmd->argv[j]));
      yyjson_mut_obj_add_int(mdoc, co, "timeout_sec", cmd->timeout_sec > 0 ? cmd->timeout_sec : 30);
      yyjson_mut_obj_add_int(mdoc, co, "max_output_bytes",
                             cmd->max_output_bytes > 0 ? cmd->max_output_bytes : 65536);
      yyjson_mut_obj_add_val(mdoc, co, "pass_args",
                             mut_str(mdoc, cmd->pass_args ? "env" : "stdin_json"));
    }
  }

  mem = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "memory", mem);
  yyjson_mut_obj_add_val(mdoc, mem, "path",
                         mut_str(mdoc, base->memory.path ? base->memory.path : "MEMORY.md"));
  yyjson_mut_obj_add_int(mdoc, mem, "max_chars",
                         base->memory.max_chars > 0 ? base->memory.max_chars : 4000);
  if (base->memory.vector_enabled) {
    yyjson_mut_val *vec = yyjson_mut_obj(mdoc);
    yyjson_mut_obj_add_val(mdoc, mem, "vector", vec);
    yyjson_mut_obj_add_bool(mdoc, vec, "enabled", 1);
    yyjson_mut_obj_add_val(
        mdoc, vec, "store",
        mut_str(mdoc, base->memory.vector_store ? base->memory.vector_store : ".neo/memory.vdb"));
    yyjson_mut_obj_add_int(mdoc, vec, "top_k",
                           base->memory.vector_top_k > 0 ? base->memory.vector_top_k : 5);
    yyjson_mut_obj_add_int(mdoc, vec, "dims",
                           base->memory.vector_dims > 0 ? base->memory.vector_dims : 64);
  }

  sess = yyjson_mut_obj(mdoc);
  yyjson_mut_obj_add_val(mdoc, root, "session", sess);
  yyjson_mut_obj_add_int(mdoc, sess, "max_turns",
                         base->session_max_turns > 0 ? base->session_max_turns : 10);

  mwfs = yyjson_val_mut_copy(mdoc, wfs);
  yyjson_mut_obj_add_val(mdoc, root, "dags", mwfs);

  written = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, &wlen);
  yyjson_mut_doc_free(mdoc);
  yyjson_doc_free(wf_doc);
  if (!written) {
    close(fd);
    unlink(path);
    return -1;
  }

  f = fdopen(fd, "w");
  if (!f) {
    free(written);
    close(fd);
    unlink(path);
    return -1;
  }
  fwrite(written, 1, wlen, f);
  fclose(f);
  free(written);

  config_init(out_conf);
  if (config_load_file(out_conf, path) != 0) {
    unlink(path);
    return -1;
  }
  if (out_conf->dag_count < 1) {
    fprintf(stderr, "neo plan: no dags in planned JSON\n");
    config_free(out_conf);
    unlink(path);
    return -1;
  }
  if (out_path && out_path_sz) {
    snprintf(out_path, out_path_sz, "%s", path);
  } else {
    unlink(path);
  }
  return 0;
}

int plan_run(const agent_config_t *conf, const char *task, int do_run, int quiet_plan,
             int target_steps, const char *save_path, int debug, int verbose) {
  char *sys = NULL;
  char *user = NULL;
  llm_response_t resp;
  char *json = NULL;
  agent_config_t planned;
  char tmp_path[256];
  int rc = -1;
  const char *dag_name;
  int steps;

  if (!conf || !task || !task[0]) return -1;
  steps = plan_resolve_target_steps(conf, target_steps);
  sys = plan_build_system_prompt(conf, steps);
  if (!sys) return -1;
  user = malloc(strlen(task) + 256);
  if (!user) {
    free(sys);
    return -1;
  }
  snprintf(user, strlen(task) + 256,
           "Task:\n%s\n\nPrefer {\"use\":[\"catalog_DAG_name\"]} only for names listed in the "
           "DAG catalog. Capability/tool names must NOT appear in \"use\"; put them in "
           "invented type:tool steps instead. If no catalog DAG fits, emit dags JSON.",
           task);
  if (debug) {
    fprintf(stderr, "neo %s: calling LLM to build DAG...\n", do_run ? "run" : "plan");
  }
  memset(&resp, 0, sizeof(resp));
  if (llm_chat(conf->model.base_url, conf->model.name, conf->model.api_key,
               conf->model.max_tokens > 0 ? conf->model.max_tokens : 4096,
               conf->model.temperature, sys, user, &resp) != 0) {
    fprintf(stderr, "neo %s: LLM request failed\n", do_run ? "run" : "plan");
    free(sys);
    free(user);
    llm_response_free(&resp);
    return -1;
  }
  free(sys);
  free(user);

  {
    char **use_names = NULL;
    int use_n = 0;
    if (plan_extract_use(resp.data ? resp.data : "", &use_names, &use_n) == 0 && use_n > 0) {
      int ui;
      int n_wf = 0, n_cap = 0, n_bad = 0;
      yyjson_mut_doc *mdoc;
      yyjson_mut_val *mroot, *marr;
      char *use_json = NULL;
      llm_response_free(&resp);
      for (ui = 0; ui < use_n; ui++) {
        if (config_find_dag(conf, use_names[ui]))
          n_wf++;
        else if (plan_name_is_capability(conf, use_names[ui]))
          n_cap++;
        else
          n_bad++;
      }
      if (n_bad > 0) {
        for (ui = 0; ui < use_n; ui++) {
          if (!config_find_dag(conf, use_names[ui]) &&
              !plan_name_is_capability(conf, use_names[ui])) {
            fprintf(stderr,
                    "neo %s: '%s' is neither a catalog DAG nor a Capability Matrix name "
                    "(use a DAG catalog name in \"use\", or invent type:tool steps)\n",
                    do_run ? "run" : "plan", use_names[ui]);
          }
        }
        plan_free_use(use_names, use_n);
        return -1;
      }
      /* 模型把能力名误写入 use：降级为单图 tool 步，走现编 materialize 路径。 */
      if (n_cap > 0 && n_wf == 0) {
        json = plan_dags_json_for_tools(use_names, use_n);
        plan_free_use(use_names, use_n);
        if (!json) return -1;
        fprintf(stderr,
                "neo %s: treating capability name(s) in \"use\" as ad-hoc tool DAG "
                "(prefer catalog DAG or type:tool invent next time)\n",
                do_run ? "run" : "plan");
        goto materialize_from_json;
      }
      if (n_cap > 0 && n_wf > 0) {
        fprintf(stderr,
                "neo %s: \"use\" mixes catalog DAGs and capability names; "
                "emit separate catalog use or type:tool invent instead\n",
                do_run ? "run" : "plan");
        plan_free_use(use_names, use_n);
        return -1;
      }
      mdoc = yyjson_mut_doc_new(NULL);
      mroot = yyjson_mut_obj(mdoc);
      yyjson_mut_doc_set_root(mdoc, mroot);
      marr = yyjson_mut_arr(mdoc);
      for (ui = 0; ui < use_n; ui++) yyjson_mut_arr_add_str(mdoc, marr, use_names[ui]);
      yyjson_mut_obj_add_val(mdoc, mroot, "use", marr);
      use_json = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, NULL);
      yyjson_mut_doc_free(mdoc);
      if (!use_json) {
        plan_free_use(use_names, use_n);
        return -1;
      }
      if (!quiet_plan) {
        fputs(use_json, stdout);
        if (use_json[0] && use_json[strlen(use_json) - 1] != '\n') fputc('\n', stdout);
        fflush(stdout);
      }
      if (save_path && save_path[0]) {
        FILE *sf = fopen(save_path, "w");
        if (!sf) {
          fprintf(stderr, "neo %s: cannot write %s\n", do_run ? "run" : "plan", save_path);
          free(use_json);
          plan_free_use(use_names, use_n);
          return -1;
        }
        fputs(use_json, sf);
        fclose(sf);
        fprintf(stderr, "neo %s: saved %s\n", do_run ? "run" : "plan", save_path);
      }
      fprintf(stderr, "neo %s: selected catalog DAG", do_run ? "run" : "plan");
      for (ui = 0; ui < use_n; ui++) fprintf(stderr, " '%s'", use_names[ui]);
      fprintf(stderr, "\n");
      if (do_run) {
        for (ui = 0; ui < use_n; ui++) {
          char *out = NULL;
          fprintf(stderr, "neo run: executing catalog DAG '%s'\n", use_names[ui]);
          if (dag_run(conf, use_names[ui], &out, verbose) != 0) {
            fprintf(stderr, "neo run: DAG run failed\n");
            free(out);
            free(use_json);
            plan_free_use(use_names, use_n);
            return -1;
          }
          if (out) {
            fputs(out, stdout);
            if (out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
            free(out);
          }
        }
      }
      free(use_json);
      plan_free_use(use_names, use_n);
      return 0;
    }
    plan_free_use(use_names, use_n);
  }

  if (plan_extract_dags_json(resp.data ? resp.data : "", &json) != 0) {
    fprintf(stderr, "neo %s: could not extract dags JSON from model output\n",
            do_run ? "run" : "plan");
    if (debug && resp.data) fprintf(stderr, "--- raw ---\n%s\n", resp.data);
    llm_response_free(&resp);
    return -1;
  }
  llm_response_free(&resp);

materialize_from_json:

  if (!quiet_plan) {
    fputs(json, stdout);
    if (json[0] && json[strlen(json) - 1] != '\n') fputc('\n', stdout);
    fflush(stdout);
  }

  if (save_path && save_path[0]) {
    FILE *sf = fopen(save_path, "w");
    if (!sf) {
      fprintf(stderr, "neo %s: cannot write %s\n", do_run ? "run" : "plan", save_path);
      free(json);
      return -1;
    }
    fputs(json, sf);
    fclose(sf);
    fprintf(stderr, "neo %s: saved %s\n", do_run ? "run" : "plan", save_path);
  }

  tmp_path[0] = '\0';
  if (plan_materialize_config(conf, json, &planned, tmp_path, sizeof(tmp_path)) != 0) {
    fprintf(stderr, "neo %s: planned DAG failed validation/load\n", do_run ? "run" : "plan");
    fprintf(stderr, "--- planned JSON ---\n%s", json);
    if (json[0] && json[strlen(json) - 1] != '\n') fputc('\n', stderr);
    fprintf(stderr, "--- end ---\n");
    free(json);
    return -1;
  }
  if (verbose || debug)
    fprintf(stderr, "neo: plan ok steps=%d\n", planned.dags[0].step_count);
  fprintf(stderr, "neo %s: validated DAG '%s' (%d steps)\n", do_run ? "run" : "plan",
          planned.dags[0].name ? planned.dags[0].name : "?",
          planned.dags[0].step_count);
  dag_name = planned.dags[0].name;

  if (do_run) {
    char *out = NULL;
    fprintf(stderr, "neo run: executing DAG '%s'\n", dag_name);
    if (dag_run(&planned, dag_name, &out, verbose) != 0) {
      fprintf(stderr, "neo run: DAG run failed\n");
      free(out);
      config_free(&planned);
      if (tmp_path[0]) unlink(tmp_path);
      free(json);
      return -1;
    }
    if (out) {
      fputs(out, stdout);
      if (out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
      free(out);
    }
  }

  config_free(&planned);
  if (tmp_path[0]) unlink(tmp_path);
  free(json);
  rc = 0;
  return rc;
}
