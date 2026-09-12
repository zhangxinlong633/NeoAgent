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
#include "neo_md_term.h"
#include "neo_memory.h"
#include "neo_session.h"
#include "plan.h"
#include "dag.h"
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
  fprintf(stderr, "       %s [OPTIONS] daemon [--socket PATH]\n", prog);
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
  fprintf(stderr, "  -R, --render        Render assistant Markdown to the terminal (md4c)\n");
  fprintf(stderr, "  -S, --session ID    Persist/reuse chat turns under .neo/sessions/ID.json\n");
  fprintf(stderr, "  --session-clear ID  Delete a saved session file and exit\n");
  fprintf(stderr, "  -o, --output FILE   (with plan/run) Save planned dags JSON\n");
  fprintf(stderr, "  --steps N           (with plan/run) Soft target step count (default 10, max 32)\n");
  fprintf(stderr, "  -h, --help          Show this help\n");
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
  int render = 0;
  const char *session_id = NULL;
  int session_clear = 0;
  int dag_mode = 0;
  const char *dag_name = NULL;
  int memory_mode = 0; /* 1=recall 2=store */
  const char *memory_arg = NULL;
  int plan_mode = 0;
  int run_mode = 0;
  int legacy_plan_run = 0;
  const char *plan_out = NULL;
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
    if (strcmp(argv[arg_start], "daemon") == 0) {
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
    if (strcmp(argv[arg_start], "--session") == 0 || strcmp(argv[arg_start], "-S") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --session requires ID\n"); return 1; }
      session_id = argv[arg_start + 1];
      arg_start += 2;
      continue;
    }
    if (strcmp(argv[arg_start], "--session-clear") == 0) {
      if (arg_start + 1 >= argc) { fprintf(stderr, "neo: --session-clear requires ID\n"); return 1; }
      session_id = argv[arg_start + 1];
      session_clear = 1;
      arg_start += 2;
      continue;
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
    int r = socket_path ? run_daemon_socket(&conf, socket_path, debug, verbose)
                        : run_daemon_stdin(&conf, debug, verbose);
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

  if (session_clear) {
    if (!neo_session_id_ok(session_id)) {
      fprintf(stderr, "neo: invalid session id (use [A-Za-z0-9_-], max 64)\n");
      return 1;
    }
    if (neo_session_clear(session_id) != 0) {
      fprintf(stderr, "neo: failed to clear session '%s'\n", session_id);
      return 1;
    }
    if (verbose) fprintf(stderr, "neo session: cleared id=%s\n", session_id);
    return 0;
  }

  if (session_id && !neo_session_id_ok(session_id)) {
    fprintf(stderr, "neo: invalid session id (use [A-Za-z0-9_-], max 64)\n");
    return 1;
  }

  if (arg_start >= argc) {
    fprintf(stderr, "Usage: neo [OPTIONS] \"your message\" or neo run \"task\" or neo daemon\n");
    return 1;
  }

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

  if (debug)
    debug_print_request(&conf, conf.model.base_url, conf.model.name, conf.model.max_tokens, conf.model.temperature,
                       system_prompt, user_message);

  llm_response_t resp = {0};
  llm_message_t *prefix = NULL;
  int n_prefix = 0;
  int err;

  if (session_id) {
    if (neo_session_load(session_id, &prefix, &n_prefix) != 0) {
      fprintf(stderr, "neo: failed to load session '%s'\n", session_id);
      config_free(&conf);
      free(system_prompt);
      free(user_message);
      free(tmp);
      return 1;
    }
    if (verbose || debug) {
      char spath[256];
      if (neo_session_path(session_id, spath, sizeof(spath)) == 0)
        fprintf(stderr, "neo session: id=%s turns~=%d path=%s\n", session_id, n_prefix / 2, spath);
    }
  }

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
    if (err == 0 && session_id && resp.data && resp.size) {
      if (neo_session_append_turn(session_id, user_message, resp.data, max_turns) != 0)
        fprintf(stderr, "neo: warning: failed to save session '%s'\n", session_id);
      else if (verbose || debug)
        fprintf(stderr, "neo session: saved id=%s\n", session_id);
    }
  }
  config_free(&conf);
  free(system_prompt);
  free(user_message);
  free(tmp);

  if (err != 0) {
    fprintf(stderr, "neo: LLM request failed\n");
    llm_response_free(&resp);
    return 1;
  }
  if (resp.data && resp.size)
    neo_print_assistant(resp.data, resp.size, render);
  llm_response_free(&resp);
  return 0;
}
