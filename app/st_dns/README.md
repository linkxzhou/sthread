# st_dns

UDP DNS 客户端示例。压测请指向本仓库 [`st_dnsserver`](../st_dnsserver/README.md)，不要用公网合成域名。

## 编译

```bash
make lib
make -C app/st_dns
# 或 make apps
```

## 运行

```bash
./main -s 127.0.0.1 -p 5353 www.1.bench.local
./main -s 127.0.0.1 -p 5353 -c 100 -n 1000 -q
./main -h
```

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `-s` | `8.8.8.8` | 上游 IP；压测用 `127.0.0.1` |
| `-p` | `53` | 上游端口；本地权威默认 **5353** |
| `-c` | `1` | 协程数 |
| `-n` | 等于 `-c` | 总查询次数 |
| `-t` | `10000` | 单次超时（毫秒） |
| `-q` | 关 | 安静（提高日志阈值） |
| `name` | `www.{i}.bench.local` | 省略则按序号生成区内名 |

查询结束后进程**退出**（不再 `Frame::Loop(true)` 挂死）。stdout 有可 grep 的 `SUMMARY success=… fail=… qps=…`。

## 压测

```bash
# 仓库根
make bench-dns                 # 默认 BENCH_PROFILE=smoke
BENCH_PROFILE=medium make bench-dns
```

见 [`scripts/bench_dns.sh`](../../scripts/bench_dns.sh)、[`reports/README.md`](../../reports/README.md)。

## 限制

- 只解析 TYPE_A；兼容宏 `mt_init_frame` 仍可用，main 已改 `st_init_frame`
- 默认 `8.8.8.8:53` 依赖公网，不作为 QPS 基线
