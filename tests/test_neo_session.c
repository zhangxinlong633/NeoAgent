/* neo_session 单元：id 校验、追加轮次、裁剪、清空。 */
#include "neo_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL: %s\n", msg); return 1; } while (0)

int main(void) {
  llm_message_t *msgs = NULL;
  int n = 0;
  char path[256];
  const char *id = "ut_sess_demo";

  if (neo_session_id_ok("ok")) { /* always true */ }
  else FAIL("ok id");
  if (neo_session_id_ok("../x")) FAIL("bad id should reject");
  if (neo_session_id_ok("")) FAIL("empty id");
  if (neo_session_id_ok("has space")) FAIL("space id");

  (void)neo_session_clear(id);
  if (neo_session_load(id, &msgs, &n) != 0) FAIL("load empty");
  if (n != 0 || msgs != NULL) FAIL("empty should be null");

  if (neo_session_append_turn(id, "飞船怎么做", "有四种方案…", 2) != 0) FAIL("append1");
  if (neo_session_append_turn(id, "4. 真正的载人", "载人飞船概览…", 2) != 0) FAIL("append2");
  /* max_turns=2 → 最多 4 条；再追加应裁掉最旧 */
  if (neo_session_append_turn(id, "再问一句", "第三轮答复", 2) != 0) FAIL("append3");

  if (neo_session_load(id, &msgs, &n) != 0) FAIL("load after");
  if (n != 4) {
    fprintf(stderr, "FAIL: expected 4 msgs got %d\n", n);
    neo_session_free(msgs, n);
    return 1;
  }
  if (strcmp(msgs[0].content, "4. 真正的载人") != 0) FAIL("trim oldest user");
  if (strcmp(msgs[3].role, "assistant") != 0) FAIL("last assistant");
  neo_session_free(msgs, n);

  if (neo_session_path(id, path, sizeof(path)) != 0) FAIL("path");
  if (strstr(path, ".neo/sessions/") == NULL) FAIL("path prefix");

  if (neo_session_clear(id) != 0) FAIL("clear");
  if (neo_session_load(id, &msgs, &n) != 0) FAIL("load cleared");
  if (n != 0) FAIL("cleared not empty");

  fprintf(stderr, "ok test_neo_session\n");
  return 0;
}
