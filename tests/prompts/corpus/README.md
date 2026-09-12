# tests/prompts/corpus/

按领域存放评测语料（JSONL，一行一条）。字段须符合 [`../schema/prompt.schema.json`](../schema/prompt.schema.json)。

| 文件 | domain |
|------|--------|
| `system.jsonl` | 本机系统 |
| `workspace.jsonl` | 仓库/工作区 |
| `network.jsonl` | 网络连通 |
| `memory.jsonl` | 记忆 |
| `finance.jsonl` | 金融 |
| `medical.jsonl` | 医疗 |
| `legal.jsonl` | 法律 |
| `general.jsonl` | 综合 |

生成：`python3 tests/prompts/runner/gen_corpus.py --target 1000`（先 seed 再扩写）。本目录无子目录。
