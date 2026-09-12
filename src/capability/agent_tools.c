/*
 * 反应式工具循环与本地 builtin：按 Capability Matrix 名分发。
 * 路径必须落在 root 的 realpath 之下；禁止绝对路径与 ".." 段。
 */
#include "agent_tools.h"
#include "capability_dir.h"
#include "capability_matrix.h"
#include "command_tools.h"
#include "mcp_stdio.h"
#include "neo_http.h"
#include "neo_memory.h"
#include "yyjson.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define NEO_MAX_TOOLS_PER_TURN 16
#define NEO_BODY_INIT (256 * 1024)

typedef struct {
  char *s;
  size_t len;
  size_t cap;
} NeoBuf;

typedef struct {
  char *id;
  char *name;
  char *arguments; /* JSON object as string */
} NeoToolCall;

static void neo_buf_free(NeoBuf *b) {
  free(b->s);
  b->s = NULL;
  b->len = b->cap = 0;
}

static int neo_buf_reserve(NeoBuf *b, size_t add) {
  size_t need = b->len + add + 1;
  if (need <= b->cap) return 0;
  size_t nc = b->cap ? b->cap : NEO_BODY_INIT;
  while (nc < need) {
    if (nc > (SIZE_MAX / 2)) return -1;
    nc *= 2;
  }
  char *p = realloc(b->s, nc);
  if (!p) return -1;
  b->s = p;
  b->cap = nc;
  return 0;
}

static int neo_buf_append(NeoBuf *b, const char *frag, size_t n) {
  if (!frag) return 0;
  if (n == 0) n = strlen(frag);
  if (neo_buf_reserve(b, n) != 0) return -1;
  memcpy(b->s + b->len, frag, n);
  b->len += n;
  b->s[b->len] = '\0';
  return 0;
}

static void neo_json_escape(const char *in, NeoBuf *b) {
  if (!in) return;
  for (; *in; in++) {
    char esc[8];
    const char *e = NULL;
    if (*in == '\\' || *in == '"' || *in == '\n' || *in == '\r' || *in == '\t') {
      esc[0] = '\\';
      esc[1] = (*in == '\n') ? 'n' : (*in == '\r') ? 'r' : (*in == '\t') ? 't' : *in;
      esc[2] = '\0';
      e = esc;
    } else {
      esc[0] = *in;
      esc[1] = '\0';
      e = esc;
    }
    neo_buf_append(b, e, strlen(e));
  }
}

static int neo_build_tools_json(NeoBuf *b, const agent_config_t *conf) {
  capability_matrix_t m;
  char *json;
  int r;
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, conf) != 0) {
    capability_matrix_free(&m);
    return -1;
  }
  json = capability_matrix_tools_json(&m);
  capability_matrix_free(&m);
  if (!json) return -1;
  r = neo_buf_append(b, json, 0);
  free(json);
  return r;
}

static char *yy_strdup_val(yyjson_val *v) {
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

static int extract_string_field(const char *obj, const char *key, char *out, size_t out_cap) {
  yyjson_doc *doc;
  yyjson_val *root, *v;
  const char *s;
  size_t n;
  if (!obj || !key || !out || out_cap == 0) return -1;
  doc = yyjson_read(obj, strlen(obj), 0);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  v = yyjson_obj_get(root, key);
  if (!yyjson_is_str(v)) {
    yyjson_doc_free(doc);
    return -1;
  }
  s = yyjson_get_str(v);
  n = yyjson_get_len(v);
  if (n >= out_cap) {
    yyjson_doc_free(doc);
    return -1;
  }
  memcpy(out, s ? s : "", n);
  out[n] = '\0';
  yyjson_doc_free(doc);
  return 0;
}

/* Optional boolean; missing key → *out_val unchanged; returns 0 on ok/-1 on bad type. */
static int extract_bool_field_opt(const char *obj, const char *key, int *out_val) {
  yyjson_doc *doc;
  yyjson_val *root, *v;
  if (!obj || !key || !out_val) return -1;
  doc = yyjson_read(obj, strlen(obj), 0);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  v = yyjson_obj_get(root, key);
  if (!v) {
    yyjson_doc_free(doc);
    return 0;
  }
  if (!yyjson_is_bool(v)) {
    yyjson_doc_free(doc);
    return -1;
  }
  *out_val = yyjson_get_bool(v) ? 1 : 0;
  yyjson_doc_free(doc);
  return 0;
}

static yyjson_val *choice0_message(yyjson_val *root) {
  yyjson_val *choices, *c0, *msg;
  choices = yyjson_obj_get(root, "choices");
  if (!yyjson_is_arr(choices) || yyjson_arr_size(choices) < 1) return NULL;
  c0 = yyjson_arr_get(choices, 0);
  msg = yyjson_obj_get(c0, "message");
  return yyjson_is_obj(msg) ? msg : NULL;
}

static int msg_content_to_llm(yyjson_val *msg, llm_response_t *out) {
  yyjson_val *content;
  out->data = NULL;
  out->size = 0;
  if (!msg) return -1;
  content = yyjson_obj_get(msg, "content");
  if (!content || yyjson_is_null(content)) {
    out->data = malloc(1);
    if (!out->data) return -1;
    out->data[0] = '\0';
    out->size = 0;
    return 0;
  }
  if (!yyjson_is_str(content)) return -1;
  out->data = yy_strdup_val(content);
  if (!out->data) return -1;
  out->size = strlen(out->data);
  return 0;
}

static void neo_tool_calls_free(NeoToolCall *tc, int n) {
  int i;
  for (i = 0; i < n; i++) {
    free(tc[i].id);
    free(tc[i].name);
    free(tc[i].arguments);
  }
}

static int parse_tool_calls_yy(yyjson_val *msg, NeoToolCall *out, int *out_n) {
  yyjson_val *tc, *el;
  size_t i, n;
  *out_n = 0;
  if (!msg) return -1;
  tc = yyjson_obj_get(msg, "tool_calls");
  if (!yyjson_is_arr(tc)) return -1;
  n = yyjson_arr_size(tc);
  if (n > (size_t)NEO_MAX_TOOLS_PER_TURN)
    capability_warn_tool_truncation(n, NEO_MAX_TOOLS_PER_TURN, stderr);
  for (i = 0; i < n && *out_n < NEO_MAX_TOOLS_PER_TURN; i++) {
    NeoToolCall *t;
    yyjson_val *fn, *args;
    el = yyjson_arr_get(tc, i);
    if (!yyjson_is_obj(el)) return -1;
    t = &out[*out_n];
    t->id = t->name = t->arguments = NULL;
    t->id = yy_strdup_val(yyjson_obj_get(el, "id"));
    fn = yyjson_obj_get(el, "function");
    if (yyjson_is_obj(fn)) {
      t->name = yy_strdup_val(yyjson_obj_get(fn, "name"));
      args = yyjson_obj_get(fn, "arguments");
      if (yyjson_is_str(args)) {
        t->arguments = yy_strdup_val(args);
      } else if (yyjson_is_obj(args) || yyjson_is_arr(args)) {
        t->arguments = yyjson_val_write(args, 0, NULL);
      }
    }
    if (t->id && t->name && t->arguments) {
      (*out_n)++;
    } else {
      free(t->id);
      free(t->name);
      free(t->arguments);
      t->id = t->name = t->arguments = NULL;
    }
  }
  return 0;
}

static int append_message_json(yyjson_val *msg, NeoBuf *dst) {
  char *s = yyjson_val_write(msg, 0, NULL);
  int r;
  if (!s) return -1;
  r = neo_buf_append(dst, s, strlen(s));
  free(s);
  return r;
}

#if defined(__APPLE__) || defined(__linux__)
static int path_is_safe_rel(const char *rel) {
  if (!rel || !rel[0] || rel[0] == '/') return 0;
  if (strstr(rel, "..")) return 0;
  return 1;
}

static int resolve_under_root(const char *root_real, const char *rel, char *full, size_t full_cap) {
  /* 相对路径拼到 root 下并用 realpath 校验前缀，防止逃出沙箱。
   * 新文件尚不存在时：校验父目录仍在 root 内。 */
  if (!path_is_safe_rel(rel)) return -1;
  if (snprintf(full, full_cap, "%s/%s", root_real, rel) >= (int)full_cap) return -1;
  char resolved[PATH_MAX];
  if (realpath(full, resolved)) {
    size_t lr = strlen(root_real);
    if (strncmp(resolved, root_real, lr) != 0) return -1;
    if (resolved[lr] != '\0' && resolved[lr] != '/') return -1;
    strncpy(full, resolved, full_cap - 1);
    full[full_cap - 1] = '\0';
    return 0;
  }
  /* New file: ensure parent directory is under root */
  char relcopy[PATH_MAX];
  if (strlen(rel) >= sizeof(relcopy)) return -1;
  strcpy(relcopy, rel);
  char *slash = strrchr(relcopy, '/');
  if (!slash) {
    strncpy(full, root_real, full_cap - 1);
    full[full_cap - 1] = '\0';
    strncat(full, "/", full_cap - strlen(full) - 1);
    strncat(full, rel, full_cap - strlen(full) - 1);
    return 0;
  }
  *slash = '\0';
  char parent_full[PATH_MAX];
  if (snprintf(parent_full, sizeof(parent_full), "%s/%s", root_real, relcopy) >= (int)sizeof(parent_full)) return -1;
  if (!realpath(parent_full, resolved)) return -1;
  size_t lr = strlen(root_real);
  if (strncmp(resolved, root_real, lr) != 0) return -1;
  if (resolved[lr] != '\0' && resolved[lr] != '/') return -1;
  if (snprintf(full, full_cap, "%s/%s", root_real, rel) >= (int)full_cap) return -1;
  return 0;
}
#else
static int resolve_under_root(const char *root_real, const char *rel, char *full, size_t full_cap) {
  (void)root_real;
  (void)rel;
  (void)full;
  (void)full_cap;
  return -1;
}
#endif

static int neo_host_eq_ci(const char *a, const char *b) {
  for (;;) {
    unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
    if (ca != cb) return 0;
    if (ca == '\0') return 1;
    a++;
    b++;
  }
}

static int http_host_in_allowlist(const char *host, const char *csv) {
  char buf[512];
  char *saveptr = NULL;
  size_t n = strlen(csv);
  if (n == 0 || n >= sizeof(buf)) return 0;
  memcpy(buf, csv, n + 1);
  for (char *tok = strtok_r(buf, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr)) {
    while (*tok == ' ' || *tok == '\t') tok++;
    {
      char *e = tok + strlen(tok);
      while (e > tok && (e[-1] == ' ' || e[-1] == '\t')) {
        e--;
        *e = '\0';
      }
    }
    if (*tok != '\0' && neo_host_eq_ci(tok, host)) return 1;
  }
  return 0;
}

static int url_https_extract_host(const char *url, char *host, size_t hostcap) {
  const char *p;
  const char *at;
  const char *slash;
  const char *end;
  size_t n;
  if (strncmp(url, "https://", 8) != 0) return -1;
  p = url + 8;
  at = strchr(p, '@');
  slash = strchr(p, '/');
  if (at != NULL && (slash == NULL || at < slash)) p = at + 1;
  end = p;
  while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') end++;
  n = (size_t)(end - p);
  if (n == 0 || n >= hostcap) return -1;
  memcpy(host, p, n);
  host[n] = '\0';
  {
    char *c = strchr(host, ':');
    if (c) *c = '\0';
  }
  if (host[0] == '[') return -1;
  return 0;
}

static int tool_http_get(const agent_config_t *conf, const char *args_json, NeoBuf *result) {
  char urlbuf[2048];
  char host[256];
  neo_http_response_t hr;
  size_t max_bytes;
  if (!conf->tools.http_fetch_enabled || conf->tools.http_allow_hosts == NULL || conf->tools.http_allow_hosts[0] == '\0')
    return neo_buf_append(result, "ERROR: http_get disabled (set http_fetch_enabled: true and http_allow_hosts)", 0);
  if (extract_string_field(args_json, "url", urlbuf, sizeof(urlbuf)) != 0)
    return neo_buf_append(result, "ERROR: missing url", 0);
  if (url_https_extract_host(urlbuf, host, sizeof(host)) != 0)
    return neo_buf_append(result, "ERROR: url must be https://hostname/... (no IPv6 literal)", 0);
  if (!http_host_in_allowlist(host, conf->tools.http_allow_hosts))
    return neo_buf_append(result, "ERROR: host not in tools.http_allow_hosts", 0);

  max_bytes = (size_t)(conf->tools.http_fetch_max_bytes > 0 ? conf->tools.http_fetch_max_bytes : 262144);
  memset(&hr, 0, sizeof(hr));
  if (neo_http_get(urlbuf, max_bytes, 30, &hr) != 0) {
    neo_http_response_free(&hr);
    return neo_buf_append(result, "ERROR: http request failed", 0);
  }
  neo_buf_append(result, "HTTP ", 0);
  {
    char nb[40];
    snprintf(nb, sizeof(nb), "%ld\n", hr.status);
    neo_buf_append(result, nb, strlen(nb));
  }
  neo_buf_append(result, hr.body ? hr.body : "", hr.body_len);
  if (max_bytes > 0 && hr.body_len >= max_bytes) neo_buf_append(result, "\n...[truncated]", 0);
  neo_http_response_free(&hr);
  return 0;
}

#if defined(__APPLE__) || defined(__linux__)
static int tool_list_dir(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  char rel[PATH_MAX];
  char full[PATH_MAX];
  struct stat st;
  int cap;
  int n;
  DIR *d;
  struct dirent *e;
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
    return neo_buf_append(result, "ERROR: missing path", 0);
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0)
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
    return neo_buf_append(result, "ERROR: not a directory", 0);
  cap = conf->tools.list_dir_max_entries > 0 ? conf->tools.list_dir_max_entries : 256;
  d = opendir(full);
  if (!d) {
    neo_buf_append(result, "ERROR: opendir: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  n = 0;
  while ((e = readdir(d)) != NULL && n < cap) {
    if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
      continue;
    if (neo_buf_append(result, e->d_name, strlen(e->d_name)) != 0) {
      closedir(d);
      return -1;
    }
    if (neo_buf_append(result, "\n", 1) != 0) {
      closedir(d);
      return -1;
    }
    n++;
  }
  closedir(d);
  return 0;
}

static int grep_name_ok(const char *name, const char *glob) {
  size_t nl, gl;
  if (!glob || !glob[0]) return 1;
  if (!name) return 0;
  nl = strlen(name);
  gl = strlen(glob);
  if (glob[0] == '*' && gl > 1) {
    const char *suf = glob + 1;
    size_t sl = strlen(suf);
    return nl >= sl && strcmp(name + nl - sl, suf) == 0;
  }
  if (gl > 0 && glob[gl - 1] == '*') {
    return strncmp(name, glob, gl - 1) == 0;
  }
  return strcmp(name, glob) == 0;
}

static int grep_file(const char *full, const char *rel_display, const char *pattern, int *left,
                     NeoBuf *result) {
  FILE *f;
  char line[4096];
  int lineno = 0;
  if (*left <= 0) return 0;
  f = fopen(full, "rb");
  if (!f) return 0;
  while (*left > 0 && fgets(line, sizeof(line), f)) {
    lineno++;
    if (strstr(line, pattern)) {
      char hdr[PATH_MAX + 64];
      size_t L = strlen(line);
      while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = '\0';
      snprintf(hdr, sizeof(hdr), "%s:%d:", rel_display, lineno);
      if (neo_buf_append(result, hdr, 0) != 0 || neo_buf_append(result, line, 0) != 0 ||
          neo_buf_append(result, "\n", 1) != 0) {
        fclose(f);
        return -1;
      }
      (*left)--;
    }
  }
  fclose(f);
  return 0;
}

static int grep_walk(const char *root_real, const char *rel, const char *pattern, const char *glob,
                     int *left, int depth, NeoBuf *result) {
  char full[PATH_MAX];
  DIR *d;
  struct dirent *e;
  struct stat st;
  if (*left <= 0 || depth > 8) return 0;
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0) return 0;
  if (stat(full, &st) != 0) return 0;
  if (S_ISREG(st.st_mode)) {
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    if (!grep_name_ok(base, glob)) return 0;
    return grep_file(full, rel, pattern, left, result);
  }
  if (!S_ISDIR(st.st_mode)) return 0;
  d = opendir(full);
  if (!d) return 0;
  while (*left > 0 && (e = readdir(d)) != NULL) {
    char child_rel[PATH_MAX];
    if (e->d_name[0] == '.') continue;
    if (snprintf(child_rel, sizeof(child_rel), "%s/%s", strcmp(rel, ".") == 0 ? "." : rel, e->d_name) >=
        (int)sizeof(child_rel))
      continue;
    /* normalize "./foo" */
    if (strncmp(child_rel, "./", 2) == 0) {
      if (grep_walk(root_real, child_rel + 2, pattern, glob, left, depth + 1, result) != 0) {
        closedir(d);
        return -1;
      }
    } else if (grep_walk(root_real, child_rel, pattern, glob, left, depth + 1, result) != 0) {
      closedir(d);
      return -1;
    }
  }
  closedir(d);
  return 0;
}

static int tool_grep(const agent_config_t *conf, const char *root_real, const char *args_json,
                     NeoBuf *result) {
  char pattern[512];
  char path[PATH_MAX];
  char glob[256];
  int left = 50;
  (void)conf;
  if (extract_string_field(args_json, "pattern", pattern, sizeof(pattern)) != 0 || !pattern[0])
    return neo_buf_append(result, "ERROR: missing pattern", 0);
  if (extract_string_field(args_json, "path", path, sizeof(path)) != 0 || !path[0])
    strcpy(path, ".");
  if (extract_string_field(args_json, "glob", glob, sizeof(glob)) != 0)
    glob[0] = '\0';
  if (grep_walk(root_real, path, pattern, glob[0] ? glob : NULL, &left, 0, result) != 0) return -1;
  if (result->len == 0) return neo_buf_append(result, "(no matches)\n", 0);
  return 0;
}

static int tool_run_command(const agent_config_t *conf, const char *root_real, const char *args_json,
                            NeoBuf *result) {
  yyjson_doc *doc;
  yyjson_val *root, *arr, *el;
  tool_command_t tmp;
  char **argv = NULL;
  size_t i, n;
  char *out = NULL;
  size_t out_len = 0;
  int rc;

  if (!conf->tools.shell_enabled)
    return neo_buf_append(result, "ERROR: run_command disabled (set tools.shell_enabled: true)", 0);
  doc = yyjson_read(args_json ? args_json : "{}", strlen(args_json ? args_json : "{}"), 0);
  if (!doc) return neo_buf_append(result, "ERROR: bad args JSON", 0);
  root = yyjson_doc_get_root(doc);
  arr = yyjson_is_obj(root) ? yyjson_obj_get(root, "argv") : NULL;
  if (!yyjson_is_arr(arr) || yyjson_arr_size(arr) < 1) {
    yyjson_doc_free(doc);
    return neo_buf_append(result, "ERROR: argv must be a non-empty array", 0);
  }
  n = yyjson_arr_size(arr);
  argv = calloc(n, sizeof(char *));
  if (!argv) {
    yyjson_doc_free(doc);
    return -1;
  }
  for (i = 0; i < n; i++) {
    el = yyjson_arr_get(arr, i);
    if (!yyjson_is_str(el)) {
      for (i = 0; i < n; i++) free(argv[i]);
      free(argv);
      yyjson_doc_free(doc);
      return neo_buf_append(result, "ERROR: argv entries must be strings", 0);
    }
    argv[i] = strdup(yyjson_get_str(el) ? yyjson_get_str(el) : "");
    if (!argv[i]) {
      size_t j;
      for (j = 0; j < i; j++) free(argv[j]);
      free(argv);
      yyjson_doc_free(doc);
      return -1;
    }
  }
  yyjson_doc_free(doc);
  memset(&tmp, 0, sizeof(tmp));
  tmp.argv = argv;
  tmp.argv_count = (int)n;
  tmp.timeout_sec = 30;
  tmp.max_output_bytes = 65536;
  tmp.pass_args = 0;
  rc = command_tool_run(conf, root_real, &tmp, "{}", &out, &out_len);
  for (i = 0; i < n; i++) free(argv[i]);
  free(argv);
  if (rc != 0) {
    int r = neo_buf_append(result, out ? out : "ERROR: run_command failed", 0);
    free(out);
    return r;
  }
  {
    int r = neo_buf_append(result, out ? out : "", 0);
    free(out);
    return r;
  }
}
#else
static int tool_list_dir(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  (void)conf;
  (void)root_real;
  (void)args_json;
  return neo_buf_append(result, "ERROR: list_dir unsupported on this platform", 0);
}
static int tool_grep(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  (void)conf;
  (void)root_real;
  (void)args_json;
  return neo_buf_append(result, "ERROR: grep unsupported on this platform", 0);
}
static int tool_run_command(const agent_config_t *conf, const char *root_real, const char *args_json,
                            NeoBuf *result) {
  (void)conf;
  (void)root_real;
  (void)args_json;
  return neo_buf_append(result, "ERROR: run_command unsupported on this platform", 0);
}
#endif

static int tool_read_file(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  char rel[PATH_MAX];
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
    return neo_buf_append(result, "ERROR: missing path", 0);
  char full[PATH_MAX];
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0)
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  FILE *f = fopen(full, "rb");
  if (!f) {
    neo_buf_append(result, "ERROR: open failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  int cap = conf->tools.max_read_bytes > 0 ? conf->tools.max_read_bytes : 262144;
  if (sz > cap) {
    fclose(f);
    return neo_buf_append(result, "ERROR: file too large", 0);
  }
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return neo_buf_append(result, "ERROR: oom", 0);
  }
  size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = '\0';
  if (neo_buf_append(result, buf, n) != 0) {
    free(buf);
    return -1;
  }
  free(buf);
  return 0;
}

static int tool_write_file(const agent_config_t *conf, const char *root_real, const char *args_json, NeoBuf *result) {
  (void)conf;
  char rel[PATH_MAX];
  size_t ccap = 512 * 1024;
  char *content = malloc(ccap);
  if (!content) return neo_buf_append(result, "ERROR: oom", 0);
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing path", 0);
  }
  if (extract_string_field(args_json, "content", content, ccap) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing content", 0);
  }
  char full[PATH_MAX];
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  }
  FILE *f = fopen(full, "wb");
  if (!f) {
    free(content);
    neo_buf_append(result, "ERROR: write failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  size_t L = strlen(content);
  if (fwrite(content, 1, L, f) != L) {
    fclose(f);
    free(content);
    return neo_buf_append(result, "ERROR: short write", 0);
  }
  fclose(f);
  free(content);
  return neo_buf_append(result, "ok", 0);
}

static int tool_append_file(const agent_config_t *conf, const char *root_real, const char *args_json,
                            NeoBuf *result) {
  (void)conf;
  char rel[PATH_MAX];
  size_t ccap = 512 * 1024;
  char *content = malloc(ccap);
  if (!content) return neo_buf_append(result, "ERROR: oom", 0);
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing path", 0);
  }
  if (extract_string_field(args_json, "content", content, ccap) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: missing content", 0);
  }
  char full[PATH_MAX];
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0) {
    free(content);
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  }
  FILE *f = fopen(full, "ab");
  if (!f) {
    free(content);
    neo_buf_append(result, "ERROR: append failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  size_t L = strlen(content);
  if (fwrite(content, 1, L, f) != L) {
    fclose(f);
    free(content);
    return neo_buf_append(result, "ERROR: short write", 0);
  }
  fclose(f);
  free(content);
  return neo_buf_append(result, "ok", 0);
}

#if defined(__APPLE__) || defined(__linux__)
static int tool_stat_path(const agent_config_t *conf, const char *root_real, const char *args_json,
                          NeoBuf *result) {
  char rel[PATH_MAX];
  char full[PATH_MAX];
  struct stat st;
  char line[512];
  const char *type;
  (void)conf;
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
    return neo_buf_append(result, "ERROR: missing path", 0);
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0)
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  if (lstat(full, &st) != 0) {
    neo_buf_append(result, "ERROR: stat failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  if (S_ISREG(st.st_mode))
    type = "file";
  else if (S_ISDIR(st.st_mode))
    type = "dir";
  else if (S_ISLNK(st.st_mode))
    type = "symlink";
  else
    type = "other";
  snprintf(line, sizeof(line), "path: %s\ntype: %s\nsize: %lld\nmtime: %lld\n", rel, type,
           (long long)st.st_size, (long long)st.st_mtime);
  return neo_buf_append(result, line, 0);
}

static int mkdir_p_under_root(const char *root_real, const char *rel, char *err, size_t err_cap) {
  /* 类似 mkdir -p，但每层创建后 realpath 校验仍在 root 下，防止链接逃逸。 */
  char full[PATH_MAX];
  char tmp[PATH_MAX];
  size_t lr;
  char *p;
  struct stat st;
  char resolved[PATH_MAX];

  if (!path_is_safe_rel(rel)) {
    snprintf(err, err_cap, "path not allowed");
    return -1;
  }
  if (snprintf(full, sizeof(full), "%s/%s", root_real, rel) >= (int)sizeof(full)) {
    snprintf(err, err_cap, "path too long");
    return -1;
  }
  lr = strlen(root_real);
  if (strlen(full) >= sizeof(tmp)) {
    snprintf(err, err_cap, "path too long");
    return -1;
  }
  memcpy(tmp, full, strlen(full) + 1);
  for (p = tmp + lr + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
      snprintf(err, err_cap, "%s", strerror(errno));
      return -1;
    }
    if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
      snprintf(err, err_cap, "not a directory");
      return -1;
    }
    if (!realpath(tmp, resolved) || strncmp(resolved, root_real, lr) != 0 ||
        (resolved[lr] != '\0' && resolved[lr] != '/')) {
      snprintf(err, err_cap, "path not allowed");
      return -1;
    }
    *p = '/';
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
    snprintf(err, err_cap, "%s", strerror(errno));
    return -1;
  }
  if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
    snprintf(err, err_cap, "not a directory");
    return -1;
  }
  if (!realpath(tmp, resolved) || strncmp(resolved, root_real, lr) != 0 ||
      (resolved[lr] != '\0' && resolved[lr] != '/')) {
    snprintf(err, err_cap, "path not allowed");
    return -1;
  }
  return 0;
}

static int tool_mkdir(const agent_config_t *conf, const char *root_real, const char *args_json,
                      NeoBuf *result) {
  char rel[PATH_MAX];
  char full[PATH_MAX];
  int parents = 0;
  char err[128];
  (void)conf;
  if (extract_string_field(args_json, "path", rel, sizeof(rel)) != 0)
    return neo_buf_append(result, "ERROR: missing path", 0);
  if (extract_bool_field_opt(args_json, "parents", &parents) != 0)
    return neo_buf_append(result, "ERROR: parents must be boolean", 0);
  if (parents) {
    if (mkdir_p_under_root(root_real, rel, err, sizeof(err)) != 0) {
      neo_buf_append(result, "ERROR: mkdir failed: ", 0);
      neo_json_escape(err, result);
      return 0;
    }
    return neo_buf_append(result, "ok", 0);
  }
  if (resolve_under_root(root_real, rel, full, sizeof(full)) != 0)
    return neo_buf_append(result, "ERROR: path not allowed", 0);
  if (mkdir(full, 0755) != 0) {
    neo_buf_append(result, "ERROR: mkdir failed: ", 0);
    neo_json_escape(strerror(errno), result);
    return 0;
  }
  return neo_buf_append(result, "ok", 0);
}
#else
static int tool_stat_path(const agent_config_t *conf, const char *root_real, const char *args_json,
                          NeoBuf *result) {
  (void)conf;
  (void)root_real;
  (void)args_json;
  return neo_buf_append(result, "ERROR: stat unsupported on this platform", 0);
}
static int tool_mkdir(const agent_config_t *conf, const char *root_real, const char *args_json,
                      NeoBuf *result) {
  (void)conf;
  (void)root_real;
  (void)args_json;
  return neo_buf_append(result, "ERROR: mkdir unsupported on this platform", 0);
}
#endif

static int run_one_tool(const agent_config_t *conf, const char *root_real, NeoToolCall *tc, NeoBuf *result) {
  int idx;
  char *out = NULL;
  size_t out_len = 0;
  result->len = 0;
  if (result->s) result->s[0] = '\0';
  fprintf(stderr, "neo tool: %s\n", tc->name);
  if (strcmp(tc->name, "read_file") == 0)
    return tool_read_file(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "write_file") == 0)
    return tool_write_file(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "append_file") == 0)
    return tool_append_file(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "stat") == 0)
    return tool_stat_path(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "mkdir") == 0)
    return tool_mkdir(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "propose_capability") == 0) {
    char *out = NULL;
    int r = capability_dir_propose(conf, tc->arguments, &out);
    if (r != 0) {
      free(out);
      return neo_buf_append(result, "ERROR: propose_capability failed", 0);
    }
    r = neo_buf_append(result, out ? out : "", 0);
    free(out);
    return r;
  }
  if (strcmp(tc->name, "memory_add") == 0) {
    char text[4096];
    neo_memory_t *mem;
    char okbuf[96];
    if (!conf->memory.vector_enabled)
      return neo_buf_append(result, "ERROR: memory_add requires memory.vector.enabled", 0);
    if (extract_string_field(tc->arguments ? tc->arguments : "{}", "text", text, sizeof(text)) != 0 ||
        !text[0])
      return neo_buf_append(result, "ERROR: memory_add requires text", 0);
    mem = neo_memory_open(conf);
    if (!mem) return neo_buf_append(result, "ERROR: memory open failed", 0);
    if (neo_memory_store(mem, text) != 0) {
      neo_memory_close(mem);
      return neo_buf_append(result, "ERROR: memory_add store failed", 0);
    }
    snprintf(okbuf, sizeof(okbuf), "OK: stored (%d chunks in memory)", neo_memory_count(mem));
    neo_memory_close(mem);
    return neo_buf_append(result, okbuf, 0);
  }
  if (strcmp(tc->name, "list_dir") == 0)
    return tool_list_dir(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "grep") == 0)
    return tool_grep(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "run_command") == 0)
    return tool_run_command(conf, root_real, tc->arguments, result);
  if (strcmp(tc->name, "http_get") == 0)
    return tool_http_get(conf, tc->arguments, result);
  if (strncmp(tc->name, "mcp_", 4) == 0) {
    capability_matrix_t mx;
    const cap_row_t *row;
    capability_matrix_init(&mx);
    if (capability_matrix_build_from_config(&mx, conf) == 0) {
      row = capability_matrix_find(&mx, tc->name);
      if (row && row->source == CAP_SRC_MCP) {
        char *out = NULL;
        size_t out_len = 0;
        int rc = mcp_stdio_call(conf, row, tc->arguments ? tc->arguments : "{}", &out, &out_len);
        capability_matrix_free(&mx);
        if (rc != 0) {
          free(out);
          return neo_buf_append(result, "ERROR: mcp dispatch failed", 0);
        }
        {
          int r = neo_buf_append(result, out ? out : "", 0);
          free(out);
          return r;
        }
      }
    }
    capability_matrix_free(&mx);
    return neo_buf_append(result, "ERROR: unknown mcp tool", 0);
  }
  idx = command_tool_find(conf, tc->name);
  if (idx >= 0) {
    if (command_tool_run(conf, root_real, &conf->tools.commands[idx],
                         tc->arguments ? tc->arguments : "{}", &out, &out_len) != 0) {
      int r = neo_buf_append(result, out ? out : "ERROR: command tool failed", 0);
      free(out);
      return r;
    }
    {
      int r = neo_buf_append(result, out ? out : "", 0);
      free(out);
      return r;
    }
  }
  return neo_buf_append(result, "ERROR: unknown tool", 0);
}

int neo_dispatch_tool(const agent_config_t *conf, const char *root_real,
                      const char *name, const char *args_json,
                      char **out_text, size_t *out_len) {
  NeoToolCall tc;
  NeoBuf result;
  int r;
  if (out_text) *out_text = NULL;
  if (out_len) *out_len = 0;
  if (!conf || !root_real || !name || !out_text) return -1;
  memset(&tc, 0, sizeof(tc));
  memset(&result, 0, sizeof(result));
  tc.name = (char *)name;
  tc.arguments = (char *)(args_json ? args_json : "{}");
  r = run_one_tool(conf, root_real, &tc, &result);
  if (r != 0) {
    neo_buf_free(&result);
    return -1;
  }
  *out_text = result.s ? result.s : strdup("");
  if (out_len) *out_len = result.len;
  /* ownership transferred */
  return 0;
}

int agent_run_with_tools(
  const agent_config_t *conf,
  const char *system_prompt,
  const llm_message_t *prefix_messages,
  int n_prefix,
  const char *user_message,
  llm_response_t *out_text) {
  out_text->data = NULL;
  out_text->size = 0;
  if (!conf || !conf->model.base_url || !system_prompt || !user_message) return -1;

#if !defined(__APPLE__) && !defined(__linux__)
  (void)prefix_messages;
  (void)n_prefix;
  fprintf(stderr, "neo: tools not supported on this platform\n");
  return -1;
#else
  char root_real[PATH_MAX];
  const char *root = (conf->tools.root && conf->tools.root[0]) ? conf->tools.root : ".";
  if (!realpath(root, root_real)) {
    fprintf(stderr, "neo: tools.root realpath failed for %s\n", root);
    return -1;
  }

  NeoBuf body = {0};
  NeoBuf hist = {0};
  if (neo_buf_reserve(&body, NEO_BODY_INIT) != 0) return -1;
  if (neo_buf_reserve(&hist, 4096) != 0) {
    neo_buf_free(&body);
    return -1;
  }

  neo_buf_append(&body, "{\"model\":\"", 0);
  neo_json_escape(conf->model.name ? conf->model.name : "gpt", &body);
  neo_buf_append(&body, "\",\"messages\":[{\"role\":\"system\",\"content\":\"", 0);
  neo_json_escape(system_prompt, &body);
  neo_buf_append(&body, "\"}", 0);

  int pi;
  for (pi = 0; pi < n_prefix && prefix_messages && prefix_messages[pi].role && prefix_messages[pi].content; pi++) {
    const char *role = prefix_messages[pi].role;
    if (strcmp(role, "assistant") != 0) role = "user";
    neo_buf_append(&body, ",{\"role\":\"", 0);
    neo_buf_append(&body, role, 0);
    neo_buf_append(&body, "\",\"content\":\"", 0);
    neo_json_escape(prefix_messages[pi].content, &body);
    neo_buf_append(&body, "\"}", 0);
  }

  neo_buf_append(&body, ",{\"role\":\"user\",\"content\":\"", 0);
  neo_json_escape(user_message, &body);
  neo_buf_append(&body, "\"}", 0);

  int maxr = conf->tools.max_rounds > 0 ? conf->tools.max_rounds : 16;
  int round;

  for (round = 0; round < maxr; round++) {
    neo_buf_append(&body, hist.s ? hist.s : "", hist.len);
    neo_buf_append(&body, "],\"tools\":", 0);
    if (neo_build_tools_json(&body, conf) != 0) {
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }
    neo_buf_append(&body, ",\"tool_choice\":\"auto\",\"max_tokens\":", 0);
    char nbuf[32];
    snprintf(nbuf, sizeof(nbuf), "%d", conf->model.max_tokens > 0 ? conf->model.max_tokens : 4096);
    neo_buf_append(&body, nbuf, 0);
    neo_buf_append(&body, ",\"temperature\":", 0);
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%.2f", conf->model.temperature);
    neo_buf_append(&body, tbuf, 0);
    neo_buf_append(&body, "}", 0);

    llm_response_t raw = {0};
    if (llm_post_chat_completions_json(conf->model.base_url, conf->model.api_key, body.s, &raw) != 0) {
      neo_buf_free(&body);
      neo_buf_free(&hist);
      return -1;
    }

    {
      yyjson_doc *doc;
      yyjson_val *root, *msg, *tc;
      int has_tools;

      doc = yyjson_read(raw.data, raw.size, 0);
      if (!doc) {
        fprintf(stderr, "neo: tool round JSON parse error\n");
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        return -1;
      }
      root = yyjson_doc_get_root(doc);
      msg = choice0_message(root);
      if (!msg) {
        fprintf(stderr, "neo: no message in response\n");
        yyjson_doc_free(doc);
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        return -1;
      }

      tc = yyjson_obj_get(msg, "tool_calls");
      has_tools = yyjson_is_arr(tc) && yyjson_arr_size(tc) > 0;

      if (!has_tools) {
        llm_response_free(out_text);
        out_text->data = NULL;
        out_text->size = 0;
        if (msg_content_to_llm(msg, out_text) != 0) {
          if (llm_extract_content_json(raw.data, out_text) != 0) {
            yyjson_doc_free(doc);
            llm_response_free(&raw);
            neo_buf_free(&body);
            neo_buf_free(&hist);
            return -1;
          }
        }
        yyjson_doc_free(doc);
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        return 0;
      }

      neo_buf_append(&hist, ",", 0);
      if (append_message_json(msg, &hist) != 0) {
        yyjson_doc_free(doc);
        llm_response_free(&raw);
        neo_buf_free(&body);
        neo_buf_free(&hist);
        llm_response_free(out_text);
        out_text->data = NULL;
        out_text->size = 0;
        return -1;
      }

      {
        NeoToolCall calls[NEO_MAX_TOOLS_PER_TURN];
        int ncalls = 0;
        NeoBuf tres = {0};
        int ci;
        memset(calls, 0, sizeof(calls));
        parse_tool_calls_yy(msg, calls, &ncalls);
        yyjson_doc_free(doc);
        llm_response_free(&raw);

        if (ncalls == 0) {
          neo_buf_free(&body);
          neo_buf_free(&hist);
          return -1;
        }

        for (ci = 0; ci < ncalls; ci++) {
          if (run_one_tool(conf, root_real, &calls[ci], &tres) != 0) {
            neo_tool_calls_free(calls, ncalls);
            neo_buf_free(&tres);
            neo_buf_free(&body);
            neo_buf_free(&hist);
            return -1;
          }
          neo_buf_append(&hist, ",{\"role\":\"tool\",\"tool_call_id\":\"", 0);
          neo_json_escape(calls[ci].id, &hist);
          neo_buf_append(&hist, "\",\"content\":\"", 0);
          neo_json_escape(tres.s ? tres.s : "", &hist);
          neo_buf_append(&hist, "\"}", 0);
          tres.len = 0;
          if (tres.s) tres.s[0] = '\0';
        }
        neo_tool_calls_free(calls, ncalls);
        neo_buf_free(&tres);
      }
    }

    /* Rebuild body prefix for next POST */
    body.len = 0;
    if (body.s) body.s[0] = '\0';
    neo_buf_append(&body, "{\"model\":\"", 0);
    neo_json_escape(conf->model.name ? conf->model.name : "gpt", &body);
    neo_buf_append(&body, "\",\"messages\":[{\"role\":\"system\",\"content\":\"", 0);
    neo_json_escape(system_prompt, &body);
    neo_buf_append(&body, "\"}", 0);
    for (pi = 0; pi < n_prefix && prefix_messages && prefix_messages[pi].role && prefix_messages[pi].content; pi++) {
      const char *role = prefix_messages[pi].role;
      if (strcmp(role, "assistant") != 0) role = "user";
      neo_buf_append(&body, ",{\"role\":\"", 0);
      neo_buf_append(&body, role, 0);
      neo_buf_append(&body, "\",\"content\":\"", 0);
      neo_json_escape(prefix_messages[pi].content, &body);
      neo_buf_append(&body, "\"}", 0);
    }
    neo_buf_append(&body, ",{\"role\":\"user\",\"content\":\"", 0);
    neo_json_escape(user_message, &body);
    neo_buf_append(&body, "\"}", 0);
    /* hist carries assistant+tool messages for next iteration */
  }

  fprintf(stderr, "neo: tool max_rounds exceeded\n");
  neo_buf_free(&body);
  neo_buf_free(&hist);
  return -1;
#endif
}
