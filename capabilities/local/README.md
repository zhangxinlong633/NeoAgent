# capabilities/local/

本机轻量命令能力：优先在本地完成、无需远端大模型即可产出结果，对应 [`docs/applications.md`](../../docs/applications.md) 基础层中的**成本与时延可控**路径。

| 文件 | 能力名 | 说明 |
|------|--------|------|
| `date_iso.json5` | `date_iso` | UTC ISO-8601 时间戳 |
| `uname_info.json5` | `uname_info` | `uname -a` |
| `pwd_print.json5` | `pwd_print` | 沙箱 cwd（`capability_matrix.root`） |
| `weather_wttr.json5` | `weather_wttr` | 经 `scripts/tools/weather-wttr.sh` 查询 wttr.in 短天气行 |
| `train_query.json5` | `train_query` | 经 `scripts/tools/train_query.sh` 查询 12306 指定日期两站间 G/D/C 车次摘要 |
| `blender_showcase.json5` | `blender_showcase` | 本机 Blender 后台渲染展示静帧（金属环+玻璃球）；脚本见同目录 `blender_showcase.py` |
| `blender_cup.json5` | `blender_cup` | 本机 Blender 画简单陶瓷杯并渲染；脚本见 `blender_cup.py` |
| `blender_watermelon.json5` | `blender_watermelon` | 本机 Blender 画西瓜并渲染；脚本见 `blender_watermelon.py` |
| `blender_product_turntable.json5` | `blender_product_turntable` | 转盘上的简易产品静帧；脚本见 `blender_product_turntable.py` |
| `blender_export_glb.json5` | `blender_export_glb` | 生成网格并导出 GLB（附预览图）；脚本见 `blender_export_glb.py` |

本目录无子目录。选型元数据见 [`AGENTS.md`](../../AGENTS.md) §4.1。
