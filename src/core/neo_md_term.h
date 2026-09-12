/* neo_md_term：用 vendored md4c 把 Markdown 渲染成终端可读文本（可选 ANSI）。 */
#ifndef NEO_MD_TERM_H
#define NEO_MD_TERM_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 将 md[0..len) 渲染到 out。use_color 非 0 时输出 ANSI。
 * 成功返回 0；解析失败返回非 0（调用方应回退为原样输出）。 */
int neo_md_term_render(const char *md, size_t len, FILE *out, int use_color);

#ifdef __cplusplus
}
#endif

#endif /* NEO_MD_TERM_H */
