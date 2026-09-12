# Daemon 多轮

交互式（TTY）：

```bash
./neo -D
# 等价：./neo daemon   ./neo --daemon
```

提示符：`User>` 输入，`neo>` 回复（默认 Markdown）。`exit` / `quit` / EOF 结束。`-d` 是 debug，不要和 `-D` 搞混。

Socket（无提示符，一发一收原文）：

```bash
./neo daemon --socket /tmp/neo.sock
echo "你好" | nc -U /tmp/neo.sock
```

内存轮次与 `-S` 落盘会话独立。细则见 [`docs/manual.md`](../../docs/manual.md) §4.5。
