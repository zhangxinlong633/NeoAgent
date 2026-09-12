# capabilities/local/

本机轻量命令能力：优先在本地完成、无需远端大模型即可产出结果，对应 [`docs/applications.md`](../../docs/applications.md) 基础层中的**成本与时延可控**路径。

| 文件 | 能力名 | 说明 |
|------|--------|------|
| `date_iso.json5` | `date_iso` | UTC ISO-8601 时间戳 |
| `uname_info.json5` | `uname_info` | `uname -a` |
| `pwd_print.json5` | `pwd_print` | 沙箱 cwd（`capability_matrix.root`） |
| `weather_wttr.json5` | `weather_wttr` | 经 `scripts/tools/weather-wttr.sh` 查询 wttr.in 短天气行 |

本目录无子目录。选型元数据见 [`AGENTS.md`](../../AGENTS.md) §4.1。
