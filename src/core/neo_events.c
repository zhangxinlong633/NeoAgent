/*
 * 结构化运行事件：默认关闭，避免吵；NEO_EVENTS=1 时写 JSONL。
 * 不做第二套日志系统——只是薄封装，供 dag / tool / llm / CLI 挂钩。
 * run_id：进程内首次 emit 懒生成，不跨进程持久化。
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
#define NEO_EVENTS_SCHEMA_V 1

/* 16 hex chars + NUL；空串表示尚未生成 */
static char g_run_id[17];

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

static void ensure_run_id(void) {
  unsigned char b[8];
  size_t i;
  FILE *f;
  if (g_run_id[0]) return;
  memset(b, 0, sizeof(b));
  f = fopen("/dev/urandom", "rb");
  if (f) {
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
      /* 读不足则混入时间，避免全零 */
      long t = (long)time(NULL) ^ (long)neo_events_now_ms();
      memcpy(b, &t, sizeof(t) < sizeof(b) ? sizeof(t) : sizeof(b));
    }
    fclose(f);
  } else {
    long t = (long)time(NULL) ^ (long)neo_events_now_ms();
    memcpy(b, &t, sizeof(t) < sizeof(b) ? sizeof(t) : sizeof(b));
  }
  for (i = 0; i < sizeof(b); i++)
    snprintf(g_run_id + i * 2, 3, "%02x", b[i]);
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
  ensure_run_id();
  ts = time(NULL);
  esc[0] = '\0';
  json_escape_append(esc, sizeof(esc), &eu, detail);

  snprintf(line, sizeof(line),
           "{\"v\":%d,\"ts\":%ld,\"run_id\":\"%s\",\"name\":\"%s\",\"ok\":%d,\"ms\":%ld,\"detail\":\"%s\"}\n",
           NEO_EVENTS_SCHEMA_V, (long)ts, g_run_id, name, ok ? 1 : 0, ms, esc);

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
