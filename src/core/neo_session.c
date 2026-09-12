/* neo_session.c — 具名会话 JSON 落盘与裁剪（与 daemon 内存历史同语义，跨进程）。 */
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
