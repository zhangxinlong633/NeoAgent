/* neo_md_term.c — md4c 回调渲染为终端文本（标题加粗、表格竖线、代码块弱化色）。
 * 不做完整 TUI；目标是 CLI `--render` 可读，失败时由调用方回退原文。 */
#include "neo_md_term.h"

#include "md4c.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEO_MD_CSI "\033["
#define NEO_MD_RESET NEO_MD_CSI "0m"
#define NEO_MD_BOLD NEO_MD_CSI "1m"
#define NEO_MD_DIM NEO_MD_CSI "2m"
#define NEO_MD_ITALIC NEO_MD_CSI "3m"
#define NEO_MD_UNDER NEO_MD_CSI "4m"
#define NEO_MD_STRIKE NEO_MD_CSI "9m"
#define NEO_MD_CYAN NEO_MD_CSI "36m"
#define NEO_MD_YELLOW NEO_MD_CSI "33m"
#define NEO_MD_BLUE NEO_MD_CSI "34m"

typedef struct {
  FILE *out;
  int color;
  int list_depth;
  int ol_stack[16];
  int in_code;
  int table_col;
  int in_thead;
  int cell_open;
} neo_md_ctx;

static void neo_md_puts(neo_md_ctx *c, const char *s) {
  if (s && *s) fputs(s, c->out);
}

static void neo_md_put(neo_md_ctx *c, const char *s, size_t n) {
  if (n) fwrite(s, 1, n, c->out);
}

static void neo_md_style(neo_md_ctx *c, const char *on) {
  if (c->color) neo_md_puts(c, on);
}

static void neo_md_reset(neo_md_ctx *c) {
  if (c->color) neo_md_puts(c, NEO_MD_RESET);
}

static void neo_md_attr_text(neo_md_ctx *c, const MD_ATTRIBUTE *a) {
  size_t i;
  if (!a || !a->text || a->size == 0) return;
  /* 链接 URL 等属性按原文写出（实体不展开，够用）。 */
  if (!a->substr_offsets) {
    neo_md_put(c, a->text, (size_t)a->size);
    return;
  }
  for (i = 0; a->substr_offsets[i] < a->size; i++) {
    MD_OFFSET off = a->substr_offsets[i];
    MD_OFFSET end = a->substr_offsets[i + 1];
    if (end > a->size) end = a->size;
    if (end > off) neo_md_put(c, a->text + off, (size_t)(end - off));
  }
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  neo_md_ctx *c = (neo_md_ctx *)userdata;
  switch (type) {
  case MD_BLOCK_DOC:
    break;
  case MD_BLOCK_QUOTE:
    neo_md_style(c, NEO_MD_DIM);
    neo_md_puts(c, "│ ");
    break;
  case MD_BLOCK_UL:
    if (c->list_depth < 16) c->ol_stack[c->list_depth] = 0;
    c->list_depth++;
    break;
  case MD_BLOCK_OL:
    if (c->list_depth < 16) {
      MD_BLOCK_OL_DETAIL *od = (MD_BLOCK_OL_DETAIL *)detail;
      c->ol_stack[c->list_depth] = od ? od->start : 1;
    }
    c->list_depth++;
    break;
  case MD_BLOCK_LI: {
    int d = c->list_depth > 0 ? c->list_depth - 1 : 0;
    int i;
    for (i = 0; i < d; i++) neo_md_puts(c, "  ");
    if (detail) {
      MD_BLOCK_LI_DETAIL *li = (MD_BLOCK_LI_DETAIL *)detail;
      if (li->is_task) {
        neo_md_puts(c, (li->task_mark == 'x' || li->task_mark == 'X') ? "[x] " : "[ ] ");
        break;
      }
    }
    if (d < 16 && c->ol_stack[d] > 0) {
      fprintf(c->out, "%d. ", c->ol_stack[d]);
      c->ol_stack[d]++;
    } else {
      neo_md_puts(c, "• ");
    }
    break;
  }
  case MD_BLOCK_HR:
    neo_md_style(c, NEO_MD_DIM);
    neo_md_puts(c, "────────\n");
    neo_md_reset(c);
    break;
  case MD_BLOCK_H: {
    MD_BLOCK_H_DETAIL *h = (MD_BLOCK_H_DETAIL *)detail;
    int level = h ? (int)h->level : 1;
    neo_md_puts(c, "\n");
    neo_md_style(c, NEO_MD_BOLD);
    if (level <= 1) neo_md_style(c, NEO_MD_YELLOW);
    else if (level == 2) neo_md_style(c, NEO_MD_BLUE);
    break;
  }
  case MD_BLOCK_CODE:
    c->in_code = 1;
    neo_md_puts(c, "\n");
    neo_md_style(c, NEO_MD_DIM);
    break;
  case MD_BLOCK_P:
    break;
  case MD_BLOCK_TABLE:
    neo_md_puts(c, "\n");
    break;
  case MD_BLOCK_THEAD:
    c->in_thead = 1;
    break;
  case MD_BLOCK_TBODY:
    c->in_thead = 0;
    break;
  case MD_BLOCK_TR:
    c->table_col = 0;
    break;
  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    if (c->table_col > 0) neo_md_puts(c, " │ ");
    if (type == MD_BLOCK_TH) neo_md_style(c, NEO_MD_BOLD);
    c->cell_open = 1;
    c->table_col++;
    break;
  default:
    break;
  }
  return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
  neo_md_ctx *c = (neo_md_ctx *)userdata;
  (void)detail;
  switch (type) {
  case MD_BLOCK_QUOTE:
    neo_md_reset(c);
    neo_md_puts(c, "\n");
    break;
  case MD_BLOCK_UL:
  case MD_BLOCK_OL:
    if (c->list_depth > 0) c->list_depth--;
    neo_md_puts(c, "\n");
    break;
  case MD_BLOCK_LI:
    neo_md_puts(c, "\n");
    break;
  case MD_BLOCK_H:
    neo_md_reset(c);
    neo_md_puts(c, "\n\n");
    break;
  case MD_BLOCK_CODE:
    c->in_code = 0;
    neo_md_reset(c);
    neo_md_puts(c, "\n");
    break;
  case MD_BLOCK_P:
    neo_md_puts(c, "\n\n");
    break;
  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    if (c->cell_open) {
      neo_md_reset(c);
      c->cell_open = 0;
    }
    break;
  case MD_BLOCK_TR:
    neo_md_puts(c, "\n");
    if (c->in_thead && c->table_col > 0) {
      int i;
      for (i = 0; i < c->table_col; i++) {
        if (i) neo_md_puts(c, "─┼─");
        neo_md_puts(c, "──");
      }
      neo_md_puts(c, "\n");
    }
    break;
  case MD_BLOCK_TABLE:
    neo_md_puts(c, "\n");
    break;
  default:
    break;
  }
  return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
  neo_md_ctx *c = (neo_md_ctx *)userdata;
  switch (type) {
  case MD_SPAN_EM:
    neo_md_style(c, NEO_MD_ITALIC);
    break;
  case MD_SPAN_STRONG:
    neo_md_style(c, NEO_MD_BOLD);
    break;
  case MD_SPAN_A:
    neo_md_style(c, NEO_MD_UNDER);
    neo_md_style(c, NEO_MD_CYAN);
    break;
  case MD_SPAN_IMG:
    neo_md_puts(c, "[img:");
    break;
  case MD_SPAN_CODE:
    neo_md_style(c, NEO_MD_CYAN);
    break;
  case MD_SPAN_DEL:
    neo_md_style(c, NEO_MD_STRIKE);
    break;
  default:
    (void)detail;
    break;
  }
  return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
  neo_md_ctx *c = (neo_md_ctx *)userdata;
  switch (type) {
  case MD_SPAN_EM:
  case MD_SPAN_STRONG:
  case MD_SPAN_CODE:
  case MD_SPAN_DEL:
    neo_md_reset(c);
    break;
  case MD_SPAN_A: {
    MD_SPAN_A_DETAIL *a = (MD_SPAN_A_DETAIL *)detail;
    neo_md_reset(c);
    if (a && a->href.text && a->href.size) {
      neo_md_style(c, NEO_MD_DIM);
      neo_md_puts(c, " (");
      neo_md_attr_text(c, &a->href);
      neo_md_puts(c, ")");
      neo_md_reset(c);
    }
    break;
  }
  case MD_SPAN_IMG:
    neo_md_puts(c, "]");
    break;
  default:
    break;
  }
  return 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata) {
  neo_md_ctx *c = (neo_md_ctx *)userdata;
  switch (type) {
  case MD_TEXT_NULLCHAR:
    neo_md_puts(c, "\xEF\xBF\xBD");
    break;
  case MD_TEXT_BR:
    neo_md_puts(c, "\n");
    break;
  case MD_TEXT_SOFTBR:
    neo_md_puts(c, " ");
    break;
  case MD_TEXT_ENTITY:
  case MD_TEXT_HTML:
  case MD_TEXT_LATEXMATH:
  case MD_TEXT_NORMAL:
  case MD_TEXT_CODE:
  default:
    if (text && size) neo_md_put(c, text, (size_t)size);
    break;
  }
  return 0;
}

int neo_md_term_render(const char *md, size_t len, FILE *out, int use_color) {
  neo_md_ctx ctx;
  MD_PARSER parser;
  int rc;

  if (!md || !out) return -1;
  if (len == 0) return 0;
  if (len > (size_t)UINT_MAX) return -1;

  memset(&ctx, 0, sizeof(ctx));
  ctx.out = out;
  ctx.color = use_color ? 1 : 0;

  memset(&parser, 0, sizeof(parser));
  parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS |
                 MD_FLAG_PERMISSIVEAUTOLINKS;
  parser.enter_block = enter_block;
  parser.leave_block = leave_block;
  parser.enter_span = enter_span;
  parser.leave_span = leave_span;
  parser.text = on_text;

  rc = md_parse(md, (MD_SIZE)len, &parser, &ctx);
  if (rc != 0) return rc;
  fflush(out);
  return 0;
}
