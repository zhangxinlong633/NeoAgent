#include "config.h"
#include "neo_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAIL(msg)                          \
  do {                                     \
    fprintf(stderr, "FAIL: %s\n", (msg));  \
    return 1;                              \
  } while (0)

static void unlink_store(const char *store) {
  char tp[512];
  if (!store) return;
  unlink(store);
  snprintf(tp, sizeof(tp), "%s.txts", store);
  unlink(tp);
}

static int test_disabled_truncates(void) {
  agent_config_t conf;
  neo_memory_t *m;
  char *out = NULL;
  const char *path = "tests/fixtures/memory_vector_sample.md";

  config_init(&conf);
  conf.memory.path = strdup(path);
  conf.memory.max_chars = 80;
  conf.memory.vector_enabled = 0;
  m = neo_memory_open(&conf);
  if (!m) FAIL("open");
  if (neo_memory_recall(m, "anything", &out) != 0 || !out) FAIL("recall");
  if (strlen(out) > 80) FAIL("should truncate");
  if (!strstr(out, "Memory")) FAIL("missing content");
  free(out);
  neo_memory_close(m);
  free(conf.memory.path);
  return 0;
}

static int test_store_recall_and_persist(void) {
  agent_config_t conf;
  neo_memory_t *m;
  char *out = NULL;
  char store[] = "tests/fixtures/.tmp_memory.vdb";

  unlink_store(store);
  config_init(&conf);
  conf.memory.vector_enabled = 1;
  conf.memory.vector_store = strdup(store);
  conf.memory.vector_top_k = 2;
  conf.memory.vector_dims = 64;
  conf.memory.max_chars = 400;

  m = neo_memory_open(&conf);
  if (!m) FAIL("open");
  if (neo_memory_store(m, "User prefers dark mode in the editor") != 0) FAIL("store dark");
  if (neo_memory_store(m, "Deploy crontab runs Friday night") != 0) FAIL("store deploy");
  if (neo_memory_count(m) < 2) FAIL("count");
  if (neo_memory_recall(m, "dark mode preference", &out) != 0 || !out) FAIL("recall");
  if (!strstr(out, "dark mode")) {
    fprintf(stderr, "FAIL: expected dark mode, got:\n%s\n", out);
    free(out);
    neo_memory_close(m);
    free(conf.memory.vector_store);
    return 1;
  }
  free(out);
  out = NULL;
  neo_memory_close(m);

  /* 重新 open，应仍能从磁盘召回 */
  m = neo_memory_open(&conf);
  if (!m) FAIL("reopen");
  if (neo_memory_count(m) < 2) FAIL("persist count");
  if (neo_memory_recall(m, "friday deploy", &out) != 0 || !out) FAIL("recall after reload");
  if (!strstr(out, "Friday") && !strstr(out, "crontab")) {
    fprintf(stderr, "FAIL: expected deploy chunk, got:\n%s\n", out);
    free(out);
    neo_memory_close(m);
    free(conf.memory.vector_store);
    return 1;
  }
  free(out);
  neo_memory_close(m);
  free(conf.memory.vector_store);
  unlink_store(store);
  return 0;
}

static int test_vector_ignores_memory_md(void) {
  agent_config_t conf;
  neo_memory_t *m;
  char *out = NULL;
  char store[] = "tests/fixtures/.tmp_memory_ignore.vdb";

  unlink_store(store);
  config_init(&conf);
  conf.memory.path = strdup("tests/fixtures/memory_vector_sample.md");
  conf.memory.vector_enabled = 1;
  conf.memory.vector_store = strdup(store);
  conf.memory.vector_top_k = 3;
  conf.memory.vector_dims = 64;
  m = neo_memory_open(&conf);
  if (!m) FAIL("open");
  /* 空库：即便 MEMORY.md 有 dark mode，也不应召回 */
  if (neo_memory_recall(m, "dark mode", &out) == 0) {
    free(out);
    neo_memory_close(m);
    free(conf.memory.path);
    free(conf.memory.vector_store);
    FAIL("should not read MEMORY.md when vector on");
  }
  neo_memory_close(m);
  free(conf.memory.path);
  free(conf.memory.vector_store);
  unlink_store(store);
  return 0;
}

static int test_config_vector_parse(void) {
  agent_config_t conf;
  const char *cfg = "tests/fixtures/memory_vector.json5";

  config_init(&conf);
  if (config_load_file(&conf, cfg) != 0) FAIL("load memory_vector.json5");
  if (!conf.memory.vector_enabled) FAIL("enabled");
  if (!conf.memory.vector_store ||
      strcmp(conf.memory.vector_store, "tests/fixtures/.tmp_cli_memory.vdb") != 0)
    FAIL("store");
  if (conf.memory.vector_top_k != 3) FAIL("top_k");
  if (conf.memory.vector_dims != 32) FAIL("dims");
  config_free(&conf);
  return 0;
}

int main(void) {
  if (test_disabled_truncates() != 0) return 1;
  if (test_store_recall_and_persist() != 0) return 1;
  if (test_vector_ignores_memory_md() != 0) return 1;
  if (test_config_vector_parse() != 0) return 1;
  printf("ok test_neo_memory\n");
  return 0;
}
