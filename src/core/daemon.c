/*
 * Daemon mode: stdin loop or Unix socket server, with session history.
 * 交互式 stdin（TTY）会加 User>/neo> 提示符，并对助手回复做 Markdown 渲染。
 */
#include "agent_tools.h"
#include "capability_matrix.h"
#include "config.h"
#include "llm.h"
#include "neo_md_term.h"
#include "neo_memory.h"
#include "neo_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SYSTEM_MAX (256 * 1024)
#define LINE_MAX   (64 * 1024)

#if defined(__linux__) || defined(__APPLE__)
#define HAVE_UNIX_SOCKET 1
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <locale.h>
#include <termios.h>
#endif

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

static void build_system_prompt(agent_config_t *conf, const char *user_message, char *out, size_t cap,
                                int verbose) {
  char *tmp = malloc(65536);
  if (!tmp) { out[0] = '\0'; return; }
  out[0] = '\0';
  strncat(out,
          "You are a helpful assistant. Follow any soul, rules, and bootstrap instructions below.\n\n",
          cap - 1);
  {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char datebuf[80];
    char line[96];
    if (utc && strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M UTC", utc) > 0)
      snprintf(line, sizeof(line), "Current date and time: %s\n\n", datebuf);
    else
      strcpy(line, "Current date and time: (unknown)\n\n");
    strncat(out, line, cap - 1);
  }
#if defined(__linux__) || defined(__APPLE__)
  if (conf->workspace.prompt_cwd) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
      strncat(out, "## Workspace\n\nNeo process working directory: ", cap - strlen(out) - 1);
      strncat(out, cwd, cap - strlen(out) - 1);
      strncat(out, "\n\n", cap - strlen(out) - 1);
    }
  }
#endif
  if (conf->soul.path && conf->soul.path[0]) {
    size_t max_soul = (conf->soul.max_chars > 0) ? (size_t)conf->soul.max_chars : 8000;
    if (read_file_into(tmp, 65536, conf->soul.path, max_soul) > 0)
      append_section(out, cap, "## Soul\n\n", conf->soul.path, tmp);
  }
  for (int i = 0; i < conf->bootstrap.path_count; i++) {
    size_t max_c = (conf->bootstrap.max_chars_per_file > 0) ? (size_t)conf->bootstrap.max_chars_per_file : 8000;
    if (read_file_into(tmp, 65536, conf->bootstrap.paths[i], max_c) > 0)
      append_section(out, cap, "## Bootstrap: ", conf->bootstrap.paths[i], tmp);
  }
  for (int i = 0; i < conf->rules.path_count; i++) {
    size_t max_c = (conf->rules.max_chars_per_file > 0) ? (size_t)conf->rules.max_chars_per_file : 8000;
    if (read_file_into(tmp, 65536, conf->rules.paths[i], max_c) > 0)
      append_section(out, cap, "## Rules: ", conf->rules.paths[i], tmp);
  }
  if (conf->memory.path) {
    neo_memory_t *mem = neo_memory_open(conf);
    char *recalled = NULL;
    if (mem && neo_memory_recall(mem, user_message, &recalled) == 0 && recalled && recalled[0]) {
      if (verbose) {
        if (conf->memory.vector_enabled)
          fprintf(stderr, "neo memory: vector store=%s chars=%zu top_k=%d dims=%d\n",
                  conf->memory.vector_store && conf->memory.vector_store[0]
                      ? conf->memory.vector_store
                      : ".neo/memory.vdb",
                  strlen(recalled),
                  conf->memory.vector_top_k > 0 ? conf->memory.vector_top_k : 5,
                  conf->memory.vector_dims > 0 ? conf->memory.vector_dims : 64);
        else
          fprintf(stderr, "neo memory: truncate path=%s chars=%zu max_chars=%d\n",
                  conf->memory.path, strlen(recalled),
                  conf->memory.max_chars > 0 ? conf->memory.max_chars : 4000);
      }
      append_section(out, cap, "## Memory (context)\n\n", "", recalled);
    }
    free(recalled);
    neo_memory_close(mem);
  }
  if (conf->tools.enabled && getenv("NEO_DISABLE_TOOLS") == NULL) {
    capability_matrix_t mx;
    char *listing;
    strncat(out,
            "\n\n## Tools (executed by host)\n"
            "Capabilities below are executed by Neo (Capability Matrix). "
            "Prefer tool calls / existing DAGs over inventing shell.\n"
            "### Capability Matrix\n",
            cap - strlen(out) - 1);
    capability_matrix_init(&mx);
    if (capability_matrix_build_from_config(&mx, conf) == 0) {
      listing = capability_matrix_prompt_listing(&mx);
      if (listing) {
        strncat(out, listing, cap - strlen(out) - 1);
        free(listing);
      }
    }
    capability_matrix_free(&mx);
  }
  free(tmp);
}

#define MAX_SESSION_MESSAGES 64
static struct {
  char *role;
  char *content;
} session_messages[MAX_SESSION_MESSAGES];
static int session_count = 0;

static void session_append(const char *role, const char *content) {
  if (!role || !content) return;
  if (session_count >= MAX_SESSION_MESSAGES) {
    free(session_messages[0].role);
    free(session_messages[0].content);
    memmove(&session_messages[0], &session_messages[1], (session_count - 1) * sizeof(session_messages[0]));
    session_count--;
  }
  session_messages[session_count].role = strdup(role);
  session_messages[session_count].content = strdup(content);
  if (session_messages[session_count].role && session_messages[session_count].content)
    session_count++;
  else {
    free(session_messages[session_count].role);
    free(session_messages[session_count].content);
  }
}

static void session_trim_to(int max_turns) {
  int max_msg = max_turns * 2;
  while (session_count > max_msg) {
    free(session_messages[0].role);
    free(session_messages[0].content);
    memmove(&session_messages[0], &session_messages[1], (session_count - 1) * sizeof(session_messages[0]));
    session_count--;
  }
}

static void session_clear_all(void) {
  int i;
  for (i = 0; i < session_count; i++) {
    free(session_messages[i].role);
    free(session_messages[i].content);
    session_messages[i].role = NULL;
    session_messages[i].content = NULL;
  }
  session_count = 0;
}

/*
 * 从落盘会话灌入内存缓冲。n_ids==0 则只清空。
 * 成功返回 0；失败 -1（已清空缓冲）。
 */
static int session_mount_disk(char *const *ids, int n_ids, int max_turns) {
  llm_message_t *msgs = NULL;
  int n = 0, i;
  session_clear_all();
  if (!ids || n_ids < 1) return 0;
  if (neo_session_load_many(ids, n_ids, &msgs, &n) != 0) return -1;
  for (i = 0; i < n; i++) {
    if (!msgs[i].role || !msgs[i].content) continue;
    session_append(msgs[i].role, msgs[i].content);
  }
  neo_session_free(msgs, n);
  session_trim_to(max_turns > 0 ? max_turns : 10);
  return 0;
}

/* 挂载时把本轮写回 ids[0]；无挂载则 no-op。 */
static void session_persist_turn(const char *write_id, const char *user, const char *assistant,
                                 int max_turns) {
  if (!write_id || !write_id[0] || !user || !assistant) return;
  if (neo_session_append_turn(write_id, user, assistant, max_turns > 0 ? max_turns : 10) != 0)
    fprintf(stderr, "neo daemon: failed to persist turn to session '%s'\n", write_id);
}

static void daemon_log_session_banner(char *const *ids, int n_ids) {
  int i;
  if (!ids || n_ids < 1) return;
  fprintf(stderr, "neo daemon: session=%s", ids[0] ? ids[0] : "?");
  for (i = 1; i < n_ids; i++) fprintf(stderr, ",%s", ids[i] ? ids[i] : "?");
  if (n_ids > 1) fprintf(stderr, " (write=%s)", ids[0] ? ids[0] : "?");
  fprintf(stderr, "\n");
}

static int do_one_turn(agent_config_t *conf, char *system_prompt, const char *user_input, llm_response_t *out) {
  if (conf->tools.enabled && getenv("NEO_DISABLE_TOOLS") == NULL) {
    llm_message_t *pmsgs = NULL;
    int np = 0;
    if (session_count > 0) {
      pmsgs = malloc((size_t)session_count * sizeof(llm_message_t));
      if (!pmsgs) return -1;
      for (int i = 0; i < session_count; i++) {
        if (!session_messages[i].content) continue;
        pmsgs[np].role = session_messages[i].role;
        pmsgs[np].content = session_messages[i].content;
        np++;
      }
    }
    int err = agent_run_with_tools(conf, system_prompt, pmsgs, np, user_input, out);
    free(pmsgs);
    return err;
  }
  llm_message_t *msgs = malloc((session_count + 1) * sizeof(llm_message_t));
  if (!msgs) return -1;
  int n = 0;
  for (int i = 0; i < session_count && session_messages[i].content; i++)
    msgs[n++] = (llm_message_t){ session_messages[i].role, session_messages[i].content };
  msgs[n++] = (llm_message_t){ "user", user_input };
  int err = llm_chat_messages(
    conf->model.base_url, conf->model.name, conf->model.api_key,
    conf->model.max_tokens, conf->model.temperature,
    system_prompt, msgs, n, out);
  free(msgs);
  return err;
}

#define D_RESET   "\033[0m"
#define D_CYAN    "\033[36m"
#define D_YELLOW  "\033[33m"
#define D_GREEN   "\033[32m"
#define D_BOLD    "\033[1m"
static void daemon_debug_print(agent_config_t *conf, const char *system_prompt, const char *user_message) {
  const char *t = getenv("TERM");
  int use_color = t && t[0] && strcmp(t, "dumb") != 0;
  const char *cy = use_color ? D_CYAN : "";
  const char *yl = use_color ? D_YELLOW : "";
  const char *gr = use_color ? D_GREEN : "";
  const char *bd = use_color ? D_BOLD : "";
  const char *re = use_color ? D_RESET : "";
  fprintf(stderr, "\n%s%s=== NEO DEBUG: request params ===%s\n", bd, cy, re);
  fprintf(stderr, "%sbase_url: %s\nmodel: %s\nmax_tokens: %d\ntemperature: %.2f\n%s", cy,
          conf->model.base_url ? conf->model.base_url : "(null)", conf->model.name ? conf->model.name : "(null)",
          conf->model.max_tokens, conf->model.temperature, re);
  if (conf->rules.path_count > 0) {
    fprintf(stderr, "%srules: ", cy);
    for (int i = 0; i < conf->rules.path_count; i++)
      fprintf(stderr, "%s%s", i ? ", " : "", conf->rules.paths[i] ? conf->rules.paths[i] : "(null)");
    fprintf(stderr, "%s\n", re);
  }
  fprintf(stderr, "\n%s%s=== NEO DEBUG: system prompt (%zu chars) ===%s\n%s%s%s\n%s%s=== END system prompt ===%s\n",
          bd, yl, strlen(system_prompt), re, yl, system_prompt, re, bd, yl, re);
  fprintf(stderr, "\n%s%s=== NEO DEBUG: user message (%zu chars) ===%s\n%s%s%s\n%s%s=== END user message ===%s\n\n",
          bd, gr, strlen(user_message), re, gr, user_message, re, bd, gr, re);
}

/* 交互式 REPL：TTY 下显示角色提示符；管道模式保持无前缀原文，方便脚本。 */
static int daemon_stdin_interactive(void) {
#if defined(__linux__) || defined(__APPLE__)
  return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#else
  return 0;
#endif
}

static int daemon_term_color(void) {
  const char *t = getenv("TERM");
  return t && t[0] && strcmp(t, "dumb") != 0;
}

#if defined(__linux__) || defined(__APPLE__)
static struct termios daemon_stdin_tio_saved;
static int daemon_stdin_tio_saved_ok;

/* 退出交互前恢复 termios，避免把调用方终端设置永久改掉。 */
static void daemon_stdin_termios_restore(void) {
  if (daemon_stdin_tio_saved_ok) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &daemon_stdin_tio_saved);
    daemon_stdin_tio_saved_ok = 0;
  }
}

/*
 * cooked 输入默认按「字节」擦除；中文等 UTF-8 多字节字符需要 IUTF8，
 * 否则一次退格只删 1 字节，留下乱码。同时清 ISTRIP，避免剥掉高位。
 */
static void daemon_stdin_utf8_enable(void) {
  struct termios tio;
  if (!isatty(STDIN_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &tio) != 0) return;
  daemon_stdin_tio_saved = tio;
  daemon_stdin_tio_saved_ok = 1;
#ifdef IUTF8
  tio.c_iflag |= IUTF8;
#endif
  tio.c_iflag &= ~(tcflag_t)ISTRIP;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) != 0)
    daemon_stdin_tio_saved_ok = 0;
}
#else
static void daemon_stdin_termios_restore(void) {}
static void daemon_stdin_utf8_enable(void) {}
#endif

static void daemon_print_assistant(const char *data, size_t size, int render, int interactive) {
  int use_color = interactive && daemon_term_color();
  const char *gr = use_color ? D_GREEN : "";
  const char *bd = use_color ? D_BOLD : "";
  const char *re = use_color ? D_RESET : "";

  if (interactive) {
    fprintf(stdout, "\n%s%sneo>%s\n", bd, gr, re);
    fflush(stdout);
  }
  if (render && data && size) {
    if (neo_md_term_render(data, size, stdout, use_color) == 0) {
      if (data[size - 1] != '\n') putchar('\n');
      if (interactive) putchar('\n');
      fflush(stdout);
      return;
    }
    fprintf(stderr, "neo: markdown render failed; printing raw text\n");
  }
  if (data && size) {
    fwrite(data, 1, size, stdout);
    if (data[size - 1] != '\n') putchar('\n');
  }
  if (interactive) putchar('\n');
  fflush(stdout);
}

int run_daemon_stdin(agent_config_t *conf, int debug, int verbose, int render,
                     char *const *session_ids, int n_ids) {
  char *system_prompt = malloc(SYSTEM_MAX);
  char *line_buf = malloc(LINE_MAX);
  int interactive;
  int max_turns;
  const char *write_id = NULL;
  if (!system_prompt || !line_buf) {
    free(system_prompt);
    free(line_buf);
    return -1;
  }
  max_turns = conf && conf->session_max_turns > 0 ? conf->session_max_turns : 10;
  if (session_mount_disk(session_ids, n_ids, max_turns) != 0) {
    fprintf(stderr, "neo daemon: failed to load session(s)\n");
    free(system_prompt);
    free(line_buf);
    return -1;
  }
  if (n_ids > 0 && session_ids) write_id = session_ids[0];
  interactive = daemon_stdin_interactive();
  if (interactive) {
#if defined(__linux__) || defined(__APPLE__)
    /* 让 libc/宽字符相关路径认 UTF-8；退格靠下面的 IUTF8。 */
    (void)setlocale(LC_CTYPE, "");
#endif
    daemon_stdin_utf8_enable();
    fprintf(stderr,
            "neo daemon: interactive mode. Prompts: User> (input) / neo> (reply). "
            "Type 'exit' or 'quit' or EOF to stop.\n");
  } else {
    fprintf(stderr, "neo daemon: stdin mode. Type 'exit' or 'quit' or EOF to stop.\n");
  }
  daemon_log_session_banner(session_ids, n_ids);
  for (;;) {
    size_t len;
    llm_response_t resp = {0};
    int use_color;
    const char *cy, *bd, *re;

    if (interactive) {
      use_color = daemon_term_color();
      cy = use_color ? D_CYAN : "";
      bd = use_color ? D_BOLD : "";
      re = use_color ? D_RESET : "";
      if (write_id && write_id[0])
        fprintf(stderr, "%s%sUser[%s]>%s ", bd, cy, write_id, re);
      else
        fprintf(stderr, "%s%sUser>%s ", bd, cy, re);
      fflush(stderr);
    }
    if (!fgets(line_buf, LINE_MAX, stdin)) break;
    len = strlen(line_buf);
    while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) line_buf[--len] = '\0';
    if (len == 0) continue;
    if (strcmp(line_buf, "exit") == 0 || strcmp(line_buf, "quit") == 0) break;
    (void)neo_memory_auto_store(conf, line_buf, verbose || debug);
    build_system_prompt(conf, line_buf, system_prompt, SYSTEM_MAX, verbose || debug);
    if (debug) daemon_debug_print(conf, system_prompt, line_buf);
    if (do_one_turn(conf, system_prompt, line_buf, &resp) != 0) {
      fprintf(stderr, "neo: LLM request failed\n");
      llm_response_free(&resp);
      continue;
    }
    if (resp.data && resp.size) {
      daemon_print_assistant(resp.data, resp.size, render, interactive);
      session_append("user", line_buf);
      session_append("assistant", resp.data);
      session_trim_to(max_turns);
      session_persist_turn(write_id, line_buf, resp.data, max_turns);
    }
    llm_response_free(&resp);
  }
  daemon_stdin_termios_restore();
  session_clear_all();
  free(system_prompt);
  free(line_buf);
  return 0;
}

#ifdef HAVE_UNIX_SOCKET
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug, int verbose,
                      char *const *session_ids, int n_ids) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  int max_turns;
  const char *write_id = NULL;
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
  unlink(socket_path);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 5) < 0) {
    perror("listen");
    close(fd);
    return -1;
  }
  max_turns = conf && conf->session_max_turns > 0 ? conf->session_max_turns : 10;
  if (session_mount_disk(session_ids, n_ids, max_turns) != 0) {
    fprintf(stderr, "neo daemon: failed to load session(s)\n");
    close(fd);
    return -1;
  }
  if (n_ids > 0 && session_ids) write_id = session_ids[0];
  fprintf(stderr, "neo daemon: listening on %s\n", socket_path);
  daemon_log_session_banner(session_ids, n_ids);

  char *system_prompt = malloc(SYSTEM_MAX);
  char *line_buf = malloc(LINE_MAX);
  if (!system_prompt || !line_buf) {
    free(system_prompt);
    free(line_buf);
    session_clear_all();
    close(fd);
    return -1;
  }

  for (;;) {
    int client = accept(fd, NULL, NULL);
    if (client < 0) continue;
    line_buf[0] = '\0';
    size_t n = 0;
    while (n < LINE_MAX - 1) {
      char c;
      if (read(client, &c, 1) != 1) break;
      if (c == '\n' || c == '\r') break;
      line_buf[n++] = c;
    }
    line_buf[n] = '\0';
    if (n > 0) {
      (void)neo_memory_auto_store(conf, line_buf, verbose || debug);
      build_system_prompt(conf, line_buf, system_prompt, SYSTEM_MAX, verbose || debug);
      if (debug) daemon_debug_print(conf, system_prompt, line_buf);
      llm_response_t resp = {0};
      if (do_one_turn(conf, system_prompt, line_buf, &resp) == 0 && resp.data && resp.size) {
        write(client, resp.data, resp.size);
        if (resp.size > 0 && resp.data[resp.size - 1] != '\n') write(client, "\n", 1);
        session_append("user", line_buf);
        session_append("assistant", resp.data);
        session_trim_to(max_turns);
        session_persist_turn(write_id, line_buf, resp.data, max_turns);
      }
      llm_response_free(&resp);
    }
    close(client);
  }
  free(system_prompt);
  free(line_buf);
  session_clear_all();
  close(fd);
  return 0;
}
#else
int run_daemon_socket(agent_config_t *conf, const char *socket_path, int debug, int verbose,
                      char *const *session_ids, int n_ids) {
  (void)conf;
  (void)socket_path;
  (void)debug;
  (void)verbose;
  (void)session_ids;
  (void)n_ids;
  fprintf(stderr, "neo: Unix socket not supported on this platform\n");
  return -1;
}
#endif
