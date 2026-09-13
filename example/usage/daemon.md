# Daemon 多轮

交互式（TTY）：

```bash
./neo -D
# 等价：./neo daemon   ./neo --daemon
```

提示符：`User>` 输入，`neo>` 回复（默认 Markdown）。`exit` / `quit` / EOF 结束。`-d` 是 debug，不要和 `-D` 搞混。

挂载落盘会话（与 chat `-S` 同语义：多 id 拼接加载，只写回第一个）：

```bash
./neo -D -S ship
# stderr: neo daemon: session=ship
# 提示符: User[ship]>
```

Socket（无提示符，一发一收原文；也可带 `-S`）：

```bash
./neo daemon --socket /tmp/neo.sock
./neo daemon -S ship --socket /tmp/neo.sock
echo "你好" | nc -U /tmp/neo.sock
```

## 与 `-S` 的关系

| | `./neo -D` | `./neo -D -S id` | chat `./neo -S id` |
|--|------------|------------------|---------------------|
| 历史 | 仅内存 | 启动灌入 + 每轮写回 | 每轮读写落盘 |
| 省略 `-S` | 纯内存（不挂 `default`） | — | 默认 `default` |

可恢复续聊也可用 [`session.md`](session.md) 的非 daemon 路径。细则：[`docs/manual.md`](../../docs/manual.md) §4。
