# example/

本目录提供两类材料：

1. **Claw 拼装演示**：`soul` / `bootstrap` / `rules` / `memory` 如何注入 system prompt（与 [`docs/claw.md`](../docs/claw.md) 一致）。
2. **日常用法样例**：见 [`usage/`](usage/)——会话、JSON 输出、DAG、角色切换、高铁查询等可复制命令。

> 本目录中的 `AGENTS.md` 是**运行时** bootstrap 示例，**不是**仓库根开发约束（开发约束见根目录 `AGENTS.md` / `CLAUDE.md`）。

更完整的说明见 **[`docs/manual.md`](../docs/manual.md)（使用手册）**；可复制短命令见 [`usage/`](usage/)；长样例见 [`docs/examples.md`](../docs/examples.md)。

## 前提

1. 在仓库根执行 `make`，得到 `./neo`。
2. 复制并填写配置：`cp config/config.json5.example config/config.json5`（或设 `NEO_API_KEY`）。
3. **所有命令在仓库根执行**（路径相对 cwd）。

## 快速入口

```bash
# 默认会话续聊 + 终端 Markdown（默认开）
./neo "用三句话介绍 Neo Agent"

# 新开默认会话（归档旧 default）
./neo -N "换个话题从头聊"

# 给脚本：OpenAI chat.completion JSON
./neo -j "只回一句你好"

# 确定性 DAG
./neo dag run show_time
```

分类样例：[`usage/README.md`](usage/README.md)。

## Claw 注入验证

编辑 `example/neo-claw-example.json5` 中的 `model.api_key`（或 `NEO_API_KEY`）后：

```bash
NEO_DISABLE_TOOLS=1 ./neo -c example/neo-claw-example.json5 -d \
  "只回答：SOUL 里默认用哪种语言回答？" 2>&1 | grep -A2 '## Soul'
```

对话：

```bash
./neo -c example/neo-claw-example.json5 \
  "根据 AGENTS 和 Soul，用两三句话介绍你是谁、在哪个仓库语境下工作"
```

## 本目录文件

1. `SOUL.md` — `soul.path`
2. `AGENTS.md` — bootstrap
3. `RULES.md` — rules
4. `MEMORY.md` — memory.path（vector 关闭时的截断注入）
5. `neo-claw-example.json5` — 上述 claw 演示用配置
6. `usage/` — 日常用法样例（命令清单）

`skills` 已废弃；正式配置用仓库根 `rules/` 与 `capability_matrix.directory`。
