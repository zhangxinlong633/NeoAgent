# docs/

面向使用者与协作者的产品文档，以及较长篇幅的设计稿。实现细节以源码与 [`AGENTS.md`](../AGENTS.md) 为准；本目录文档描述**对外行为**与迁移指引。

## 建议阅读顺序

1. 仓库根 [`README.md`](../README.md)（英文）/ [`README_zh.md`](../README_zh.md)（中文）— 目标型 Agent、业务层次（与 `applications` 对齐）、上手样例  
2. [`examples.md`](examples.md) — 可复现命令、**开箱组合**、排错  
3. [`tool.md`](tool.md) / [`dag.md`](dag.md) — 矩阵与 DAG 细则  
4. [`architecture.md`](architecture.md) / [`applications.md`](applications.md) — 目标架构与场景全文

## 主要文档

| 文件 | 内容 |
|------|------|
| `examples.md` | 命令行使用样例（对话、矩阵、DAG、plan/run、排错） |
| `architecture.md` | 智能体目标架构（调度/执行/规划）与本仓库实现对照 |
| `neo-architecture-zh.svg` / `neo-architecture-en.svg` | README 开篇运行时架构图（双路径 + Memory） |
| `applications.md` | 应用场景、问题域与生态位（基础 / 行业 / 未来） |
| `tool.md` | Capability Matrix、commands、MCP、能力目录、Unix 白名单 |
| `dag.md` | DAG、plan/run、workflow 目录 |
| `claw.md` | soul / bootstrap / rules / memory（skills 已废弃） |
| `migrate-json.md` | YAML → JSON5 迁移 |

## 子目录

| 子目录 | 职责 |
|--------|------|
| `superpowers/` | 设计规格与实施计划（非日常用户手册） |
