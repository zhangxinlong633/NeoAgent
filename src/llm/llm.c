#include "llm.h"
#include "neo_events.h"
#include "neo_http.h"
#include "yyjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Copy in → heap string with only valid UTF-8; drop ASCII controls except \n\t;
 * replace broken sequences with '?'. Caller frees.
 */
static char *utf8_sanitize_dup(const char *in) {
  size_t n, i, j;
  char *out;
  if (!in) in = "";
  n = strlen(in);
  out = malloc(n + 1);
  if (!out) return NULL;
  j = 0;
  for (i = 0; i < n;) {
    unsigned char c = (unsigned char)in[i];
    if (c == '\n' || c == '\t') {
      out[j++] = (char)c;
      i++;
    } else if (c < 0x20 || c == 0x7F) {
      i++;
    } else if (c < 0x80) {
      out[j++] = (char)c;
      i++;
    } else {
      size_t need = 0;
      size_t k;
      int ok = 1;
      if ((c & 0xE0) == 0xC0) need = 2;
      else if ((c & 0xF0) == 0xE0) need = 3;
      else if ((c & 0xF8) == 0xF0) need = 4;
      else {
        out[j++] = '?';
        i++;
        continue;
      }
      if (i + need > n) {
        out[j++] = '?';
        break;
      }
      for (k = 1; k < need; k++) {
        if (((unsigned char)in[i + k] & 0xC0) != 0x80) {
          ok = 0;
          break;
        }
      }
      if (!ok) {
        out[j++] = '?';
        i++;
        continue;
      }
      for (k = 0; k < need; k++) out[j++] = in[i + k];
      i += need;
    }
  }
  out[j] = '\0';
  return out;
}

static yyjson_mut_val *mut_msg(yyjson_mut_doc *doc, const char *role, const char *content) {
  yyjson_mut_val *o;
  char *clean;
  if (!doc || !role) return NULL;
  clean = utf8_sanitize_dup(content ? content : "");
  if (!clean) return NULL;
  o = yyjson_mut_obj(doc);
  if (!o || !yyjson_mut_obj_add_strcpy(doc, o, "role", role) ||
      !yyjson_mut_obj_add_strcpy(doc, o, "content", clean)) {
    free(clean);
    return NULL;
  }
  free(clean);
  return o;
}

/* Build chat/completions body; caller frees *out_json via free(). */
static char *build_chat_body(const char *model, int max_tokens, double temperature,
                             const char *system_prompt, const llm_message_t *messages,
                             int n_messages) {
  yyjson_mut_doc *doc;
  yyjson_mut_val *root, *msgs, *m;
  char *json;
  int i;

  doc = yyjson_mut_doc_new(NULL);
  if (!doc) return NULL;
  root = yyjson_mut_obj(doc);
  if (!root) {
    yyjson_mut_doc_free(doc);
    return NULL;
  }
  yyjson_mut_doc_set_root(doc, root);
  if (!yyjson_mut_obj_add_strcpy(doc, root, "model", model && model[0] ? model : "deepseek-chat")) {
    yyjson_mut_doc_free(doc);
    return NULL;
  }
  msgs = yyjson_mut_arr(doc);
  if (!msgs || !yyjson_mut_obj_add_val(doc, root, "messages", msgs)) {
    yyjson_mut_doc_free(doc);
    return NULL;
  }
  m = mut_msg(doc, "system", system_prompt);
  if (!m || !yyjson_mut_arr_add_val(msgs, m)) {
    yyjson_mut_doc_free(doc);
    return NULL;
  }
  for (i = 0; i < n_messages; i++) {
    const char *role;
    if (!messages[i].role || !messages[i].content) continue;
    role = (strcmp(messages[i].role, "assistant") == 0) ? "assistant" : "user";
    m = mut_msg(doc, role, messages[i].content);
    if (!m || !yyjson_mut_arr_add_val(msgs, m)) {
      yyjson_mut_doc_free(doc);
      return NULL;
    }
  }
  if (!yyjson_mut_obj_add_int(doc, root, "max_tokens", max_tokens) ||
      !yyjson_mut_obj_add_real(doc, root, "temperature", temperature)) {
    yyjson_mut_doc_free(doc);
    return NULL;
  }
  json = yyjson_mut_write(doc, 0, NULL);
  yyjson_mut_doc_free(doc);
  return json;
}

void llm_response_free(llm_response_t *r) {
  if (!r) return;
  free(r->data);
  r->data = NULL;
  r->size = 0;
}

int llm_extract_content_json(const char *json, llm_response_t *out) {
  yyjson_doc *doc;
  yyjson_val *root, *choices, *first, *message, *content;
  const char *s;
  size_t len;

  if (out) {
    out->data = NULL;
    out->size = 0;
  }
  if (!json || !out) return -1;
  doc = yyjson_read(json, strlen(json), 0);
  if (!doc) return -1;
  root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) {
    yyjson_doc_free(doc);
    return -1;
  }
  choices = yyjson_obj_get(root, "choices");
  if (!yyjson_is_arr(choices) || yyjson_arr_size(choices) < 1) {
    yyjson_doc_free(doc);
    return -1;
  }
  first = yyjson_arr_get(choices, 0);
  message = yyjson_obj_get(first, "message");
  if (!yyjson_is_obj(message)) {
    yyjson_doc_free(doc);
    return -1;
  }
  content = yyjson_obj_get(message, "content");
  if (yyjson_is_null(content) || !content) {
    out->data = malloc(1);
    if (!out->data) {
      yyjson_doc_free(doc);
      return -1;
    }
    out->data[0] = '\0';
    out->size = 0;
    yyjson_doc_free(doc);
    return 0;
  }
  if (!yyjson_is_str(content)) {
    yyjson_doc_free(doc);
    return -1;
  }
  s = yyjson_get_str(content);
  len = yyjson_get_len(content);
  out->data = malloc(len + 1);
  if (!out->data) {
    yyjson_doc_free(doc);
    return -1;
  }
  memcpy(out->data, s ? s : "", len);
  out->data[len] = '\0';
  out->size = len;
  yyjson_doc_free(doc);
  return 0;
}

static int do_request(const char *url, const char *api_key, const char *body, llm_response_t *out,
                      long *http_code) {
  neo_http_response_t hr;
  int err;

  out->data = NULL;
  out->size = 0;
  memset(&hr, 0, sizeof(hr));
  err = neo_http_post_json(url, api_key, body, 120, &hr);
  if (err != 0) {
    neo_http_response_free(&hr);
    return -1;
  }
  *http_code = hr.status;
  out->data = hr.body;
  out->size = hr.body_len;
  hr.body = NULL; /* 所有权交给 out */
  neo_http_response_free(&hr);
  return 0;
}

static int llm_post_body(const char *base_url, const char *api_key, const char *body,
                         llm_response_t *out) {
  char url[1024];
  long code = 0;
  int err;
  long t0;

  if (!base_url || !body || !out) return -1;
  out->data = NULL;
  out->size = 0;
  snprintf(url, sizeof(url), "%s/chat/completions", base_url);

  t0 = neo_events_now_ms();
  err = do_request(url, api_key, body, out, &code);
  if (err == 0 && (code == 429 || code == 503 || (code >= 500 && code < 600))) {
    llm_response_free(out);
    {
      struct timespec ts = {1, 0};
      nanosleep(&ts, NULL);
    }
    err = do_request(url, api_key, body, out, &code);
  }

  if (err != 0) {
    neo_events_emit("llm_done", 0, neo_events_now_ms() - t0, "request_failed");
    neo_events_emit("error", 0, 0, "llm_request");
    llm_response_free(out);
    return -1;
  }
  if (code != 200) {
    if (out->data && out->size)
      fprintf(stderr, "neo: LLM HTTP %ld: %.*s\n", code,
              (int)(out->size > 512 ? 512 : out->size), out->data);
    neo_events_emit("llm_done", 0, neo_events_now_ms() - t0, "http_not_200");
    neo_events_emit("error", 0, 0, "llm_http");
    llm_response_free(out);
    return -1;
  }
  neo_events_emit("llm_done", 1, neo_events_now_ms() - t0, "ok");
  return 0;
}

int llm_chat(const char *base_url, const char *model, const char *api_key, int max_tokens,
             double temperature, const char *system_prompt, const char *user_message,
             llm_response_t *out) {
  llm_message_t msgs[1];
  char *body;
  llm_response_t extracted = {0};

  if (!out) return -1;
  out->data = NULL;
  out->size = 0;
  if (max_tokens <= 0) max_tokens = 4096;
  if (max_tokens > 16384) max_tokens = 16384;
  if (temperature < 0.0 || temperature > 2.0) temperature = 0.7;

  msgs[0].role = "user";
  msgs[0].content = user_message ? user_message : "";
  body = build_chat_body(model, max_tokens, temperature, system_prompt, msgs, 1);
  if (!body) return -1;
  if (llm_post_body(base_url, api_key, body, out) != 0) {
    free(body);
    return -1;
  }
  free(body);
  if (!out->data) {
    llm_response_free(out);
    return -1;
  }
  if (llm_extract_content_json(out->data, &extracted) == 0) {
    llm_response_free(out);
    *out = extracted;
  }
  return 0;
}

int llm_post_chat_completions_json(const char *base_url, const char *api_key,
                                   const char *json_body, llm_response_t *out) {
  return llm_post_body(base_url, api_key, json_body, out);
}

int llm_chat_messages(const char *base_url, const char *model, const char *api_key,
                      int max_tokens, double temperature, const char *system_prompt,
                      const llm_message_t *messages, int n_messages, llm_response_t *out) {
  char *body;
  llm_response_t extracted = {0};

  if (!out) return -1;
  out->data = NULL;
  out->size = 0;
  if (max_tokens <= 0) max_tokens = 4096;
  if (max_tokens > 16384) max_tokens = 16384;
  if (temperature < 0.0 || temperature > 2.0) temperature = 0.7;

  body = build_chat_body(model, max_tokens, temperature, system_prompt, messages, n_messages);
  if (!body) return -1;
  if (llm_post_body(base_url, api_key, body, out) != 0) {
    free(body);
    return -1;
  }
  free(body);
  if (!out->data) {
    llm_response_free(out);
    return -1;
  }
  if (llm_extract_content_json(out->data, &extracted) == 0) {
    llm_response_free(out);
    *out = extracted;
  }
  return 0;
}
