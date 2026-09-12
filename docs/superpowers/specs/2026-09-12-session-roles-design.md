# 多角色同会话（CLI `--role`）设计

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-12 |
| 状态 | 待审阅 |
| 范围 | 配置声明角色 + CLI `--role`；与具名会话 `-S` 共享历史 |
| 关联 | `neo_session`、claw 拼装（`docs/claw.md`）；非 `neo-team`、非 DAG 队形编排 |

## 1. 目标与非目标

**目标**

- 同一 `-S ID` 会话内，按回合切换**角色**（如 `researcher` / `writer`）。
- 历史消息共享；本轮仅改变 system 中的角色指令。
- 落盘后可读出「哪一轮是谁答的」，便于后续角色读上下文。

**非目标（v1）**

- 一轮内自动多角色接力（研究员→写手自动连跑）。
- 角色私有记忆或按角色裁剪 Capability Matrix。
- 多进程 / 多 profile 并行（见 `scripts/neo-team`、旧 team-pipeline 规划）。
- daemon 首版可不接 `--role`（可列为 follow-up；若改动小则同批接上）。

## 2. 决策摘要

| 议题 | 选择 |
|------|------|
| 角色来源 | **配置内联** `roles` 对象（不强制目录文件；避免与 claw 路径体系抢概念） |
| CLI | `--role NAME`；可与 `-S` / `-R` 同用；无 `--role` 则行为与现网一致 |
| Prompt 位置 | soul / bootstrap / rules / memory 之后注入 `## Role: NAME` + 角色文案 |
| 落盘前缀 | assistant 正文前加 `[NAME] `（仅当本轮指定了 `--role`）；user 不加前缀 |
| 非法角色 | stderr 报错并退出非零；不静默回退 |
| 与多会话混合 | `-S a,b --role writer` 合法：历史仍拼接，写回第一个 ID，本轮角色仅影响本回合 |

## 3. 配置

```json5
roles: {
  researcher: {
    description: "检索与拆解事实",
    prompt: "你本轮只做调研：列事实、来源假设与待核实点；不要写终稿。",
  },
  writer: {
    description: "成稿",
    prompt: "你本轮只写面向用户的终稿；沿用历史中的调研结论，勿重复长篇检索。",
  },
}
```

约定：

1. 键名 = 角色 id：`[A-Za-z0-9_-]`，长度 1..64（与 session id 字符集对齐）。
2. `prompt` 必填；`description` 可选（仅文档 / 未来 list，v1 可不暴露 CLI list）。
3. 未配置 `roles` 时，使用 `--role` → 报错「no roles configured」。
4. 不引入顶层键 `agents`（避免与产品「Neo Agent」及未来进程编排混淆）。

## 4. CLI 与会话

```bash
./neo -S ship --role researcher -R "飞船怎么做（先调研）"
./neo -S ship --role writer -R "根据上文写成短文"
```

会话文件仍为 `.neo/sessions/<ID>.json`（`version` / `id` / `messages`）。消息 schema 不变：仅 `role` ∈ {`user`,`assistant`} + `content`。角色信息编进 `content` 前缀，**不**新增 JSON 字段（避免破坏现有 `neo_session_load`）。

示例 assistant content：

```text
[researcher] …
```

加载进下轮 LLM 时原样作为历史；当前轮 system 另有 `## Role`。

## 5. 实现入口

| 路径 | 改动 |
|------|------|
| `src/core/config.h` / `config.c` | 解析 `roles`；释放；查找 by name |
| `src/cli/main.c` | 解析 `--role`；拼 `## Role`；append 时加前缀 |
| `config/config.json5.example` | 注释示例两个角色 |
| `tests/` | 解析 + 查找单元测试；可选 CLI 冒烟（不强制 live LLM） |
| `README.md` / `README_zh.md` / `docs/examples.md` | 用法一行 |

复杂逻辑中文注释（AGENTS §7）。

## 6. 验收

1. 无 `--role`：行为与现网一致（无前缀、无 `## Role`）。
2. `--role` 合法：system 含角色文案；落盘 assistant 以 `[NAME] ` 开头。
3. `--role` 未知 / 未配置：非零退出，stderr 明确。
4. `-S` + `--role`：第二轮可见第一轮带前缀的历史。
5. `make test` 通过。

## 7. 后续（不在本变更）

- `--role-list`
- `roles/` 目录装载（md 文件）
- daemon / socket 支持 `--role`
- 自动角色接力编排（原方案 B）
