# tests/prompts/runner/

提示词评测执行器：schema 校验、调用 `./neo`、规则打分、可选裁判与缺口草案。

| 文件 | 职责 |
|------|------|
| `run.py` | CLI 入口 |
| `score.py` | 规则判分 |
| `schema_validate.py` | JSONL 校验 |
| `neo_invoke.py` | 子进程调用 neo |
| `gen_corpus.py` | 语料扩写 |
| `judge.py` | 可选 LLM judge（`--judge`） |
| `gaps.py` | 失败聚类 / proposed / 门闩 PR |
| `testdata/` | 小型 fixture |
| `test_score.py` | 打分单测 |

本目录无嵌套业务子目录（`testdata/` 除外）。关联 [`../README.md`](../README.md)。
