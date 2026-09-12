# tests/prompts/gaps/

评测失败后的缺口产物索引。运行时草案目录 `drafts/` 已 gitignore；优先落盘的能力草案也可写入仓库 `capabilities/proposed/`（须人工确认后移入加载目录）。

| 路径 | 说明 |
|------|------|
| `drafts/` | 本地生成的 JSON5 草案（不入库） |
| `README.md` | 本契约 |

自动 PR 仅当 `NEO_PROMPTS_AUTOPR=1`；危险域（医疗诊断、法律意见等）只出报告。关联 [`../README.md`](../README.md)。
