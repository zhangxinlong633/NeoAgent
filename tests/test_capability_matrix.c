#include "agent_tools.h"
#include "capability_matrix.h"
#include "config.h"
#include "mcp_stdio.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAIL(msg)                          \
  do {                                     \
    fprintf(stderr, "FAIL: %s\n", (msg));  \
    return 1;                              \
  } while (0)

static int expect_strstr(const char *hay, const char *needle, const char *label) {
  if (!hay || !strstr(hay, needle)) {
    fprintf(stderr, "FAIL: %s missing '%s': %s\n", label, needle, hay ? hay : "(null)");
    return 1;
  }
  return 0;
}

static int test_empty_and_basics(void) {
  capability_matrix_t m;
  char *json;
  char *listing;

  capability_matrix_init(&m);
  if (m.count != 0) FAIL("expected empty");
  if (capability_matrix_find(&m, "read_file") != NULL) FAIL("find on empty");
  json = capability_matrix_tools_json(&m);
  if (!json || strcmp(json, "[]") != 0) {
    free(json);
    FAIL("tools_json expected []");
  }
  free(json);
  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "none")) {
    free(listing);
    FAIL("listing expected none");
  }
  free(listing);
  capability_matrix_free(&m);
  return 0;
}

static int test_build_schema_fixture(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char *json;
  char *listing;
  const cap_row_t *row;

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_commands_schema.json5") != 0)
    FAIL("load schema fixture");
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("build_from_config");
  }

  row = capability_matrix_find(&m, "read_file");
  if (!row || row->source != CAP_SRC_BUILTIN || row->effect != CAP_EFFECT_READ ||
      row->builtin_id != CAP_BUILTIN_READ_FILE)
    FAIL("read_file metadata");

  row = capability_matrix_find(&m, "write_file");
  if (!row || row->effect != CAP_EFFECT_WRITE) FAIL("write_file effect");

  row = capability_matrix_find(&m, "grep");
  if (!row || row->builtin_id != CAP_BUILTIN_GREP) FAIL("grep row");

  row = capability_matrix_find(&m, "stat");
  if (!row || row->effect != CAP_EFFECT_READ || row->builtin_id != CAP_BUILTIN_STAT)
    FAIL("stat row");
  row = capability_matrix_find(&m, "mkdir");
  if (!row || row->effect != CAP_EFFECT_WRITE || row->builtin_id != CAP_BUILTIN_MKDIR)
    FAIL("mkdir row");
  row = capability_matrix_find(&m, "append_file");
  if (!row || row->effect != CAP_EFFECT_WRITE || row->builtin_id != CAP_BUILTIN_APPEND_FILE)
    FAIL("append_file row");

  if (capability_matrix_find(&m, "http_get")) FAIL("http_get should be absent");
  if (capability_matrix_find(&m, "run_command")) FAIL("run_command absent when shell off");

  row = capability_matrix_find(&m, "schema_echo");
  if (!row || row->source != CAP_SRC_COMMAND || row->effect != CAP_EFFECT_EXEC || !row->command)
    FAIL("schema_echo command metadata");
  if (!row->parameters_json || !strstr(row->parameters_json, "properties"))
    FAIL("schema_echo parameters");

  json = capability_matrix_tools_json(&m);
  if (!json || expect_strstr(json, "\"type\":\"function\"", "tools_json shape") ||
      expect_strstr(json, "schema_echo", "tools_json name") ||
      expect_strstr(json, "\"msg\"", "tools_json msg")) {
    free(json);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(json);

  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "schema_echo") || !strstr(listing, "command") ||
      !strstr(listing, "builtin")) {
    free(listing);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("listing labels");
  }
  free(listing);

  /* disabled row omitted from OpenAI tools JSON */
  row = capability_matrix_find(&m, "write_file");
  ((cap_row_t *)row)->enabled = 0;
  json = capability_matrix_tools_json(&m);
  if (!json || strstr(json, "write_file") || !strstr(json, "read_file")) {
    free(json);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("disabled row should omit write_file only");
  }
  free(json);

  {
    char *cap = NULL;
    size_t cap_sz = 0;
    FILE *mem = open_memstream(&cap, &cap_sz);
    int w;
    if (!mem) {
      capability_matrix_free(&m);
      config_free(&conf);
      FAIL("open_memstream");
    }
    w = capability_warn_tool_truncation(20, 16, mem);
    fclose(mem);
    if (!w || !cap || !strstr(cap, "truncated")) {
      free(cap);
      capability_matrix_free(&m);
      config_free(&conf);
      FAIL("truncation warn");
    }
    free(cap);
    if (capability_warn_tool_truncation(3, 16, stderr) != 0) {
      capability_matrix_free(&m);
      config_free(&conf);
      FAIL("unexpected warn");
    }
  }

  capability_matrix_free(&m);
  config_free(&conf);
  return 0;
}

static int test_matrix_disabled(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char *json;

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_matrix_off.json5") != 0)
    FAIL("load matrix_off");
  if (conf.tools.enabled) FAIL("expected enabled=false");
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("build disabled");
  }
  if (m.count != 0 || capability_matrix_find(&m, "read_file") ||
      capability_matrix_find(&m, "should_not_appear")) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("disabled matrix should be empty");
  }
  json = capability_matrix_tools_json(&m);
  if (!json || strcmp(json, "[]") != 0) {
    free(json);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("disabled tools_json");
  }
  free(json);
  capability_matrix_free(&m);
  config_free(&conf);
  return 0;
}

static int test_http_policy(void) {
  capability_matrix_t m;
  agent_config_t conf;
  const cap_row_t *row;
  char root[PATH_MAX];
  char *out = NULL;
  size_t out_len = 0;

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_http_on.json5") != 0)
    FAIL("load http fixture");
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("build http");
  }
  row = capability_matrix_find(&m, "http_get");
  if (!row || row->effect != CAP_EFFECT_NETWORK || row->builtin_id != CAP_BUILTIN_HTTP_GET) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("http_get row");
  }

  if (!realpath(".", root)) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("realpath");
  }
  /* allowlist reject — no network needed */
  if (neo_dispatch_tool(&conf, root, "http_get", "{\"url\":\"https://evil.example/x\"}", &out,
                        &out_len, 0) != 0) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("http_get dispatch");
  }
  if (!out || !strstr(out, "ERROR") || !strstr(out, "allow")) {
    fprintf(stderr, "FAIL: http allowlist: %s\n", out ? out : "(null)");
    free(out);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);

  capability_matrix_free(&m);
  config_free(&conf);
  return 0;
}

static int test_add_mcp_row(void) {
  capability_matrix_t m;
  const cap_row_t *row;
  char *listing;

  capability_matrix_init(&m);
  if (capability_matrix_add_mcp(&m, "mcp_unit_ping", "unit ping",
                                "{\"type\":\"object\"}", "unit", "ping") != 0)
    FAIL("add_mcp");
  row = capability_matrix_find(&m, "mcp_unit_ping");
  if (!row || row->source != CAP_SRC_MCP || !row->mcp_server || strcmp(row->mcp_server, "unit") ||
      !row->mcp_tool || strcmp(row->mcp_tool, "ping")) {
    capability_matrix_free(&m);
    FAIL("add_mcp metadata");
  }
  /* duplicate name is soft-skip */
  if (capability_matrix_add_mcp(&m, "mcp_unit_ping", "dup", "{}", "unit", "ping") != 0) {
    capability_matrix_free(&m);
    FAIL("dup add_mcp");
  }
  if (m.count != 1) {
    capability_matrix_free(&m);
    FAIL("dup should not grow count");
  }
  listing = capability_matrix_prompt_listing(&m);
  if (!listing || !strstr(listing, "mcp_unit_ping") || !strstr(listing, "mcp")) {
    free(listing);
    capability_matrix_free(&m);
    FAIL("mcp listing");
  }
  free(listing);
  capability_matrix_free(&m);
  return 0;
}

static int test_dispatch_builtins(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char root[PATH_MAX];
  char *out = NULL;
  size_t out_len = 0;
  const char *tmp_rel = "tests/fixtures/_cap_matrix_tmp.txt";
  const char *mark = "CAP_MATRIX_MARK_42";

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_commands_schema.json5") != 0)
    FAIL("load for dispatch");
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("build for dispatch");
  }
  if (!realpath(".", root)) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("realpath");
  }

  {
    char args[512];
    snprintf(args, sizeof(args),
             "{\"path\":\"%s\",\"content\":\"%s\\nline2\\n\"}", tmp_rel, mark);
    if (neo_dispatch_tool(&conf, root, "write_file", args, &out, &out_len, 0) != 0) {
      capability_matrix_free(&m);
      config_free(&conf);
      FAIL("write_file");
    }
    free(out);
    out = NULL;
  }

  {
    char args[256];
    snprintf(args, sizeof(args), "{\"path\":\"%s\"}", tmp_rel);
    if (neo_dispatch_tool(&conf, root, "read_file", args, &out, &out_len, 0) != 0) {
      unlink(tmp_rel);
      capability_matrix_free(&m);
      config_free(&conf);
      FAIL("read_file");
    }
    if (!out || !strstr(out, mark)) {
      fprintf(stderr, "FAIL: read_file content: %s\n", out ? out : "(null)");
      free(out);
      unlink(tmp_rel);
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
    free(out);
    out = NULL;
  }

  if (neo_dispatch_tool(&conf, root, "list_dir", "{\"path\":\"tests/fixtures\"}", &out, &out_len, 0) !=
      0) {
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("list_dir");
  }
  if (!out || !strstr(out, "tools_commands_schema.json5")) {
    fprintf(stderr, "FAIL: list_dir: %s\n", out ? out : "(null)");
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  out = NULL;

  {
    char args[256];
    snprintf(args, sizeof(args), "{\"pattern\":\"%s\",\"path\":\"tests/fixtures\"}", mark);
    if (neo_dispatch_tool(&conf, root, "grep", args, &out, &out_len, 0) != 0) {
      unlink(tmp_rel);
      capability_matrix_free(&m);
      config_free(&conf);
      FAIL("grep");
    }
    if (!out || !strstr(out, mark)) {
      fprintf(stderr, "FAIL: grep: %s\n", out ? out : "(null)");
      free(out);
      unlink(tmp_rel);
      capability_matrix_free(&m);
      config_free(&conf);
      return 1;
    }
    free(out);
    out = NULL;
  }

  /* mkdir (parents) + append_file + stat */
  if (neo_dispatch_tool(&conf, root, "mkdir",
                        "{\"path\":\"tests/fixtures/_cap_mkdir_nest/a\",\"parents\":true}", &out,
                        &out_len, 0) != 0) {
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("mkdir");
  }
  if (!out || strcmp(out, "ok") != 0) {
    fprintf(stderr, "FAIL: mkdir: %s\n", out ? out : "(null)");
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  out = NULL;

  if (neo_dispatch_tool(&conf, root, "append_file",
                        "{\"path\":\"tests/fixtures/_cap_mkdir_nest/a/note.txt\",\"content\":\"A\"}",
                        &out, &out_len, 0) != 0) {
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("append_file 1");
  }
  free(out);
  out = NULL;
  if (neo_dispatch_tool(&conf, root, "append_file",
                        "{\"path\":\"tests/fixtures/_cap_mkdir_nest/a/note.txt\",\"content\":\"B\"}",
                        &out, &out_len, 0) != 0) {
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("append_file 2");
  }
  free(out);
  out = NULL;
  if (neo_dispatch_tool(&conf, root, "read_file",
                        "{\"path\":\"tests/fixtures/_cap_mkdir_nest/a/note.txt\"}", &out,
                        &out_len, 0) != 0) {
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("read after append");
  }
  if (!out || strcmp(out, "AB") != 0) {
    fprintf(stderr, "FAIL: append content: %s\n", out ? out : "(null)");
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  out = NULL;

  if (neo_dispatch_tool(&conf, root, "stat",
                        "{\"path\":\"tests/fixtures/_cap_mkdir_nest/a/note.txt\"}", &out,
                        &out_len, 0) != 0) {
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("stat");
  }
  if (!out || !strstr(out, "type: file") || !strstr(out, "size:")) {
    fprintf(stderr, "FAIL: stat: %s\n", out ? out : "(null)");
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  out = NULL;
  unlink("tests/fixtures/_cap_mkdir_nest/a/note.txt");
  rmdir("tests/fixtures/_cap_mkdir_nest/a");
  rmdir("tests/fixtures/_cap_mkdir_nest");

  if (neo_dispatch_tool(&conf, root, "read_file", "{\"path\":\"../etc/passwd\"}", &out, &out_len, 0) !=
      0) {
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("escape dispatch");
  }
  if (!out || !strstr(out, "ERROR") || !strstr(out, "path")) {
    fprintf(stderr, "FAIL: path escape: %s\n", out ? out : "(null)");
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  out = NULL;

  if (neo_dispatch_tool(&conf, root, "no_such_tool", "{}", &out, &out_len, 0) != 0) {
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("unknown tool dispatch");
  }
  if (!out || !strstr(out, "unknown tool")) {
    fprintf(stderr, "FAIL: unknown tool msg: %s\n", out ? out : "(null)");
    free(out);
    unlink(tmp_rel);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);

  unlink(tmp_rel);
  capability_matrix_free(&m);
  config_free(&conf);
  return 0;
}

static int test_shell_and_mcp(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char root[PATH_MAX];
  char *out = NULL;
  size_t out_len = 0;

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_commands_schema.json5") != 0)
    FAIL("reload schema");
  conf.tools.shell_enabled = 1;
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("build shell");
  }
  if (!capability_matrix_find(&m, "run_command")) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("missing run_command");
  }
  if (!realpath(".", root)) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("realpath");
  }
  if (neo_dispatch_tool(&conf, root, "run_command",
                        "{\"argv\":[\"tests/fixtures/bin/echo-argv.sh\",\"hi\"]}", &out,
                        &out_len, 0) != 0) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("run_command");
  }
  if (!out || !strstr(out, "hi")) {
    fprintf(stderr, "FAIL: run_command: %s\n", out ? out : "(null)");
    free(out);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  capability_matrix_free(&m);
  config_free(&conf);

  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_mcp_stdio.json5") != 0)
    FAIL("load mcp");
  if (conf.tools.mcp_server_count < 1) {
    config_free(&conf);
    FAIL("mcp_servers");
  }
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("mcp build");
  }
  if (!capability_matrix_find(&m, "mcp_mock_echo")) {
    capability_matrix_free(&m);
    config_free(&conf);
    mcp_stdio_shutdown_all();
    FAIL("mcp_mock_echo");
  }
  if (!realpath(".", root)) {
    capability_matrix_free(&m);
    config_free(&conf);
    mcp_stdio_shutdown_all();
    FAIL("realpath mcp");
  }
  if (neo_dispatch_tool(&conf, root, "mcp_mock_echo", "{\"text\":\"hi\"}", &out, &out_len, 0) != 0) {
    capability_matrix_free(&m);
    config_free(&conf);
    mcp_stdio_shutdown_all();
    FAIL("mcp dispatch");
  }
  if (!out || !strstr(out, "hi")) {
    fprintf(stderr, "FAIL: mcp echo: %s\n", out ? out : "(null)");
    free(out);
    capability_matrix_free(&m);
    config_free(&conf);
    mcp_stdio_shutdown_all();
    return 1;
  }
  free(out);
  capability_matrix_free(&m);
  config_free(&conf);
  mcp_stdio_shutdown_all();
  return 0;
}

static int test_cap_dir_and_propose(void) {
  capability_matrix_t m;
  agent_config_t conf;
  char root[PATH_MAX];
  char *out = NULL;
  size_t out_len = 0;
  const char *prop = "tests/fixtures/cap_pack/proposed/unit_prop.json5";

  unlink(prop);
  config_init(&conf);
  if (config_load_file(&conf, "tests/fixtures/tools_cap_dir.json5") != 0)
    FAIL("load cap dir fixture");
  if (conf.tools.command_count < 1 || !conf.tools.commands[0].name ||
      strcmp(conf.tools.commands[0].name, "dir_echo") != 0) {
    config_free(&conf);
    FAIL("directory command not loaded");
  }
  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) {
    config_free(&conf);
    FAIL("build cap dir matrix");
  }
  if (!capability_matrix_find(&m, "dir_echo")) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("dir_echo missing from matrix");
  }
  if (!capability_matrix_find(&m, "propose_capability")) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("propose_capability missing");
  }
  if (!realpath(".", root)) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("realpath");
  }
  if (neo_dispatch_tool(
          &conf, root, "propose_capability",
          "{\"name\":\"unit_prop\",\"description\":\"unit proposed\",\"argv\":[\"./tests/fixtures/bin/"
          "echo-argv.sh\"]}",
          &out, &out_len, 0) != 0) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("propose dispatch");
  }
  if (!out || !strstr(out, "proposed:") || !strstr(out, "Not loaded")) {
    fprintf(stderr, "FAIL: propose msg: %s\n", out ? out : "(null)");
    free(out);
    capability_matrix_free(&m);
    config_free(&conf);
    return 1;
  }
  free(out);
  if (access(prop, R_OK) != 0) {
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("proposed file missing");
  }
  /* proposed must not appear in matrix this session */
  if (capability_matrix_find(&m, "unit_prop")) {
    unlink(prop);
    capability_matrix_free(&m);
    config_free(&conf);
    FAIL("proposed should not be live");
  }
  unlink(prop);
  capability_matrix_free(&m);
  config_free(&conf);
  return 0;
}

static int test_memory_add_row_and_dispatch(void) {
  agent_config_t conf;
  capability_matrix_t m;
  char *out = NULL;
  size_t out_len = 0;
  char store[] = "tests/fixtures/.tmp_memory_add.vdb";
  char tp[512];

  unlink(store);
  snprintf(tp, sizeof(tp), "%s.txts", store);
  unlink(tp);

  config_init(&conf);
  conf.tools.enabled = 1;
  conf.tools.root = strdup(".");
  conf.memory.vector_enabled = 1;
  conf.memory.vector_store = strdup(store);
  conf.memory.vector_dims = 32;
  conf.memory.vector_top_k = 3;

  capability_matrix_init(&m);
  if (capability_matrix_build_from_config(&m, &conf) != 0) FAIL("build");
  if (!capability_matrix_find(&m, "memory_add")) FAIL("memory_add missing");
  capability_matrix_free(&m);

  if (neo_dispatch_tool(&conf, ".", "memory_add", "{\"text\":\"remember dark mode prefs\"}", &out,
                        &out_len, 0) != 0)
    FAIL("dispatch");
  if (!out || !strstr(out, "OK")) {
    free(out);
    FAIL("dispatch result");
  }
  free(out);
  free(conf.tools.root);
  free(conf.memory.vector_store);
  unlink(store);
  unlink(tp);
  return 0;
}

int main(void) {
  if (test_empty_and_basics()) return 1;
  if (test_build_schema_fixture()) return 1;
  if (test_matrix_disabled()) return 1;
  if (test_http_policy()) return 1;
  if (test_add_mcp_row()) return 1;
  if (test_dispatch_builtins()) return 1;
  if (test_shell_and_mcp()) return 1;
  if (test_cap_dir_and_propose()) return 1;
  if (test_memory_add_row_and_dispatch()) return 1;
  printf("ok\n");
  return 0;
}
