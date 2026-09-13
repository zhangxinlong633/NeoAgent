/* neo_events：开关、写文件、字段完整性。 */
#include "neo_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  char path[] = "/tmp/neo-events-ut-XXXXXX";
  int fd;
  FILE *f;
  char buf[512];
  size_t n;

  unsetenv("NEO_EVENTS");
  unsetenv("NEO_EVENTS_PATH");
  if (neo_events_enabled()) {
    fprintf(stderr, "default should be off\n");
    return 1;
  }
  neo_events_emit("dag_step", 1, 1, "should_not_write");

  fd = mkstemp(path);
  if (fd < 0) {
    perror("mkstemp");
    return 1;
  }
  close(fd);
  unlink(path);

  setenv("NEO_EVENTS", "1", 1);
  setenv("NEO_EVENTS_PATH", path, 1);
  if (!neo_events_enabled()) {
    fprintf(stderr, "NEO_EVENTS=1 should enable\n");
    return 1;
  }
  neo_events_emit("dag_step", 1, 42, "demo:step");
  neo_events_emit("tool_call", 1, 0, "count_run");
  neo_events_emit("error", 0, 0, "boom \"x\"");

  f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "expected events file at %s\n", path);
    unsetenv("NEO_EVENTS");
    unsetenv("NEO_EVENTS_PATH");
    return 1;
  }
  n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  unlink(path);
  unsetenv("NEO_EVENTS");
  unsetenv("NEO_EVENTS_PATH");
  buf[n] = '\0';
  if (!strstr(buf, "\"name\":\"dag_step\"") || !strstr(buf, "\"ms\":42") ||
      !strstr(buf, "demo:step") || !strstr(buf, "tool_call") || !strstr(buf, "error")) {
    fprintf(stderr, "bad events payload:\n%s\n", buf);
    return 1;
  }
  if (!strstr(buf, "\\\"") && strstr(buf, "boom")) {
    /* escaped quote inside detail */
    fprintf(stderr, "expected escaped quote in detail:\n%s\n", buf);
    return 1;
  }
  if (!strstr(buf, "\"v\":1")) {
    fprintf(stderr, "missing v:1:\n%s\n", buf);
    return 1;
  }
  {
    const char *p1 = strstr(buf, "\"run_id\":\"");
    const char *p2;
    char id1[32];
    size_t i = 0;
    if (!p1) {
      fprintf(stderr, "missing run_id:\n%s\n", buf);
      return 1;
    }
    p1 += strlen("\"run_id\":\"");
    while (p1[i] && p1[i] != '"' && i + 1 < sizeof(id1)) {
      id1[i] = p1[i];
      i++;
    }
    id1[i] = '\0';
    if (i < 8) {
      fprintf(stderr, "run_id too short: %s\n", id1);
      return 1;
    }
    p2 = strstr(p1, "\"run_id\":\"");
    if (!p2 || strncmp(p2 + strlen("\"run_id\":\""), id1, i) != 0) {
      fprintf(stderr, "run_id not stable across lines:\n%s\n", buf);
      return 1;
    }
  }
  printf("ok\n");
  return 0;
}
