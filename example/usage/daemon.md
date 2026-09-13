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

## 与 `-S` 的关系（今日行为）

| | Daemon `-D` | 具名会话 `-S` |
|--|-------------|---------------|
| 历史 | 进程内存 | `.neo/sessions/<id>.json` |
| 退出后 | 丢失 | 可续聊 |
| 互相读写 | **否**（带 `-S` 也会被忽略） | 不进 daemon |

要可恢复多轮 → 用 [`session.md`](session.md) 的 `-S`，不要用 daemon。  
本机连续 REPL、不在乎落盘 → 用本节的 `-D`。

细则与推荐路径：[`docs/manual.md`](../../docs/manual.md) §4。
