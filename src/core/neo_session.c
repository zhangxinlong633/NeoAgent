/* neo_session.c — 具名会话 JSON 落盘与裁剪（与 daemon 内存历史同语义，跨进程）。
 * 多 ID：parse/load_many 只负责拼接上下文；append 仍写单个 id（CLI 写第一个）。 */
#include "neo_session.h"

#include "yyjson.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define neo_mkdir(path) _mkdir(path)
#define neo_unlink(path) _unlink(path)
#else
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#define neo_mkdir(path) mkdir((path), 0755)
#define neo_unlink(path) unlink(path)
#endif

int neo_session_id_ok(const char *id) {
  size_t i, n;
  if (!id || !id[0]) return 0;
  n = strlen(id);
  if (n > 64) return 0;
  for (i = 0; i < n; i++) {
    char c = id[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-')
      continue;
    return 0;
  }
  return 1;
}

/* 去掉首尾空白；原地修改。 */
static char *trim_inplace(char *s) {
  char *end;
  if (!s) return s;
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  if (!*s) return s;
  end = s + strlen(s) - 1;
  while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
    *end = '\0';
    end--;
  }
  return s;
}

void neo_session_free_ids(char **ids, int n) {
  int i;
  if (!ids) return;
  for (i = 0; i < n; i++) free(ids[i]);
  free(ids);
}

int neo_session_parse_ids(const char *spec, char ***out_ids, int *out_n) {
  char *buf = NULL;
  char **ids = NULL;
  int n = 0, i;
  char *p, *tok;

  if (!out_ids || !out_n) return -1;
  *out_ids = NULL;
  *out_n = 0;
  if (!spec || !spec[0]) return -1;

  buf = strdup(spec);
  if (!buf) return -1;
  ids = calloc(NEO_SESSION_MAX_IDS, sizeof(char *));
  if (!ids) {
    free(buf);
    return -1;
  }

  p = buf;
  while (p) {
    char *id;
    tok = p;
    p = strchr(p, ',');
    if (p) {
      *p = '\0';
      p++;
    }
    id = trim_inplace(tok);
    if (!id[0]) {
      neo_session_free_ids(ids, n);
      free(buf);
      return -1;
    }
    if (!neo_session_id_ok(id)) {
      neo_session_free_ids(ids, n);
      free(buf);
      return -1;
    }
    for (i = 0; i < n; i++) {
      if (strcmp(ids[i], id) == 0) {
        neo_session_free_ids(ids, n);
        free(buf);
        return -1; /* 禁止重复，避免同一历史注入两次 */
      }
    }
    if (n >= NEO_SESSION_MAX_IDS) {
      neo_session_free_ids(ids, n);
      free(buf);
      return -1;
    }
    ids[n] = strdup(id);
    if (!ids[n]) {
      neo_session_free_ids(ids, n);
      free(buf);
      return -1;
    }
    n++;
  }
  free(buf);
  if (n == 0) {
    free(ids);
    return -1;
  }
  *out_ids = ids;
  *out_n = n;
  return 0;
}

int neo_session_path(const char *id, char *out, size_t out_sz) {
  int w;
  if (!neo_session_id_ok(id) || !out || out_sz < 32) return -1;
  w = snprintf(out, out_sz, ".neo/sessions/%s.json", id);
  if (w < 0 || (size_t)w >= out_sz) return -1;
  return 0;
}

static int ensure_session_dir(void) {
  if (neo_mkdir(".neo") != 0 && errno != EEXIST) return -1;
  if (neo_mkdir(".neo/sessions") != 0 && errno != EEXIST) return -1;
  return 0;
}

void neo_session_free(llm_message_t *msgs, int n) {
  int i;
  if (!msgs) return;
  for (i = 0; i < n; i++) {
    free((void *)msgs[i].role);
    free((void *)msgs[i].content);
  }
  free(msgs);
}

int neo_session_load(const char *id, llm_message_t **out_msgs, int *n) {
  char path[256];
  yyjson_doc *doc;
  yyjson_val *root, *arr, *el;
  size_t idx, max;
  llm_message_t *msgs = NULL;
  int count = 0;

  if (!out_msgs || !n) return -1;
  *out_msgs = NULL;
  *n = 0;
  if (neo_session_path(id, path, sizeof(path)) != 0) return -1;

  doc = yyjson_read_file(path, 0, NULL, NULL);
  if (!doc) return 0; /* 无文件 = 空会话 */

  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  arr = yyjson_obj_get(root, "messages");
  if (!arr || !yyjson_is_arr(arr)) {
    yyjson_doc_free(doc);
    return 0;
  }
  max = yyjson_arr_size(arr);
  if (max == 0) {
    yyjson_doc_free(doc);
    return 0;
  }
  msgs = calloc(max, sizeof(llm_message_t));
  if (!msgs) {
    yyjson_doc_free(doc);
    return -1;
  }
  yyjson_arr_foreach(arr, idx, max, el) {
    const char *role, *content;
    yyjson_val *vr, *vc;
    if (!yyjson_is_obj(el)) continue;
    vr = yyjson_obj_get(el, "role");
    vc = yyjson_obj_get(el, "content");
    role = yyjson_get_str(vr);
    content = yyjson_get_str(vc);
    if (!role || !content) continue;
    if (strcmp(role, "user") != 0 && strcmp(role, "assistant") != 0) continue;
    msgs[count].role = strdup(role);
    msgs[count].content = strdup(content);
    if (!msgs[count].role || !msgs[count].content) {
      free((void *)msgs[count].role);
      free((void *)msgs[count].content);
      neo_session_free(msgs, count);
      yyjson_doc_free(doc);
      return -1;
    }
    count++;
  }
  yyjson_doc_free(doc);
  *out_msgs = msgs;
  *n = count;
  return 0;
}

int neo_session_load_many(char *const *ids, int n_ids, llm_message_t **out_msgs, int *n) {
  llm_message_t *all = NULL;
  int total = 0, i, j;

  if (!out_msgs || !n || !ids || n_ids <= 0) return -1;
  *out_msgs = NULL;
  *n = 0;

  for (i = 0; i < n_ids; i++) {
    llm_message_t *part = NULL;
    int pn = 0;
    llm_message_t *grown;
    if (!ids[i] || !neo_session_id_ok(ids[i])) {
      neo_session_free(all, total);
      return -1;
    }
    if (neo_session_load(ids[i], &part, &pn) != 0) {
      neo_session_free(all, total);
      return -1;
    }
    if (pn == 0) continue;
    grown = realloc(all, (size_t)(total + pn) * sizeof(llm_message_t));
    if (!grown) {
      neo_session_free(part, pn);
      neo_session_free(all, total);
      return -1;
    }
    all = grown;
    for (j = 0; j < pn; j++) {
      all[total + j] = part[j];
      part[j].role = NULL;
      part[j].content = NULL;
    }
    free(part);
    total += pn;
  }
  *out_msgs = all;
  *n = total;
  return 0;
}

static int save_messages(const char *id, const llm_message_t *msgs, int n) {
  char path[256];
  yyjson_mut_doc *doc;
  yyjson_mut_val *root, *arr;
  char *json;
  FILE *f;
  size_t i;
  int ok = -1;

  if (neo_session_path(id, path, sizeof(path)) != 0) return -1;
  if (ensure_session_dir() != 0) return -1;

  doc = yyjson_mut_doc_new(NULL);
  if (!doc) return -1;
  root = yyjson_mut_obj(doc);
  arr = yyjson_mut_arr(doc);
  if (!root || !arr) {
    yyjson_mut_doc_free(doc);
    return -1;
  }
  yyjson_mut_doc_set_root(doc, root);
  yyjson_mut_obj_add_int(doc, root, "version", 1);
  yyjson_mut_obj_add_strcpy(doc, root, "id", id);
  for (i = 0; i < (size_t)n; i++) {
    yyjson_mut_val *o;
    if (!msgs[i].role || !msgs[i].content) continue;
    o = yyjson_mut_obj(doc);
    if (!o) break;
    yyjson_mut_obj_add_strcpy(doc, o, "role", msgs[i].role);
    yyjson_mut_obj_add_strcpy(doc, o, "content", msgs[i].content);
    yyjson_mut_arr_add_val(arr, o);
  }
  yyjson_mut_obj_add_val(doc, root, "messages", arr);
  json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
  yyjson_mut_doc_free(doc);
  if (!json) return -1;

  f = fopen(path, "w");
  if (f) {
    if (fputs(json, f) >= 0) ok = 0;
    fclose(f);
  }
  free(json);
  return ok;
}

int neo_session_append_turn(const char *id, const char *user, const char *assistant, int max_turns) {
  llm_message_t *msgs = NULL;
  llm_message_t *grown = NULL;
  int n = 0, max_msg, i;
  int rc = -1;

  if (!neo_session_id_ok(id) || !user || !assistant) return -1;
  if (max_turns <= 0) max_turns = 10;
  max_msg = max_turns * 2;

  if (neo_session_load(id, &msgs, &n) != 0) return -1;
  grown = realloc(msgs, (size_t)(n + 2) * sizeof(llm_message_t));
  if (!grown) {
    neo_session_free(msgs, n);
    return -1;
  }
  msgs = grown;
  msgs[n].role = strdup("user");
  msgs[n].content = strdup(user);
  msgs[n + 1].role = strdup("assistant");
  msgs[n + 1].content = strdup(assistant);
  if (!msgs[n].role || !msgs[n].content || !msgs[n + 1].role || !msgs[n + 1].content) {
    neo_session_free(msgs, n + 2);
    return -1;
  }
  n += 2;

  while (n > max_msg) {
    free((void *)msgs[0].role);
    free((void *)msgs[0].content);
    memmove(&msgs[0], &msgs[1], (size_t)(n - 1) * sizeof(llm_message_t));
    n--;
  }

  rc = save_messages(id, msgs, n);
  neo_session_free(msgs, n);
  (void)i;
  return rc;
}

int neo_session_clear(const char *id) {
  char path[256];
  if (neo_session_path(id, path, sizeof(path)) != 0) return -1;
  if (neo_unlink(path) != 0 && errno != ENOENT) return -1;
  return 0;
}

/*
 * 归档 default：rename 文件，避免复制丢数据；目标 id 冲突则加 -1/-2…
 * 无 default 不算失败（新对话场景）。
 */
int neo_session_archive_default(char *out_id, size_t out_sz) {
  char from[256], to[256];
  char base[32], try_id[65];
  time_t now;
  struct tm *tm;
  struct stat st;
  int n;

  if (out_id && out_sz) out_id[0] = '\0';
  if (neo_session_path(NEO_SESSION_DEFAULT_ID, from, sizeof(from)) != 0) return -1;
  if (stat(from, &st) != 0) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  now = time(NULL);
  tm = localtime(&now);
  if (!tm || strftime(base, sizeof(base), "%Y%m%d-%H%M%S", tm) == 0) return -1;

  for (n = 0; n < 100; n++) {
    if (n == 0)
      snprintf(try_id, sizeof(try_id), "%s", base);
    else
      snprintf(try_id, sizeof(try_id), "%s-%d", base, n);
    if (!neo_session_id_ok(try_id)) return -1;
    if (neo_session_path(try_id, to, sizeof(to)) != 0) return -1;
    if (stat(to, &st) == 0) continue; /* 已存在，换下一个 */
    if (rename(from, to) != 0) return -1;
    if (out_id && out_sz) {
      snprintf(out_id, out_sz, "%s", try_id);
    }
    return 0;
  }
  return -1;
}

void neo_session_list_free(neo_session_info_t *list, int n) {
  (void)n;
  free(list);
}

#ifdef _WIN32
int neo_session_list(neo_session_info_t **out, int *n) {
  if (!out || !n) return -1;
  *out = NULL;
  *n = 0;
  return 0; /* Windows 列表未实现；多 ID 读写不依赖 list */
}
#else
static int cmp_session_info(const void *a, const void *b) {
  const neo_session_info_t *x = a, *y = b;
  return strcmp(x->id, y->id);
}

/* 从 *.json 文件名抽出 id；非法则返回 0。 */
static int id_from_session_filename(const char *name, char *id_out, size_t id_sz) {
  size_t len;
  if (!name || !id_out || id_sz < 2) return 0;
  len = strlen(name);
  if (len < 6 || strcmp(name + len - 5, ".json") != 0) return 0;
  len -= 5;
  if (len == 0 || len > 64 || len >= id_sz) return 0;
  memcpy(id_out, name, len);
  id_out[len] = '\0';
  return neo_session_id_ok(id_out);
}

int neo_session_list(neo_session_info_t **out, int *n) {
  DIR *d;
  struct dirent *ent;
  neo_session_info_t *list = NULL;
  int cap = 0, count = 0;

  if (!out || !n) return -1;
  *out = NULL;
  *n = 0;

  d = opendir(".neo/sessions");
  if (!d) {
    if (errno == ENOENT) return 0;
    return -1;
  }
  while ((ent = readdir(d)) != NULL) {
    char id[65];
    char path[256];
    struct stat st;
    yyjson_doc *doc;
    yyjson_val *root, *arr;
    neo_session_info_t *grown;
    int nmsg = 0;

    if (!id_from_session_filename(ent->d_name, id, sizeof(id))) continue;
    if (neo_session_path(id, path, sizeof(path)) != 0) continue;
    if (stat(path, &st) != 0) continue;

    doc = yyjson_read_file(path, 0, NULL, NULL);
    if (doc) {
      root = yyjson_doc_get_root(doc);
      if (yyjson_is_obj(root)) {
        arr = yyjson_obj_get(root, "messages");
        if (arr && yyjson_is_arr(arr)) nmsg = (int)yyjson_arr_size(arr);
      }
      yyjson_doc_free(doc);
    }

    if (count >= cap) {
      int ncap = cap ? cap * 2 : 8;
      grown = realloc(list, (size_t)ncap * sizeof(neo_session_info_t));
      if (!grown) {
        free(list);
        closedir(d);
        return -1;
      }
      list = grown;
      cap = ncap;
    }
    memset(&list[count], 0, sizeof(list[count]));
    memcpy(list[count].id, id, strlen(id) + 1);
    list[count].n_messages = nmsg;
    list[count].mtime = st.st_mtime;
    count++;
  }
  closedir(d);

  if (count > 1) qsort(list, (size_t)count, sizeof(neo_session_info_t), cmp_session_info);
  *out = list;
  *n = count;
  return 0;
}
#endif
