/*
 * 本地 hashing bag-of-features embedding（无外部 API）。
 */
#include "neo_embed.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

int neo_embed_text(const char *text, size_t dims, float *out) {
  size_t i, n, t;
  double norm;
  if (!out || dims == 0) return -1;
  memset(out, 0, dims * sizeof(float));
  if (!text) text = "";
  n = strlen(text);
  /* 字符 3-gram → 桶累加 */
  for (i = 0; i + 2 < n || (n > 0 && i < n); i++) {
    unsigned h = 2166136261u;
    size_t j, lim = (i + 3 <= n) ? 3 : (n - i);
    if (lim == 0) break;
    for (j = 0; j < lim; j++) {
      unsigned char c = (unsigned char)text[i + j];
      if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
      h ^= c;
      h *= 16777619u;
    }
    out[h % dims] += 1.0f;
    if (i + 1 >= n) break;
  }
  /* 再混入按空白切分的 token hash */
  t = 0;
  while (text[t]) {
    unsigned h = 2166136261u;
    while (text[t] && isspace((unsigned char)text[t])) t++;
    if (!text[t]) break;
    while (text[t] && !isspace((unsigned char)text[t])) {
      unsigned char c = (unsigned char)text[t++];
      if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
      h ^= c;
      h *= 16777619u;
    }
    out[h % dims] += 1.5f;
  }
  norm = 0.0;
  for (i = 0; i < dims; i++) norm += (double)out[i] * (double)out[i];
  if (norm < 1e-12) {
    out[0] = 1.0f;
    return 0;
  }
  norm = sqrt(norm);
  for (i = 0; i < dims; i++) out[i] = (float)((double)out[i] / norm);
  return 0;
}

float neo_embed_cosine_distance(const float *a, const float *b, size_t dims) {
  size_t i;
  double dot = 0.0;
  if (!a || !b || dims == 0) return 1.0f;
  for (i = 0; i < dims; i++) dot += (double)a[i] * (double)b[i];
  if (dot > 1.0) dot = 1.0;
  if (dot < -1.0) dot = -1.0;
  return (float)(1.0 - dot);
}
