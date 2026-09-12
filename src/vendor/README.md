# src/vendor/

第三方源码 amalgamation 存放处。保持上游风格；升级时宜整文件替换，并运行 `make test` 验证。

| 文件 | 用途 |
|------|------|
| `yyjson.c` / `yyjson.h` | JSON5 配置与运行时 JSON |
| `BearHttpsClientOne.c` / `BearHttpsClient.h` | BearSSL 系 HTTPS 客户端（单文件 amalgamation）；由 `neo_http` 调用，业务代码勿直接 include。DNS：勿默认依赖库内 DoH；`neo_http` 用系统 `getaddrinfo` + `set_known_ips` 钉 IPv4（darwin 上勿开 `BEARSSL_USSE_GET_ADDRINFO`，TLS 易失败） |
| `md4c.c` / `md4c.h` / `md4c.LICENSE.md` | [MD4C](https://github.com/mity/md4c) Markdown 解析器；由 `neo_md_term` 做 CLI `--render` 终端渲染 |

不强制适用本仓库的中文注释约束（见 [`AGENTS.md`](../../AGENTS.md) §7）。本目录无子目录。
