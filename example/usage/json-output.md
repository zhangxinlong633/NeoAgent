# 机读输出：OpenAI JSON 与文件

诊断仍在 stderr；程序请解析 stdout 或 `-o` 文件。

## OpenAI `chat.completion` JSON

```bash
./neo -j "用一句话介绍 Neo Agent"
```

取正文（与官方 SDK 一致）：

```bash
./neo -j "用一句话介绍 Neo Agent" | jq -r '.choices[0].message.content'
```

成功时大致形状：

```json
{
  "id": "chatcmpl-neo-…",
  "object": "chat.completion",
  "created": 1690000000,
  "model": "…",
  "choices": [
    {
      "index": 0,
      "message": { "role": "assistant", "content": "…" },
      "finish_reason": "stop"
    }
  ],
  "usage": { "prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0 },
  "neo": { "session": "default", "role": null }
}
```

失败时为 `{"error":{"message","type","code","param"}}`，进程 exit 1。`usage` 目前为占位 0。

## 写入文件

```bash
# 只写原文，不刷终端
./neo -o /tmp/neo-reply.txt "用三句话介绍产品三角"

# JSON 写入文件
./neo -j -o /tmp/neo-reply.json "用一句话介绍 Neo"
```

## 与 plan 的 `-o`

`neo plan -o FILE` / `neo run … -o FILE` 仍表示保存**规划出的 dags JSON**，与聊天模式按子命令区分。
