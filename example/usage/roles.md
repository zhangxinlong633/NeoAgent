# 同会话角色切换（`--role`）

在 `config/config.json5` 增加（可参考 `config/config.json5.example` 注释）：

```json5
roles: {
  researcher: {
    description: "调研",
    prompt: "本轮只做调研：列事实与待核实点；不要写终稿。",
  },
  writer: {
    prompt: "本轮只写面向用户的终稿；沿用历史中的调研结论。",
  },
}
```

然后同一 `-S`（或默认 `default`）切换角色：

```bash
./neo -S demo --role researcher "先调研：纸飞机和载人飞船差在哪"
./neo -S demo --role writer "根据上文写成短文"
```

本轮 system 会注入 `## Role`；落盘的 assistant 前缀为 `[researcher] ` / `[writer] `，方便后续角色读历史。

未配置 `roles` 时使用 `--role` 会报错退出。
