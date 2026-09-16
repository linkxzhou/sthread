# reports/

sthread 端到端压测报告目录（plan/08）。

| 文件 | 是否入库 | 说明 |
| --- | --- | --- |
| `baseline-http.md` | **是**（冻结） | 官方 HTTP 基线，一份；重跑请用脚本生成带时间戳的文件 |
| `baseline-dns.md` | **是**（冻结） | 官方 DNS 基线 |
| `http-YYYYMMDD-HHMM.md` | 否（gitignore） | `make bench-http` 产出 |
| `dns-YYYYMMDD-HHMM.md` | 否（gitignore） | `make bench-dns` 产出 |
| `*.log` | 否 | 脚本把 server stdout 落到这里 |

## 生成

仓库根目录：

```bash
make apps
make bench-http          # 默认 BENCH_PROFILE=smoke
make bench-dns
BENCH_PROFILE=medium make bench-http
BENCH_PROFILE=all make bench
```

环境变量：

| 变量 | 默认 | 用途 |
| --- | --- | --- |
| `BENCH_PROFILE` | `smoke` | `smoke` / `medium` / `heavy` / `all` |
| `BENCH_HTTP_PORT` | `8765` | HTTP 监听端口 |
| `BENCH_DNS_PORT` | `5353` | DNS 监听端口 |
| `BENCH_HTTP_HOST` / `BENCH_DNS_HOST` | `127.0.0.1` | 本机地址 |

脚本用 **PID** 停服务（`trap`），不会 `pkill main`。

数字不可跨机器直接对比：请看报告里的 OS / arch / commit。Linux 与 macOS 数字不必一致。
