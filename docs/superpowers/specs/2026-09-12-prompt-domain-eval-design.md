# 领域提示词评测集（tests/prompts）设计

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-12 |
| 状态 | 已确认待实施 |
| 位置 | `tests/prompts/`（与现有 C 单元测试并列） |
| 关联 | Capability Matrix、`propose_capability`、Policy；产品定位见 [`docs/applications.md`](../../applications.md) |

## 1. 背景与目标

现有 `tests/` 覆盖矩阵/DAG/memory 等工程冒烟，**没有**跨领域用户提示词评测。目标是：

1. 在 `tests/prompts/` 建立**结构化**多领域语料（约 **1000** 条）。
2. 跑批 `./neo`，同时验证：
   - **应能办**（正确工具/DAG + 合理答复）
   - **应拒答 / 应提议补能力**（诚实边界，不编造、不乱塞过时规则）
3. 对失败项尽量自动生成能力草案并开 **PR（不自动 merge）**，危险类只出报告。

成功标准（首期）：schema + README + runner 可跑 subset；语料 ≥1000 且通过校验；报告可读；至少一类安全缺口能产出 proposed 草案 + PR 草稿。

## 2. 决策摘要

| 项 | 选择 |
|----|------|
| 评测目标 | 能力覆盖 **与** 拒答/补能力边界（兼顾） |
| 领域范围 | **广域**（含金融/医疗/法律等；多数为 refuse/propose） |
| 失败闭环 | **尽量自动**写能力草案 + 开 PR；强 Policy；不自动 merge |
| 判定 | **混合**：金标规则为主，灰区可选 LLM judge |
| 架构 | 同仓 `tests/prompts/` 子系统（方案 2） |
| CI | **不**并入默认 `make test`；可选 `make test-prompts` / nightly |

## 3. 目录与职责

```
tests/prompts/
  README.md                 # 目录契约
  schema/prompt.schema.json # 单条 JSON Schema
  corpus/                   # 按领域 JSONL，合计 ~1000
  runner/                   # 校验、跑批、判分、缺口流水线
  reports/                  # 跑批产物（gitignore）
  gaps/                     # 失败聚类与草案输出（运行时态可 gitignore）
```

| 路径 | 职责 |
|------|------|
| `corpus/*.jsonl` | 领域语料；文件名与 `domain` 对齐（如 `system.jsonl`、`finance.jsonl`） |
| `runner/` | 调 `./neo -v`、解析 `neo tool:`、规则判分、可选裁判、写报告、触发 gaps |
| `reports/<run-id>/` | `summary.json`、`results.jsonl`、`failures.jsonl`（脱敏） |
| `gaps/drafts/` | 生成的能力 JSON5 草案；或写入 `capabilities/proposed/`（与现有约定一致时优先后者） |

`tests/README.md` 增加对本目录的入口说明。默认 `make test` **不**跑本评测。

## 4. 语料 Schema

每行一条 JSON（JSONL），经 `prompt.schema.json` 校验。

| 字段 | 必填 | 说明 |
|------|------|------|
| `id` | 是 | 稳定 ID，如 `finance-0042` |
| `domain` | 是 | `system` \| `workspace` \| `network` \| `memory` \| `finance` \| `medical` \| `legal` \| `general`（可扩展，扩展须改 schema 枚举） |
| `prompt` | 是 | 用户输入 |
| `expect.outcome` | 是 | `succeed` \| `refuse` \| `propose` |
| `expect.tools` | 否 | 期望工具名列表；`succeed` 时宜填写 |
| `expect.forbid_tools` | 否 | 禁止出现的工具 |
| `expect.must_include` / `must_exclude` | 否 | 回复关键词约束 |
| `tags` | 否 | 如 `live-http`、`needs-git`、`sensitive` |
| `notes` | 否 | 出题意图 |

### 4.1 判定优先级

1. **规则**：对照 `expect` + stderr 中 `neo tool:` + 关键词。
2. **灰区**：规则无法判定时，可选 LLM judge（同一或配置的模型）；裁判 prompt 固定、输出结构化 `pass|fail|uncertain`。
3. **结果码**：`pass` / `fail` / `skip`（无 API key、无外网、缺依赖等）。

### 4.2 目标比例（生成器默认，可调）

约 **30%** `succeed`（可贴现行矩阵/DAG）、**50%** `refuse`、**20%** `propose`。  
广域题以边界为主，避免 1000 条都要求「办成」。

## 5. 跑批数据流

```
corpus/*.jsonl
  → schema 校验 / 去重
  → 每条：./neo -v "$prompt"（可选 -R；可 --domain / --limit / 并发上限）
  → 采集 stdout + stderr
  → 规则判分 →（灰区）LLM judge
  → reports/<run-id>/{summary.json, results.jsonl, failures.jsonl}
```

Runner 能力：

- dry-run：只校验语料
- subset：先 50 条冒烟
- 密钥来自本机配置 / 环境变量；报告脱敏（禁止写入 api_key）

入口建议：`./tests/prompts/runner/run.py` 与可选 Makefile 目标 `test-prompts`。

## 6. 失败 → 自动补能力闭环

对 `fail` 且缺口类型为「缺能力 / 应 propose」的项：

1. **聚类**：`domain` + 缺口类型（缺工具 / Policy 挡 / 幻觉乱答）。
2. **生成草案**：带 `name` / `description` / `when` / `when_not` / `argv` 的 JSON5；落盘 `capabilities/proposed/` 或 `gaps/drafts/`。
3. **开 PR**：feature 分支 + `gh pr create`；正文含失败样例与风险；**不自动 merge**。
4. **禁止自动 PR**（仅报告）：医疗诊断结论、法律意见、未授权破坏性写盘、无 allowlist 的任意网络、默认打开 `shell_enabled` 等。
5. **允许自动 PR 候选**：只读探测、明确 argv 的 command、已有 Policy 模式的受控 HTTP（如天气类）等。

与现有 `propose_capability` 对齐：草案默认不进运行时矩阵，须人工移入加载目录并重启后生效。

## 7. 语料生成

| 批次 | 内容 |
|------|------|
| Seed | 每领域 10～20 条手写金标 |
| Expand | 生成器按模板扩到合计 ≥1000；schema 校验；`prompt` 去重 |
| Audit | 随机抽检 ≥5%；`medical`/`legal` 默认不得标成「应给出诊断/判决」 |

建议条数分布：`system`+`workspace`+`network`+`memory` ≈ 300；`finance`+`medical`+`legal`+`general` ≈ 700。

## 8. 非目标

- 不做通用 LLM 竞技场或「模型智商」排行。
- 不把 1000 条 live 跑批并入默认 `make test` / 默认 PR CI。
- 不自动 merge；不自动放开危险 Policy。
- 不将含密钥的报告提交进 git。
- 首期不做分布式评测集群。

## 9. 测试与验收

| 项 | 要求 |
|----|------|
| Runner 单测 | 小 fixture（3～5 条）覆盖 schema 与规则判分 |
| 语料 | `≥1000` 且全部通过 schema |
| 冒烟 | `--limit 50` 可完成并写出 summary |
| 闭环 | 至少一类安全失败能产出 proposed 草案与 PR 草稿（人工确认） |

## 10. 实施分期（供 writing-plans 展开）

1. **骨架**：目录 README、schema、gitignore、`tests/README` 入口、空/样例 corpus。  
2. **Runner v1**：跑批 + 规则判分 + reports。  
3. **语料**：seed + 生成器 → 1000 条 + 抽检。  
4. **Judge（可选开关）**：灰区 LLM 裁判。  
5. **Gaps 流水线**：聚类 → proposed → PR 草稿 + 危险类黑名单。

## 11. 开放细节（实施时定，不阻塞本 spec）

- Runner 语言：优先 Python 3（仓库已有 fixture 脚本习惯），或 POSIX shell + jq；实施计划选定一种。
- LLM judge 是否默认关闭（推荐默认关，显式 `--judge` 打开）。
- 自动 PR 是否需环境变量门闩（如 `NEO_PROMPTS_AUTOPR=1`）——推荐需要，避免误开。
