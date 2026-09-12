# capabilities/

本目录为 **Capability Matrix** 的文件化能力包：一能力对应一个 JSON5 文件。配置 `capability_matrix.directory` 指向本目录后，启动时按 `manifest.json5` 的 `load[]` **逐项**扫描子目录中的 `*.json5` / `*.json`，合并进矩阵。

权威说明：[`docs/tool.md`](../docs/tool.md)、[`docs/manual.md`](../docs/manual.md)、[`docs/examples.md`](../docs/examples.md)（含 **开箱组合**）、[`docs/applications.md`](../docs/applications.md)、[`docs/architecture.md`](../docs/architecture.md)、[`AGENTS.md`](../AGENTS.md) §4、§4.1。

## 职责边界

- 定义可执行的命令能力（`name`、`argv`、`parameters` 及选型元数据）。
- 不替代 builtin 路径工具，也不构成与矩阵并行的第二套名字空间。
- `proposed/` 与未列入 `load` / 未列入场景内 `enabled.json5` 的定义默认不加载。
- 加载器**不递归**：能力文件放在场景子目录内，且该目录名须出现在 `manifest.load` 中。

## 与应用场景的对应

| 场景层次（applications） | 本目录落点 | 是否默认加载 |
|--------------------------|------------|--------------|
| 基础层：本机轻处理 / 成本可控 | `local/` | 是 |
| 基础层：工程化工具调用（仓库探针） | `git/` | 是 |
| 基础层：Unix 工具集（百余定义 + 白名单） | `unix/` | 是（仅 `enabled.json5` 名单） |
| 行业层：工业 / 边缘执行器 | `industry/` | 否（预留） |
| 未来层：能力分发标准 | 非本仓库交付 | — |

## 子目录

| 子目录 | 职责 |
|--------|------|
| `local/` | 本机系统探针（时间、主机、cwd） |
| `git/` | Git 短查（status / log） |
| `unix/` | Unix 常用工具定义全集 + `enabled.json5` 装载白名单 + `CATALOG.md` |
| `industry/` | 行业执行器预留；**不**列入 `load` |
| `proposed/` | `propose_capability` 草稿；默认不进入矩阵 |

根目录 `manifest.json5` 声明 `load` 与 `proposed`。场景目录内可选 `enabled.json5`（`load: ["name", ...]`）进一步裁剪。
