#include "config.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  agent_config_t c;
  const neo_role_t *r;

  config_init(&c);
  if (config_load_file(&c, "tests/fixtures/roles_min.json5") != 0) {
    fprintf(stderr, "load failed\n");
    return 1;
  }
  if (c.role_count != 2) {
    fprintf(stderr, "want 2 roles got %d\n", c.role_count);
    config_free(&c);
    return 1;
  }
  r = config_find_role(&c, "researcher");
  if (!r || !r->prompt || strstr(r->prompt, "调研") == NULL) {
    fprintf(stderr, "researcher missing\n");
    config_free(&c);
    return 1;
  }
  if (!r->description || strcmp(r->description, "调研") != 0) {
    fprintf(stderr, "researcher description\n");
    config_free(&c);
    return 1;
  }
  r = config_find_role(&c, "writer");
  if (!r || !r->prompt || strcmp(r->prompt, "只写终稿。") != 0) {
    fprintf(stderr, "writer missing\n");
    config_free(&c);
    return 1;
  }
  if (config_find_role(&c, "nope") != NULL) {
    fprintf(stderr, "unknown should be null\n");
    config_free(&c);
    return 1;
  }
  config_free(&c);
  fprintf(stderr, "ok test_config_roles\n");
  return 0;
}
