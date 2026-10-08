# 性能剖析记录（2026-10-08）

> 给 [`plan/12-performance-optimization.md`](../plan/12-performance-optimization.md) 的证据。数字来自这次云主机上的实测，**不要手改**。`perf.data`、`strace` 原文和 `reports/curve-*` 不入库。

## 环境

| 项 | 值 |
| --- | --- |
| 日期 (UTC) | 2026-10-08 |
| 提交 | `849ebf1` |
| OS | Linux 6.12.94+ x86_64，KVM，4 vCPU |
| CPU | Intel(R) Xeon(R) Processor（家族 6，型号 207） |
| 内存 | 16398384 kB；`vm.max_map_count=65530` |
| nofile | 524288 |
| 编译器 | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0，`x86_64-linux-gnu` |
| glibc | 2.39 |
| 官方曲线构建 | `make apps TRACE=0`（`DEBUG=1` → `-g2`，`ASAN=0`） |
| 优化级别 | 库 `src/makefile` 在 `ST_CXXFLAGS` 后加 `-O1`；app 只有 `ST_CXXFLAGS`，**没有 `-O`**（即 `-O0`）。`http_parser.c` 单独 `-g` |
| perf | `/usr/lib/linux-tools-6.8.0-146/perf`。本机 KVM 上 `cycles`/`instructions` 为 `<not supported>`，采样用软件事件 `cpu-clock`。`kernel.perf_event_paranoid=-1` 只在这台机器上临时放开 |

`make.inc` 的默认 `TRACE=1` 会定义 `-DTRACE`。`make bench-curve` 强制 `TRACE=0`，`LOG_TRACE` 被编译成空宏。

## 1. `make bench-curve`（未钉核）

负载是 `st_httpserver` + `st_httpclient`，不是 `st_wrk`。每个点 `n = max(50000, c*30)`，重复 3 次，下表是中位数。0 错误，服务端每轮重启后仍存活。

| conc | 本次 QPS 中位 | 本次 p50 / p99 (ms) | 冻结基线 QPS（`e62c158`，同机型） |
| --- | ---: | --- | ---: |
| 1 | 20275.75 | 0 / 1 | 21710.81 |
| 10 | 31036.62 | 0 / 1 | 38461.54 |
| 50 | 27932.96 | 2 / 3 | 38051.75 |
| 100 | 35971.22 | 3 / 3 | 37037.04 |
| 200 | 36576.44 | 5 / 6 | 36576.44 |
| 500 | 35063.11 | 14 / 20 | 36549.71 |
| 1000 | 23957.83 | 24 / 30 | 35868.01 |

c=1000 三次是 23957.83 / 34626.04 / 23912.00，中位数被偏低的两次拉下来。冻结基线在 c=500 也有过 22925 对 36792 的摆动。形状一致：c=1 约 2.0–2.2 万，c≥10 在安静时顶在 3.5–3.8 万，p50 约等于 `conc / QPS`（c=200：200/36576×1000 ≈ 5.5 ms，实测 5 ms）。

同一次曲线里的 DNS 突发（`-n == -c`，1 ms 时钟，不是持续速率）：

| conc | QPS 中位 | elapsed ms 中位 | errors |
| --- | ---: | ---: | ---: |
| 1 | 1000 | 1 | 0 |
| 100 | 9090.91 | 11 | 0 |
| 1000 | 55555.56 | 18 | 0 |

`make bench-dns`（默认 smoke，`-c 10 -n 10`）：`success=10 fail=0 qps=909.09 elapsed_ms=11`。

## 2. 短连接 HTTP：每次请求的系统调用

`strace -c` 挂在已经起来的进程上。服务端 2000 次请求（`-c 30`），客户端另一次 2000 次。strace 会把 QPS 打到几千，下面只看次数。

服务端（约 14 次系统调用 / 请求）：

| 调用 | 次数 | 约合 / 请求 | 备注 |
| --- | ---: | ---: | --- |
| `mmap` | 2000 | 1.00 | 每个连接一块栈 |
| `accept` | 2067（67 次 EAGAIN） | 1.03 | |
| `epoll_ctl` | 6134（2000 次失败） | 3.07 + 1 次 ENOENT | 失败次数 = 请求数 |
| `recvfrom` | 2000 | 1.00 | |
| `sendto` | 2000 | 1.00 | |
| `close` | 2000 | 1.00 | |
| `write` | 4000 | 2.00 | 见下，两行 ERR 日志 |
| `rt_sigprocmask` | 8201 | 4.10 | glibc `get/swapcontext` 碰信号掩码 |
| `epoll_wait` | 134 | 0.07 | 一次等待收多条事件 |

客户端（协程复用，`mmap` 只有启动时的 58 次）：

| 调用 | 次数 | 约合 / 请求 | 备注 |
| --- | ---: | ---: | --- |
| `socket` | 2000 | 1 | |
| `connect` | 4000（2000 次失败） | 2 | 非阻塞 `EINPROGRESS` 再完成一次 |
| `epoll_ctl` | 10000（2000 次失败） | 5 | |
| `sendto` / `recvfrom` / `close` | 各 2000 | 1 | |
| `fcntl` | 4257 | ~2 | |
| `rt_sigprocmask` | 4262 | 2.1 | 工人协程不按请求重建 |
| `epoll_wait` | 134 | 0.07 | |

`-k` 对这个服务端无效。`-c 40 -n 15000` 三次：短连接 QPS 35129 / 30738 / 34722，`-k` 为 37313 / 36946 / 36408。两边 stdout 都是 30004 行（15000 请求 + 1 次健康检查，各两行 ERR）。服务端始终 `Connection: close`。

## 3. 日志内容

`st_httpserver` 把级别设成 `LLOG_ERR`，日志写到 fd 1。15000 次请求的 stdout 里各有 15001 行：

- `st_thread.cc:443` `del event failed`
- `st_thread.cc:409` `del fd: %d failed`

一次失败的 `EPOLL_CTL_DEL`（兴趣已经被 `Schedule` 摘掉）打两行。`TRACE=0` 消不掉它们。

`TRACE=1` 重编后，`-c 10 -n 4000` 两次都是 `elapsed_ms=120`（QPS 33333，1 ms 时钟的格子），服务端 stdout 只有 1 行。`LOG_TRACE` 在 `LLOG_ERR` 下不会真正打印，只多一次 `LogAble`。这不能证明默认 `TRACE=1` 便宜：默认日志级别是 `LLOG_PVERB`，在调用 `LOG_LEVEL` 之前会把每次入队都写出来。压测脚本继续用 `TRACE=0`。

## 4. 虚拟内存

服务端每接受一个连接就 `new StThread` → `context_make` 里 `malloc` 约 266240 字节，协程函数末尾 `Yield` 后再也不还到池里。

| 请求数 | VmSize 前 → 后 | 每请求 |
| --- | --- | ---: |
| 4000 | 13400 → 1075216 kB | 272006 B |
| 16000 | 13400 → 4258792 kB | 271846 B |
| 20000 | 13400 → 5320072 kB | 271702 B |

公式 `MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE` = 266240。多出来的约 5.5 KB 是 `Stack` 结构、协程对象和分配器开销。`/proc/pid/maps` 行数停在 42（相邻匿名映射被内核合并），所以没有顶到 `max_map_count`，但地址空间照样线性涨。20000 请求的 RSS 从 6032 kB 到 192384 kB，约 9.5 KB/请求（栈顶一两页被碰过，其余只占虚拟地址）。

## 5. perf（`cpu-clock`，dwarf 调用栈）

钉核：服务端 CPU 0，客户端 CPU 1。`-c 50 -n 30000`，服务端 375 个样本，客户端 270 个。下面是 children（一个样本可以同时算进多层）。

服务端：

| 符号 | children |
| --- | ---: |
| `do_syscall_64` | 62.93% |
| `CallBack` | 51.73% |
| `CreateThread` / `InitStack` | 26.93% |
| `context_make` | 25.07% |
| `st_send` / `SendData` | 21.87% |
| 缺页（`do_user_addr_fault`） | 20.53% |
| `malloc` | 16.53% |
| `swapcontext` 自身 | 0.53% |

客户端：

| 符号 | children |
| --- | ---: |
| `do_syscall_64` | 84.81% |
| `ensure_conn` | 43.33% |
| `StClientConnection::Create` | 42.59% |
| `st_connect` | 36.30% |
| `http_parser_execute` 自身 | 1.85% |

扁平采样里还能看到 `__nf_conntrack_find_get`、`ipt_do_table`、`nf_nat_ipv4_pre_routing`。这台 VM 的 loopback 过 netfilter。绝对 QPS 含这份税，同机前后对比才有意义。

`perf stat` 的 `cpu-clock:u` 和 `cpu-clock:k` 在这台内核上打出几乎相同的总数，不能拿来拆用户态/内核态。拆分用 `/usr/bin/time` 和 `/proc/pid/stat`：

- 客户端 `-c 50 -n 20000`：user 0.043 s，sys 0.486 s，墙钟 0.536 s（约 92% 在内核）
- 服务端同一次量级：user 8 tick，sys 54 tick（`CLK_TCK=100`，约 87% 在内核）

## 6. `-O2` 对照

先把 `-O2` 二进制和默认二进制都链好（静态链进 `libmthread.a`），再交替跑 `-c 10 -n 15000`，避免 conntrack 表涨落被当成编译器效果。

| 轮 | 默认（库 `-O1`，app `-O0`） | 库和 app 都 `-O2` |
| --- | ---: | ---: |
| 1 | 24590 | 25253 |
| 2 | 26316 | 26738 |
| 3 | 25000 | 26738 |

差在噪声里（大约 0 到 +7%）。更早一次未交替的「默认 3.5 万、`-O2` 2.6 万」对不上这张表，归到同机漂移（冻结基线自己的 c=500 也飘过 1.4 万）。`-O2` 的系统调用种类和默认一致（800 请求：`mmap` 800，`rt_sigprocmask` 3320，`epoll_ctl` 2480 其中 800 次失败，`write` 1600）。

## 7. UDP 服务端在空转

`st_dnsserver` 不按查询建协程（`VmSize` 在查询前后保持 12928 kB）。它在 `CreateSocket` 之后把事件项改成只读再 `Add`。epoll 的 `AddEvent` 会把新掩码和旧掩码按位或（`stlib/st_epoll.h`）；kqueue 的同名函数会 `EV_DELETE` 掉不要的过滤器。UDP 套接字一直可写，所以 Linux 上 `EPOLLOUT` 去不掉。

空闲 0.3 s：user 15 tick + sys 15 tick = 30 tick，整颗核。处理完一次查询后再空闲 0.3 s，仍然约 30 tick。

挂上 strace 之后看到的 6 万次 `epoll_ctl` / `recvfrom` 失败，大部分是这段空转，不是「每个查询 100 次系统调用」。因此第 1 节的 DNS QPS **不能**当成 UDP 数据通路的吞吐。
