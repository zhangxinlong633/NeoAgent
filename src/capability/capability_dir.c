/*
 * 能力目录实现：manifest + load[] 扫描、propose 落盘（不热加载）。
 */
#include "capability_dir.h"
#include "yyjson.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CAP_DIR_MAX_COMMANDS 128
#define CAP_DIR_MAX_ARGV 16
#define CAP_DIR_MAX_ENABLED 128

static char *dup_s(const char *s) {
  size_t n;
  char *o;
  if (!s) return NULL;
  n = strlen(s);
  o = malloc(n + 1);
  if (!o) return NULL;
  memcpy(o, s, n + 1);
  return o;
}

/* 保留名：禁止与 C builtin / propose_capability 冲突。 */
static int name_reserved(const char *name) {
  static const char *r[] = {"read_file", "write_file", "append_file", "list_dir", "http_get",
                            "grep",      "run_command", "stat",        "mkdir",    "propose_capability",
                            "memory_add", NULL};
  int i;
  for (i = 0; r[i]; i++)
    if (name && strcmp(name, r[i]) == 0) return 1;
  return 0;
}

static int name_ok(const char *name) {
  size_t i, n;
  if (!name || !name[0]) return 0;
  n = strlen(name);
  if (n > 64) return 0;
  if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) return 0;
  for (i = 1; i < n; i++) {
    char c = name[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
      return 0;
  }
  return !name_reserved(name);
}

static int path_safe_rel(const char *rel) {
  if (!rel || !rel[0] || rel[0] == '/') return 0;
  if (strstr(rel, "..")) return 0;
  return 1;
}

/* argv[0]：相对 root（无 ..）或绝对路径（无 .. 段）。 */
static int path_safe_argv0(const char *p) {
  const char *s;
  if (!p || !p[0]) return 0;
  if (p[0] == '/') {
    s = p;
    while (*s) {
      if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == '\0')) return 0;
      while (*s && *s != '/') s++;
      if (*s == '/') s++;
    }
    return 1;
  }
  return path_safe_rel(p);
}

static int ends_with(const char *s, const char *suf) {
  size_t ns, nf;
  if (!s || !suf) return 0;
  ns = strlen(s);
  nf = strlen(suf);
  return ns >= nf && strcmp(s + ns - nf, suf) == 0;
}

static int mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  size_t len;
  char *p;
  if (!path || !path[0]) return -1;
  len = strlen(path);
  if (len >= sizeof(tmp)) return -1;
  memcpy(tmp, path, len + 1);
  for (p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

static void free_cmd(tool_command_t *cmd) {
  int i;
  if (!cmd) return;
  free(cmd->name);
  free(cmd->description);
  free(cmd->when);
  free(cmd->when_not);
  free(cmd->tags);
  free(cmd->outcome);
  free(cmd->parameters_json);
  if (cmd->argv) {
    for (i = 0; i < cmd->argv_count; i++) free(cmd->argv[i]);
    free(cmd->argv);
  }
  memset(cmd, 0, sizeof(*cmd));
}

static int parse_string_array(yyjson_val *arr, char ***out, int *out_n, int max_n) {
  size_t i, n;
  char **a;
  if (!yyjson_is_arr(arr)) return -1;
  n = yyjson_arr_size(arr);
  if ((int)n > max_n) return -1;
  a = calloc(n ? n : 1, sizeof(char *));
  if (!a) return -1;
  for (i = 0; i < n; i++) {
    yyjson_val *el = yyjson_arr_get(arr, i);
    if (!yyjson_is_str(el)) {
      size_t j;
      for (j = 0; j < i; j++) free(a[j]);
      free(a);
      return -1;
    }
    a[i] = dup_s(yyjson_get_str(el));
    if (!a[i]) {
      size_t j;
      for (j = 0; j < i; j++) free(a[j]);
      free(a);
      return -1;
    }
  }
  *out = a;
  *out_n = (int)n;
  return 0;
}

static int parse_command_obj(yyjson_val *el, tool_command_t *cmd, const char *src_path) {
  yyjson_val *v, *argv, *pa, *params;
  memset(cmd, 0, sizeof(*cmd));
  cmd->name = yyjson_is_str(yyjson_obj_get(el, "name")) ? dup_s(yyjson_get_str(yyjson_obj_get(el, "name")))
                                                         : NULL;
  cmd->description =
      yyjson_is_str(yyjson_obj_get(el, "description"))
          ? dup_s(yyjson_get_str(yyjson_obj_get(el, "description")))
          : NULL;
  {
    yyjson_val *mv;
    mv = yyjson_obj_get(el, "when");
    if (yyjson_is_str(mv))
      cmd->when = dup_s(yyjson_get_str(mv));
    else if (yyjson_is_arr(mv) || yyjson_is_obj(mv))
      cmd->when = yyjson_val_write(mv, 0, NULL);
    mv = yyjson_obj_get(el, "when_not");
    if (yyjson_is_str(mv))
      cmd->when_not = dup_s(yyjson_get_str(mv));
    else if (yyjson_is_arr(mv) || yyjson_is_obj(mv))
      cmd->when_not = yyjson_val_write(mv, 0, NULL);
    mv = yyjson_obj_get(el, "tags");
    if (yyjson_is_str(mv))
      cmd->tags = dup_s(yyjson_get_str(mv));
    else if (yyjson_is_arr(mv) || yyjson_is_obj(mv))
      cmd->tags = yyjson_val_write(mv, 0, NULL);
    mv = yyjson_obj_get(el, "outcome");
    if (yyjson_is_str(mv))
      cmd->outcome = dup_s(yyjson_get_str(mv));
    else if (yyjson_is_arr(mv) || yyjson_is_obj(mv))
      cmd->outcome = yyjson_val_write(mv, 0, NULL);
  }
  v = yyjson_obj_get(el, "timeout_sec");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) cmd->timeout_sec = (int)yyjson_get_sint(v);
  v = yyjson_obj_get(el, "max_output_bytes");
  if (yyjson_is_int(v) || yyjson_is_uint(v)) cmd->max_output_bytes = (int)yyjson_get_sint(v);
  pa = yyjson_obj_get(el, "pass_args");
  cmd->pass_args = 0;
  if (yyjson_is_str(pa) && yyjson_get_str(pa) && !strcmp(yyjson_get_str(pa), "env")) cmd->pass_args = 1;
  argv = yyjson_obj_get(el, "argv");
  if (argv && parse_string_array(argv, &cmd->argv, &cmd->argv_count, CAP_DIR_MAX_ARGV) != 0) {
    fprintf(stderr, "neo: capability dir %s: bad argv\n", src_path);
    free_cmd(cmd);
    return -1;
  }
  params = yyjson_obj_get(el, "parameters");
  if (params) {
    if (!yyjson_is_obj(params)) {
      fprintf(stderr, "neo: capability dir %s: parameters must be object\n", src_path);
      free_cmd(cmd);
      return -1;
    }
    cmd->parameters_json = yyjson_val_write(params, 0, NULL);
    if (!cmd->parameters_json) {
      free_cmd(cmd);
      return -1;
    }
  }
  if (!cmd->name || !cmd->name[0] || !cmd->argv || cmd->argv_count < 1) {
    fprintf(stderr, "neo: capability dir %s: name and argv required\n", src_path);
    free_cmd(cmd);
    return -1;
  }
  if (name_reserved(cmd->name)) {
    fprintf(stderr, "neo: capability dir %s: name '%s' is reserved\n", src_path, cmd->name);
    free_cmd(cmd);
    return -1;
  }
  if (cmd->timeout_sec <= 0) cmd->timeout_sec = 30;
  if (cmd->max_output_bytes <= 0) cmd->max_output_bytes = 65536;
  return 0;
}

static int append_command(agent_config_t *conf, tool_command_t *cmd) {
  tool_command_t *np;
  int i;
  if (conf->tools.command_count >= CAP_DIR_MAX_COMMANDS) {
    fprintf(stderr, "neo: capability_matrix: too many commands (incl. directory)\n");
    return -1;
  }
  for (i = 0; i < conf->tools.command_count; i++) {
    if (conf->tools.commands[i].name && cmd->name &&
        strcmp(conf->tools.commands[i].name, cmd->name) == 0) {
      fprintf(stderr, "neo: capability_matrix: duplicate command name '%s' (from directory)\n",
              cmd->name);
      return -1;
    }
  }
  np = realloc(conf->tools.commands, (conf->tools.command_count + 1) * sizeof(tool_command_t));
  if (!np) return -1;
  conf->tools.commands = np;
  conf->tools.commands[conf->tools.command_count] = *cmd;
  memset(cmd, 0, sizeof(*cmd)); /* ownership transferred */
  conf->tools.command_count++;
  return 0;
}

static int load_one_file(agent_config_t *conf, const char *path) {
  yyjson_doc *doc;
  yyjson_val *root;
  yyjson_read_err err;
  tool_command_t cmd;
  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: capability dir: cannot parse %s (%s)\n", path, err.msg ? err.msg : "?");
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    fprintf(stderr, "neo: capability dir %s: expected object\n", path);
    yyjson_doc_free(doc);
    return -1;
  }
  if (parse_command_obj(root, &cmd, path) != 0) {
    yyjson_doc_free(doc);
    return -1;
  }
  yyjson_doc_free(doc);
  if (append_command(conf, &cmd) != 0) {
    free_cmd(&cmd);
    return -1;
  }
  return 0;
}

/* 读取子目录 enabled.json5：若存在则仅装载 load[] 中列出的能力名（与文件名茎一致）。 */
static int read_enabled_names(const char *dir_abs, char ***names, int *n_out) {
  char path[PATH_MAX];
  yyjson_doc *doc;
  yyjson_val *root, *load;
  yyjson_read_err err;
  size_t i, n;
  char **a;

  *names = NULL;
  *n_out = 0;
  if (snprintf(path, sizeof(path), "%s/enabled.json5", dir_abs) >= (int)sizeof(path)) return -1;
  if (access(path, R_OK) != 0) return 0; /* 无白名单文件 → 全量加载 */

  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: capability dir: bad enabled.json5 in %s\n", dir_abs);
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    fprintf(stderr, "neo: capability dir: enabled.json5 must be object\n");
    return -1;
  }
  load = yyjson_obj_get(root, "load");
  if (!yyjson_is_arr(load)) {
    yyjson_doc_free(doc);
    fprintf(stderr, "neo: capability dir: enabled.json5.load must be array\n");
    return -1;
  }
  n = yyjson_arr_size(load);
  if ((int)n > CAP_DIR_MAX_ENABLED) {
    yyjson_doc_free(doc);
    fprintf(stderr, "neo: capability dir: enabled.json5.load too long\n");
    return -1;
  }
  a = calloc(n ? n : 1, sizeof(char *));
  if (!a) {
    yyjson_doc_free(doc);
    return -1;
  }
  for (i = 0; i < n; i++) {
    yyjson_val *el = yyjson_arr_get(load, i);
    if (!yyjson_is_str(el) || !yyjson_get_str(el) || !yyjson_get_str(el)[0]) {
      size_t j;
      for (j = 0; j < i; j++) free(a[j]);
      free(a);
      yyjson_doc_free(doc);
      fprintf(stderr, "neo: capability dir: bad enabled.json5.load entry\n");
      return -1;
    }
    a[i] = dup_s(yyjson_get_str(el));
    if (!a[i]) {
      size_t j;
      for (j = 0; j < i; j++) free(a[j]);
      free(a);
      yyjson_doc_free(doc);
      return -1;
    }
  }
  yyjson_doc_free(doc);
  *names = a;
  *n_out = (int)n;
  return 0;
}

static int name_in_enabled(char **names, int n, const char *stem) {
  int i;
  if (!names || n <= 0) return 1; /* 无白名单则放行 */
  for (i = 0; i < n; i++) {
    if (names[i] && strcmp(names[i], stem) == 0) return 1;
  }
  return 0;
}

static void free_names(char **names, int n) {
  int i;
  if (!names) return;
  for (i = 0; i < n; i++) free(names[i]);
  free(names);
}

/* 从路径取文件名茎：foo/bar/unix_wc.json5 → unix_wc */
static int file_stem(const char *path, char *out, size_t out_sz) {
  const char *base, *dot;
  size_t n;
  if (!path || !out || out_sz < 2) return -1;
  base = strrchr(path, '/');
  base = base ? base + 1 : path;
  dot = strrchr(base, '.');
  if (!dot || dot == base) return -1;
  n = (size_t)(dot - base);
  if (n + 1 > out_sz) return -1;
  memcpy(out, base, n);
  out[n] = '\0';
  return 0;
}

static int load_subdir(agent_config_t *conf, const char *dir_abs) {
  DIR *d;
  struct dirent *de;
  char **enabled = NULL;
  int enabled_n = 0;

  /* 若存在 enabled.json5，则只装载其中列出的能力定义（其余 json5 保留作目录内参考，不进矩阵）。 */
  if (read_enabled_names(dir_abs, &enabled, &enabled_n) != 0) return -1;

  d = opendir(dir_abs);
  if (!d) {
    free_names(enabled, enabled_n);
    fprintf(stderr, "neo: capability dir: cannot open %s: %s\n", dir_abs, strerror(errno));
    return -1;
  }
  while ((de = readdir(d)) != NULL) {
    char path[PATH_MAX];
    char stem[128];
    struct stat st;
    if (de->d_name[0] == '.') continue;
    if (strcmp(de->d_name, "enabled.json5") == 0 || strcmp(de->d_name, "enabled.json") == 0) continue;
    if (!ends_with(de->d_name, ".json5") && !ends_with(de->d_name, ".json")) continue;
    if (snprintf(path, sizeof(path), "%s/%s", dir_abs, de->d_name) >= (int)sizeof(path)) {
      closedir(d);
      free_names(enabled, enabled_n);
      return -1;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if (file_stem(path, stem, sizeof(stem)) != 0) continue;
    if (!name_in_enabled(enabled, enabled_n, stem)) continue;
    if (load_one_file(conf, path) != 0) {
      closedir(d);
      free_names(enabled, enabled_n);
      return -1;
    }
  }
  closedir(d);
  free_names(enabled, enabled_n);
  return 0;
}

static int read_manifest(const char *dir_abs, char ***load_dirs, int *load_n, char **proposed) {
  char path[PATH_MAX];
  yyjson_doc *doc;
  yyjson_val *root, *load, *prop;
  yyjson_read_err err;
  size_t i, n;

  *load_dirs = NULL;
  *load_n = 0;
  *proposed = dup_s("proposed");
  if (!*proposed) return -1;

  if (snprintf(path, sizeof(path), "%s/manifest.json5", dir_abs) >= (int)sizeof(path)) return -1;
  if (access(path, R_OK) != 0) {
    /* default: load commands/ */
    char **a = calloc(1, sizeof(char *));
    if (!a) return -1;
    a[0] = dup_s("commands");
    if (!a[0]) {
      free(a);
      return -1;
    }
    *load_dirs = a;
    *load_n = 1;
    return 0;
  }
  doc = yyjson_read_file(path, YYJSON_READ_JSON5, NULL, &err);
  if (!doc) {
    fprintf(stderr, "neo: capability dir: bad manifest %s\n", path);
    return -1;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  prop = yyjson_obj_get(root, "proposed");
  if (yyjson_is_str(prop) && yyjson_get_str(prop) && yyjson_get_str(prop)[0]) {
    free(*proposed);
    *proposed = dup_s(yyjson_get_str(prop));
    if (!*proposed) {
      yyjson_doc_free(doc);
      return -1;
    }
  }
  load = yyjson_obj_get(root, "load");
  if (!load) {
    char **a = calloc(1, sizeof(char *));
    if (!a) {
      yyjson_doc_free(doc);
      return -1;
    }
    a[0] = dup_s("commands");
    *load_dirs = a;
    *load_n = 1;
    yyjson_doc_free(doc);
    return 0;
  }
  if (!yyjson_is_arr(load)) {
    fprintf(stderr, "neo: capability dir: manifest.load must be array\n");
    yyjson_doc_free(doc);
    return -1;
  }
  n = yyjson_arr_size(load);
  {
    char **a = calloc(n ? n : 1, sizeof(char *));
    if (!a) {
      yyjson_doc_free(doc);
      return -1;
    }
    for (i = 0; i < n; i++) {
      yyjson_val *el = yyjson_arr_get(load, i);
      if (!yyjson_is_str(el) || !path_safe_rel(yyjson_get_str(el))) {
        size_t j;
        for (j = 0; j < i; j++) free(a[j]);
        free(a);
        yyjson_doc_free(doc);
        fprintf(stderr, "neo: capability dir: bad manifest.load entry\n");
        return -1;
      }
      a[i] = dup_s(yyjson_get_str(el));
      if (!a[i]) {
        size_t j;
        for (j = 0; j < i; j++) free(a[j]);
        free(a);
        yyjson_doc_free(doc);
        return -1;
      }
    }
    *load_dirs = a;
    *load_n = (int)n;
  }
  yyjson_doc_free(doc);
  return 0;
}

int capability_dir_load_into_config(agent_config_t *conf) {
  /* 将 directory/commands 等 load 子目录中的能力文件追加到 conf->tools.commands。
   * 故意不扫描 proposed/，与 propose_capability「当轮不生效」一致。 */
  char dir_abs[PATH_MAX];
  char **load_dirs = NULL;
  int load_n = 0, i;
  char *proposed = NULL;
  const char *dir;

  if (!conf || !conf->tools.directory || !conf->tools.directory[0]) return 0;
  dir = conf->tools.directory;
  if (!path_safe_rel(dir) && strcmp(dir, ".") != 0) {
    /* allow relative paths only (including nested like caps/v1) */
    if (dir[0] == '/') {
      fprintf(stderr, "neo: capability_matrix.directory must be relative\n");
      return -1;
    }
    if (strstr(dir, "..")) {
      fprintf(stderr, "neo: capability_matrix.directory must not contain ..\n");
      return -1;
    }
  }
  if (!realpath(dir, dir_abs)) {
    fprintf(stderr, "neo: capability_matrix.directory realpath failed for %s: %s\n", dir,
            strerror(errno));
    return -1;
  }
  if (read_manifest(dir_abs, &load_dirs, &load_n, &proposed) != 0) {
    free(proposed);
    return -1;
  }
  free(proposed); /* only needed for propose path; load skips it */
  for (i = 0; i < load_n; i++) {
    char sub[PATH_MAX];
    if (!load_dirs[i] || !path_safe_rel(load_dirs[i])) {
      fprintf(stderr, "neo: capability dir: refusing load path\n");
      for (i = 0; i < load_n; i++) free(load_dirs[i]);
      free(load_dirs);
      return -1;
    }
    if (snprintf(sub, sizeof(sub), "%s/%s", dir_abs, load_dirs[i]) >= (int)sizeof(sub)) {
      for (i = 0; i < load_n; i++) free(load_dirs[i]);
      free(load_dirs);
      return -1;
    }
    if (load_subdir(conf, sub) != 0) {
      for (i = 0; i < load_n; i++) free(load_dirs[i]);
      free(load_dirs);
      return -1;
    }
  }
  for (i = 0; i < load_n; i++) free(load_dirs[i]);
  free(load_dirs);
  return 0;
}

char *capability_dir_proposed_subdir(const agent_config_t *conf) {
  char dir_abs[PATH_MAX];
  char **load_dirs = NULL;
  int load_n = 0;
  char *proposed = NULL;
  if (!conf || !conf->tools.directory || !conf->tools.directory[0]) return NULL;
  if (!realpath(conf->tools.directory, dir_abs)) return NULL;
  if (read_manifest(dir_abs, &load_dirs, &load_n, &proposed) != 0) {
    int i;
    for (i = 0; i < load_n; i++) free(load_dirs[i]);
    free(load_dirs);
    return NULL;
  }
  {
    int i;
    for (i = 0; i < load_n; i++) free(load_dirs[i]);
    free(load_dirs);
  }
  return proposed;
}

int capability_dir_propose(const agent_config_t *conf, const char *args_json, char **out_text) {
  /* 方案 A：只写 proposed/<name>.json5，不 append 进当前矩阵；提示挪到 load[] 场景子目录后重启。 */
  yyjson_doc *doc;
  yyjson_val *root, *argv, *params, *v;
  const char *name, *desc;
  char *proposed_sub = NULL;
  char dir_abs[PATH_MAX];
  char out_dir[PATH_MAX];
  char out_path[PATH_MAX];
  char rel_out[PATH_MAX];
  yyjson_mut_doc *mdoc;
  yyjson_mut_val *mroot, *marr;
  size_t i, n;
  FILE *f;
  char *json_text = NULL;
  char msg[PATH_MAX + 256];

  if (out_text) *out_text = NULL;
  if (!conf || !out_text) return -1;
  if (!conf->tools.directory || !conf->tools.directory[0]) {
    *out_text = dup_s("ERROR: propose_capability requires capability_matrix.directory");
    return 0;
  }
  if (!realpath(conf->tools.directory, dir_abs)) {
    *out_text = dup_s("ERROR: capability directory not found");
    return 0;
  }
  proposed_sub = capability_dir_proposed_subdir(conf);
  if (!proposed_sub || !path_safe_rel(proposed_sub)) {
    free(proposed_sub);
    *out_text = dup_s("ERROR: bad proposed subdir in manifest");
    return 0;
  }

  doc = yyjson_read(args_json ? args_json : "{}", strlen(args_json ? args_json : "{}"), 0);
  if (!doc) {
    free(proposed_sub);
    *out_text = dup_s("ERROR: bad args JSON");
    return 0;
  }
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: args must be object");
    return 0;
  }
  v = yyjson_obj_get(root, "name");
  name = yyjson_is_str(v) ? yyjson_get_str(v) : NULL;
  v = yyjson_obj_get(root, "description");
  desc = yyjson_is_str(v) ? yyjson_get_str(v) : NULL;
  argv = yyjson_obj_get(root, "argv");
  if (!name_ok(name)) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: invalid or reserved name");
    return 0;
  }
  if (!desc || !desc[0]) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: missing description");
    return 0;
  }
  if (!yyjson_is_arr(argv) || yyjson_arr_size(argv) < 1) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: argv must be non-empty array");
    return 0;
  }
  n = yyjson_arr_size(argv);
  if ((int)n > CAP_DIR_MAX_ARGV) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: too many argv entries");
    return 0;
  }
  for (i = 0; i < n; i++) {
    yyjson_val *el = yyjson_arr_get(argv, i);
    const char *s = yyjson_is_str(el) ? yyjson_get_str(el) : NULL;
    if (!s || (i == 0 && !path_safe_argv0(s))) {
      yyjson_doc_free(doc);
      free(proposed_sub);
      *out_text = dup_s("ERROR: argv[0] must be relative under root or absolute path without ..");
      return 0;
    }
    if (!yyjson_is_str(el)) {
      yyjson_doc_free(doc);
      free(proposed_sub);
      *out_text = dup_s("ERROR: argv entries must be strings");
      return 0;
    }
  }

  if (snprintf(out_dir, sizeof(out_dir), "%s/%s", dir_abs, proposed_sub) >= (int)sizeof(out_dir)) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: path too long");
    return 0;
  }
  if (mkdir_p(out_dir) != 0) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: cannot create proposed directory");
    return 0;
  }
  if (snprintf(out_path, sizeof(out_path), "%s/%s.json5", out_dir, name) >= (int)sizeof(out_path)) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: path too long");
    return 0;
  }
  if (access(out_path, F_OK) == 0) {
    yyjson_doc_free(doc);
    free(proposed_sub);
    *out_text = dup_s("ERROR: proposed file already exists");
    return 0;
  }

  mdoc = yyjson_mut_doc_new(NULL);
  mroot = yyjson_mut_obj(mdoc);
  yyjson_mut_doc_set_root(mdoc, mroot);
  yyjson_mut_obj_add_str(mdoc, mroot, "name", name);
  yyjson_mut_obj_add_str(mdoc, mroot, "description", desc);
  {
    const char *meta_keys[] = {"when", "when_not", "tags", "outcome"};
    size_t mi;
    for (mi = 0; mi < sizeof(meta_keys) / sizeof(meta_keys[0]); mi++) {
      yyjson_val *mv = yyjson_obj_get(root, meta_keys[mi]);
      if (yyjson_is_str(mv))
        yyjson_mut_obj_add_str(mdoc, mroot, meta_keys[mi], yyjson_get_str(mv));
      else if (yyjson_is_arr(mv) || yyjson_is_obj(mv)) {
        yyjson_mut_val *mm = yyjson_val_mut_copy(mdoc, mv);
        if (mm) yyjson_mut_obj_add_val(mdoc, mroot, meta_keys[mi], mm);
      }
    }
  }
  marr = yyjson_mut_arr(mdoc);
  for (i = 0; i < n; i++)
    yyjson_mut_arr_add_str(mdoc, marr, yyjson_get_str(yyjson_arr_get(argv, i)));
  yyjson_mut_obj_add_val(mdoc, mroot, "argv", marr);
  v = yyjson_obj_get(root, "timeout_sec");
  if (yyjson_is_int(v) || yyjson_is_uint(v))
    yyjson_mut_obj_add_int(mdoc, mroot, "timeout_sec", yyjson_get_sint(v));
  else
    yyjson_mut_obj_add_int(mdoc, mroot, "timeout_sec", 30);
  v = yyjson_obj_get(root, "pass_args");
  if (yyjson_is_str(v))
    yyjson_mut_obj_add_str(mdoc, mroot, "pass_args", yyjson_get_str(v));
  else
    yyjson_mut_obj_add_str(mdoc, mroot, "pass_args", "stdin_json");
  params = yyjson_obj_get(root, "parameters");
  if (yyjson_is_obj(params)) {
    yyjson_mut_val *mp = yyjson_val_mut_copy(mdoc, params);
    if (mp) yyjson_mut_obj_add_val(mdoc, mroot, "parameters", mp);
  }

  json_text = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, NULL);
  yyjson_mut_doc_free(mdoc);
  yyjson_doc_free(doc);
  if (!json_text) {
    free(proposed_sub);
    *out_text = dup_s("ERROR: serialize failed");
    return 0;
  }

  f = fopen(out_path, "wb");
  if (!f) {
    free(json_text);
    free(proposed_sub);
    *out_text = dup_s("ERROR: write failed");
    return 0;
  }
  fwrite(json_text, 1, strlen(json_text), f);
  fputc('\n', f);
  fclose(f);
  free(json_text);

  snprintf(rel_out, sizeof(rel_out), "%s/%s/%s.json5", conf->tools.directory, proposed_sub, name);
  snprintf(msg, sizeof(msg),
           "proposed: %s\nNot loaded this session. Move into a load[] scenario subdir "
           "(e.g. local/) and restart neo to enable.",
           rel_out);
  free(proposed_sub);
  *out_text = dup_s(msg);
  return 0;
}
