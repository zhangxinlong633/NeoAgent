# 能力矩阵与工具

## 让模型调本地能力

```bash
./neo "请阅读 README.md，用三句中文概括产品价值。"
./neo "看下南京今天的天气"
./neo "看一下明天南京到北京的高铁信息"
```

高铁能力为 `train_query`（`capabilities/local/train_query.json5`）。站名可用中文城市/站名。zsh 中带 `?` 的句子请加引号：

```bash
./neo "可以使用其他途径查一下吗？"
```

## 记忆 CLI（不调用大模型）

需配置 `memory.vector.enabled`：

```bash
./neo memory store "用户偏好深色模式"
./neo memory recall "深色模式"
```

## 矩阵与 Policy

1. 能力登记在 `capabilities/` + `capability_matrix`。
2. `shell_enabled` 默认关；`http_get` 需 `http_fetch_enabled` + `http_allow_hosts`。
3. 细则：[`docs/tool.md`](../../docs/tool.md)。
