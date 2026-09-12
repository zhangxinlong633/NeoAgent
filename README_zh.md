# Neo Agent

**面向明确目标的可移植 Agent 运行时。**

> English: [`README.md`](README.md)

Neo Agent 接收可执行的目标（状态核查、材料整理、固定运维流程等），并按约定完成执行。产品定位不是开放式闲聊助手，而是在可控边界内**稳定办完一类工作**。

### 运行时架构

Neo Agent 以闭环方式办事：**大模型是决策大脑**（理解目标并生成或选用 DAG）；**DAG 是调度框架**（按图推进、路径可复查）；**Capability Matrix 是武器库**（提供受控工具）；二者结合把一件事办完。贯穿全程的 **Memory** 自动或定量存储有价值信息，并召回反哺决策，直至达成业务目标。

<p align="center">
  <img src="docs/neo-architecture-zh.webp" alt="Neo Agent 闭环架构：LLM 决策大脑、DAG 调度、Capability Matrix 武器库、Memory 自主记忆" width="920" />
</p>

| 维度 | 组件 | 业务含义 |
|------|------|----------|
| 决策 | **LLM** | 决策大脑：规划步骤，生成或选用流程 |
| 调度 | **DAG** | 调度框架：按约定拓扑推进 |
| 工具 | **Capability Matrix** | 武器库：清单内本事与工具 |
| 边界 | **Policy** | 守门：默认禁止与资源上限 |
| 留存 | **Memory** | 自主记忆：自动存储 + 定量/主动写入，召回增强后续决策 |

产品三角 **DAG ∥ Capability Matrix ∥ Policy** 仍是执行脊梁；LLM 与 Memory 分别承担决策与跨会话留存，形成闭环。部署形态偏轻量：更换机器后调整配置即可使用。

场景与定位详见 [`docs/applications.md`](docs/applications.md)；命令示例见下文与 [`docs/examples.md`](docs/examples.md)。

---

## 业务扩展层次

Neo Agent 的核心价值不在「模型孰强」，而在目标型 Agent 能否**稳定调度、按清单调用、控制成本与数据边界**。下列层次与 [`docs/applications.md`](docs/applications.md) 一致：优先交付当前可落地能力；后续层为方向与愿景（勿视为已全部实现）。

### 当前交付：使 Agent 任务可完成、可复查

| 业务诉求 | Neo Agent 的支撑方式 |
|----------|----------------|
| 多步骤目标须跑完且路径可复查 | 以 DAG 固化「取数 → 整理 → 判断 → 通知/落盘」 |
| 内部能力须可被 Agent 使用且可治理 | 以能力矩阵登记脚本、命令与接口；任务仅可调用清单内能力 |
| 降低「事事回云」的成本、时延与外泄面 | 简单步骤优先本地能力；困难推理再调用大模型 |
| **跨会话保留偏好与事实（自主记忆）** | 本地向量记忆：对话中可自动写入、按需召回；亦可由模型调用 `memory_add`；数据默认留在本机 |

典型用途：仓库/文档旁路助手、定时目标执行、按 SOP 的日常检查，以及需要**跨轮次记住用户偏好**的轻量助理。

### 方向：现场与边缘

当近数据、低时延、弱网闭环成为硬需求时，同一套「DAG + 能力矩阵」可向现场延伸（完整形态见架构路线图；今日以预留与契约为主）。

| 方向 | 业务画面 | Neo Agent 的角色 |
|------|----------|------------|
| 工业边缘 | 质检、联锁、异常处置需在产线侧闭环 | 工控侧调度；节点侧执行已登记能力 |
| 具身与移动平台 | 巡检、服务机器人、机载任务编排 | 任务级调度与能力治理（不替代硬实时运动控制） |
| 楼宇 / 厂区联动 | 传感触发后的跨设备短路径动作 | 区域侧按图调度，减少事事绕公有云 |

### 愿景：智能基础设施

架构成熟后，形态可能由「助手」扩展为「一层基础设施」（**非现行功能清单**）：能力包扩展、网关内私有数字助理、跨系统可治理的能力分发。

### 生态位

云端大模型提供深度认知；**Neo Agent 提供将智能接入真实系统的调度与执行脊梁**——目标可编排、能力可治理、边界可强制、记忆可本地留存。细则见 [`docs/applications.md`](docs/applications.md) 第 5–6 节。

---

## 适用对象

- 需要围绕明确目标运行 Agent 任务的个人或小团队  
- 希望同类目标执行路径可控、结果可复查的业务与运维  
- 希望部署轻便、按环境改配置即可的使用者  
- 需要**本机自主记忆**（偏好/事实跨会话保留，且不依赖外部 embedding 服务）的场景  

若目标是「最强编码 IDE」或「超大型工作流中台」，则与 Neo Agent 的产品方向不一致。

---

## 当前能力概览

1. **目标下达与执行**：自然语言输入；规划后执行，或仅输出计划。  
2. **声明式 DAG 调度**：将常用目标固化为流程，一键按图执行。  
3. **能力矩阵调用**：在策略范围内读写文件、检索仓库、执行已登记命令。  
4. **多种入口**：交互式终端、daemon、定时与管道。  
5. **自主记忆（本地向量库）**：写入、召回、启发式自动存储，以及模型侧 `memory_add`；不调用外部 embedding API。  

---

## 快速上手

1. 具备网络访问的 macOS 或 Linux 主机（用于调用所选大模型 API）。  
2. 在仓库根目录执行 `make`，生成二进制 `neo`。  
3. 复制并编辑配置：

```bash
cp config/config.json5.example config/config.json5
# 填写 api_key 与模型名称；如需自主记忆，开启 memory.vector（见下文）
```

4. 验证：

```bash
./neo "你是谁"
```

---

## 常用命令

在仓库根目录执行（须已配置有效 API 密钥）：

```bash
# 产品能力说明
./neo "用三句话说明 Neo Agent 适合完成哪些目标型任务"

# 执行目录中的 DAG
./neo dag run show_time
./neo dag run workspace_brief

# 自然语言目标：规划并执行 / 仅规划
./neo run "查看系统时间"
./neo plan "梳理当前仓库近期工作重点"

# 经能力矩阵读取文件并概括
./neo "请阅读 README.md，用三句中文概括产品价值。"

# —— 自主记忆（须先启用 memory.vector.enabled）——
./neo memory store "用户偏好深色模式"
./neo memory recall "深色模式"
./neo -v "请记住我喜欢用大号字体"   # auto_store 开启时自动写入本地库
```

更多示例见 [`docs/examples.md`](docs/examples.md)；记忆与 claw 配置见 [`docs/claw.md`](docs/claw.md)。

---

## 使用方式一览

| 意图 | 命令 |
|------|------|
| 单次问答 | `./neo "问题"` |
| 按已定 DAG 执行 | `./neo dag run <名称>` 或 `./neo run <名称>` |
| 自然语言目标（规划并执行） | `./neo run "目标"` |
| 仅规划、不执行 | `./neo plan "目标"` |
| 记忆写入 / 召回（不调用大模型） | `./neo memory store "…"` / `./neo memory recall "…"` |

---

## 自主记忆

Neo Agent 提供**本机自主记忆**：跨会话保留偏好与事实，检索增强注入对话，且**不依赖外部 embedding 服务**。启用后，系统可在对话中自动识别「请记住 / 偏好」类意图并写入本地库；模型亦可主动调用能力矩阵中的 `memory_add`。

### 配置

```json5
memory: {
  path: "MEMORY.md",           // vector 关闭时：全文截断注入
  max_chars: 4000,
  vector: {
    enabled: true,             // 开启本地向量记忆
    store: ".neo/memory.vdb",
    top_k: 5,
    dims: 64,
    auto_store: {
      enabled: true,           // 启发式自主写入（默认 false，建议按需打开）
      max_chars: 500,
    },
  },
}
```

### 行为说明

| 机制 | 说明 |
|------|------|
| **召回注入** | `vector.enabled` 时，按用户问题从本地库检索片段，注入 system prompt 的 `## Memory`；不再回退读取 `MEMORY.md` 全文 |
| **启发式自主存储** | `auto_store.enabled` 时，用户语句含「记住 / 偏好 / remember / prefer」等线索则自动写入向量库（`-v` 输出 `neo memory: auto-store`） |
| **模型主动存储** | 矩阵工具 `memory_add`：模型判断需持久化的事实时显式调用 |
| **CLI 运维** | `memory store` / `memory recall` 用于无大模型路径下的写入与试召回 |
| **数据边界** | 向量与文本 sidecar 落在 `vector.store`（默认 `.neo/`）；不写回 `MEMORY.md`；embedding 在进程内完成 |

权威说明与 claw 拼装顺序见 [`docs/claw.md`](docs/claw.md)。

---

## 文档索引

| 主题 | 文档 |
|------|------|
| 业务场景与定位 | [`docs/applications.md`](docs/applications.md) |
| 使用样例 | [`docs/examples.md`](docs/examples.md) |
| 能力矩阵 | [`docs/tool.md`](docs/tool.md) |
| DAG 调度 | [`docs/dag.md`](docs/dag.md) |
| 身份、规则与记忆 | [`docs/claw.md`](docs/claw.md) |
| 贡献约定 | [`AGENTS.md`](AGENTS.md) |
| English README | [`README.md`](README.md) |

---

## 开发者说明

构建与测试：`make` / `make test`。产品三角：**DAG ∥ Capability Matrix ∥ Policy**。约定见 [`AGENTS.md`](AGENTS.md)；架构见 [`docs/architecture.md`](docs/architecture.md)。
