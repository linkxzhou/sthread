# st_dnsserver

基于 `StServer<DnsConn, eUDP_CONN>` 的最小 DNS 权威样例：绑定 UDP 后用 `st_recvfrom` / `st_sendto` 应答。

默认监听 `0.0.0.0:5353`（免 root，不与系统 53 抢端口）。

## 能力范围

- 解析标准 DNS query（HEADER + QNAME + QTYPE / QCLASS）
- `*.bench.local` / `*.bench.sthread.local` 的 **TYPE_A / IN** → 固定 `127.0.0.1`，TTL 60
- **非 A**（以及区外 A）：空 ANSWER + NOERROR（plan/08 D2=b）
- 格式坏包：丢弃

不是完整权威实现（无 RFC 全覆盖、无压缩指针跟随、无 TCP）。

## 编译

```bash
# 仓库根目录
make lib
make -C app/st_dnsserver
# 或
make apps
```

## 运行

```bash
./main                # 0.0.0.0:5353
./main 127.0.0.1 5353
```

## 与 st_dns 联调

```bash
# 终端 1
./app/st_dnsserver/main 127.0.0.1 5353

# 终端 2
./app/st_dns/main -s 127.0.0.1 -p 5353 www.1.bench.local
./app/st_dns/main -s 127.0.0.1 -p 5353 -c 100 -n 1000
```

一键压测：仓库根目录 `make bench-dns`（见 [`scripts/bench_dns.sh`](../../scripts/bench_dns.sh)）。
