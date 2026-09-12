/*
 * neo_memory：本地向量记忆门面。
 * vector 开：块只进 vdb + sidecar（不写 MEMORY.md）；recall 只查库。
 * vector 关：recall 回退为 memory.path 全文截断。
 * 本文件是仓库内唯一 include vdb.h 的业务代码。
 */
#include "neo_memory.h"
#include "neo_embed.h"
#include "vdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TXTS_MAGIC "NEOTXTS1"
#define TXTS_SUFFIX ".txts"
#define MAX_STORE_CHUNK 4096

struct neo_memory {
  const agent_config_t *conf;
  vdb_database *db;
  char **texts;
  int n_texts;
  int texts_cap;
};

static const char *store_path(const agent_config_t *conf) {
  if (conf && conf->memory.vector_store && conf->memory.vector_store[0])
    return conf->memory.vector_store;
  return ".neo/memory.vdb";
}

static size_t dims_of(const agent_config_t *conf) {
  int d = conf && conf->memory.vector_dims > 0 ? conf->memory.vector_dims : 64;
  if (d < 8) d = 8;
  if (d > 256) d = 256;
  return (size_t)d;
}

static void texts_path(const char *store, char *out, size_t out_sz) {
  snprintf(out, out_sz, "%s%s", store, TXTS_SUFFIX);
}

static char *read_entire_file(const char *path, size_t max_bytes) {
  FILE *f;
  char *buf;
  long sz;
  size_t n;
  if (!path || !path[0]) return NULL;
  f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  if ((size_t)sz > max_bytes) sz = (long)max_bytes;
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = '\0';
  return buf;
}

static size_t utf8_prefix(const char *s, size_t max) {
  size_t i, n;
  if (!s || max == 0) return 0;
  n = strlen(s);
  if (n <= max) return n;
  i = max;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}

static void ensure_parent_dir(const char *path) {
  char tmp[512];
  char *slash;
  size_t n;
  if (!path) return;
  n = strlen(path);
  if (n >= sizeof(tmp)) return;
  memcpy(tmp, path, n + 1);
  slash = strrchr(tmp, '/');
  if (!slash || slash == tmp) return;
  *slash = '\0';
#if !defined(_WIN32)
  mkdir(tmp, 0755);
#endif
}

static void neo_memory_clear_index(neo_memory_t *m) {
  int i;
  if (!m) return;
  if (m->db) {
    vdb_destroy(m->db);
    m->db = NULL;
  }
  for (i = 0; i < m->n_texts; i++) free(m->texts[i]);
  free(m->texts);
  m->texts = NULL;
  m->n_texts = 0;
  m->texts_cap = 0;
}

static int ensure_db(neo_memory_t *m) {
  size_t dims;
  if (!m) return -1;
  if (m->db) return 0;
  dims = dims_of(m->conf);
  m->db = vdb_create(dims, VDB_METRIC_COSINE);
  return m->db ? 0 : -1;
}

static int push_chunk(neo_memory_t *m, const char *text, size_t len) {
  float *vec;
  char idbuf[32];
  char *copy;
  size_t dims;
  int rc;
  if (!m || !text || len == 0) return 0;
  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' '))
    len--;
  while (len > 0 && (*text == '\n' || *text == '\r' || *text == ' ')) {
    text++;
    len--;
  }
  if (len == 0) return 0;
  if (ensure_db(m) != 0) return -1;
  dims = vdb_dimensions(m->db);
  if (m->n_texts >= m->texts_cap) {
    int nc = m->texts_cap ? m->texts_cap * 2 : 16;
    char **nt = realloc(m->texts, (size_t)nc * sizeof(char *));
    if (!nt) return -1;
    m->texts = nt;
    m->texts_cap = nc;
  }
  copy = malloc(len + 1);
  if (!copy) return -1;
  memcpy(copy, text, len);
  copy[len] = '\0';
  vec = calloc(dims, sizeof(float));
  if (!vec) {
    free(copy);
    return -1;
  }
  if (neo_embed_text(copy, dims, vec) != 0) {
    free(vec);
    free(copy);
    return -1;
  }
  snprintf(idbuf, sizeof(idbuf), "chunk-%d", m->n_texts);
  rc = (int)vdb_add_vector(m->db, vec, idbuf, copy);
  free(vec);
  if (rc != VDB_OK) {
    free(copy);
    return -1;
  }
  m->texts[m->n_texts++] = copy;
  return 0;
}

/* sidecar：magic + 重复 (u32le len + bytes)，与 vdb 向量一一对应。 */
static int save_texts(neo_memory_t *m, const char *path) {
  FILE *f;
  int i;
  if (!m || !path) return -1;
  ensure_parent_dir(path);
  f = fopen(path, "wb");
  if (!f) return -1;
  if (fwrite(TXTS_MAGIC, 1, 8, f) != 8) {
    fclose(f);
    return -1;
  }
  for (i = 0; i < m->n_texts; i++) {
    unsigned char hdr[4];
    size_t len = m->texts[i] ? strlen(m->texts[i]) : 0;
    hdr[0] = (unsigned char)(len & 0xff);
    hdr[1] = (unsigned char)((len >> 8) & 0xff);
    hdr[2] = (unsigned char)((len >> 16) & 0xff);
    hdr[3] = (unsigned char)((len >> 24) & 0xff);
    if (fwrite(hdr, 1, 4, f) != 4) {
      fclose(f);
      return -1;
    }
    if (len > 0 && fwrite(m->texts[i], 1, len, f) != len) {
      fclose(f);
      return -1;
    }
  }
  fclose(f);
  return 0;
}

static int load_texts(neo_memory_t *m, const char *path, size_t expect) {
  FILE *f;
  char magic[8];
  size_t i;
  if (!m || !path) return -1;
  f = fopen(path, "rb");
  if (!f) return -1;
  if (fread(magic, 1, 8, f) != 8 || memcmp(magic, TXTS_MAGIC, 8) != 0) {
    fclose(f);
    return -1;
  }
  for (i = 0; i < expect; i++) {
    unsigned char hdr[4];
    size_t len;
    char *copy;
    if (fread(hdr, 1, 4, f) != 4) {
      fclose(f);
      return -1;
    }
    len = (size_t)hdr[0] | ((size_t)hdr[1] << 8) | ((size_t)hdr[2] << 16) | ((size_t)hdr[3] << 24);
    if (len > 1024 * 1024) {
      fclose(f);
      return -1;
    }
    if (m->n_texts >= m->texts_cap) {
      int nc = m->texts_cap ? m->texts_cap * 2 : 16;
      char **nt = realloc(m->texts, (size_t)nc * sizeof(char *));
      if (!nt) {
        fclose(f);
        return -1;
      }
      m->texts = nt;
      m->texts_cap = nc;
    }
    copy = malloc(len + 1);
    if (!copy) {
      fclose(f);
      return -1;
    }
    if (len > 0 && fread(copy, 1, len, f) != len) {
      free(copy);
      fclose(f);
      return -1;
    }
    copy[len] = '\0';
    m->texts[m->n_texts++] = copy;
  }
  fclose(f);
  return 0;
}

static int persist(neo_memory_t *m) {
  const char *sp;
  char tp[512];
  if (!m || !m->db) return -1;
  sp = store_path(m->conf);
  texts_path(sp, tp, sizeof(tp));
  ensure_parent_dir(sp);
  if (vdb_save(m->db, sp) != VDB_OK) return -1;
  return save_texts(m, tp);
}

static int load_from_disk(neo_memory_t *m) {
  const char *sp;
  char tp[512];
  vdb_database *db;
  size_t n, i;
  if (!m) return -1;
  sp = store_path(m->conf);
  texts_path(sp, tp, sizeof(tp));
  db = vdb_load(sp);
  if (!db) return -1;
  if (vdb_dimensions(db) != dims_of(m->conf)) {
    /* dims 与配置不一致则放弃旧库，避免错配 */
    vdb_destroy(db);
    return -1;
  }
  n = vdb_count(db);
  neo_memory_clear_index(m);
  m->db = db;
  if (n == 0) return 0;
  if (load_texts(m, tp, n) != 0 || (size_t)m->n_texts != n) {
    neo_memory_clear_index(m);
    return -1;
  }
  /* sidecar 文本挂回 metadata（search 结果可兜底）；索引仍以 texts[] 为准 */
  for (i = 0; i < n; i++)
    m->db->vectors[i].metadata = m->texts[i];
  return 0;
}

int neo_memory_reindex(neo_memory_t *m) {
  /* 运维：从磁盘重新装入；无库则建空库。不再从 MEMORY.md 导入。 */
  if (!m || !m->conf || !m->conf->memory.vector_enabled) return -1;
  if (load_from_disk(m) == 0) return 0;
  neo_memory_clear_index(m);
  return ensure_db(m);
}

neo_memory_t *neo_memory_open(const agent_config_t *conf) {
  neo_memory_t *m;
  if (!conf) return NULL;
  m = calloc(1, sizeof(*m));
  if (!m) return NULL;
  m->conf = conf;
  if (conf->memory.vector_enabled) {
    if (load_from_disk(m) != 0) {
      neo_memory_clear_index(m);
      if (ensure_db(m) != 0) {
        free(m);
        return NULL;
      }
    }
  }
  return m;
}

void neo_memory_close(neo_memory_t *m) {
  if (!m) return;
  neo_memory_clear_index(m);
  free(m);
}

int neo_memory_store(neo_memory_t *m, const char *text) {
  size_t len, max_chunk = MAX_STORE_CHUNK;
  const char *p;
  if (!m || !m->conf || !text) return -1;
  if (!m->conf->memory.vector_enabled) return -1;
  if (ensure_db(m) != 0) return -1;
  len = strlen(text);
  p = text;
  while (len > 0) {
    size_t take = len > max_chunk ? utf8_prefix(p, max_chunk) : len;
    if (take == 0) take = len > max_chunk ? max_chunk : len;
    if (push_chunk(m, p, take) != 0) return -1;
    p += take;
    len -= take;
  }
  return persist(m);
}

static int recall_full_file(const agent_config_t *conf, char **out) {
  char *body;
  size_t n, take;
  int max_chars;
  if (!conf || !conf->memory.path || !out) return -1;
  *out = NULL;
  max_chars = conf->memory.max_chars > 0 ? conf->memory.max_chars : 4000;
  body = read_entire_file(conf->memory.path, (size_t)max_chars + 1);
  if (!body || !body[0]) {
    free(body);
    return -1;
  }
  n = strlen(body);
  take = utf8_prefix(body, (size_t)max_chars);
  if (take < n) body[take] = '\0';
  *out = body;
  return 0;
}

int neo_memory_recall(neo_memory_t *m, const char *query, char **out) {
  float *qvec;
  vdb_result_set *rs;
  size_t dims, budget, used;
  int k, i;
  char *buf;
  if (!m || !m->conf || !out) return -1;
  *out = NULL;

  /* vector 关：仅 MEMORY.md 截断 */
  if (!m->conf->memory.vector_enabled)
    return recall_full_file(m->conf, out);

  /* vector 开：只查库，不回退 MEMORY.md */
  if (!m->db || m->n_texts < 1) return -1;

  dims = vdb_dimensions(m->db);
  if (dims == 0) return -1;
  qvec = calloc(dims, sizeof(float));
  if (!qvec) return -1;
  if (neo_embed_text(query ? query : "", dims, qvec) != 0) {
    free(qvec);
    return -1;
  }
  k = m->conf->memory.vector_top_k > 0 ? m->conf->memory.vector_top_k : 5;
  rs = vdb_search(m->db, qvec, (size_t)k);
  free(qvec);
  if (!rs || rs->count == 0) {
    vdb_free_result_set(rs);
    return -1;
  }

  budget = (size_t)(m->conf->memory.max_chars > 0 ? m->conf->memory.max_chars : 4000);
  buf = malloc(budget + 256);
  if (!buf) {
    vdb_free_result_set(rs);
    return -1;
  }
  buf[0] = '\0';
  used = 0;
  for (i = 0; i < (int)rs->count; i++) {
    const char *txt = NULL;
    size_t tl, need;
    size_t idx = rs->results[i].index;
    if (idx < (size_t)m->n_texts) txt = m->texts[idx];
    else if (rs->results[i].metadata) txt = (const char *)rs->results[i].metadata;
    if (!txt || !txt[0]) continue;
    tl = strlen(txt);
    need = tl + 8;
    if (used + need > budget) break;
    if (used > 0) {
      memcpy(buf + used, "\n\n---\n\n", 7);
      used += 7;
      buf[used] = '\0';
    }
    memcpy(buf + used, txt, tl);
    used += tl;
    buf[used] = '\0';
  }
  vdb_free_result_set(rs);
  if (used == 0) {
    free(buf);
    return -1;
  }
  *out = buf;
  return 0;
}

int neo_memory_count(const neo_memory_t *m) {
  return m ? m->n_texts : 0;
}
