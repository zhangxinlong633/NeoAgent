/*
 * 结构化运行事件：默认关闭，避免吵；NEO_EVENTS=1 时写 JSONL。
 * 不做第二套日志系统——只是薄封装，供 dag / tool / llm / CLI 挂钩。
 */
#include "neo_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/time.h>
#endif

#define NEO_EVENTS_DETAIL_MAX 200

long neo_events_now_ms(void) {
#if defined(__APPLE__) || defined(__linux__)
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) return 0;
  return (long)tv.tv_sec * 1000L + (long)tv.tv_usec / 1000L;
#else
  return (long)time(NULL) * 1000L;
#endif
}

int neo_events_enabled(void) {
  const char *e = getenv("NEO_EVENTS");
  if (!e || !e[0]) return 0;
  if (e[0] == '0' || strcasecmp(e, "false") == 0 || strcasecmp(e, "no") == 0 ||
      strcasecmp(e, "off") == 0)
    return 0;
  if (e[0] == '1' || strcasecmp(e, "true") == 0 || strcasecmp(e, "yes") == 0 ||
      strcasecmp(e, "on") == 0)
    return 1;
  /* 其它非空值也视为开启，便于 NEO_EVENTS=jsonl */
  return 1;
}

static void json_escape_append(char *dst, size_t dst_sz, size_t *used, const char *s) {
  size_t i;
  if (!s) s = "";
  for (i = 0; s[i] && *used + 2 < dst_sz; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') {
      if (*used + 3 >= dst_sz) break;
      dst[(*used)++] = '\\';
      dst[(*used)++] = (char)c;
    } else if (c < 0x20) {
      if (*used + 7 >= dst_sz) break;
      *used += (size_t)snprintf(dst + *used, dst_sz - *used, "\\u%04x", c);
    } else {
      dst[(*used)++] = (char)c;
    }
  }
  dst[*used] = '\0';
}

void neo_events_emit(const char *name, int ok, long ms, const char *detail) {
  char line[768];
  char esc[NEO_EVENTS_DETAIL_MAX * 6 + 8];
  size_t eu = 0;
  const char *path;
  FILE *f;
  time_t ts;

  if (!neo_events_enabled()) return;
  if (!name || !name[0]) name = "unknown";
  if (ms < 0) ms = 0;
  ts = time(NULL);
  esc[0] = '\0';
  json_escape_append(esc, sizeof(esc), &eu, detail);

  snprintf(line, sizeof(line),
           "{\"ts\":%ld,\"name\":\"%s\",\"ok\":%d,\"ms\":%ld,\"detail\":\"%s\"}\n",
           (long)ts, name, ok ? 1 : 0, ms, esc);

  path = getenv("NEO_EVENTS_PATH");
  if (path && path[0]) {
    f = fopen(path, "a");
    if (!f) {
      fprintf(stderr, "neo events: cannot open %s\n", path);
      fputs(line, stderr);
      return;
    }
    fputs(line, f);
    fclose(f);
    return;
  }
  fputs(line, stderr);
}
