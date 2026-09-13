#ifndef NEO_EVENTS_H
#define NEO_EVENTS_H

/*
 * 结构化运行事件（JSONL）。默认关闭；NEO_EVENTS=1 开启。
 * 输出：NEO_EVENTS_PATH 指定文件（追加），否则 stderr。
 * 每行：{"ts":unix,"name":"...","ok":0|1,"ms":N,"detail":"..."}
 */

/* 1 表示已开启（环境变量 NEO_EVENTS=1/true/yes/on）。 */
int neo_events_enabled(void);

/*
 * 写一行事件。name 必填；detail 可空（会截断并 JSON 转义）。
 * ok：1 成功 / 0 失败；ms：耗时毫秒（未知可传 0）。
 */
void neo_events_emit(const char *name, int ok, long ms, const char *detail);

/* 粗粒度毫秒时钟，供调用方算耗时（未开启事件时也可调用）。 */
long neo_events_now_ms(void);

#endif
