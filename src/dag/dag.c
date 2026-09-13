/*
 * 确定性 DAG runner：tool、llm、loop、route。
 * 有 depends_on 或 route 时走拓扑排序；route 未选中分支 skip，汇合步按「依赖是否全 skip」决定。
 */
#include "dag.h"
#include "agent_tools.h"
#include "capability_matrix.h"
#include "llm.h"
#include "neo_events.h"
#include "yyjson.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

#define DAG_MAX_STEPS 32

typedef struct {
  char *id;
  char *text;
} dag_out_map_t;

static size_t utf8_prefix_len(const char *s, size_t max);

static int dag_is_word_char(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/* 大小写不敏感整词查找。 */
static int dag_find_word_ci(const char *hay, const char *needle) {
  size_t nlen;
  const char *p;
  if (!hay || !needle || !needle[0]) return 0;
  nlen = strlen(needle);
  for (p = hay; *p; p++) {
    size_t i;
    for (i = 0; i < nlen; i++) {
      unsigned char a = (unsigned char)p[i];
      unsigned char b = (unsigned char)needle[i];
      if (!a) return 0;
      if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
      if (a != b) break;
    }
    if (i == nlen) {
      unsigned char before = (p == hay) ? 0 : (unsigned char)p[-1];
      unsigned char after = (unsigned char)p[nlen];
      if (!dag_is_word_char(before) && !dag_is_word_char(after)) return 1;
    }
  }
  return 0;
}

int dag_parse_fail_llm_reply(const char *text) {
  if (!text || !text[0]) return 0;
  /* ABORT 优先，避免「retry then abort」误成 RETRY */
  if (dag_find_word_ci(text, "abort")) return 0;
  if (dag_find_word_ci(text, "retry")) return 1;
  return 0;
}

/* 拼步骤类型摘要，供 -v / 失败路径 stderr 可读（如 type=tool tool=date_iso）。 */
static void dag_step_kind(const dag_step_t *st, char *buf, size_t buf_sz) {
  if (!buf || buf_sz < 8) return;
  buf[0] = '\0';
  if (!st) {
    snprintf(buf, buf_sz, "type=?");
    return;
  }
  switch (st->type) {
    case DAG_STEP_TOOL:
      snprintf(buf, buf_sz, "type=tool tool=%s", st->tool && st->tool[0] ? st->tool : "?");
      break;
    case DAG_STEP_LLM:
      snprintf(buf, buf_sz, "type=llm tools=%s", st->tools_on ? "on" : "off");
      break;
    case DAG_STEP_LOOP:
      snprintf(buf, buf_sz, "type=loop");
      break;
    case DAG_STEP_ROUTE:
      snprintf(buf, buf_sz, "type=route");
      break;
    default:
      snprintf(buf, buf_sz, "type=?");
      break;
  }
}

static void dag_map_free(dag_out_map_t *m, int n) {
  int i;
  for (i = 0; i < n; i++) {
    free(m[i].id);
    free(m[i].text);
  }
}

static void dag_map_set(dag_out_map_t *m, int *n, const char *id, const char *text) {
  int i;
  for (i = 0; i < *n; i++) {
    if (m[i].id && id && strcmp(m[i].id, id) == 0) {
      free(m[i].text);
      m[i].text = text ? strdup(text) : strdup("");
      return;
    }
  }
  if (*n >= DAG_MAX_STEPS) return;
  m[*n].id = id ? strdup(id) : NULL;
  m[*n].text = text ? strdup(text) : strdup("");
  (*n)++;
}

char *dag_expand_template(const char *tmpl, const char *prev,
                               const char **ids, const char **texts, int n_maps) {
  size_t cap = tmpl ? strlen(tmpl) + 64 : 64;
  size_t len = 0;
  char *out;
  const char *p;
  if (!tmpl) return NULL;
  out = malloc(cap);
  if (!out) return NULL;
  out[0] = '\0';
  p = tmpl;
  while (*p) {
    if (p[0] == '{' && p[1] == '{') {
      const char *end = strstr(p + 2, "}}");
      char key[128];
      size_t klen;
      const char *rep = NULL;
      if (!end) {
        free(out);
        return NULL;
      }
      klen = (size_t)(end - (p + 2));
      if (klen >= sizeof(key)) {
        free(out);
        return NULL;
      }
      memcpy(key, p + 2, klen);
      key[klen] = '\0';
      if (strcmp(key, "prev") == 0) {
        rep = prev ? prev : "";
      } else if (strncmp(key, "steps.", 6) == 0) {
        const char *sid = key + 6;
        int i;
        rep = ""; /* skipped / not-yet-run steps expand to empty */
        for (i = 0; i < n_maps; i++) {
          if (ids[i] && strcmp(ids[i], sid) == 0) {
            rep = texts[i] ? texts[i] : "";
            break;
          }
        }
      } else {
        free(out);
        return NULL;
      }
      {
        size_t rlen = strlen(rep);
        if (len + rlen + 1 > cap) {
          size_t ncap = (len + rlen + 1) * 2;
          char *nb = realloc(out, ncap);
          if (!nb) {
            free(out);
            return NULL;
          }
          out = nb;
          cap = ncap;
        }
        memcpy(out + len, rep, rlen);
        len += rlen;
        out[len] = '\0';
      }
      p = end + 2;
      continue;
    }
    if (len + 2 > cap) {
      size_t ncap = cap * 2;
      char *nb = realloc(out, ncap);
      if (!nb) {
        free(out);
        return NULL;
      }
      out = nb;
      cap = ncap;
    }
    out[len++] = *p++;
    out[len] = '\0';
  }
  return out;
}

static int dag_run_step(const agent_config_t *conf, const char *root_real, const char *dag_name,
                       const dag_t *wf, const dag_step_t *st,
                       dag_out_map_t *map, int *map_n, char **prev_io);

static int dag_run_tool_step(const agent_config_t *conf, const char *root_real, const char *dag_name,
                            const dag_step_t *st, dag_out_map_t *map, int *map_n, char **prev_io) {
  char *out = NULL;
  size_t out_len = 0;
  const char *args_tmpl = (st->args_json && st->args_json[0]) ? st->args_json : "{}";
  char *args = NULL;
  const char *ids[DAG_MAX_STEPS];
  const char *texts[DAG_MAX_STEPS];
  int i;
  int attempts = 1 + (st->retry_max > 0 ? st->retry_max : 0);
  int attempt;
  int last_fail = -1;

  for (i = 0; i < *map_n; i++) {
    ids[i] = map[i].id;
    texts[i] = map[i].text;
  }
  /* tool args 也支持 {{steps.id}} / {{prev}}，便于把上一步输出写入 path/content 等字段。 */
  args = dag_expand_template(args_tmpl, *prev_io, ids, texts, *map_n);
  if (!args) {
    fprintf(stderr, "neo: DAG:%s step:%s: args template expand failed\n", dag_name, st->id);
    return -1;
  }
  if (!conf->tools.enabled) {
    fprintf(stderr, "neo: DAG:%s step:%s: tools disabled\n", dag_name, st->id);
    free(args);
    return -1;
  }

  for (attempt = 0; attempt < attempts; attempt++) {
    free(out);
    out = NULL;
    out_len = 0;
    if (attempt > 0)
      fprintf(stderr, "neo: DAG:%s step:%s: retry attempt %d/%d\n", dag_name,
              st->id ? st->id : "?", attempt + 1, attempts);
    if (neo_dispatch_tool(conf, root_real, st->tool, args, &out, &out_len,
                          st->timeout_sec) != 0) {
      fprintf(stderr, "neo: DAG:%s step:%s: tool dispatch failed\n", dag_name, st->id);
      last_fail = -1;
      continue;
    }
    if (out && strncmp(out, "ERROR:", 6) == 0) {
      fprintf(stderr, "neo: DAG:%s step:%s: %s\n", dag_name, st->id, out);
      last_fail = -1;
      continue;
    }
    if (out && strncmp(out, "EXIT:", 5) == 0) {
      fprintf(stderr, "neo: DAG:%s step:%s: tool non-zero exit\n", dag_name, st->id);
      last_fail = -1;
      continue;
    }
    free(args);
    dag_map_set(map, map_n, st->id, out ? out : "");
    free(*prev_io);
    *prev_io = out ? strdup(out) : strdup("");
    free(out);
    return 0;
  }

  /* 本地 retry 耗尽：可选 LLM 热线（只答 RETRY/ABORT，不改图、不改 args） */
  if (conf->dag_runtime.on_tool_fail_llm && conf->dag_runtime.on_tool_fail_max_calls > 0) {
    int llm_i, max_llm = conf->dag_runtime.on_tool_fail_max_calls;
    const char *sys =
        "You are Neo's tool-failure hotline. Decide whether to RETRY the same tool "
        "with the same arguments, or ABORT the DAG. Reply with a short reason, then "
        "a final line that is exactly RETRY or ABORT. Do not call tools.";
    for (llm_i = 0; llm_i < max_llm; llm_i++) {
      char user_buf[8192];
      size_t err_take;
      llm_response_t resp;
      int action;
      const char *err = out ? out : "(no output)";
      err_take = utf8_prefix_len(err, 1500);
      snprintf(user_buf, sizeof(user_buf),
               "DAG=%s\nstep=%s\ntool=%s\nlocal_attempts=%d\nerror:\n%.*s\n",
               dag_name ? dag_name : "?", st->id ? st->id : "?",
               st->tool ? st->tool : "?", attempts, (int)err_take, err);
      memset(&resp, 0, sizeof(resp));
      if (llm_chat(conf->model.base_url, conf->model.name, conf->model.api_key,
                   conf->model.max_tokens > 256 ? 256 : conf->model.max_tokens,
                   conf->model.temperature, sys, user_buf, &resp) != 0) {
        fprintf(stderr, "neo: DAG:%s step:%s: on_fail_llm call=%d/%d failed → ABORT\n",
                dag_name, st->id ? st->id : "?", llm_i + 1, max_llm);
        llm_response_free(&resp);
        break;
      }
      action = dag_parse_fail_llm_reply(resp.data);
      fprintf(stderr, "neo: DAG:%s step:%s: on_fail_llm call=%d/%d action=%s\n",
              dag_name, st->id ? st->id : "?", llm_i + 1, max_llm,
              action ? "RETRY" : "ABORT");
      llm_response_free(&resp);
      if (!action) break;

      free(out);
      out = NULL;
      out_len = 0;
      if (neo_dispatch_tool(conf, root_real, st->tool, args, &out, &out_len,
                          st->timeout_sec) != 0) {
        fprintf(stderr, "neo: DAG:%s step:%s: tool dispatch failed after LLM RETRY\n",
                dag_name, st->id);
        last_fail = -1;
        continue;
      }
      if (out && (strncmp(out, "ERROR:", 6) == 0 || strncmp(out, "EXIT:", 5) == 0)) {
        fprintf(stderr, "neo: DAG:%s step:%s: tool still failing after LLM RETRY\n",
                dag_name, st->id);
        last_fail = -1;
        continue;
      }
      free(args);
      dag_map_set(map, map_n, st->id, out ? out : "");
      free(*prev_io);
      *prev_io = out ? strdup(out) : strdup("");
      free(out);
      return 0;
    }
  }

  free(args);
  free(out);
  return last_fail;
}

/* 多路 cases：返回选中下标；无 cases 返回 -1；未命中且无默认返回 -1。 */
static int dag_route_select_case(const dag_step_t *st, const char *expanded) {
  int i, def = -1;
  if (!st || st->route_case_count < 1) return -1;
  for (i = 0; i < st->route_case_count; i++) {
    const char *m = st->route_cases[i].match;
    if (!m || !m[0]) {
      def = i;
      continue;
    }
    if (expanded && strstr(expanded, m)) return i;
  }
  return def;
}

/* Max byte length ending on a UTF-8 boundary. */
static size_t utf8_prefix_len(const char *s, size_t max) {
  size_t i, n;
  if (!s || max == 0) return 0;
  n = strlen(s);
  if (n <= max) return n;
  i = max;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}

static char *dag_append_prior_outputs(char *prompt, const dag_out_map_t *map, int map_n) {
  size_t cap, len, i;
  size_t prior_budget = 20000; /* keep prompts bounded for LLM providers */
  char *out;
  if (!prompt) return NULL;
  if (map_n <= 0) return prompt;
  len = strlen(prompt);
  cap = len + prior_budget + 256;
  if (cap > 120000) cap = 120000;
  out = realloc(prompt, cap);
  if (!out) return prompt;
  prompt = out;
  len = strlen(prompt);
  if (len + 80 < cap) {
    snprintf(prompt + len, cap - len,
             "\n\n---\nPrior step outputs (already available — do NOT ask the user to paste them):\n");
    len = strlen(prompt);
  }
  for (i = 0; i < (size_t)map_n; i++) {
    size_t idl, tl, room, take, max_take;
    if (!map[i].id || !map[i].text || !map[i].text[0]) continue;
    if (strcmp(map[i].text, "then") == 0 || strcmp(map[i].text, "else") == 0) continue;
    idl = strlen(map[i].id);
    tl = strlen(map[i].text);
    /* Skip if template expand already inlined this output */
    if (tl >= 48) {
      char tip[64];
      size_t tip_n = utf8_prefix_len(map[i].text, 48);
      if (tip_n >= 24) {
        memcpy(tip, map[i].text, tip_n);
        tip[tip_n] = '\0';
        if (strstr(prompt, tip)) continue;
      }
    }
    room = (len + 1 < cap) ? (cap - len - 1) : 0;
    if (room < idl + 32) break;
    max_take = 6000;
    if (prior_budget < max_take) max_take = prior_budget;
    take = tl;
    if (take > max_take) take = max_take;
    if (idl + take + 16 > room) take = room > idl + 16 ? room - idl - 16 : 0;
    take = utf8_prefix_len(map[i].text, take);
    if (take == 0 && tl > 0) continue;
    snprintf(prompt + len, cap - len, "\n### %s\n", map[i].id);
    len = strlen(prompt);
    if (take > 0 && len + take < cap) {
      memcpy(prompt + len, map[i].text, take);
      len += take;
      prompt[len] = '\0';
      if (prior_budget > take) prior_budget -= take;
      else prior_budget = 0;
      if (take < tl && len + 24 < cap) {
        snprintf(prompt + len, cap - len, "\n...[truncated]...\n");
        len = strlen(prompt);
      } else if (len + 2 < cap) {
        prompt[len++] = '\n';
        prompt[len] = '\0';
      }
    }
    if (prior_budget < 64) break;
  }
  return prompt;
}

static int dag_run_llm_step(const agent_config_t *conf, const char *dag_name, const dag_step_t *st,
                           dag_out_map_t *map, int *map_n, char **prev_io) {
  const char *ids[DAG_MAX_STEPS];
  const char *texts[DAG_MAX_STEPS];
  char *prompt = NULL;
  llm_response_t resp;
  const char *sys =
      "You are a Neo DAG worker step. Follow the user instruction. "
      "If prior step outputs are included below, treat them as given context and "
      "never ask the human to paste or re-send earlier drafts.";
  int i;
  for (i = 0; i < *map_n; i++) {
    ids[i] = map[i].id;
    texts[i] = map[i].text;
  }
  prompt = dag_expand_template(st->prompt, *prev_io, ids, texts, *map_n);
  if (!prompt) {
    fprintf(stderr, "neo: DAG:%s step:%s: template expand failed\n", dag_name, st->id);
    return -1;
  }
  prompt = dag_append_prior_outputs(prompt, map, *map_n);
  if (st->tools_on) {
    char *sys_tools = strdup(sys);
    if (!sys_tools) {
      free(prompt);
      return -1;
    }
    memset(&resp, 0, sizeof(resp));
    if (agent_run_with_tools(conf, sys_tools, NULL, 0, prompt, &resp) != 0) {
      fprintf(stderr, "neo: DAG:%s step:%s: llm+tools failed\n", dag_name, st->id);
      free(sys_tools);
      free(prompt);
      llm_response_free(&resp);
      return -1;
    }
    free(sys_tools);
  } else {
    memset(&resp, 0, sizeof(resp));
    if (llm_chat(conf->model.base_url, conf->model.name, conf->model.api_key,
                 conf->model.max_tokens, conf->model.temperature, sys, prompt, &resp) != 0) {
      fprintf(stderr, "neo: DAG:%s step:%s: llm failed\n", dag_name, st->id);
      free(prompt);
      llm_response_free(&resp);
      return -1;
    }
  }
  free(prompt);
  dag_map_set(map, map_n, st->id, resp.data ? resp.data : "");
  free(*prev_io);
  *prev_io = resp.data ? strdup(resp.data) : strdup("");
  llm_response_free(&resp);
  return 0;
}

static const dag_step_t *dag_find_step(const dag_t *wf, const char *id) {
  int i;
  for (i = 0; i < wf->step_count; i++) {
    if (wf->steps[i].id && strcmp(wf->steps[i].id, id) == 0) return &wf->steps[i];
  }
  return NULL;
}

static int dag_run_step(const agent_config_t *conf, const char *root_real, const char *dag_name,
                       const dag_t *wf, const dag_step_t *st,
                       dag_out_map_t *map, int *map_n, char **prev_io) {
  int i, iter;
  if (st->type == DAG_STEP_TOOL)
    return dag_run_tool_step(conf, root_real, dag_name, st, map, map_n, prev_io);
  if (st->type == DAG_STEP_LLM)
    return dag_run_llm_step(conf, dag_name, st, map, map_n, prev_io);
  if (st->type == DAG_STEP_LOOP) {
    for (iter = 0; iter < st->max_iters; iter++) {
      for (i = 0; i < st->over_count; i++) {
        const dag_step_t *inner = dag_find_step(wf, st->over_ids[i]);
        if (!inner) {
          fprintf(stderr, "neo: DAG:%s step:%s: missing over id\n", dag_name, st->id);
          return -1;
        }
        if (dag_run_step(conf, root_real, dag_name, wf, inner, map, map_n, prev_io) != 0)
          return -1;
      }
    }
    return 0;
  }
  if (st->type == DAG_STEP_ROUTE) {
    /* route handled by DAG runner; treat as no-op success if called directly */
    return 0;
  }
  fprintf(stderr, "neo: DAG:%s step:%s: unknown type\n", dag_name, st->id);
  return -1;
}

static int dag_index_of(const dag_t *wf, const char *id) {
  int i;
  for (i = 0; i < wf->step_count; i++) {
    if (wf->steps[i].id && id && strcmp(wf->steps[i].id, id) == 0) return i;
  }
  return -1;
}

static int dag_is_graph_mode(const dag_t *wf) {
  /* 任一 depends_on 或 route 步 → DAG 模式；否则保持线性 + loop。 */
  int i;
  for (i = 0; i < wf->step_count; i++) {
    if (wf->steps[i].depends_count > 0) return 1;
    if (wf->steps[i].type == DAG_STEP_ROUTE) return 1;
  }
  return 0;
}

/* Kahn 拓扑排序；遇环返回 -1。order[] 为步骤下标。 */
static int dag_topo_sort(const dag_t *wf, int *order, int *order_n) {
  int n = wf->step_count;
  int indeg[DAG_MAX_STEPS];
  int i, j, left;
  if (n > DAG_MAX_STEPS) return -1;
  memset(indeg, 0, sizeof(indeg));
  for (i = 0; i < n; i++) {
    for (j = 0; j < wf->steps[i].depends_count; j++) {
      if (dag_index_of(wf, wf->steps[i].depends_on[j]) < 0) return -1;
      indeg[i]++;
    }
  }
  *order_n = 0;
  left = n;
  while (left > 0) {
    int picked = -1;
    for (i = 0; i < n; i++) {
      if (indeg[i] == 0) {
        picked = i;
        break;
      }
    }
    if (picked < 0) {
      fprintf(stderr, "neo: DAG:%s: dependency cycle\n", wf->name);
      return -1;
    }
    order[(*order_n)++] = picked;
    indeg[picked] = -1;
    left--;
    for (i = 0; i < n; i++) {
      for (j = 0; j < wf->steps[i].depends_count; j++) {
        int d = dag_index_of(wf, wf->steps[i].depends_on[j]);
        if (d == picked && indeg[i] > 0) indeg[i]--;
      }
    }
  }
  return 0;
}

static void dag_mark_skip_ids(const dag_t *wf, int *skip, char **ids, int nids) {
  int i, idx;
  for (i = 0; i < nids; i++) {
    idx = dag_index_of(wf, ids[i]);
    if (idx >= 0) skip[idx] = 1;
  }
}

static int dag_run_graph(const agent_config_t *conf, const char *root_real, const dag_t *wf,
                      char **out_text, int verbose) {
  /* DAG 执行：拓扑序串行。route 命中则 skip 另一侧分支；
   * 汇合步仅当「全部 depends_on 都被 skip」时才 skip（一侧跑通仍执行）。 */
  int order[DAG_MAX_STEPS];
  int order_n = 0;
  int skip[DAG_MAX_STEPS];
  dag_out_map_t map[DAG_MAX_STEPS];
  int map_n = 0;
  char *prev = NULL;
  int oi, i, j;

  memset(skip, 0, sizeof(skip));
  memset(map, 0, sizeof(map));
  if (dag_topo_sort(wf, order, &order_n) != 0) return -1;

  for (oi = 0; oi < order_n; oi++) {
    int idx = order[oi];
    const dag_step_t *st = &wf->steps[idx];
    int dep_all_skipped = 1;
    int has_dep = 0;

    for (j = 0; j < st->depends_count; j++) {
      int d = dag_index_of(wf, st->depends_on[j]);
      if (d < 0) continue;
      has_dep = 1;
      if (!skip[d]) dep_all_skipped = 0;
    }
    /* 路由汇合：只有依赖全部被 skip 才 skip 本步；任一依赖跑过则继续。 */
    if (has_dep && dep_all_skipped) {
      char kind[96];
      skip[idx] = 1;
      dag_step_kind(st, kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=skip\n", wf->name,
                st->id ? st->id : "?", kind);
      continue;
    }
    if (skip[idx]) {
      char kind[96];
      dag_step_kind(st, kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=skip\n", wf->name,
                st->id ? st->id : "?", kind);
      continue;
    }

    if (st->type == DAG_STEP_ROUTE) {
      const char *ids[DAG_MAX_STEPS];
      const char *texts[DAG_MAX_STEPS];
      char *expanded;
      char kind[96];
      int hit = 0;
      int sel = -1;
      for (i = 0; i < map_n; i++) {
        ids[i] = map[i].id;
        texts[i] = map[i].text;
      }
      expanded = dag_expand_template(st->route_on, prev, ids, texts, map_n);
      if (!expanded) {
        fprintf(stderr, "neo: DAG:%s step:%s: route on expand failed\n", wf->name, st->id);
        dag_map_free(map, map_n);
        free(prev);
        return -1;
      }
      dag_step_kind(st, kind, sizeof(kind));
      if (st->route_case_count > 0) {
        /* 多路 cases：选中一支，skip 其余支的 then[] */
        sel = dag_route_select_case(st, expanded);
        for (i = 0; i < st->route_case_count; i++) {
          if (i == sel) continue;
          dag_mark_skip_ids(wf, skip, st->route_cases[i].then_ids, st->route_cases[i].then_count);
        }
        if (verbose)
          fprintf(stderr, "neo: step end   dag=%s id=%s %s case=%d status=ok\n", wf->name,
                  st->id ? st->id : "?", kind, sel);
        {
          char label[32];
          if (sel >= 0)
            snprintf(label, sizeof(label), "case:%d", sel);
          else
            snprintf(label, sizeof(label), "case:none");
          dag_map_set(map, &map_n, st->id, label);
        }
      } else {
        if (st->route_match && st->route_match[0] && strstr(expanded, st->route_match))
          hit = 1;
        if (verbose)
          fprintf(stderr, "neo: step end   dag=%s id=%s %s hit=%d status=ok\n", wf->name,
                  st->id ? st->id : "?", kind, hit);
        if (hit)
          dag_mark_skip_ids(wf, skip, st->route_else, st->route_else_count);
        else
          dag_mark_skip_ids(wf, skip, st->route_then, st->route_then_count);
        dag_map_set(map, &map_n, st->id, hit ? "then" : "else");
      }
      free(expanded);
      continue;
    }

    {
      char kind[96];
      char detail[160];
      long t0;
      int step_rc;
      dag_step_kind(st, kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step start dag=%s id=%s %s\n", wf->name,
                st->id ? st->id : "?", kind);
      snprintf(detail, sizeof(detail), "%s:%s", wf->name ? wf->name : "?",
               st->id ? st->id : "?");
      t0 = neo_events_now_ms();
      step_rc = dag_run_step(conf, root_real, wf->name, wf, st, map, &map_n, &prev);
      neo_events_emit("dag_step", step_rc == 0, neo_events_now_ms() - t0, detail);
      if (step_rc != 0) {
        /* 失败摘要始终打出，便于无 -v 时定位卡在哪一步。 */
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=fail\n", wf->name,
                st->id ? st->id : "?", kind);
        neo_events_emit("error", 0, 0, detail);
        dag_map_free(map, map_n);
        free(prev);
        return -1;
      }
      if (verbose)
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=ok\n", wf->name,
                st->id ? st->id : "?", kind);
    }
  }

  *out_text = prev ? prev : strdup("");
  dag_map_free(map, map_n);
  return 0;
}

/*
 * 解析 dag->requires（JSON 数组文本或单个能力名），逐个查矩阵。
 * 不做 PATH/模糊匹配；缺省或空串视为无依赖。
 */
int dag_check_requires(const agent_config_t *conf, const char *cmd, const dag_t *dag) {
  capability_matrix_t mx;
  const char *c;
  const char *req;
  const char *dname;
  int n_miss = 0;

  if (!conf || !dag) return -1;
  c = (cmd && cmd[0]) ? cmd : "dag";
  dname = (dag->name && dag->name[0]) ? dag->name : "?";
  req = dag->requires;
  if (!req || !req[0]) return 0;

  capability_matrix_init(&mx);
  if (capability_matrix_build_from_config(&mx, conf) != 0) {
    capability_matrix_free(&mx);
    fprintf(stderr, "neo %s: cannot build Capability Matrix to check requires for DAG '%s'\n",
            c, dname);
    return -1;
  }

  if (req[0] == '[') {
    yyjson_doc *doc = yyjson_read(req, strlen(req), 0);
    yyjson_val *arr, *el;
    size_t idx, max;
    if (!doc || !(arr = yyjson_doc_get_root(doc)) || !yyjson_is_arr(arr)) {
      fprintf(stderr, "neo %s: DAG '%s' has invalid requires (want JSON string array)\n",
              c, dname);
      if (doc) yyjson_doc_free(doc);
      capability_matrix_free(&mx);
      return -1;
    }
    yyjson_arr_foreach(arr, idx, max, el) {
      const char *nm;
      if (!yyjson_is_str(el)) {
        fprintf(stderr, "neo %s: DAG '%s' requires entry must be string\n", c, dname);
        n_miss++;
        continue;
      }
      nm = yyjson_get_str(el);
      if (!nm || !nm[0]) continue;
      if (!capability_matrix_find(&mx, nm)) {
        fprintf(stderr,
                "neo %s: DAG '%s' missing required capability '%s' "
                "(not in Capability Matrix or not enabled)\n",
                c, dname, nm);
        n_miss++;
      }
    }
    yyjson_doc_free(doc);
  } else {
    /* 单名字符串形式 */
    if (!capability_matrix_find(&mx, req)) {
      fprintf(stderr,
              "neo %s: DAG '%s' missing required capability '%s' "
              "(not in Capability Matrix or not enabled)\n",
              c, dname, req);
      n_miss = 1;
    }
  }

  capability_matrix_free(&mx);
  return n_miss;
}

int dag_run(const agent_config_t *conf, const char *dag_name, char **out_text,
                 int verbose) {
  const dag_t *wf;
  char root_real[PATH_MAX];
  const char *root;
  dag_out_map_t map[DAG_MAX_STEPS];
  int map_n = 0;
  char *prev = NULL;
  int i;

  if (out_text) *out_text = NULL;
  if (!conf || !dag_name || !out_text) return -1;
  wf = config_find_dag(conf, dag_name);
  if (!wf) {
    fprintf(stderr, "neo: unknown DAG '%s'\n", dag_name);
    if (conf->dag_count > 0) {
      fprintf(stderr, "neo: available:");
      for (i = 0; i < conf->dag_count; i++)
        fprintf(stderr, " %s", conf->dags[i].name ? conf->dags[i].name : "?");
      fprintf(stderr, "\n");
    }
    return -1;
  }
  /* 执行前预检 requires：缺能力则拒绝跑图（plan/run/dag 共用此入口）。 */
  if (dag_check_requires(conf, "run", wf) != 0) {
    neo_events_emit("error", 0, 0, dag_name);
    return -1;
  }
  neo_events_emit("session_start", 1, 0, dag_name);

#if !defined(__APPLE__) && !defined(__linux__)
  fprintf(stderr, "neo: DAG not supported on this platform\n");
  return -1;
#else
  root = (conf->tools.root && conf->tools.root[0]) ? conf->tools.root : ".";
  if (!realpath(root, root_real)) {
    fprintf(stderr, "neo: tools.root realpath failed\n");
    neo_events_emit("error", 0, 0, "tools.root realpath");
    return -1;
  }

  if (dag_is_graph_mode(wf))
    {
      int r = dag_run_graph(conf, root_real, wf, out_text, verbose);
      if (r == 0)
        fprintf(stderr, "neo: run done dag=%s status=ok\n", wf->name);
      return r;
    }

  memset(map, 0, sizeof(map));

  /* Legacy: steps referenced by a loop's over are definition-only until the loop runs them. */
  for (i = 0; i < wf->step_count; i++) {
    int skip = 0;
    int j, k;
    if (wf->steps[i].type == DAG_STEP_LOOP) {
      char kind[96];
      char detail[160];
      long t0;
      int step_rc;
      dag_step_kind(&wf->steps[i], kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step start dag=%s id=%s %s\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
      snprintf(detail, sizeof(detail), "%s:%s", wf->name ? wf->name : "?",
               wf->steps[i].id ? wf->steps[i].id : "?");
      t0 = neo_events_now_ms();
      step_rc = dag_run_step(conf, root_real, wf->name, wf, &wf->steps[i], map, &map_n, &prev);
      neo_events_emit("dag_step", step_rc == 0, neo_events_now_ms() - t0, detail);
      if (step_rc != 0) {
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=fail\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
        neo_events_emit("error", 0, 0, detail);
        dag_map_free(map, map_n);
        free(prev);
        return -1;
      }
      if (verbose)
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=ok\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
      continue;
    }
    for (j = 0; j < wf->step_count; j++) {
      if (wf->steps[j].type != DAG_STEP_LOOP) continue;
      for (k = 0; k < wf->steps[j].over_count; k++) {
        if (wf->steps[j].over_ids[k] && wf->steps[i].id &&
            strcmp(wf->steps[j].over_ids[k], wf->steps[i].id) == 0)
          skip = 1;
      }
    }
    if (skip) continue;
    {
      char kind[96];
      char detail[160];
      long t0;
      int step_rc;
      dag_step_kind(&wf->steps[i], kind, sizeof(kind));
      if (verbose)
        fprintf(stderr, "neo: step start dag=%s id=%s %s\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
      snprintf(detail, sizeof(detail), "%s:%s", wf->name ? wf->name : "?",
               wf->steps[i].id ? wf->steps[i].id : "?");
      t0 = neo_events_now_ms();
      step_rc = dag_run_step(conf, root_real, wf->name, wf, &wf->steps[i], map, &map_n, &prev);
      neo_events_emit("dag_step", step_rc == 0, neo_events_now_ms() - t0, detail);
      if (step_rc != 0) {
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=fail\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
        neo_events_emit("error", 0, 0, detail);
        dag_map_free(map, map_n);
        free(prev);
        return -1;
      }
      if (verbose)
        fprintf(stderr, "neo: step end   dag=%s id=%s %s status=ok\n", wf->name,
                wf->steps[i].id ? wf->steps[i].id : "?", kind);
    }
  }

  *out_text = prev ? prev : strdup("");
  dag_map_free(map, map_n);
  fprintf(stderr, "neo: run done dag=%s status=ok\n", wf->name);
  return 0;
#endif
}
