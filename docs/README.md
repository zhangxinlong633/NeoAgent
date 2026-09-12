# docs/

面向使用者与协作者的产品文档，以及较长篇幅的设计稿。实现细节以源码与 [`AGENTS.md`](../AGENTS.md) 为准；本目录描述**对外行为**与迁移指引。

## 建议阅读顺序

1. 仓库根 [`README.md`](../README.md) / [`README_zh.md`](../README_zh.md) — 产品一句话、架构图、Features、安装
2. **[`manual.md`](manual.md) — 使用手册（推荐主入口）**
3. [`../example/usage/`](../example/usage/) — 可复制短命令
4. [`examples.md`](examples.md) — 开箱组合、长样例、排错表
5. [`tool.md`](tool.md) / [`dag.md`](dag.md) / [`claw.md`](claw.md) — 子系统细则
6. [`applications.md`](applications.md) / [`architecture.md`](architecture.md) — 场景与目标架构

## 主要文档

1. `manual.md` — **使用手册**（安装、CLI、会话、JSON 对接、矩阵、DAG、记忆、排错）
2. `examples.md` — 命令行样例与开箱组合
3. `tool.md` — Capability Matrix、commands、MCP、能力目录
4. `dag.md` — DAG、plan/run、目录包
5. `claw.md` — soul / bootstrap / rules / memory
6. `applications.md` — 应用场景与生态位
7. `architecture.md` — 目标架构与实现对照
8. `migrate-json.md` — YAML → JSON5 迁移
9. `neo-architecture-*.webp` / `.svg` / `.png` — 运行时架构图

## 子目录

1. `superpowers/` — 设计规格与实施计划（**不是**日常用户手册）
