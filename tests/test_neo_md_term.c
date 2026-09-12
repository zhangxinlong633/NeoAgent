/* neo_md_term 冒烟：标题 / 表格 / 加粗应出现在无色渲染结果中。 */
#include "neo_md_term.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL: %s\n", msg); return 1; } while (0)

int main(void) {
  static const char *md =
      "## Hello\n\n"
      "This is **bold** and `code`.\n\n"
      "| A | B |\n"
      "|---|---|\n"
      "| 1 | 2 |\n";
  char *buf = NULL;
  size_t n = 0;
  FILE *mem;
  int rc;

  mem = open_memstream(&buf, &n);
  if (!mem) FAIL("open_memstream");
  rc = neo_md_term_render(md, strlen(md), mem, 0);
  fclose(mem);
  if (rc != 0) FAIL("render");
  if (!buf) FAIL("empty buf");
  if (!strstr(buf, "Hello")) FAIL("missing heading text");
  if (!strstr(buf, "bold")) FAIL("missing bold text");
  if (!strstr(buf, "code")) FAIL("missing code text");
  if (!strstr(buf, "│") && !strstr(buf, "|")) {
    /* 渲染器用 U+2502；无匹配则失败 */
    FAIL("missing table separator");
  }
  free(buf);
  fprintf(stderr, "ok test_neo_md_term\n");
  return 0;
}
