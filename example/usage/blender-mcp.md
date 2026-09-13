# Blender MCP（可选，不默认开）

Neo 的 Blender **默认路径**是本机 headless 能力（无需打开 Blender GUI）：

```bash
./neo dag run blender_cup
./neo dag run blender_product_turntable
./neo dag run blender_export_glb
```

若需要**交互式**控制已打开的 Blender（选物体、改场景、执行插件命令），可选用社区 stdio MCP bridge。以下以 [ahujasid/blender-mcp](https://github.com/ahujasid/blender-mcp) 为例；**非 Neo 捆绑依赖**，CI 不要求安装。

## 前置

1. 本机已安装 Blender，并启用 bridge 自带的 addon（按其 README：`uvx blender-mcp …` 安装/启用）。
2. 在 Blender 侧栏启动 addon 的 socket 服务（默认 `localhost:9876`）。
3. 本机可运行 `uvx`（或把 `command` 写成 `uvx` 的绝对路径）。

## Neo 配置片段（注释样例，勿默认提交进生产配置）

复制到 `config/config.json5` 的 `capability_matrix` 内（**保持注释直到你真正需要**）：

```json5
capability_matrix: {
  enabled: true,
  root: ".",
  directory: "capabilities",
  // --- optional Blender MCP (stdio) — NOT enabled by default ---
  // mcp_servers: [
  //   {
  //     name: "blender",
  //     // Prefer absolute path if GUI-launched Neo cannot see PATH:
  //     // command: "/opt/homebrew/bin/uvx",
  //     command: "uvx",
  //     args: ["blender-mcp"],
  //     enabled: true,
  //   },
  // ],
}
```

当前 Neo **不解析** `mcp_servers[].env`。若 bridge 需要改主机/端口，在启动 Neo 的 shell 里导出后再跑：

```bash
export BLENDER_HOST=localhost
export BLENDER_PORT=9876
./neo -v "用 Blender 列出当前场景物体"
```

矩阵对外名形如 `mcp_blender_<tool>`（server 名 + tool 名，非法字符变 `_`）。权威 MCP 说明见 [`docs/tool.md`](../../docs/tool.md)。

## Policy / 安全

- Bridge 常暴露 **任意代码执行** 类工具（名称因版本而异，常见含 `execute_code` / 类似语义）。Neo v1 **不会**按名自动裁剪 MCP 工具：一旦 server 握手成功，其 `tools/list` 全部进入 Capability Matrix。
- 建议：
  1. 演示与可重复产出优先用 `blender_*` headless 命令能力，不要开 MCP。
  2. 仅在本机、可信场景短暂启用 `mcp_servers`；用完把条目注释掉或 `enabled: false`。
  3. 不要在共享/无人值守环境默认挂载 Blender MCP。
- `shell_enabled` 与 MCP 无关；关 shell **不能**挡住 MCP 的 `execute_code`。

## 与 headless 套餐的分工

| 路径 | 适用 |
|------|------|
| `blender_showcase` / `cup` / `watermelon` / `product_turntable` / `export_glb` | 无 GUI、可脚本、可进 DAG catalog |
| Blender MCP | 已打开的工程、交互建模、插件工作流 |

不强制 CI 安装 Blender 或 `uvx`；缺依赖时 MCP server 启动失败只会跳过该 server，其它矩阵行仍可用。
