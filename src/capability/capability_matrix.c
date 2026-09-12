#include "capability_matrix.h"
#include "mcp_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 本文件：矩阵行生命周期、OpenAI tools JSON 发射、从 conf 物化 builtin/commands/MCP。 */

static const char *DEFAULT_PARAMS = "{\"type\":\"object\"}";

static const char *PARAMS_READ =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":"
    "\"Relative path under workspace root\"}},\"required\":[\"path\"]}";
static const char *PARAMS_WRITE =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},"
    "\"required\":[\"path\",\"content\"]}";
static const char *PARAMS_LIST =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":"
    "\"Relative directory; use . for workspace root\"}},\"required\":[\"path\"]}";
static const char *PARAMS_HTTP =
    "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}";
static const char *PARAMS_GREP =
    "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},"
    "\"glob\":{\"type\":\"string\"}},\"required\":[\"pattern\"]}";
static const char *PARAMS_RUN =
    "{\"type\":\"object\",\"properties\":{\"argv\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
    "\"description\":\"Executable relative to capability_matrix.root, then args (no shell)\"}},\"required\":[\"argv\"]}";
static const char *PARAMS_STAT =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":"
    "\"Relative path under workspace root\"}},\"required\":[\"path\"]}";
static const char *PARAMS_MKDIR =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"parents\":{\"type\":\"boolean\","
    "\"description\":\"Create intermediate directories (default false)\"}},\"required\":[\"path\"]}";
static const char *PARAMS_APPEND =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},"
    "\"required\":[\"path\",\"content\"]}";
static const char *PARAMS_PROPOSE =
    "{\"type\":\"object\",\"properties\":{"
    "\"name\":{\"type\":\"string\",\"description\":\"Capability id (letters, digits, underscore)\"},"
    "\"description\":{\"type\":\"string\",\"description\":\"What it does and produces\"},"
    "\"when\":{\"type\":\"string\",\"description\":\"When the model should choose this capability\"},"
    "\"when_not\":{\"type\":\"string\",\"description\":\"When to avoid this capability\"},"
    "\"tags\":{\"type\":\"string\",\"description\":\"Optional coarse tags\"},"
    "\"outcome\":{\"type\":\"string\",\"description\":\"What success looks like\"},"
    "\"argv\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
    "\"description\":\"Executable relative to root, then args\"},"
    "\"parameters\":{\"type\":\"object\"},"
    "\"timeout_sec\":{\"type\":\"integer\"},"
    "\"pass_args\":{\"type\":\"string\"}},"
    "\"required\":[\"name\",\"description\",\"argv\"]}";
static const char *PARAMS_MEMORY_ADD =
    "{\"type\":\"object\",\"properties\":{"
    "\"text\":{\"type\":\"string\",\"description\":\"Fact or preference to remember\"}},"
    "\"required\":[\"text\"]}";

void capability_matrix_init(capability_matrix_t *m) {
  if (!m) return;
  m->rows = NULL;
  m->count = 0;
  m->cap = 0;
}

static void free_row(cap_row_t *r) {
  if (!r) return;
  free(r->name);
  free(r->source_detail);
  free(r->description);
  free(r->when);
  free(r->when_not);
  free(r->tags);
  free(r->parameters_json);
  free(r->mcp_server);
  free(r->mcp_tool);
  memset(r, 0, sizeof(*r));
}

void capability_matrix_free(capability_matrix_t *m) {
  int i;
  if (!m) return;
  for (i = 0; i < m->count; i++) free_row(&m->rows[i]);
  free(m->rows);
  m->rows = NULL;
  m->count = 0;
  m->cap = 0;
}

const cap_row_t *capability_matrix_find(const capability_matrix_t *m, const char *name) {
  int i;
  if (!m || !name) return NULL;
  for (i = 0; i < m->count; i++) {
    if (m->rows[i].enabled && m->rows[i].name && strcmp(m->rows[i].name, name) == 0)
      return &m->rows[i];
  }
  return NULL;
}

static char *dup_s(const char *s) {
  size_t n;
  char *p;
  if (!s) return NULL;
  n = strlen(s) + 1;
  p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

static int ensure_cap(capability_matrix_t *m) {
  cap_row_t *np;
  int ncap;
  if (m->count < m->cap) return 0;
  ncap = m->cap ? m->cap * 2 : 16;
  np = realloc(m->rows, (size_t)ncap * sizeof(cap_row_t));
  if (!np) return -1;
  m->rows = np;
  m->cap = ncap;
  return 0;
}

/* 把选型元数据折进 OpenAI function.description / listing。 */
static char *enrich_desc(const char *base, const char *when, const char *when_not, const char *tags,
                         const char *outcome) {
  char buf[2048];
  if (!when && !when_not && !tags && !outcome) return dup_s(base ? base : "");
  snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s%s", base ? base : "", when ? " When: " : "",
           when ? when : "", when_not ? " Avoid: " : "", when_not ? when_not : "",
           tags ? " Tags: " : "", tags ? tags : "", outcome ? " Outcome: " : "",
           outcome ? outcome : "");
  return dup_s(buf);
}

static int add_row(capability_matrix_t *m, const char *name, cap_source_t src, const char *desc,
                   const char *params, cap_effect_t effect, int builtin_id,
                   const tool_command_t *cmd, const char *when, const char *when_not,
                   const char *tags, const char *outcome) {
  cap_row_t *r;
  const char *use_when = when;
  const char *use_when_not = when_not;
  const char *use_tags = tags;
  const char *use_outcome = outcome;
  if (cmd) {
    if (cmd->when) use_when = cmd->when;
    if (cmd->when_not) use_when_not = cmd->when_not;
    if (cmd->tags) use_tags = cmd->tags;
    if (cmd->outcome) use_outcome = cmd->outcome;
  }
  if (ensure_cap(m) != 0) return -1;
  r = &m->rows[m->count];
  memset(r, 0, sizeof(*r));
  r->name = dup_s(name);
  r->when = use_when ? dup_s(use_when) : NULL;
  r->when_not = use_when_not ? dup_s(use_when_not) : NULL;
  r->tags = use_tags ? dup_s(use_tags) : NULL;
  r->description =
      enrich_desc(desc ? desc : name, use_when, use_when_not, use_tags, use_outcome);
  r->parameters_json = dup_s(params && params[0] ? params : DEFAULT_PARAMS);
  r->source = src;
  r->effect = effect;
  r->enabled = 1;
  r->builtin_id = builtin_id;
  r->command = cmd;
  if (cmd) {
    r->timeout_sec = cmd->timeout_sec;
    r->max_output_bytes = cmd->max_output_bytes;
  }
  if (!r->name || !r->description || !r->parameters_json) {
    free_row(r);
    return -1;
  }
  m->count++;
  return 0;
}

int capability_matrix_build_from_config(capability_matrix_t *m, const agent_config_t *conf) {
  /* 清空后按 Policy 注册：路径 builtin 常驻；run_command/http_get 看开关；
   * commands 来自配置+目录加载；最后 mcp_stdio_load_into_matrix。 */
  int i;
  if (!m) return -1;
  capability_matrix_free(m);
  capability_matrix_init(m);
  if (!conf || !conf->tools.enabled) return 0;

  if (add_row(m, "read_file", CAP_SRC_BUILTIN,
              "Read a UTF-8 text file under the workspace root.", PARAMS_READ, CAP_EFFECT_READ,
              CAP_BUILTIN_READ_FILE, NULL, "Need file contents by relative path.",
              "Do not use for directory listings (list_dir) or search (grep).", "fs,read",
              "File text or ERROR.") != 0)
    return -1;
  if (add_row(m, "write_file", CAP_SRC_BUILTIN,
              "Write content to a file under the workspace root (create/overwrite).", PARAMS_WRITE,
              CAP_EFFECT_WRITE, CAP_BUILTIN_WRITE_FILE, NULL, "Create or fully replace a file.",
              "Do not use to append (append_file) or create dirs only (mkdir).", "fs,write",
              "OK or ERROR.") != 0)
    return -1;
  if (add_row(m, "list_dir", CAP_SRC_BUILTIN,
              "List names of files and subdirectories at a path under the workspace root (non-recursive).",
              PARAMS_LIST, CAP_EFFECT_READ, CAP_BUILTIN_LIST_DIR, NULL,
              "Need names in one directory level.", "Do not use for file contents or recursive trees.",
              "fs,read", "Newline-separated names.") != 0)
    return -1;
  if (add_row(m, "grep", CAP_SRC_BUILTIN,
              "Search for a regex-like substring in files under the workspace root (literal match).",
              PARAMS_GREP, CAP_EFFECT_READ, CAP_BUILTIN_GREP, NULL,
              "Find which files contain a pattern.", "Do not use to read a known single file.",
              "fs,search", "Matching lines or paths.") != 0)
    return -1;
  if (add_row(m, "stat", CAP_SRC_BUILTIN,
              "Return type/size/mtime for a path under the workspace root.", PARAMS_STAT,
              CAP_EFFECT_READ, CAP_BUILTIN_STAT, NULL, "Need metadata without reading body.",
              "Do not use when you need file contents.", "fs,read", "type/size/mtime text.") != 0)
    return -1;
  if (add_row(m, "mkdir", CAP_SRC_BUILTIN,
              "Create a directory under the workspace root. Optional parents=true for mkdir -p.",
              PARAMS_MKDIR, CAP_EFFECT_WRITE, CAP_BUILTIN_MKDIR, NULL, "Need a directory to exist.",
              "Do not use to write file contents.", "fs,write", "OK or ERROR.") != 0)
    return -1;
  if (add_row(m, "append_file", CAP_SRC_BUILTIN,
              "Append UTF-8 text to a file under the workspace root (create if missing).",
              PARAMS_APPEND, CAP_EFFECT_WRITE, CAP_BUILTIN_APPEND_FILE, NULL,
              "Add to end of an existing or new file.", "Do not use to overwrite whole file.",
              "fs,write", "OK or ERROR.") != 0)
    return -1;

  if (conf->tools.directory && conf->tools.directory[0]) {
    if (add_row(m, "propose_capability", CAP_SRC_BUILTIN,
                "Propose a new command capability as a JSON5 file under directory/proposed/ "
                "(not loaded until moved to a load subdir and neo restarts).",
                PARAMS_PROPOSE, CAP_EFFECT_WRITE, CAP_BUILTIN_PROPOSE_CAPABILITY, NULL,
                "Need a new external command capability that is missing from the matrix.",
                "Do not use for one-off shell; drafts are not hot-loaded.", "meta,propose",
                "Path of proposed JSON5 + reminder to move+restart.") != 0)
      return -1;
  }

  /* 仅本地向量记忆开启时暴露；写入 vdb，不写 MEMORY.md */
  if (conf->memory.vector_enabled) {
    if (add_row(m, "memory_add", CAP_SRC_BUILTIN,
                "Store a short fact or preference into Neo local vector memory (not MEMORY.md).",
                PARAMS_MEMORY_ADD, CAP_EFFECT_WRITE, CAP_BUILTIN_MEMORY_ADD, NULL,
                "User asked to remember something, or a durable preference/fact should persist.",
                "Do not store secrets, one-off task chatter, or huge dumps.", "memory",
                "OK with chunk count, or ERROR.") != 0)
      return -1;
  }

  if (conf->tools.shell_enabled) {
    if (add_row(m, "run_command", CAP_SRC_BUILTIN,
                "Run an allowlisted argv under tools.root (no shell). Requires tools.shell_enabled.",
                PARAMS_RUN, CAP_EFFECT_EXEC, CAP_BUILTIN_RUN_COMMAND, NULL,
                "Need a one-off argv under root when no named command fits.",
                "Prefer named commands when available; no shell metacharacters.", "exec",
                "Command stdout/stderr text.") != 0)
      return -1;
  }

  if (conf->tools.http_fetch_enabled && conf->tools.http_allow_hosts &&
      conf->tools.http_allow_hosts[0]) {
    if (add_row(m, "http_get", CAP_SRC_BUILTIN,
                "HTTPS GET for allowlisted hosts only. No redirects. Body truncated.", PARAMS_HTTP,
                CAP_EFFECT_NETWORK, CAP_BUILTIN_HTTP_GET, NULL,
                "Fetch HTTPS body from an allowlisted host.",
                "Do not use for non-HTTPS or non-allowlisted hosts.", "network,http",
                "Response body text (may truncate).") != 0)
      return -1;
  }

  for (i = 0; i < conf->tools.command_count; i++) {
    const tool_command_t *cmd = &conf->tools.commands[i];
    const char *desc;
    const char *params;
    if (!cmd->name || !cmd->name[0]) continue;
    if (capability_matrix_find(m, cmd->name)) {
      fprintf(stderr, "neo: capability matrix: skip duplicate name '%s'\n", cmd->name);
      continue;
    }
    desc = (cmd->description && cmd->description[0]) ? cmd->description : cmd->name;
    params = (cmd->parameters_json && cmd->parameters_json[0]) ? cmd->parameters_json : DEFAULT_PARAMS;
    if (add_row(m, cmd->name, CAP_SRC_COMMAND, desc, params, CAP_EFFECT_EXEC, CAP_BUILTIN_NONE, cmd,
                NULL, NULL, NULL, NULL) != 0)
      return -1;
  }

  mcp_stdio_load_into_matrix(conf, m);
  return 0;
}

static int buf_grow(char **buf, size_t *cap, size_t need) {
  char *nbuf;
  size_t ncap = *cap ? *cap : 256;
  while (ncap < need) ncap *= 2;
  if (ncap == *cap) return 0;
  nbuf = realloc(*buf, ncap);
  if (!nbuf) return -1;
  *buf = nbuf;
  *cap = ncap;
  return 0;
}

static int buf_append(char **buf, size_t *len, size_t *cap, const char *s) {
  size_t n;
  if (!s) s = "";
  n = strlen(s);
  if (buf_grow(buf, cap, *len + n + 1) != 0) return -1;
  memcpy(*buf + *len, s, n + 1);
  *len += n;
  return 0;
}

static int buf_append_json_str(char **buf, size_t *len, size_t *cap, const char *s) {
  size_t i;
  if (buf_append(buf, len, cap, "\"") != 0) return -1;
  if (!s) s = "";
  for (i = 0; s[i]; i++) {
    char tmp[8];
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') {
      tmp[0] = '\\';
      tmp[1] = (char)c;
      tmp[2] = '\0';
      if (buf_append(buf, len, cap, tmp) != 0) return -1;
    } else if (c < 0x20) {
      snprintf(tmp, sizeof(tmp), "\\u%04x", c);
      if (buf_append(buf, len, cap, tmp) != 0) return -1;
    } else {
      tmp[0] = (char)c;
      tmp[1] = '\0';
      if (buf_append(buf, len, cap, tmp) != 0) return -1;
    }
  }
  return buf_append(buf, len, cap, "\"");
}

char *capability_matrix_tools_json(const capability_matrix_t *m) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  int i, first = 1;
  if (buf_append(&buf, &len, &cap, "[") != 0) {
    free(buf);
    return NULL;
  }
  if (m) {
    for (i = 0; i < m->count; i++) {
      const cap_row_t *r = &m->rows[i];
      if (!r->enabled || !r->name) continue;
      if (!first && buf_append(&buf, &len, &cap, ",") != 0) {
        free(buf);
        return NULL;
      }
      first = 0;
      if (buf_append(&buf, &len, &cap, "{\"type\":\"function\",\"function\":{\"name\":") != 0 ||
          buf_append_json_str(&buf, &len, &cap, r->name) != 0 ||
          buf_append(&buf, &len, &cap, ",\"description\":") != 0 ||
          buf_append_json_str(&buf, &len, &cap, r->description ? r->description : "") != 0 ||
          buf_append(&buf, &len, &cap, ",\"parameters\":") != 0 ||
          buf_append(&buf, &len, &cap,
                     r->parameters_json && r->parameters_json[0] ? r->parameters_json : DEFAULT_PARAMS) !=
              0 ||
          buf_append(&buf, &len, &cap, "}}") != 0) {
        free(buf);
        return NULL;
      }
    }
  }
  if (buf_append(&buf, &len, &cap, "]") != 0) {
    free(buf);
    return NULL;
  }
  return buf;
}

static const char *src_label(cap_source_t s) {
  switch (s) {
    case CAP_SRC_BUILTIN: return "builtin";
    case CAP_SRC_COMMAND: return "command";
    case CAP_SRC_MCP: return "mcp";
    default: return "?";
  }
}

static const char *effect_label(cap_effect_t e) {
  switch (e) {
    case CAP_EFFECT_READ: return "read";
    case CAP_EFFECT_WRITE: return "write";
    case CAP_EFFECT_EXEC: return "exec";
    case CAP_EFFECT_NETWORK: return "network";
    default: return "?";
  }
}

char *capability_matrix_prompt_listing(const capability_matrix_t *m) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  int i, any = 0;
  char line[1536];
  if (!m || m->count == 0) return dup_s("(none)\n");
  for (i = 0; i < m->count; i++) {
    const cap_row_t *r = &m->rows[i];
    if (!r->enabled || !r->name) continue;
    any = 1;
    /* description 已含 When/Avoid；再单列 when/when_not 便于扫描选型。 */
    snprintf(line, sizeof(line), "- %s (%s, %s): %s\n", r->name, src_label(r->source),
             effect_label(r->effect), r->description ? r->description : "");
    if (buf_append(&buf, &len, &cap, line) != 0) {
      free(buf);
      return NULL;
    }
    if (r->when) {
      snprintf(line, sizeof(line), "  when: %s\n", r->when);
      if (buf_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
    if (r->when_not) {
      snprintf(line, sizeof(line), "  when_not: %s\n", r->when_not);
      if (buf_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
    if (r->tags) {
      snprintf(line, sizeof(line), "  tags: %s\n", r->tags);
      if (buf_append(&buf, &len, &cap, line) != 0) {
        free(buf);
        return NULL;
      }
    }
  }
  if (!any) {
    free(buf);
    return dup_s("(none)\n");
  }
  return buf;
}

int capability_warn_tool_truncation(size_t n_calls, int max_per_turn, FILE *err) {
  if (!err || max_per_turn < 1) return 0;
  if (n_calls <= (size_t)max_per_turn) return 0;
  fprintf(err, "neo: tool_calls truncated to %d (got %zu)\n", max_per_turn, n_calls);
  return 1;
}

int capability_matrix_add_mcp(capability_matrix_t *m, const char *name, const char *description,
                              const char *parameters_json, const char *mcp_server,
                              const char *mcp_tool) {
  cap_row_t *r;
  if (!m || !name || !mcp_server || !mcp_tool) return -1;
  if (capability_matrix_find(m, name)) {
    fprintf(stderr, "neo: capability matrix: skip duplicate MCP name '%s'\n", name);
    return 0;
  }
  if (ensure_cap(m) != 0) return -1;
  r = &m->rows[m->count];
  memset(r, 0, sizeof(*r));
  r->name = dup_s(name);
  r->description = dup_s(description ? description : name);
  r->parameters_json = dup_s(parameters_json && parameters_json[0] ? parameters_json : DEFAULT_PARAMS);
  r->source = CAP_SRC_MCP;
  r->source_detail = dup_s(mcp_server);
  r->mcp_server = dup_s(mcp_server);
  r->mcp_tool = dup_s(mcp_tool);
  r->effect = CAP_EFFECT_EXEC;
  r->enabled = 1;
  r->builtin_id = CAP_BUILTIN_NONE;
  if (!r->name || !r->description || !r->parameters_json || !r->mcp_server || !r->mcp_tool) {
    free_row(r);
    return -1;
  }
  m->count++;
  return 0;
}
