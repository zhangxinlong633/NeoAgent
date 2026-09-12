# Neo README 运行时架构图（方案 B）

| 属性 | 内容 |
|------|------|
| 日期 | 2026-09-12 |
| 状态 | 已落地于 README |
| 载体 | SVG（非 Mermaid） |

## 决策

- 叙事为 **Agent 闭环**：LLM（决策大脑）→ 生成/选用 DAG（调度框架）→ Capability Matrix（武器库）经 Policy 执行 → Memory（自动 + 定量存储）召回反哺 → 达成业务目标。
- 产品三角 **DAG ∥ Matrix ∥ Policy** 仍为执行脊梁；LLM / Memory 入图但不改写 `AGENTS.md` 三角定义。
- 载体为 SVG；业务语言 + 组件名成对出现。

## 产物

| 文件 | 用途 |
|------|------|
| [`neo-architecture-zh.svg`](../neo-architecture-zh.svg) | 中文 README |
| [`neo-architecture-en.svg`](../neo-architecture-en.svg) | 英文 README |

## 读法

业务目标进入决策大脑；大脑产出或选用 DAG；DAG 与能力矩阵结合办事；Memory 在全程自动/定量留存有价值信息并反哺决策，形成闭环。
