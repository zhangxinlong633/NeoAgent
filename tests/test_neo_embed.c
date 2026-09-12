#include "neo_embed.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FAIL(msg)                          \
  do {                                     \
    fprintf(stderr, "FAIL: %s\n", (msg));  \
    return 1;                              \
  } while (0)

static int test_normalize_and_stable(void) {
  float a[32], b[32];
  size_t i;
  double norm = 0.0;
  float dist;

  if (neo_embed_text("hello neo memory", 32, a) != 0) FAIL("embed a");
  if (neo_embed_text("hello neo memory", 32, b) != 0) FAIL("embed b");
  for (i = 0; i < 32; i++) {
    if (a[i] != b[i]) FAIL("not stable");
    norm += (double)a[i] * (double)a[i];
  }
  if (fabs(norm - 1.0) > 1e-4) FAIL("not unit length");
  dist = neo_embed_cosine_distance(a, b, 32);
  if (dist > 1e-5f) FAIL("identical should be distance ~0");
  return 0;
}

static int test_similar_closer_than_unrelated(void) {
  float q[64], near[64], far[64];
  float d_near, d_far;

  if (neo_embed_text("prefer dark mode editor", 64, q) != 0) FAIL("q");
  if (neo_embed_text("user likes dark theme in the editor", 64, near) != 0) FAIL("near");
  if (neo_embed_text("deploy kubernetes on friday night", 64, far) != 0) FAIL("far");
  d_near = neo_embed_cosine_distance(q, near, 64);
  d_far = neo_embed_cosine_distance(q, far, 64);
  if (!(d_near < d_far)) {
    fprintf(stderr, "FAIL: expected near < far (got %f vs %f)\n", d_near, d_far);
    return 1;
  }
  return 0;
}

int main(void) {
  if (test_normalize_and_stable() != 0) return 1;
  if (test_similar_closer_than_unrelated() != 0) return 1;
  printf("ok test_neo_embed\n");
  return 0;
}
