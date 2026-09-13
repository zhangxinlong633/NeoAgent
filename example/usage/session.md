# 会话：具名、新开、混合

## 具名会话

```bash
./neo -S ship "飞船怎么做"
./neo -S ship "展开第 4 种：真正的载人/货运飞船"
```

## 新开默认会话

把当前 `default` 归档为 `YYYYMMDD-HHMMSS`，再在空的 `default` 上聊：

```bash
./neo -N "换个话题从头聊"
# 也可只归档、不提问：
./neo -N
```

`-N` 不能与 `-S` 同用。

## 多会话混合注入

按序拼接历史，本轮只写回**第一个** id：

```bash
./neo -S cook "怎么做番茄炒蛋"
./neo -S ship,cook "结合两边的话题继续说"
```

## 列表与清除

```bash
./neo --session-list
./neo --session-clear ship
./neo --session-clear default
```

落盘目录：`.neo/sessions/<id>.json`。轮数上限见配置 `session.max_turns`（默认 10）。

这与 `neo -D`（daemon）**无关**：daemon 只用内存，不读写上述文件。对照见 [`daemon.md`](daemon.md) 与 [`docs/manual.md`](../../docs/manual.md) §4。
