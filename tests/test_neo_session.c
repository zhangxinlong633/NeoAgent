/* neo_session 单元：id 校验、多 ID 解析、拼接加载、追加轮次、裁剪、清空。 */
#include "neo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } while (0)

int main(void) {
  const char *id = "ut_neo_session";
  const char *id_a = "ut_mix_a";
  const char *id_b = "ut_mix_b";
  llm_message_t *msgs = NULL;
  int n = 0;
  char path[256];
  char **ids = NULL;
  int n_ids = 0;

  if (neo_session_id_ok("ok")) { /* always true */ }

  if (neo_session_id_ok("../x")) FAIL("bad id should reject");
  if (neo_session_id_ok("")) FAIL("empty id");
  if (neo_session_id_ok("has space")) FAIL("space id");

  if (neo_session_parse_ids("ship, cook", &ids, &n_ids) != 0) FAIL("parse two");
  if (n_ids != 2) FAIL("parse n");
  if (strcmp(ids[0], "ship") != 0 || strcmp(ids[1], "cook") != 0) FAIL("parse values");
  neo_session_free_ids(ids, n_ids);
  ids = NULL;

  if (neo_session_parse_ids("a,a", &ids, &n_ids) == 0) FAIL("dup should fail");
  if (neo_session_parse_ids("a,,b", &ids, &n_ids) == 0) FAIL("empty segment");
  if (neo_session_parse_ids("../x", &ids, &n_ids) == 0) FAIL("bad id in list");

  (void)neo_session_clear(id);
  if (neo_session_load(id, &msgs, &n) != 0) FAIL("load empty");
  if (n != 0 || msgs != NULL) FAIL("empty should be null");

  if (neo_session_append_turn(id, "飞船怎么做", "有四种方案…", 2) != 0) FAIL("append1");
  if (neo_session_append_turn(id, "4. 真正的载人", "载人飞船概览…", 2) != 0) FAIL("append2");
  /* max_turns=2 → 第三轮应挤掉最早一轮 */
  if (neo_session_append_turn(id, "再问一句", "第三轮答复", 2) != 0) FAIL("append3");

  if (neo_session_load(id, &msgs, &n) != 0) FAIL("load after");
  if (n != 4) {
    neo_session_free(msgs, n);
    FAIL("trimmed to 2 turns = 4 msgs");
  }
  if (strcmp(msgs[0].content, "4. 真正的载人") != 0) {
    neo_session_free(msgs, n);
    FAIL("oldest should be dropped");
  }
  neo_session_free(msgs, n);

  if (neo_session_path(id, path, sizeof(path)) != 0) FAIL("path");
  if (strstr(path, ".neo/sessions/") == NULL) FAIL("path prefix");

  if (neo_session_clear(id) != 0) FAIL("clear");
  if (neo_session_load(id, &msgs, &n) != 0) FAIL("load cleared");
  if (n != 0) FAIL("cleared empty");

  /* 多会话拼接：a 然后 b */
  (void)neo_session_clear(id_a);
  (void)neo_session_clear(id_b);
  if (neo_session_append_turn(id_a, "A1", "Ra1", 10) != 0) FAIL("a1");
  if (neo_session_append_turn(id_b, "B1", "Rb1", 10) != 0) FAIL("b1");
  if (neo_session_parse_ids("ut_mix_a,ut_mix_b", &ids, &n_ids) != 0) FAIL("parse mix");
  if (neo_session_load_many(ids, n_ids, &msgs, &n) != 0) FAIL("load_many");
  neo_session_free_ids(ids, n_ids);
  if (n != 4) {
    neo_session_free(msgs, n);
    FAIL("mix 4 msgs");
  }
  if (strcmp(msgs[0].content, "A1") != 0 || strcmp(msgs[2].content, "B1") != 0) {
    neo_session_free(msgs, n);
    FAIL("mix order");
  }
  neo_session_free(msgs, n);
  (void)neo_session_clear(id_a);
  (void)neo_session_clear(id_b);

  /* 归档 default */
  {
    char archived[65];
    (void)neo_session_clear(NEO_SESSION_DEFAULT_ID);
    if (neo_session_archive_default(archived, sizeof(archived)) != 0) FAIL("archive empty");
    if (archived[0]) FAIL("empty should not set id");
    if (neo_session_append_turn(NEO_SESSION_DEFAULT_ID, "u", "a", 10) != 0) FAIL("default turn");
    if (neo_session_archive_default(archived, sizeof(archived)) != 0) FAIL("archive");
    if (!archived[0] || !neo_session_id_ok(archived)) FAIL("archive id");
    if (strcmp(archived, NEO_SESSION_DEFAULT_ID) == 0) FAIL("archive != default");
    if (neo_session_load(NEO_SESSION_DEFAULT_ID, &msgs, &n) != 0) FAIL("default after");
    if (n != 0) {
      neo_session_free(msgs, n);
      FAIL("default cleared by rename");
    }
    if (neo_session_load(archived, &msgs, &n) != 0) FAIL("load archived");
    if (n != 2) {
      neo_session_free(msgs, n);
      FAIL("archived msgs");
    }
    neo_session_free(msgs, n);
    (void)neo_session_clear(archived);
  }

  fprintf(stderr, "ok test_neo_session\n");
  return 0;
}
