#ifndef NEO_EMBED_H
#define NEO_EMBED_H

#include <stddef.h>

/*
 * 本地文本向量化：无外网、无模型文件。
 * 输出 L2 归一化向量，长度 = dims（建议 8..256）。
 */
int neo_embed_text(const char *text, size_t dims, float *out);

/* 余弦距离：0 表示同向；输入应已归一化。 */
float neo_embed_cosine_distance(const float *a, const float *b, size_t dims);

#endif
