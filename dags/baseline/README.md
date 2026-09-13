# dags/baseline/

对应 [`docs/applications.md`](../../docs/applications.md) **基础层：复杂任务的稳定执行**——用冻结 DAG 替代纯 ReAct 长程漫游。

| 文件 | 图名 | 说明 |
|------|------|------|
| `show_time.json5` | `show_time` | `date_iso` → 打印 UTC ISO 时间 |
| `repo_pulse.json5` | `repo_pulse` | `git_status_short` → `git_log_five` → LLM 一段话汇总 |
| `blender_showcase.json5` | `blender_showcase` | `blender_showcase` → 本机 Blender 渲染展示静帧（需已安装 Blender） |
| `blender_cup.json5` | `blender_cup` | `blender_cup` → 本机 Blender 画简单杯子并渲染 |
| `blender_watermelon.json5` | `blender_watermelon` | `blender_watermelon` → 本机 Blender 画西瓜并渲染 |
| `blender_product_turntable.json5` | `blender_product_turntable` | `blender_product_turntable` → 转盘产品静帧 |
| `blender_export_glb.json5` | `blender_export_glb` | `blender_export_glb` → 导出 GLB 样例 |

依赖能力须已在矩阵中（`date_iso` 来自 `capabilities/local/`；git 能力来自 `capabilities/git/`；Blender 演示能力来自 `capabilities/local/`，需本机安装 Blender）。本目录无子目录。
