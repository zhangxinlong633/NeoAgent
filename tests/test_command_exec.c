#include "command_tools.h"
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  agent_config_t c;
  char root_real[PATH_MAX];
  char *out = NULL;
  size_t out_len = 0;
  int idx;
  int rc;
  tool_command_t abs_cmd;
  char *argv_abs[4];

  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/commands_min.json5") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (!realpath(".", root_real)) {
    fprintf(stderr, "realpath failed\n");
    config_free(&c);
    return 1;
  }
  idx = command_tool_find(&c, "echo_args");
  if (idx < 0) {
    fprintf(stderr, "find failed\n");
    config_free(&c);
    return 1;
  }
  rc = command_tool_run(&c, root_real, &c.tools.commands[idx], "{\"ping\":1}", &out, &out_len, 0);
  if (rc != 0 || !out) {
    fprintf(stderr, "run failed rc=%d\n", rc);
    free(out);
    config_free(&c);
    return 1;
  }
  if (strstr(out, "name=echo_args") == NULL) {
    fprintf(stderr, "missing name in output:\n%s\n", out);
    free(out);
    config_free(&c);
    return 1;
  }
  if (strstr(out, "stdin={\"ping\":1}") == NULL) {
    fprintf(stderr, "missing stdin in output:\n%s\n", out);
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  out = NULL;

  /* 绝对路径 argv[0]：白名单声明的 /bin/date */
  memset(&abs_cmd, 0, sizeof(abs_cmd));
  abs_cmd.name = (char *)"date_iso";
  argv_abs[0] = (char *)"/bin/date";
  argv_abs[1] = (char *)"-u";
  argv_abs[2] = (char *)"+%Y-%m-%dT%H:%M:%SZ";
  argv_abs[3] = NULL;
  abs_cmd.argv = argv_abs;
  abs_cmd.argv_count = 3;
  abs_cmd.timeout_sec = 5;
  abs_cmd.max_output_bytes = 256;
  abs_cmd.pass_args = 1; /* env：避免把 JSON 喂给 date */
  rc = command_tool_run(&c, root_real, &abs_cmd, "{}", &out, &out_len, 0);
  if (rc != 0 || !out || strlen(out) < 10) {
    fprintf(stderr, "absolute argv run failed rc=%d out=%s\n", rc, out ? out : "(null)");
    free(out);
    config_free(&c);
    return 1;
  }
  free(out);
  out = NULL;

  /* 矩阵默认 timeout 30，override=1 应墙钟杀掉 sleep 5 */
  {
    tool_command_t slow;
    char *argv_slow[2];
    memset(&slow, 0, sizeof(slow));
    slow.name = (char *)"sleep_n";
    argv_slow[0] = (char *)"./tests/fixtures/bin/sleep_n.sh";
    argv_slow[1] = NULL;
    slow.argv = argv_slow;
    slow.argv_count = 1;
    slow.timeout_sec = 30;
    slow.max_output_bytes = 256;
    slow.pass_args = 0;
    rc = command_tool_run(&c, root_real, &slow, "{}", &out, &out_len, 1);
    if (!out || strncmp(out, "ERROR: timeout", 14) != 0) {
      fprintf(stderr, "expected ERROR: timeout, rc=%d out=%s\n", rc, out ? out : "(null)");
      free(out);
      config_free(&c);
      return 1;
    }
    (void)rc;
    free(out);
    out = NULL;
  }

  config_free(&c);
  printf("ok\n");
  return 0;
}
