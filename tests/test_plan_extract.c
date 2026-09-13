#include "plan.h"
#include "config.h"
#include "dag_dir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int expect_contains(const char *hay, const char *needle, const char *label) {
  if (!hay || !strstr(hay, needle)) {
    fprintf(stderr, "FAIL %s: missing %s\n", label, needle);
    return 1;
  }
  return 0;
}

int main(void) {
  char *json = NULL;
  agent_config_t base, planned;
  char tmp[256];
  int fails = 0;
  const char *fenced =
      "Sure:\n```json\n"
      "{\"dags\":[{\"name\":\"planned\",\"steps\":["
      "{\"id\":\"a\",\"type\":\"llm\",\"prompt\":\"hello\"}]}]}"
      "\n```\n";
  const char *bare =
      "{\"dags\":[{\"name\":\"bare\",\"steps\":["
      "{\"id\":\"x\",\"type\":\"llm\",\"prompt\":\"x\"}]}]}";

  if (plan_extract_dags_json(fenced, &json) != 0) {
    fprintf(stderr, "extract fenced failed\n");
    return 1;
  }
  fails += expect_contains(json, "\"dags\"", "fenced");
  fails += expect_contains(json, "planned", "fenced");
  free(json);
  json = NULL;

  if (plan_extract_dags_json(bare, &json) != 0) {
    fprintf(stderr, "extract bare failed\n");
    return 1;
  }
  fails += expect_contains(json, "bare", "bare");
  free(json);
  json = NULL;

  if (plan_extract_dags_json("no json here", &json) == 0) {
    fprintf(stderr, "expected extract failure\n");
    free(json);
    return 1;
  }

  config_init(&base);
  base.model.base_url = strdup("http://127.0.0.1:9");
  base.model.name = strdup("test-model");
  base.model.api_key = strdup("k");
  base.model.max_tokens = 100;
  base.model.temperature = 0.0;
  base.tools.enabled = 0;
  base.tools.root = strdup(".");
  base.memory.path = strdup("MEMORY.md");
  base.memory.max_chars = 100;
  base.session_max_turns = 2;

  if (plan_materialize_config(&base, bare, &planned, tmp, sizeof(tmp)) != 0) {
    fprintf(stderr, "materialize failed\n");
    config_free(&base);
    return 1;
  }
  if (planned.dag_count < 1 || !planned.dags[0].name ||
      strcmp(planned.dags[0].name, "bare") != 0) {
    fprintf(stderr, "bad materialize name\n");
    config_free(&planned);
    config_free(&base);
    if (tmp[0]) unlink(tmp);
    return 1;
  }
  config_free(&planned);
  if (tmp[0]) unlink(tmp);

  {
    char *prompt = plan_build_system_prompt(&base, 10);
    if (!prompt || !strstr(prompt, "JSON") || !strstr(prompt, "10")) {
      fprintf(stderr, "prompt missing JSON/target\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    if (!strstr(prompt, "knowledge") || !strstr(prompt, "engineering")) {
      fprintf(stderr, "prompt missing knowledge/engineering routing\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    if (!strstr(prompt, "DAG catalog") || !strstr(prompt, "\"use\"")) {
      fprintf(stderr, "prompt missing DAG catalog / use preference\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    if (strstr(prompt, "Even for Q&A")) {
      fprintf(stderr, "prompt still forces Q&A team flow\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    if (!strstr(prompt, "self-check") || !strstr(prompt, "draft")) {
      fprintf(stderr, "prompt missing knowledge editorial pipeline\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    if (!strstr(prompt, "Do not pad") && !strstr(prompt, "do not pad")) {
      fprintf(stderr, "prompt missing do-not-pad guidance\n");
      free(prompt);
      config_free(&base);
      return 1;
    }
    free(prompt);
  }

  {
    char **names = NULL;
    int n = 0;
    if (plan_extract_use("```json\n{\"use\":[\"dir_count\"]}\n```", &names, &n) != 0 || n != 1 ||
        !names || strcmp(names[0], "dir_count") != 0) {
      fprintf(stderr, "extract use array failed\n");
      plan_free_use(names, n);
      config_free(&base);
      return 1;
    }
    plan_free_use(names, n);
    if (plan_extract_use("{\"use\":\"solo\"}", &names, &n) != 0 || n != 1 || strcmp(names[0], "solo")) {
      fprintf(stderr, "extract use string failed\n");
      plan_free_use(names, n);
      config_free(&base);
      return 1;
    }
    plan_free_use(names, n);
  }

  {
    char *tool_names[2];
    char *syn = NULL;
    tool_names[0] = (char *)"date_iso";
    tool_names[1] = (char *)"list_dir";
    syn = plan_dags_json_for_tools(tool_names, 2);
    if (!syn || !strstr(syn, "adhoc_tools") || !strstr(syn, "date_iso") || !strstr(syn, "list_dir")) {
      fprintf(stderr, "synthesize adhoc_tools failed: %s\n", syn ? syn : "(null)");
      free(syn);
      config_free(&base);
      return 1;
    }
    free(syn);
  }

  config_free(&base);

  {
    agent_config_t c;
    char *cat;
    config_init(&c);
    if (config_load_file(&c, "tests/fixtures/tools_dag_dir.json5") != 0) {
      fprintf(stderr, "load dag dir fixture failed\n");
      return 1;
    }
    if (!config_find_dag(&c, "dir_count")) {
      fprintf(stderr, "dir_count not loaded from dag_directory\n");
      config_free(&c);
      return 1;
    }
    cat = dag_dir_catalog_listing(&c);
    if (!cat || !strstr(cat, "dir_count") || !strstr(cat, "DAG:")) {
      fprintf(stderr, "catalog missing dir_count or DAG: prefix: %s\n", cat ? cat : "(null)");
      free(cat);
      config_free(&c);
      return 1;
    }
    free(cat);
    config_free(&c);
  }

  /* Task A1: catalog hint + unknown use 报告 */
  {
    agent_config_t c;
    dag_t *dags;
    char *hint;
    char *names[1];
    int nbad;
    config_init(&c);
    dags = calloc(3, sizeof(dag_t));
    if (!dags) {
      fprintf(stderr, "oom\n");
      return 1;
    }
    dags[0].name = strdup("alpha_dag");
    dags[1].name = strdup("beta_dag");
    dags[2].name = strdup("gamma_very_long_catalog_name_for_truncation_test");
    c.dags = dags;
    c.dag_count = 3;
    hint = plan_format_catalog_hint(&c, 2, 240);
    if (!hint || !strstr(hint, "alpha_dag") || !strstr(hint, "beta_dag") || !strstr(hint, "...")) {
      fprintf(stderr, "catalog hint max_names failed: %s\n", hint ? hint : "(null)");
      free(hint);
      config_free(&c);
      return 1;
    }
    free(hint);
    hint = plan_format_catalog_hint(&c, 16, 20);
    if (!hint || !strstr(hint, "...")) {
      fprintf(stderr, "catalog hint max_chars failed: %s\n", hint ? hint : "(null)");
      free(hint);
      config_free(&c);
      return 1;
    }
    free(hint);
    names[0] = "totally_unknown_xyz";
    nbad = plan_report_unknown_use_names(&c, "plan", names, 1);
    if (nbad != 1) {
      fprintf(stderr, "expected 1 unknown use, got %d\n", nbad);
      config_free(&c);
      return 1;
    }
    config_free(&c);
  }

  if (fails) return 1;
  printf("ok\n");
  return 0;
}
