# 单次对话与默认会话

## 纯问答

```bash
./neo "用三句话介绍 Neo 的产品三角"
```

Markdown 默认渲染到终端；只要原文：

```bash
./neo --no-render "用三句话介绍 Neo 的产品三角"
```

显式打开渲染（与默认相同）：

```bash
./neo -R "用表格对比 DAG 与 Capability Matrix"
```

## 默认会话 `default`

不写 `-S` 时，历史写入 `.neo/sessions/default.json`，下次直接续聊：

```bash
./neo "飞船怎么做"
./neo "展开第 4 种"
```

## 指定配置 / 模型

```bash
./neo -c config/config.json5 -m deepseek-chat "南京有哪些必去的地方？"
./neo -v "请记住我喜欢大号字体"
```

`-v` / `-d` 诊断走 stderr，不污染 stdout。
