# tests/prompts/

多领域提示词评测子系统：结构化语料、跑批 `./neo`、规则判分（可选 LLM judge）、失败缺口草案。

**不**并入默认 `make test`（避免 CI 消耗 API）。入口：

```bash
python3 tests/prompts/runner/run.py --help
python3 tests/prompts/runner/run.py --corpus tests/prompts/corpus --dry-run
make test-prompts-unit   # 规则打分单测
make test-prompts        # 单测 + 最多 50 条 live（需本机配置）
```

设计见 [`docs/superpowers/specs/2026-09-12-prompt-domain-eval-design.md`](../../docs/superpowers/specs/2026-09-12-prompt-domain-eval-design.md)；实施计划见 [`docs/superpowers/plans/2026-09-12-prompt-domain-eval.md`](../../docs/superpowers/plans/2026-09-12-prompt-domain-eval.md)。

## 子目录

| 子目录 | 职责 |
|--------|------|
| `schema/` | 单条提示词 JSON Schema |
| `corpus/` | 按领域 JSONL 语料（约 1000 条） |
| `runner/` | 校验、跑批、判分、生成语料、缺口流水线 |
| `reports/` | 跑批产物（gitignore） |
| `gaps/` | 失败聚类与草案索引（`drafts/` gitignore） |

自动开 PR 须设置环境变量 `NEO_PROMPTS_AUTOPR=1`；永不自动 merge。
