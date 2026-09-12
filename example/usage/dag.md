# DAG / plan / run

## 跑 catalog 里的具名 DAG

```bash
./neo dag run show_time
./neo dag run workspace_brief
./neo run show_time
```

## 自然语言：规划并执行 / 仅规划

```bash
./neo run "查看系统时间"
./neo plan "梳理当前仓库近期工作重点"
./neo plan --steps 4 "解释 neo plan 与 neo dag run 的区别"
./neo plan -o /tmp/neo-plan.json5 "检查一下当前仓库是否干净"
```

## Profile

```bash
./neo -p demo dag run demo_loop
```

细则：[`docs/dag.md`](../../docs/dag.md)、[`docs/examples.md`](../../docs/examples.md)。
