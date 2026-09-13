#include "config.h"
#include "dag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int count_lines(const char *path) {
  FILE *f = fopen(path, "r");
  int n = 0;
  char buf[64];
  if (!f) return -1;
  while (fgets(buf, sizeof(buf), f)) n++;
  fclose(f);
  return n;
}

int main(void) {
  agent_config_t c;
  char *out = NULL;
  const dag_t *wf;

  unlink("tests/fixtures/count.out");
  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/dag_runner.json5") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  wf = config_find_dag(&c, "diamond");
  if (!wf || wf->step_count != 4 || wf->steps[1].depends_count != 1) {
    fprintf(stderr, "parse diamond bad\n");
    config_free(&c);
    return 1;
  }
  if (dag_run(&c, "diamond", &out, 0) != 0) {
    fprintf(stderr, "diamond run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  if (count_lines("tests/fixtures/count.out") != 4) {
    fprintf(stderr, "diamond want 4 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  unlink("tests/fixtures/count.out");
  out = NULL;
  if (dag_run(&c, "route_demo", &out, 0) != 0) {
    fprintf(stderr, "route run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  /* seed + take_then only */
  if (count_lines("tests/fixtures/count.out") != 2) {
    fprintf(stderr, "route want 2 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  unlink("tests/fixtures/count.out");
  out = NULL;
  if (dag_run(&c, "route_merge", &out, 0) != 0) {
    fprintf(stderr, "route_merge run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  /* seed + join (fix_branch skipped on PASS) */
  if (count_lines("tests/fixtures/count.out") != 2) {
    fprintf(stderr, "route_merge want 2 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  unlink("tests/fixtures/count.out");
  out = NULL;
  if (dag_run(&c, "route_cases", &out, 0) != 0) {
    fprintf(stderr, "route_cases run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  /* seed + take_docx only */
  if (count_lines("tests/fixtures/count.out") != 2) {
    fprintf(stderr, "route_cases want 2 lines got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }

  unlink("tests/fixtures/count.out");
  unlink("tests/fixtures/flaky.flag");
  out = NULL;
  if (dag_run(&c, "tool_retry", &out, 0) != 0) {
    fprintf(stderr, "tool_retry run failed\n");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  if (count_lines("tests/fixtures/count.out") != 1) {
    fprintf(stderr, "tool_retry want 1 line got %d\n", count_lines("tests/fixtures/count.out"));
    config_free(&c);
    return 1;
  }
  unlink("tests/fixtures/flaky.flag");

  config_free(&c);

  /* 步级 timeout_sec=1 + retry.max=1：两次墙钟超时后失败 */
  {
    agent_config_t tc;
    char *tout = NULL;
    FILE *errf;
    char errpath[] = "/tmp/neo-dag-timeout-err-XXXXXX";
    int efd;
    char ebuf[4096];
    size_t en;
    int saved_err;
    config_init(&tc);
    if (config_load_file(&tc, "tests/fixtures/dag_timeout.json5") != 0) {
      fprintf(stderr, "dag_timeout load failed\n");
      return 1;
    }
    if (!config_find_dag(&tc, "tool_timeout") ||
        config_find_dag(&tc, "tool_timeout")->steps[0].timeout_sec != 1) {
      fprintf(stderr, "dag_timeout parse timeout_sec bad\n");
      config_free(&tc);
      return 1;
    }
    efd = mkstemp(errpath);
    if (efd < 0) {
      perror("mkstemp");
      config_free(&tc);
      return 1;
    }
    close(efd);
    unlink(errpath);
    errf = fopen(errpath, "w+");
    if (!errf) {
      config_free(&tc);
      return 1;
    }
    saved_err = dup(2);
    dup2(fileno(errf), 2);
    if (dag_run(&tc, "tool_timeout", &tout, 0) == 0) {
      dup2(saved_err, 2);
      close(saved_err);
      fclose(errf);
      unlink(errpath);
      fprintf(stderr, "tool_timeout should fail\n");
      free(tout);
      config_free(&tc);
      return 1;
    }
    fflush(stderr);
    dup2(saved_err, 2);
    close(saved_err);
    rewind(errf);
    en = fread(ebuf, 1, sizeof(ebuf) - 1, errf);
    ebuf[en] = '\0';
    fclose(errf);
    unlink(errpath);
    free(tout);
    config_free(&tc);
    if (!strstr(ebuf, "ERROR: timeout") || !strstr(ebuf, "retry attempt")) {
      fprintf(stderr, "tool_timeout stderr missing timeout/retry:\n%s\n", ebuf);
      return 1;
    }
  }

  printf("ok\n");
  return 0;
}
