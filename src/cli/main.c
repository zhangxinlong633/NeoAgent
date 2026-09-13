/*
 * Neo CLI 入口：单次查询、daemon、dag run、plan/run。
 * 用法：neo [OPTIONS] "user message" | neo daemon | neo dag run NAME | neo plan|run ...
 * 环境：NEO_CONFIG / NEO_MODEL / NEO_API_KEY；助手回复走 stdout，诊断走 stderr。
 */
#include "agent_tools.h"
#include "capability_matrix.h"
#include "config.h"
#include "daemon.h"
#include "llm.h"
#include "neo_events.h"
#include "neo_md_term.h"
#include "neo_memory.h"
#include "neo_session.h"
#include "plan.h"
#include "dag.h"
#include "yyjson.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NEO_DISABLE_TOOLS_GETENV
#define NEO_DISABLE_TOOLS_GETENV "NEO_DISABLE_TOOLS"
#endif

#define SYSTEM_MAX (256 * 1024)
#define USER_MAX   (64 * 1024)

static size_t read_file_into(char *buf, size_t cap, const char *path, size_t max_chars) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  size_t n = 0;
  if (max_chars <= 0 || max_chars > cap - 1) max_chars = cap - 1;
  while (n < max_chars && fgets(buf + n, (int)(cap - n), f))
    n = strlen(buf);
  if (n >= cap - 1) n = cap - 2;
  buf[n] = '\0';
  fclose(f);
  return n;
}

static void append_section(char *dest, size_t cap, const char *title, const char *path, const char *content) {
  if (!content || !content[0]) return;
  size_t used = strlen(dest);
  if (used + strlen(title) + strlen(path) + strlen(content) + 64 > cap) return;
  strncat(dest, title, cap - used - 1);
  strncat(dest, path, cap - used - 1);
  strncat(dest, "\n\n", cap - used - 1);
  strncat(dest, content, cap - used - 1);
  strncat(dest, "\n\n", cap - used - 1);
}

static void build_user_message(char *buf, size_t cap, char **argv, int start, int argc) {
  buf[0] = '\0';
  for (int i = start; i < argc; i++) {
    if (i > start) strncat(buf, " ", cap - strlen(buf) - 1);
    strncat(buf, argv[i], cap - strlen(buf) - 1);
  }
}

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s [OPTIONS] \"your message\"\n", prog);
  fprintf(stderr, "       %s [OPTIONS] daemon|-D|--daemon [--socket PATH]\n", prog);
  fprintf(stderr, "       %s [OPTIONS] dag run NAME\n", prog);
  fprintf(stderr, "       %s [OPTIONS] plan [--steps N] [-o FILE] \"task\"\n", prog);
  fprintf(stderr, "       %s [OPTIONS] run NAME|\"task\" [--steps N] [-o FILE]\n", prog);
  fprintf(stderr, "       %s [OPTIONS] memory recall \"query\"\n", prog);
  fprintf(stderr, "       %s [OPTIONS] memory store \"note\"\n", prog);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -c, --config PATH   Config file (default: config/config.json5)\n");
  fprintf(stderr, "  -p, --profile NAME  Use config/profiles/NAME/ (fallback: profiles/NAME/)\n");
  fprintf(stderr, "  -m, --model NAME    Override model name\n");
  fprintf(stderr, "  -d, --debug         Print system prompt, user message and request params to stderr\n");
  fprintf(stderr, "  -v, --verbose       Step / capability / memory summaries on stderr\n");
  fprintf(stderr, "  -R, --render        Render assistant Markdown (default: on)\n");
  fprintf(stderr, "  --no-render         Print raw Markdown / plain text (disable render)\n");
  fprintf(stderr, "  -S, --session ID[,ID...]  Chat: session id(s), default \"%s\"; "
          "daemon (-D): optional mount (load many, write first; no default)\n",
          NEO_SESSION_DEFAULT_ID);
  fprintf(stderr, "  -N, --session-new   Archive default session to YYYYMMDD-HHMMSS, then chat fresh\n");
  fprintf(stderr, "  --session-list      List saved sessions under .neo/sessions/ and exit\n");
  fprintf(stderr, "  --session-clear ID  Delete a saved session file and exit\n");
  fprintf(stderr, "  --role NAME         Use config roles.NAME prompt for this turn\n");
  fprintf(stderr, "  -j, --json          Chat: OpenAI chat.completion JSON on stdout\n");
  fprintf(stderr, "  -o, --output FILE   Chat: write reply/JSON to FILE; plan/run: save dags JSON\n");
  fprintf(stderr, "  --steps N           (with plan/run) Soft target step count (default 10, max 32)\n");
  fprintf(stderr, "  -h, --help          Show this help\n");
  fprintf(stderr, "  -D, --daemon        Same as subcommand daemon (stdin multi-turn)\n");
  fprintf(stderr, "  daemon              Run as daemon: read from stdin, reply to stdout\n");
  fprintf(stderr, "  --socket PATH       (with daemon) Listen on Unix socket instead of stdin\n");
  fprintf(stderr, "  dag run NAME        Run a declarative DAG from config/catalog\n");
  fprintf(stderr, "  plan \"task\"         LLM emits a DAG (validate only; JSON on stdout)\n");
  fprintf(stderr, "  run NAME|\"task\"     Run named DAG, or plan+execute a task\n");
  fprintf(stderr, "  memory recall Q     Dry-run local memory recall (no LLM); text on stdout\n");
  fprintf(stderr, "  memory store TEXT   Append note into local vector DB (not MEMORY.md)\n");
}

/* 助手正文输出：可选 md4c 终端渲染；失败则回退原文。 */
static void neo_print_assistant(const char *data, size_t size, int render) {
  int use_color;
  if (!data || !size) return;
  if (render) {
    use_color = isatty(STDOUT_FILENO) ? 1 : 0;
    if (neo_md_term_render(data, size, stdout, use_color) == 0) {
      if (data[size - 1] != '\n') putchar('\n');
      return;
    }
    fprintf(stderr, "neo: markdown render failed; printing raw text\n");
  }
  fwrite(data, 1, size, stdout);
  if (data[size - 1] != '\n') putchar('\n');
}

/* 将助手原文写入文件；成功 0。 */
static int neo_write_reply_file(const char *path, const char *data, size_t size) {
  FILE *f;
  if (!path || !path[0]) return -1;
  f = fopen(path, "w");
  if (!f) return -1;
  if (data && size) {
    if (fwrite(data, 1, size, f) != size) {
      fclose(f);
      return -1;
    }
    if (data[size - 1] != '\n' && fputc('\n', f) == EOF) {
      fclose(f);
      return -1;
    }
  }
  if (fclose(f) != 0) return -1;
  return 0;
}

/*
 * 聊天机读输出：OpenAI chat.completion 兼容 JSON（便于现有 SDK/脚本对接）。
 * 成功：id/object/created/model/choices[0].message.content/finish_reason/usage；
 * 另附 neo.session / neo.role（扩展字段，标准客户端可忽略）。
 * 失败：{"error":{"message","type","code"}}。
 */
static int neo_emit_chat_json(FILE *fp, int ok, const char *text, size_t text_len,
                              const char *session_id, const char *role,
                              const char *model, const char *error) {
  yyjson_mut_doc *doc;
  yyjson_mut_val *root;
  char *json;
  int rc = -1;
  time_t created;

  if (!fp) return -1;
  created = time(NULL);
  doc = yyjson_mut_doc_new(NULL);
  if (!doc) return -1;
  root = yyjson_mut_obj(doc);
  if (!root) {
    yyjson_mut_doc_free(doc);
    return -1;
  }
  yyjson_mut_doc_set_root(doc, root);

  if (!ok) {
    yyjson_mut_val *err = yyjson_mut_obj(doc);
    if (!err) {
      yyjson_mut_doc_free(doc);
      return -1;
    }
    yyjson_mut_obj_add_str(doc, err, "message",
                           error && error[0] ? error : "failed");
    yyjson_mut_obj_add_str(doc, err, "type", "neo_error");
    yyjson_mut_obj_add_str(doc, err, "code", "llm_request_failed");
    yyjson_mut_obj_add_null(doc, err, "param");
    yyjson_mut_obj_add_val(doc, root, "error", err);
  } else {
    yyjson_mut_val *choices, *choice, *message, *usage, *neo;
    char idbuf[64];

    snprintf(idbuf, sizeof(idbuf), "chatcmpl-neo-%ld", (long)created);
    yyjson_mut_obj_add_strcpy(doc, root, "id", idbuf);
    yyjson_mut_obj_add_str(doc, root, "object", "chat.completion");
    yyjson_mut_obj_add_int(doc, root, "created", (int64_t)created);
    yyjson_mut_obj_add_strcpy(doc, root, "model",
                             model && model[0] ? model : "unknown");

    message = yyjson_mut_obj(doc);
    choice = yyjson_mut_obj(doc);
    choices = yyjson_mut_arr(doc);
    if (!message || !choice || !choices) {
      yyjson_mut_doc_free(doc);
      return -1;
    }
    yyjson_mut_obj_add_str(doc, message, "role", "assistant");
    if (text && text_len)
      yyjson_mut_obj_add_strn(doc, message, "content", text, text_len);
    else
      yyjson_mut_obj_add_str(doc, message, "content", "");

    yyjson_mut_obj_add_int(doc, choice, "index", 0);
    yyjson_mut_obj_add_val(doc, choice, "message", message);
    yyjson_mut_obj_add_null(doc, choice, "logprobs");
    yyjson_mut_obj_add_str(doc, choice, "finish_reason", "stop");
    yyjson_mut_arr_add_val(choices, choice);
    yyjson_mut_obj_add_val(doc, root, "choices", choices);

    usage = yyjson_mut_obj(doc);
    if (!usage) {
      yyjson_mut_doc_free(doc);
      return -1;
    }
    /* 本地封装不拆 usage；填 0 保持字段形状兼容 */
    yyjson_mut_obj_add_int(doc, usage, "prompt_tokens", 0);
    yyjson_mut_obj_add_int(doc, usage, "completion_tokens", 0);
    yyjson_mut_obj_add_int(doc, usage, "total_tokens", 0);
    yyjson_mut_obj_add_val(doc, root, "usage", usage);

    neo = yyjson_mut_obj(doc);
    if (!neo) {
      yyjson_mut_doc_free(doc);
      return -1;
    }
    if (session_id && session_id[0])
      yyjson_mut_obj_add_strcpy(doc, neo, "session", session_id);
    else
      yyjson_mut_obj_add_null(doc, neo, "session");
    if (role && role[0])
      yyjson_mut_obj_add_strcpy(doc, neo, "role", role);
    else
      yyjson_mut_obj_add_null(doc, neo, "role");
    yyjson_mut_obj_add_val(doc, root, "neo", neo);
  }

  json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, NULL);
  yyjson_mut_doc_free(doc);
  if (!json) return -1;
  if (fputs(json, fp) >= 0 && fputc('\n', fp) != EOF) rc = 0;
  free(json);
  return rc;
}

/* stderr 一行说明本轮 Memory 注入方式（-v / -d / memory 子命令）。 */
static void neo_memory_log(const agent_config_t *conf, const char *recalled) {
  size_t n;
  if (!conf) return;
  n = recalled ? strlen(recalled) : 0;
  if (conf->memory.vector_enabled) {
    fprintf(stderr, "neo memory: vector store=%s chars=%zu top_k=%d dims=%d\n",
            conf->memory.vector_store && conf->memory.vector_store[0]
                ? conf->memory.vector_store
                : ".neo/memory.vdb",
            n,
            conf->memory.vector_top_k > 0 ? conf->memory.vector_top_k : 5,
            conf->memory.vector_dims > 0 ? conf->memory.vector_dims : 64);
  } else {
    fprintf(stderr, "neo memory: truncate path=%s chars=%zu max_chars=%d\n",
            conf->memory.path ? conf->memory.path : "(null)", n,
            conf->memory.max_chars > 0 ? conf->memory.max_chars : 4000);
  }
}

static int cmd_memory_recall(agent_config_t *conf, const char *query) {
  neo_memory_t *mem;
  char *out = NULL;
  if (!conf || !query) return 1;
  if (!conf->memory.vector_enabled && (!conf->memory.path || !conf->memory.path[0])) {
    fprintf(stderr, "neo: memory.path not set (and vector disabled)\n");
    return 1;
  }
  mem = neo_memory_open(conf);
  if (!mem) {
    fprintf(stderr, "neo: memory open failed\n");
    return 1;
  }
  if (neo_memory_recall(mem, query, &out) != 0 || !out) {
    fprintf(stderr, "neo: memory recall empty or failed\n");
    neo_memory_close(mem);
    return 1;
  }
  neo_memory_log(conf, out);
  fputs(out, stdout);
  if (out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
  free(out);
  neo_memory_close(mem);
  return 0;
}

static int cmd_memory_store(agent_config_t *conf, const char *text) {
  neo_memory_t *mem;
  if (!conf || !text || !text[0]) {
    fprintf(stderr, "neo: memory store requires non-empty text\n");
    return 1;
  }
  if (!conf->memory.vector_enabled) {
    fprintf(stderr, "neo: memory store requires memory.vector.enabled=true\n");
    return 1;
  }
  mem = neo_memory_open(conf);
  if (!mem) {
    fprintf(stderr, "neo: memory open failed\n");
    return 1;
  }
  if (neo_memory_store(mem, text) != 0) {
    fprintf(stderr, "neo: memory store failed\n");
    neo_memory_close(mem);
    return 1;
  }
  fprintf(stderr, "neo memory: store ok chunks=%d store=%s\n", neo_memory_count(mem),
          conf->memory.vector_store && conf->memory.vector_store[0]
              ? conf->memory.vector_store
              : ".neo/memory.vdb");
  neo_memory_close(mem);
  return 0;
}

/* Prefer config/ layout; keep repo-root paths as fallback. */
static const char *neo_default_config_path(void) {
  if (access("config/config.json5", R_OK) == 0) return "config/config.json5";
  if (access("config.json5", R_OK) == 0) return "config.json5";
  return "config/config.json5";
}

static int neo_resolve_profile_dir(const char *name, char *out, size_t out_sz) {
  char cand[PATH_MAX];
  if (!name || !name[0] || !out) return -1;
  if (snprintf(cand, sizeof(cand), "config/profiles/%s", name) >= (int)sizeof(cand))
    return -1;
  if (access(cand, F_OK) == 0) {
    if (strlen(cand) + 1 > out_sz) return -1;
    memcpy(out, cand, strlen(cand) + 1);
    return 0;
  }
  if (snprintf(cand, sizeof(cand), "profiles/%s", name) >= (int)sizeof(cand))
    return -1;
  if (access(cand, F_OK) == 0) {
    if (strlen(cand) + 1 > out_sz) return -1;
    memcpy(out, cand, strlen(cand) + 1);
    return 0;
  }
  /* Prefer new layout in error message path */
  if (snprintf(out, out_sz, "config/profiles/%s", name) >= (int)out_sz)
    return -1;
  return -1;
}

/* ANSI colors for debug (no-op if stderr not a tty; call debug_color_ok() to decide) */
static int debug_color_ok(void) {
  const char *t = getenv("TERM");
  return t && t[0] && strcmp(t, "dumb") != 0;
}
#define D_RESET   "\033[0m"
#define D_CYAN    "\033[36m"
#define D_YELLOW  "\033[33m"
#define D_GREEN   "\033[32m"
#define D_BOLD    "\033[1m"

static void debug_print_request(agent_config_t *conf,
                                const char *base_url, const char *model, int max_tokens, double temperature,
                                const char *system_prompt, const char *user_message) {
  int use_color = debug_color_ok();
  const char *cy = use_color ? D_CYAN : "";
  const char *yl = use_color ? D_YELLOW : "";
  const char *gr = use_color ? D_GREEN : "";
  const char *bd = use_color ? D_BOLD : "";
  const char *re = use_color ? D_RESET : "";

  fprintf(stderr, "\n%s%s=== NEO DEBUG: request params ===%s\n", bd, cy, re);
  fprintf(stderr, "%sbase_url: %s\nmodel: %s\nmax_tokens: %d\ntemperature: %.2f\n%s",
          cy, base_url ? base_url : "(null)", model ? model : "(null)", max_tokens, temperature, re);
  if (conf && conf->rules.path_count > 0) {
    fprintf(stderr, "%srules: ", cy);
    for (int i = 0; i < conf->rules.path_count; i++)
      fprintf(stderr, "%s%s", i ? ", " : "", conf->rules.paths[i] ? conf->rules.paths[i] : "(null)");
    fprintf(stderr, "%s\n", re);
  }
  fprintf(stderr, "\n%s%s=== NEO DEBUG: system prompt (%zu chars) ===%s\n%s%s%s\n%s%s=== END system prompt ===%s\n",
          bd, yl, system_prompt ? strlen(system_prompt) : 0u, re, yl, system_prompt ? system_prompt : "", re, bd, yl, re);
  fprintf(stderr, "\n%s%s=== NEO DEBUG: user message (%zu chars) ===%s\n%s%s%s\n%s%s=== END user message ===%s\n\n",
          bd, gr, user_message ? strlen(user_message) : 0u, re, gr, user_message ? user_message : "", re, bd, gr, re);
}

int main(int argc, char **argv) {
  const char *config_path = getenv("NEO_CONFIG");
  const char *profile = getenv("NEO_PROFILE");
  int config_set = 0;
  char profile_dir[PATH_MAX];
  int used_profile = 0;
  if (!config_path || !config_path[0]) config_path = neo_default_config_path();
  else config_set = 1;
  const char *model_override = NULL;
  int arg_start = 1;

  const char *socket_path = NULL;
  int daemon_mode = 0;
  int debug = 0;
  int verbose = 0;
  int render = 1; /* 默认终端 Markdown 渲染；--no-render 关闭 */

  const char *session_spec = NULL;
  int session_clear = 0;
  int session_list = 0;
  int session_new = 0;
  const char *role_name = NULL;
  int dag_mode = 0;
  const char *dag_name = NULL;
  int memory_mode = 0; /* 1=recall 2=store */
  const char *memory_arg = NULL;
  int plan_mode = 0;
  int run_mode = 0;
  int legacy_plan_run = 0;
  const char *plan_out = NULL; /* -o：plan/run 存 DAG；chat 写回复 */
  int out_json = 0;
  int cli_steps = 0; /* 0 = unset; resolved later */

  while (arg_start < argc) {
    if (strcmp(argv[arg_start], "--help") == 0 || strcmp(argv[arg_start], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[arg_start], "--config") == 0 || strcmp(argv[arg_start], "-c") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --config requires PATH\n"); return 1; }
      config_path = argv[arg_start + 1];
      config_set = 1;
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--profile") == 0 || strcmp(argv[arg_start], "-p") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --profile requires NAME\n"); return 1; }
      profile = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--model") == 0 || strcmp(argv[arg_start], "-m") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --model requires NAME\n"); return 1; }
      model_override = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--output") == 0 || strcmp(argv[arg_start], "-o") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --output requires FILE\n"); return 1; }
      plan_out = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--json") == 0 || strcmp(argv[arg_start], "-j") == 0) {
      out_json = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--steps") == 0) {
      char *end = NULL;
      long v;
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --steps requires N\n"); return 1; }
      v = strtol(argv[arg_start + 1], &end, 10);
      if (!end || *end || v < 1 || v > PLAN_MAX_TARGET_STEPS) {
        fprintf(stderr, "neo: --steps must be an integer 1..%d\n", PLAN_MAX_TARGET_STEPS);
        return 1;
      }
      cli_steps = (int)v;
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--run") == 0) {
      legacy_plan_run = 1;
      arg_start++;
      continue;
    }
    /* daemon 子命令；-D/--daemon 别名（-d 仍是 --debug） */
    if (strcmp(argv[arg_start], "daemon") == 0 ||
        strcmp(argv[arg_start], "--daemon") == 0 ||
        strcmp(argv[arg_start], "-D") == 0) {
      daemon_mode = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "plan") == 0) {
      plan_mode = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "run") == 0) {
      run_mode = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "dag") == 0) {
      if (arg_start + 2 >= argc || strcmp(argv[arg_start + 1], "run") != 0) {
        fprintf(stderr, "neo: usage: dag run NAME\n");
        return 1;
      }
      dag_mode = 1;
      dag_name = argv[arg_start + 2];
      arg_start += 3;
      continue;
    }
    if (strcmp(argv[arg_start], "memory") == 0) {
      if (arg_start + 2 >= argc) {
        fprintf(stderr, "neo: usage: memory recall|store \"text\"\n");
        return 1;
      }
      if (strcmp(argv[arg_start + 1], "recall") == 0) {
        memory_mode = 1;
      } else if (strcmp(argv[arg_start + 1], "store") == 0) {
        memory_mode = 2;
      } else {
        fprintf(stderr, "neo: usage: memory recall|store \"text\"\n");
        return 1;
      }
      memory_arg = argv[arg_start + 2];
      arg_start += 3;
      continue;
    }
    if (strcmp(argv[arg_start], "workflow") == 0) {
      fprintf(stderr, "neo: 'workflow' was removed; use 'dag run NAME' or 'run NAME'\n");
      return 1;
    }
    if (strcmp(argv[arg_start], "--socket") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --socket requires PATH\n"); return 1; }
      socket_path = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--debug") == 0 || strcmp(argv[arg_start], "-d") == 0) {
      debug = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--verbose") == 0 || strcmp(argv[arg_start], "-v") == 0) {
      verbose = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--render") == 0 || strcmp(argv[arg_start], "-R") == 0) {
      render = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--no-render") == 0) {
      render = 0;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--session") == 0 || strcmp(argv[arg_start], "-S") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --session requires ID[,ID...]\n"); return 1; }
      session_spec = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--session-new") == 0 || strcmp(argv[arg_start], "-N") == 0) {
      session_new = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--session-list") == 0) {
      session_list = 1;
      arg_start++;
      continue;
    }
    if (strcmp(argv[arg_start], "--session-clear") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --session-clear requires ID\n"); return 1; }
      session_spec = argv[arg_start + 1];
      session_clear = 1;
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--role") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --role requires NAME\n"); return 1; }
      role_name = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    /* 未知 -* 不当作聊天正文，避免 ./neo -D 之类误打进 default session */
    if (argv[arg_start][0] == '-' && argv[arg_start][1] != '\0') {
      fprintf(stderr, "neo: unknown option '%s'\n", argv[arg_start]);
      print_usage(argv[0]);
      return 1;
    }
    break;
  }

  if (profile && profile[0]) {
#if defined(__linux__) || defined(__APPLE__)
    if (neo_resolve_profile_dir(profile, profile_dir, sizeof(profile_dir)) != 0) {
      fprintf(stderr, "neo: profile not found (tried config/profiles/%s and profiles/%s)\n",
              profile, profile);
      return 1;
    }
    if (chdir(profile_dir) != 0) {
      fprintf(stderr, "neo: cannot chdir to profile '%s'\n", profile_dir);
      return 1;
    }
    used_profile = 1;
    if (!config_set) {
      config_path = "neo.json5";
    }
#else
    fprintf(stderr, "neo: profiles require Linux/macOS\n");
    return 1;
#endif
    (void)used_profile;
  }

  if (daemon_mode) {
    agent_config_t conf;
    char **d_ids = NULL;
    int d_n = 0;
    int r;
    if (session_new) {
      fprintf(stderr, "neo: -N/--session-new cannot combine with daemon\n");
      return 1;
    }
    if (session_list || session_clear) {
      fprintf(stderr, "neo: --session-list/--session-clear cannot combine with daemon\n");
      return 1;
    }
    if (session_spec) {
      if (neo_session_parse_ids(session_spec, &d_ids, &d_n) != 0) {
        fprintf(stderr, "neo: invalid -S/--session for daemon "
                "(ids: [A-Za-z0-9_-], comma-separated, max 8, no duplicates)\n");
        return 1;
      }
    }
    config_init(&conf);
    if (config_load_file(&conf, config_path) != 0) {
      fprintf(stderr, "neo: failed to load config from %s\n", config_path);
      neo_session_free_ids(d_ids, d_n);
      config_free(&conf);
      return 1;
    }
    config_apply_env(&conf);
    if (model_override) {
      free(conf.model.name);
      conf.model.name = malloc(strlen(model_override) + 1);
      if (conf.model.name) strcpy(conf.model.name, model_override);
    }
    neo_events_emit("session_start", 1, 0, d_n > 0 && d_ids ? d_ids[0] : "daemon");
    r = socket_path ? run_daemon_socket(&conf, socket_path, debug, verbose, d_ids, d_n)
                    : run_daemon_stdin(&conf, debug, verbose, render, d_ids, d_n);
    neo_session_free_ids(d_ids, d_n);
    config_free(&conf);
    return r != 0;
  }

  if (memory_mode) {
    agent_config_t conf;
    int r;
    config_init(&conf);
    if (config_load_file(&conf, config_path) != 0) {
      fprintf(stderr, "neo: failed to load config from %s\n", config_path);
      config_free(&conf);
      return 1;
    }
    config_apply_env(&conf);
    if (memory_mode == 2)
      r = cmd_memory_store(&conf, memory_arg);
    else
      r = cmd_memory_recall(&conf, memory_arg);
    config_free(&conf);
    return r != 0;
  }

  if (dag_mode) {
    agent_config_t conf;
    char *out = NULL;
    int r;
    config_init(&conf);
    if (config_load_file(&conf, config_path) != 0) {
      fprintf(stderr, "neo: failed to load config from %s\n", config_path);
      config_free(&conf);
      return 1;
    }
    config_apply_env(&conf);
    if (model_override) {
      free(conf.model.name);
      conf.model.name = malloc(strlen(model_override) + 1);
      if (conf.model.name) strcpy(conf.model.name, model_override);
    }
    r = dag_run(&conf, dag_name, &out, verbose);
    if (r == 0 && out) fputs(out, stdout);
    if (out && out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
    free(out);
    config_free(&conf);
    return r != 0;
  }

  if (legacy_plan_run && !plan_mode && !run_mode) {
    fprintf(stderr, "neo: use 'neo run' instead of '--run'\n");
    return 1;
  }

  if (plan_mode || run_mode) {
    agent_config_t conf;
    int r;
    if (legacy_plan_run) {
      fprintf(stderr, "neo: use 'neo run' instead of 'plan --run'\n");
      return 1;
    }
    if (arg_start >= argc) {
      fprintf(stderr, "neo: %s requires a task string\n", run_mode ? "run" : "plan");
      return 1;
    }
    config_init(&conf);
    if (config_load_file(&conf, config_path) != 0) {
      fprintf(stderr, "neo: failed to load config from %s\n", config_path);
      config_free(&conf);
      return 1;
    }
    config_apply_env(&conf);
    if (model_override) {
      free(conf.model.name);
      conf.model.name = malloc(strlen(model_override) + 1);
      if (conf.model.name) strcpy(conf.model.name, model_override);
    }
    /* run：若首参精确匹配已加载图名 → 直接跑 DAG，不经 planner */
    if (run_mode && config_find_dag(&conf, argv[arg_start])) {
      char *out = NULL;
      int wr;
      if (arg_start + 1 < argc) {
        fprintf(stderr, "neo: run: unexpected arguments after DAG name '%s'\n",
                argv[arg_start]);
        config_free(&conf);
        return 1;
      }
      if (cli_steps || plan_out)
        fprintf(stderr, "neo: run: --steps/-o ignored when running named DAG\n");
      wr = dag_run(&conf, argv[arg_start], &out, verbose);
      if (wr == 0 && out) fputs(out, stdout);
      if (out && out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
      free(out);
      config_free(&conf);
      return wr != 0;
    }
    /* plan：JSON on stdout；run 未命中图名：plan then execute */
    r = plan_run(&conf, argv[arg_start], run_mode ? 1 : 0, run_mode ? 1 : 0, cli_steps, plan_out,
                 debug, verbose);
    config_free(&conf);
    return r != 0;
  }

  if (session_list) {
    neo_session_info_t *slist = NULL;
    int sn = 0, si;
    if (neo_session_list(&slist, &sn) != 0) {
      fprintf(stderr, "neo: failed to list sessions\n");
      return 1;
    }
    if (sn == 0) {
      printf("(no sessions under .neo/sessions/)\n");
    } else {
      printf("%-20s %6s  %s\n", "ID", "TURNS", "MTIME");
      for (si = 0; si < sn; si++) {
        char tbuf[64];
        struct tm *tm = localtime(&slist[si].mtime);
        if (!tm || strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm) == 0)
          snprintf(tbuf, sizeof(tbuf), "%ld", (long)slist[si].mtime);
        printf("%-20s %6d  %s\n", slist[si].id, slist[si].n_messages / 2, tbuf);
      }
    }
    neo_session_list_free(slist, sn);
    return 0;
  }

  if (session_clear) {
    if (!session_spec || !neo_session_id_ok(session_spec)) {
      fprintf(stderr, "neo: --session-clear needs a single id ([A-Za-z0-9_-], max 64)\n");
      return 1;
    }
    if (neo_session_clear(session_spec) != 0) {
      fprintf(stderr, "neo: failed to clear session '%s'\n", session_spec);
      return 1;
    }
    if (verbose) fprintf(stderr, "neo session: cleared id=%s\n", session_spec);
    return 0;
  }

  /* -N：把 default 挪到时间戳 id，再在空的 default 上聊（可与消息同用；不可与 -S 同用）。 */
  if (session_new) {
    char archived[65];
    if (session_spec) {
      fprintf(stderr, "neo: --session-new cannot combine with -S/--session\n");
      return 1;
    }
    if (neo_session_archive_default(archived, sizeof(archived)) != 0) {
      fprintf(stderr, "neo: failed to archive default session\n");
      return 1;
    }
    if (archived[0])
      fprintf(stderr, "neo session: archived default -> %s\n", archived);
    else if (verbose)
      fprintf(stderr, "neo session: default was empty (nothing archived)\n");
  }

  if (arg_start >= argc) {
    if (session_new) return 0;
    fprintf(stderr, "Usage: neo [OPTIONS] \"your message\" or neo run \"task\" or neo daemon\n");
    return 1;
  }

  /* 单次聊天默认落盘到 default（显式 -S 仍可覆盖）。 */
  if (!session_spec) session_spec = NEO_SESSION_DEFAULT_ID;

  agent_config_t conf;
  config_init(&conf);
  if (config_load_file(&conf, config_path) != 0) {
    fprintf(stderr, "neo: failed to load config from %s\n", config_path);
    config_free(&conf);
    return 1;
  }
  config_apply_env(&conf);
  if (model_override) {
    free(conf.model.name);
    conf.model.name = malloc(strlen(model_override) + 1);
    if (conf.model.name) strcpy(conf.model.name, model_override);
  }

  char *system_prompt = malloc(SYSTEM_MAX);
  char *user_message  = malloc(USER_MAX);
  char *tmp           = malloc(65536);
  if (!system_prompt || !user_message) {
    free(system_prompt);
    free(user_message);
    free(tmp);
    config_free(&conf);
    return 1;
  }
  system_prompt[0] = '\0';
  user_message[0]  = '\0';
  if (!tmp) tmp = malloc(1024);

  strncat(system_prompt,
          "You are a helpful assistant. Follow any soul, rules, and bootstrap instructions below.\n\n",
          SYSTEM_MAX - 1);

  {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char datebuf[80];
    char line[96];
    if (utc && strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M UTC", utc) > 0)
      snprintf(line, sizeof(line), "Current date and time: %s\n\n", datebuf);
    else
      strcpy(line, "Current date and time: (unknown)\n\n");
    strncat(system_prompt, line, SYSTEM_MAX - 1);
  }

#if defined(__linux__) || defined(__APPLE__)
  if (conf.workspace.prompt_cwd) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
      strncat(system_prompt, "## Workspace\n\nNeo process working directory: ", SYSTEM_MAX - strlen(system_prompt) - 1);
      strncat(system_prompt, cwd, SYSTEM_MAX - strlen(system_prompt) - 1);
      strncat(system_prompt, "\n\n", SYSTEM_MAX - strlen(system_prompt) - 1);
    }
  }
#endif

  build_user_message(user_message, USER_MAX, argv, arg_start, argc);
  /* 启发式先写入，再 recall，使本轮 system 可能已含新记忆 */
  (void)neo_memory_auto_store(&conf, user_message, verbose || debug);
  if (tmp && conf.soul.path && conf.soul.path[0]) {
    size_t max_soul = (conf.soul.max_chars > 0) ? (size_t)conf.soul.max_chars : 8000;
    if (read_file_into(tmp, 65536, conf.soul.path, max_soul) > 0)
      append_section(system_prompt, SYSTEM_MAX, "## Soul\n\n", conf.soul.path, tmp);
  }
  if (tmp) {
    for (int i = 0; i < conf.bootstrap.path_count; i++) {
      const char *path = conf.bootstrap.paths[i];
      size_t max_c = (conf.bootstrap.max_chars_per_file > 0) ? (size_t)conf.bootstrap.max_chars_per_file : 8000;
      if (read_file_into(tmp, 65536, path, max_c) > 0)
        append_section(system_prompt, SYSTEM_MAX, "## Bootstrap: ", path, tmp);
    }
  }
  if (tmp) {
    for (int i = 0; i < conf.rules.path_count; i++) {
      const char *path = conf.rules.paths[i];
      size_t max_c = (conf.rules.max_chars_per_file > 0) ? (size_t)conf.rules.max_chars_per_file : 8000;
      if (read_file_into(tmp, 65536, path, max_c) > 0)
        append_section(system_prompt, SYSTEM_MAX, "## Rules: ", path, tmp);
    }
  }

  if (conf.memory.path) {
    neo_memory_t *mem = neo_memory_open(&conf);
    char *recalled = NULL;
    if (mem && neo_memory_recall(mem, user_message, &recalled) == 0 && recalled && recalled[0]) {
      if (verbose || debug) neo_memory_log(&conf, recalled);
      append_section(system_prompt, SYSTEM_MAX, "## Memory (context)\n\n", "", recalled);
    }
    free(recalled);
    neo_memory_close(mem);
  }

  if (conf.tools.enabled && getenv(NEO_DISABLE_TOOLS_GETENV) == NULL) {
    capability_matrix_t mx;
    char *listing;
    strncat(system_prompt,
            "\n\n## Tools (executed by host)\n"
            "Capabilities below are executed by Neo (Capability Matrix). "
            "Prefer tool calls / existing DAGs over inventing shell. "
            "Path tools are relative to capability_matrix.root (no leading /, no `..`). "
            "Command argv[0] may be relative under root or an absolute allowlisted path. "
            "http_get (when listed) is HTTPS-only with host allowlist and no redirects.\n"
            "### Capability Matrix\n",
            SYSTEM_MAX - strlen(system_prompt) - 1);
    capability_matrix_init(&mx);
    if (capability_matrix_build_from_config(&mx, &conf) == 0) {
      listing = capability_matrix_prompt_listing(&mx);
      if (listing) {
        strncat(system_prompt, listing, SYSTEM_MAX - strlen(system_prompt) - 1);
        free(listing);
      }
    }
    capability_matrix_free(&mx);
  }

  /* 多角色同会话：本轮注入 ## Role；历史仍共享 -S 落盘。 */
  if (role_name) {
    const neo_role_t *role = config_find_role(&conf, role_name);
    if (conf.role_count <= 0) {
      fprintf(stderr, "neo: --role requires config roles{} (none configured)\n");
      config_free(&conf);
      free(system_prompt);
      free(user_message);
      free(tmp);
      return 1;
    }
    if (!role || !role->prompt) {
      fprintf(stderr, "neo: unknown --role '%s'\n", role_name);
      config_free(&conf);
      free(system_prompt);
      free(user_message);
      free(tmp);
      return 1;
    }
    strncat(system_prompt, "\n\n## Role: ", SYSTEM_MAX - strlen(system_prompt) - 1);
    strncat(system_prompt, role->name, SYSTEM_MAX - strlen(system_prompt) - 1);
    strncat(system_prompt, "\n\n", SYSTEM_MAX - strlen(system_prompt) - 1);
    strncat(system_prompt, role->prompt, SYSTEM_MAX - strlen(system_prompt) - 1);
    strncat(system_prompt, "\n", SYSTEM_MAX - strlen(system_prompt) - 1);
    if (verbose || debug)
      fprintf(stderr, "neo role: %s\n", role->name);
  }

  if (debug)
    debug_print_request(&conf, conf.model.base_url, conf.model.name, conf.model.max_tokens, conf.model.temperature,
                       system_prompt, user_message);

  llm_response_t resp = {0};
  llm_message_t *prefix = NULL;
  int n_prefix = 0;
  char **session_ids = NULL;
  int n_session_ids = 0;
  const char *session_write_id = NULL;
  int err;

  if (session_spec) {
    int si;
    if (neo_session_parse_ids(session_spec, &session_ids, &n_session_ids) != 0) {
      fprintf(stderr,
              "neo: invalid --session (comma-separated [A-Za-z0-9_-], max %d, no duplicates)\n",
              NEO_SESSION_MAX_IDS);
      config_free(&conf);
      free(system_prompt);
      free(user_message);
      free(tmp);
      return 1;
    }
    session_write_id = session_ids[0];
    if (neo_session_load_many(session_ids, n_session_ids, &prefix, &n_prefix) != 0) {
      fprintf(stderr, "neo: failed to load session(s) '%s'\n", session_spec);
      neo_session_free_ids(session_ids, n_session_ids);
      config_free(&conf);
      free(system_prompt);
      free(user_message);
      free(tmp);
      return 1;
    }
    if (verbose || debug) {
      fprintf(stderr, "neo session: load");
      for (si = 0; si < n_session_ids; si++)
        fprintf(stderr, "%s%s", si ? "," : "=", session_ids[si]);
      fprintf(stderr, " msgs=%d write=%s\n", n_prefix, session_write_id);
    }
  }

  neo_events_emit("session_start", 1, 0, session_write_id ? session_write_id : "chat");

  if (conf.tools.enabled && getenv(NEO_DISABLE_TOOLS_GETENV) == NULL)
    err = agent_run_with_tools(&conf, system_prompt, prefix, n_prefix, user_message, &resp);
  else if (n_prefix > 0) {
    llm_message_t *msgs = malloc((size_t)(n_prefix + 1) * sizeof(llm_message_t));
    int ni = 0;
    if (!msgs) {
      err = -1;
    } else {
      for (ni = 0; ni < n_prefix; ni++)
        msgs[ni] = prefix[ni];
      msgs[n_prefix] = (llm_message_t){ "user", user_message };
      err = llm_chat_messages(
        conf.model.base_url, conf.model.name, conf.model.api_key,
        conf.model.max_tokens, conf.model.temperature,
        system_prompt, msgs, n_prefix + 1, &resp);
      free(msgs);
    }
  } else
    err = llm_chat(
      conf.model.base_url,
      conf.model.name,
      conf.model.api_key,
      conf.model.max_tokens,
      conf.model.temperature,
      system_prompt,
      user_message,
      &resp
    );
  neo_session_free(prefix, n_prefix);
  {
    int max_turns = conf.session_max_turns > 0 ? conf.session_max_turns : 10;
    if (err == 0 && session_write_id && resp.data && resp.size) {
      const char *to_save = resp.data;
      char *prefixed = NULL;
      if (role_name) {
        size_t rn = strlen(role_name);
        size_t need = rn + 3 + resp.size + 1; /* [NAME] + space + body */
        prefixed = malloc(need);
        if (prefixed) {
          snprintf(prefixed, need, "[%s] %s", role_name, resp.data);
          to_save = prefixed;
        }
      }
      if (neo_session_append_turn(session_write_id, user_message, to_save, max_turns) != 0)
        fprintf(stderr, "neo: warning: failed to save session '%s'\n", session_write_id);
      else if (verbose || debug)
        fprintf(stderr, "neo session: saved id=%s%s%s\n", session_write_id,
                role_name ? " role=" : "", role_name ? role_name : "");
      free(prefixed);
    }
  }

  /* 机读 / 文件输出须在释放 config 与 session 前完成（要用 model/session 名）。 */
  {
    int emit_rc = 0;
    const char *model_name = conf.model.name;
    FILE *out_fp = NULL;

    if (err != 0) {
      if (out_json) {
        if (plan_out && plan_out[0]) {
          out_fp = fopen(plan_out, "w");
          if (!out_fp) {
            fprintf(stderr, "neo: cannot write %s\n", plan_out);
            emit_rc = -1;
          } else {
            emit_rc = neo_emit_chat_json(out_fp, 0, NULL, 0, session_write_id, role_name,
                                         model_name, "LLM request failed");
            fclose(out_fp);
          }
        } else {
          emit_rc = neo_emit_chat_json(stdout, 0, NULL, 0, session_write_id, role_name,
                                       model_name, "LLM request failed");
        }
      } else {
        fprintf(stderr, "neo: LLM request failed\n");
      }
      neo_session_free_ids(session_ids, n_session_ids);
      config_free(&conf);
      free(system_prompt);
      free(user_message);
      free(tmp);
      llm_response_free(&resp);
      return 1;
    }

    if (out_json) {
      if (plan_out && plan_out[0]) {
        out_fp = fopen(plan_out, "w");
        if (!out_fp) {
          fprintf(stderr, "neo: cannot write %s\n", plan_out);
          emit_rc = -1;
        } else {
          emit_rc = neo_emit_chat_json(out_fp, 1, resp.data, resp.size, session_write_id,
                                       role_name, model_name, NULL);
          fclose(out_fp);
        }
      } else {
        emit_rc = neo_emit_chat_json(stdout, 1, resp.data, resp.size, session_write_id,
                                     role_name, model_name, NULL);
      }
    } else if (plan_out && plan_out[0]) {
      /* 聊天 -o：只写原文到文件，不刷终端（便于脚本对接） */
      if (neo_write_reply_file(plan_out, resp.data, resp.size) != 0) {
        fprintf(stderr, "neo: cannot write %s\n", plan_out);
        emit_rc = -1;
      } else if (verbose || debug) {
        fprintf(stderr, "neo: wrote reply to %s\n", plan_out);
      }
    } else if (resp.data && resp.size) {
      neo_print_assistant(resp.data, resp.size, render);
    }

    neo_session_free_ids(session_ids, n_session_ids);
    config_free(&conf);
    free(system_prompt);
    free(user_message);
    free(tmp);
    llm_response_free(&resp);
    return emit_rc != 0 ? 1 : 0;
  }
}
