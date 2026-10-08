# reports/

sthread 端到端压测报告目录（plan/08）。

| 文件 | 是否入库 | 说明 |
| --- | --- | --- |
| `baseline-http.md` | **是**（冻结） | 官方 HTTP 基线，一份；重跑请用脚本生成带时间戳的文件 |
| `baseline-dns.md` | **是**（冻结） | 官方 DNS 基线 |
| `baseline-curve.md` | **是**（冻结） | 官方并发曲线（中位数表、环境、限制） |
| `baseline-curve.csv` | **是**（冻结） | 曲线原始重复，勿手改数字 |
| `http-YYYYMMDD-HHMM.md` | 否（gitignore） | `make bench-http` 产出 |
| `dns-YYYYMMDD-HHMM.md` | 否（gitignore） | `make bench-dns` 产出 |
| `curve-*` | 否（gitignore） | `make bench-curve` 产出（csv / md / svg） |
| `perf-analysis-20261008.md` | **是** | plan/12 的测量记录（系统调用、VmSize、perf）。不替代冻结基线 |
| `*.log` | 否 | 脚本把 server stdout 落到这里 |

## 生成

仓库根目录：

```bash
make apps
make bench-http          # 默认 BENCH_PROFILE=smoke；内部 TRACE=0 重编
make bench-dns
BENCH_PROFILE=medium make bench-http
BENCH_PROFILE=all make bench
make bench-curve             # 并发曲线；默认每个点 3 次，n = max(50000, c*30)
```

`make bench-*` 会以 **TRACE=0** 重编 lib/apps（默认 `TRACE=1` 的 `LOG_TRACE` 会淹没 SUMMARY 并拖垮 QPS）。冻结基线见 `baseline-*.md`。

环境变量：

| 变量 | 默认 | 用途 |
| --- | --- | --- |
| `BENCH_PROFILE` | `smoke` | `smoke` / `medium` / `heavy` / `all` |
| `BENCH_HTTP_PORT` | `8765` | HTTP 监听端口 |
| `BENCH_DNS_PORT` | `5353` | DNS 监听端口 |
| `BENCH_HTTP_HOST` / `BENCH_DNS_HOST` | `127.0.0.1` | 本机地址 |
| `BENCH_CURVE_CONCS` | `1 10 50 100 200 500 1000` | `make bench-curve` 的并发列表（空格分隔） |
| `BENCH_CURVE_REPEATS` | `3` | 每个点重复次数，图和表用中位数 |
| `BENCH_CURVE_MIN_N` | `50000` | 每个 HTTP 点至少这么多请求 |
| `BENCH_CURVE_PER_CORO` | `30` | `n = max(MIN_N, c * PER_CORO)` |
| `BENCH_CURVE_DNS` | `1` | `0` 则跳过 DNS 突发探测（不进曲线） |

脚本用 **PID** 停服务（`trap`），不会 `pkill main`。

数字不可跨机器直接对比：请看报告里的 OS / arch / commit。Linux 与 macOS 数字不必一致。
