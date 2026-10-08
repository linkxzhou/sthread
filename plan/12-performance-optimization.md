# 12 · 性能优化（短连接曲线为什么在 c=10 就平了）

> **状态：本轮三项已落地（2026-10-08）。** 基线提交 `849ebf1`。改前测量在 [`reports/perf-analysis-20261008.md`](../reports/perf-analysis-20261008.md)，落地数字在 [`reports/perf-p1-p3-20261008.md`](../reports/perf-p1-p3-20261008.md) 与 §12。
>
> **本轮只做三件事**：P1（O1 epoll 掩码替换 + O2 去掉多余 `EPOLL_CTL_DEL` / 两行 `LOG_ERROR`）、P3（O3 协程栈复用）、默认栈改为 **128KB**（`STACK = 131072`，取代 D6「保持 260096」）。**延期**：O7 / `-O2`、O5、O4 keepalive 曲线、O6、O9、O8、P8 多进程样例。拍板全文在 §11。
>
> 硬约束沿用总索引：C++98；`.clang-format`（LLVM 基线 + `m_x_`）+ CI 的 clang-format-18；**只用 makefile**；零第三方运行时依赖；vendored 代码保留 [`COPYRIGHT`](../COPYRIGHT)；**不发明根 `LICENSE`**；注释用中文。Windows 不支持。平台：Linux x86_64/arm64、macOS、Android arm64-v8a / x86_64。三件套闸门仍是 `make lib`、`make -C tests run`、`make -C stlib/tests run`，外加 `make apps`。`make clean` 之后 `git status --porcelain --ignored` 必须为空。

---

## 1. 目标与非目标

### 1.1 目标

解释并抬高 `make bench-curve` 看到的平台期：短连接 HTTP 从并发 10 起吞吐停在大约 3.6–3.8 万 QPS，延迟随并发线性变长。优化按「同机前后对比」验收，并分开客户端和服务端的成本。

### 1.2 非目标

| # | 不做 | 理由 |
| --- | --- | --- |
| N1 | 不改 `MEM_PAGE_SIZE`（2048）、`eConnType` 数值、`st_*` / `tcp_sendrecv` / `udp_sendrecv` 的签名和状态码。`ty`/`tx` 拆分、`ss_sp`/`ss_size` 余量不动 | 公开 API。`STACK` 按 §11 改为 131072，不再冻在 260096 |
| N2 | 不做 M:N，不让协程跨 OS 线程 | `Instance<T>()` 仍是线程局部 |
| N3 | 不改 `st_wrk` 的命令行、`SUMMARY` 行和 `JSON` 行 | 08 的解析约定 |
| N4 | 不把 QPS 数字写进 CI | 这台 KVM 上同一点可以差 1 万以上 |
| N5 | 不在这一篇里实现 07-D1 的真连接池 | 先用样例自己持有连接把曲线分开；池是另一项 |
| N6 | 不引入 CMake、gtest、boost.context，或其他运行时库 | makefile + 现有 ucontext |
| N7 | 不用这次偏吵的曲线覆盖 `reports/baseline-curve.*` 和 readme 性能表 | 见 D7 |

---

## 2. 这次实际测的是什么

`scripts/bench_curve.sh` 起的是 `app/st_httpserver/main` 和 `app/st_httpclient/st_httpclient`。`st_wrk` 不在这条曲线上：它的 `-d` 只是时长标签，每个工人打完一批就退出，撑不起持续速率。曲线注释和冻结基线都已经写了这一点。

每个 HTTP 点：`n = max(50000, 并发 × 30)`，重复 3 次取中位数，客户端超时 15000 ms。服务端每轮重启，因为协程栈不回收（下面第 4.2 节）。连接是短的：服务端写死 `Connection: close`，模板参数是 `eTCP_CONN`。

编译：`make bench-curve` → `make apps TRACE=0`。

| 目标 | 实际标志 |
| --- | --- |
| `libmthread` | `src/makefile`：`C_ARGS = $(ST_CXXFLAGS) -O1`（`COVERAGE=1` 时不加 `-O1`，好跟 coverage 的 `-O0` 错开） |
| 各个 app | `FLAGS = $(ST_CXXFLAGS)`，其中没有 `-O`，gcc 默认 `-O0`。`DEBUG=1` 再加 `-g2` |
| `http_parser.c` | `app/st_httpclient/makefile` 只写了 `-g` |
| `TRACE` | 默认 1。压测强制 0，`LOG_TRACE` 变成空宏 |

`-g2` 不改变生成的指令，只是调试信息。真正要分开的是 `-O0` / `-O1` / `-O2`。

机器（与冻结基线同一档）：Linux 6.12.94+，x86_64，4 vCPU KVM，Intel Xeon（家族 6 / 型号 207），16 GB，g++ 13.3.0，glibc 2.39，`nofile=524288`。硬件性能计数器不可用（`perf stat` 里 `cycles` 是 `<not supported>`），调用栈用 `cpu-clock` + DWARF。

---

## 3. 曲线复现

完整表在分析记录第 1 节。中位数：

| conc | 本次 QPS | p50 / p99 (ms) | 冻结 QPS（`e62c158`） |
| --- | ---: | --- | ---: |
| 1 | 20276 | 0 / 1 | 21711 |
| 10 | 31037 | 0 / 1 | 38462 |
| 50 | 27933 | 2 / 3 | 38052 |
| 100 | 35971 | 3 / 3 | 37037 |
| 200 | 36576 | 5 / 6 | 36576 |
| 500 | 35063 | 14 / 20 | 36550 |
| 1000 | 23958 | 24 / 30 | 35868 |

0 错误。c=200 与冻结基线同一中位数。c=10 和 c=1000 比冻结基线吵：c=1000 三次是 23958 / 34626 / 23912。钉在 CPU 0/1 上、`n=20000` 的安静对照，c=1 两次都是 20040，c=10 是 36364 和 37736，c=100 是 35778 和 35273，又回到冻结基线的 3.5–3.8 万。

所以平台期还在，绝对数字有 ±15% 甚至更大的同机漂移（冻结基线 c=500 也曾出现 22925 对 36792）。验收只能「同一次实验里、同一台机器、取中位数」，不能拿单次结果去对 readme 上的表。

延迟是排队，不是另一条变慢的路径。单线程服务速率大约 3.6 万/秒时，并发 c 的平均等待是 `c / 36000` 秒：c=200 → 5.5 ms（实测 p50 = 5），c=500 → 14 ms（实测 14），c=1000 → 28 ms（实测 24–30）。c=1 的 2.0–2.2 万 QPS 表示一次来回大约 45–50 µs；c≥10 只抬到大约 1.8 倍，因为客户端和服务端可以各占一颗核，重叠之后顶在较慢的那一侧（大约 26 µs）。两边都要改，只改服务端抬不满。

---

## 4. 瓶颈（按证据，不按猜测）

### 4.1 时间几乎都在内核里

客户端 `-c 50 -n 20000`：user 0.043 s，sys 0.486 s（约 92% 内核）。服务端同量级的 tick 大约 user 13% / sys 87%。`perf` 里服务端 63%、客户端 85% 的样本落在 `do_syscall_64` 下面。

因此「把 app 从 `-O0` 编到 `-O2`」动不到大头。交替跑默认二进制和全 `-O2` 二进制（`-c 10 -n 15000`，三轮）：默认 24590 / 26316 / 25000，`-O2` 25253 / 26738 / 26738。系统调用种类不变。**这条曲线上 `-O2` 的收益在噪声里，大约 0 到 +7%。** 更早一次「`-O2` 掉到 2.6 万」没有在交替实验里复现，当作漂移，不写成编译器回归。

`-O2` 仍然应该成为发布默认值（app 今天是 `-O0`，库是 `-O1`），因为 keepalive、大 body、HTTP 解析变热之后用户态占比会上升。它不是平台期的原因。

### 4.2 服务端：每个连接一块 266 KB 的栈，而且泄漏

`StServer::Loop` 每 `accept` 一次就 `CreateThread`。`StThread` 构造函数调用 `InitStack` → `context_make`：`calloc` 一个 `Stack`，再 `malloc` 约 266240 字节（`STACK=260096`，`MEM_PAGE_SIZE=2048`）。`ActiveThreadStartUp` 跑完回调后 `Yield`，协程不再入队，对象也不还回 `UtilPtrPool<StThread>`（`StThreadSchedule` 析构函数里的 TODO）。

实测 VmSize 每请求大约 272 KB（266240 再加结构和分配器）。20000 请求从 13 MB 涨到 5.2 GB。RSS 只涨约 9.5 KB/请求，因为栈只被碰了开头几页，缺页清零仍进了 perf：`CreateThread`/`InitStack` 占服务端样本的 **27%**，`context_make` 25%，缺页 21%。`strace` 里每请求恰好 1 次 `mmap`。

客户端相反：`-c N` 只建 N 个工人，2000 次请求的 `mmap` 只有启动时的 58 次。平台期里「建栈」是服务端单方面的成本。

曲线脚本每轮重启服务端，就是在躲这件事。长跑还会让后面的请求变贵（16000 请求时 VmSize 已经 4.3 GB，那次 QPS 31k，短跑可以到 36k；样本少，方向对，幅度要同机再确认）。

### 4.3 客户端：短连接的握手

客户端 children：`ensure_conn` 43%，`Create` 43%，`st_connect` 36%。`http_parser_execute` 自身只有 1.9%。`DoInput` 的逐字节扫头部在服务端扁平采样里只有 0.5%。

`strace`（每请求）：1 次 `socket`，2 次 `connect`（一次 `EINPROGRESS`，一次完成），大约 5 次 `epoll_ctl`，1 次 `send`，1 次 `recv`，1 次 `close`，大约 2 次 `rt_sigprocmask`。解析和拷贝不是这条曲线的头。

`-k` 救不了现状。服务端发送 `Connection: close` 且 `Keeplive()` 为假，`CallBack` 只转一圈。`-k` 与短连接的 QPS 差在噪声里，日志行数仍是每请求两行，说明连接照样关掉。`st_httpclient -k` 只有在响应也是 keep-alive 时才把 `StHttpConn` 留在工人手里；那是样例自己持有，不是 07-D1 的池。

### 4.4 每次请求多一次失败的 `epoll_ctl`，并打两行错误日志

`Schedule` 返回前会 `Delete` 兴趣。`CallBack` 出口再 `ClearItem` → `Delete`。第二次 `EPOLL_CTL_DEL` 得到 `ENOENT`。`DeleteFd`（`st_thread.cc:443`）和 `Delete`（`:409`）各打一行 `LOG_ERROR`。15000 请求 + 1 次健康检查 = 15001 + 15001 行。`strace` 里 `epoll_ctl` 的失败次数等于请求数，`write` 等于请求数的两倍。

`LOG_ERROR` 会 `gettimeofday` + `localtime_r` + `snprintf` + `write`。级别是 `LLOG_ERR`，所以压测关不掉。这不是 `TRACE`。

### 4.5 `rt_sigprocmask`：glibc 的 ucontext，不是 libtask asm

Linux 走系统 `<ucontext.h>`（`stlib/ucontext/ucontext.h` 里 `USE_UCONTEXT 1`）。macOS / Android 走 vendored libtask asm，不碰这个路径。服务端每请求大约 4.1 次 `rt_sigprocmask`，客户端大约 2.1 次（工人不重建）。`swapcontext` 自身只占样本的 0.5%：贵的是系统调用，而且其中一部分是 `context_make` 里的 `getcontext`（新建栈才有）。栈复用之后要重测，再决定要不要让 Linux 也走 asm。

### 4.6 epoll 是水平触发，等待能合并，登记在抖

`epoll_wait` 大约 0.07 次/请求：一次返回多条就绪，不是「每个请求一次 wait」。贵的是 `epoll_ctl`（服务端约 3 次成功 + 1 次失败）。`AddEvent` 在掩码从空变成非空时 `ADD`，否则 `MOD`；`DelEvent` 在剩余掩码为空时 `DEL`。没有 `EPOLLET`。

`st_recv` 先 `WaitFdReady` 再 `recv`，数据已经在套接字里也会走一轮登记和切换。`st_send` 是先 `send` 再等，方向是对的。

### 4.7 `accept` 出来的 fd 是阻塞的

`sys_socket` 会把监听 fd 设成 `O_NONBLOCK`。`st_accept` 调用的是真实 `accept`，新 fd 不经过 `sys_socket`，也不设 `O_NONBLOCK`。这次基准的响应只有约 150 字节，`send` 在 loopback 上一次完成，所以没有堵死进程。一个慢客户端仍可以在阻塞 `send` 上挂住整颗 OS 线程。要做「先读一次再等」之前必须先改这个，否则第一次 `recv` 就可能堵死调度器。

### 4.8 UDP：epoll 掩码或上去之后去不掉，空转占满一颗核

`StIOState::AddEvent`（`stlib/st_epoll.h`）在登记前执行 `mask |= m_file_[fd].mask`。kqueue 的同名函数在新掩码不含某过滤器时 `EV_DELETE`。两边公开行为不一致。

`st_dnsserver` 知道「UDP 常可写，留下 `EPOLLOUT` 会空转」，于是 `DisableOutput` 再 `Add`。在 epoll 上这次 `Add` 把旧的写兴趣或回来，去不掉。空闲 0.3 秒整颗核（30/30 tick）。查询前后 `VmSize` 不变，说明 DNS 服务端没有按包建栈；曲线上的 DNS QPS 是 1 ms 时钟下的一次性突发，而且服务端在空转。`make bench-dns` 的 smoke（10 次查询，11 ms，909 QPS）同样不能当吞吐。

HTTP 监听套接字也在 `CreateSocket` 里先挂了写兴趣。空闲的 `st_httpserver` 是 0 tick，说明监听套接字的 `EPOLLOUT` 没有把 epoll 打成忙等。问题集中在「永远可写」的 UDP fd，以及任何以为 `Disable` + `Add` 能清掉旧位的调用方。

### 4.9 其余看过、但不是平台期的东西

| 项 | 结论 |
| --- | --- |
| `TCP_NODELAY` | 没设。c=1 的 p50 已经是 0（低于 1 ms，约 50 µs），不是 Nagle 的 40 ms。留到广域网延迟，不指望抬这条曲线 |
| `Util::TimeMs` | `gettimeofday`。热路径上有，但没进 perf 前列 |
| `Wakeup` 里的 `dynamic_cast` | 每次 daemon 循环一次。相对缺页和 `connect` 很小 |
| `ForeachPrint` | `TRACE=0` 时 `LOG_TRACE` 是空宏，参数不算。不是成本 |
| 8 KB 收发缓冲 | 连接对象进池（最多 256 个空闲）。不是每请求一次 `malloc` 266 KB 的那种成本 |
| `listen` backlog 128 | `somaxconn` 在这台机器上是 4096。c=10 的平台期与 backlog 无关；c=1000 的排队可以再看 |
| netfilter | perf 里有 conntrack / iptables。绝对 QPS 含这份税 |

---

## 5. 优化项

每项都给：问题、改哪里、预期、风险、怎么验收、改动面。预期是同机短连接曲线的 QPS 幅度，除非另行说明。噪声大约 ±15%，低于这个幅度的项要靠系统调用次数或 VmSize 验收，不能只看一次 QPS。

### O1 · epoll 的 `AddEvent` 改成「设成这组兴趣」，和 kqueue 对齐

- **问题**：第 4.8 节。空闲 DNS 服务端 100% CPU。kqueue 已经会删掉不要的过滤器。
- **改动**：`stlib/st_epoll.h` 的 `AddEvent`。新掩码里没有的位要 `EPOLL_CTL_MOD` 或 `DEL` 掉，不能 `|=` 旧掩码。调用方（`StEventSchedule::Add`）传入的是 item 上已经 `Enable`/`Disable` 过的完整掩码。补一个 Linux 单测：UDP fd 先挂写、再只挂读，之后空闲若干毫秒，进程 CPU 接近 0，并且仍能收一张包。kqueue 侧对一下「只读 / 只写 / 都要」三种掩码，避免修 epoll 时把 macOS 改出差异。
- **预期**：HTTP 曲线几乎不动（空闲 httpserver 本来就不转）。DNS 空闲 CPU 从 100% 降到大约 0。这是正确性，也是后面 UDP 数字的前提。
- **风险**：如果有调用方依赖「Add 只会加位、不会清位」，行为会变。当前 `WaitFdReady` 和 `CallBack` 都是先把读写位设全再 `Add`，意图是替换。Android 与 Linux 同一份 epoll。macOS 走 kqueue，接口保持「传入的 mask 就是结果」。
- **验收**：单测 + 空闲 `st_dnsserver` 的 tick。然后用固定的 `-c 200 -n 200` 记一笔新的 UDP 延迟，写进分析记录，不假装现在的 5 万 QPS 还能用。
- **改动面**：一个函数 + 一个单测。先做。

### O2 · 不要对已经摘掉的兴趣再 `DEL`，更不要当成错误打日志

- **问题**：第 4.4 节。每请求 1 次失败的 `epoll_ctl` 和 2 次 `write`。
- **改动**：`src/st_thread.cc` 的 `Delete` / `ClearItem`。兴趣已经是空就直接返回。`ENOENT` 在「本来就没登记」时不打 `LOG_ERROR`。成功路径的日志级别维持 `LLOG_ERR` 只留给真失败。
- **预期**：短连接 QPS **大约 +5% 到 +15%**（从「每请求约 14 次系统调用里有 3 次是废的」估算，不是从 `-O2` 那种噪声里读出来的）。
- **风险**：低。别把真正的 `ADD` 失败也吞掉。macOS 上 kqueue 的 `ENOENT` 已经在删除不存在的过滤器时被忽略，把 epoll 收成同样的安静。
- **验收**：`strace -c` 里失败的 `epoll_ctl` 和多出来的 `write` 接近 0。stdout 不再按请求涨。同机 `bench-curve` 的中位数不低于改前。
- **改动面**：`st_thread.cc` 里两条路径，加一条断言或单测覆盖「Schedule 之后 ClearItem」。

### O3 · 协程栈复用（短连接曲线的主要一项）

- **问题**：第 4.2 节。27% 的服务端样本，每请求 1 次 `mmap` 和大约 272 KB 虚拟地址，对象泄漏。
- **改动**：`ActiveThreadStartUp`（`src/st_thread.h`）在回调结束后不要 `Yield` 进黑洞。协程在函数里循环：跑完一个 `StClosure`，把自己放回 `UtilPtrPool<StThread>` 的空闲队列并挂起，下一个 `CreateThread` 把新回调写进去再唤醒。栈和 `ucontext` 只在池里没有空闲对象时 `context_make` 一次。池要有上限（沿用 `UtilPtrPool` 的 `m_max_free_`，或单独给协程一个更大的上限），超出的才 `context_free`。不改 `STACK`、`ss_sp`/`ss_size` 余量、`ty`/`tx` 拆分。`GetUniqid` 的互斥锁只在真正新建时拿。
- **预期**：短连接 QPS **大约 +15% 到 +35%**。算法：建栈占服务端样本约 27%，服务端是较慢的那一侧（约 26 µs）；去掉之后两侧接近，吞吐从约 1/26 µs 靠近 1/20 µs。VmSize 不再随请求线性涨，这是硬指标。客户端已经在复用，这项主要利好服务端和所有「每请求一个协程」的调用方。
- **风险**：中。协程函数不能返回（返回会把栈还给 `makecontext` 的调用约定）。必须在 `ActiveThreadStartUp` 里循环。池是线程局部的，和现在的 `Instance<>` 一致。macOS / Android 的 asm `swapcontext` 必须仍能多次切回同一块栈；这是 libtask 的正常用法，但要在 macOS CI 上跑协程单测，不能只在 Linux 上相信。不要在 `Reset()` 里 `FreeStack`。
- **验收**：
  - 5 万次短连接之后，服务端 `VmSize` 增量小于 64 MB（今天大约 13 GB）。
  - `strace` 的 `mmap` 次数接近并发数，而不是请求数。
  - `make -C tests run` 与 `make -C stlib/tests run` 在 Linux 通过；macOS CI 至少通过协程创建/Yield/Sleep。
  - 同机 `bench-curve` 中位数上升，且 c=1 的 p50 仍低于 1 ms。
- **改动面**：`src/st_thread.h`、`src/st_thread.cc`，可能加一个很小的单测（建 100 个协程，看池大小不再跟完成数走）。不动 `stlib/st_context.cc` 的布局。

### O4 · 样例级 keepalive 曲线（不实现连接池）

- **问题**：第 4.3 节。客户端大约三分之一以上的样本在 `connect`。服务端每个请求还付一次 O3 的账。
- **改动**：只动样例。`app/st_httpserver` 增加一个开关（命令行或环境变量，默认仍是 `Connection: close` + `eTCP_CONN`），打开时发送 `Connection: keep-alive`，并用 `eTCP_KEEPLIVE_CONN` 让 `CallBack` 的 `while (Keeplive())` 在同一协程上多转几圈。`scripts/bench_curve.sh` 增加一条可选的 keepalive 曲线（例如 `BENCH_CURVE_KEEPALIVE=1`），客户端加 `-k`。默认 `make bench-curve` 仍是短连接，readme 上的表不偷偷换成另一条工作负载。
- **预期**：这是**另一条曲线**。短连接数字不靠它变好。keepalive 稳态去掉握手、`close` 和「每请求一块栈」，有机会到短连接的 **1.5–3 倍**。上限取决于单线程上剩下的 `recv`/`send`/切换。必须实测后再写进文档，不要先把 3 倍写死。
- **风险**：低，只要默认路径不变。`eTCP_KEEPLIVE_CONN` 今天会让 `CallBack` 多转，但 `FreePtr` 仍不复用连接对象（07-D1）。样例要自己持有这条连接直到对端关掉，和 `st_httpclient -k` 已经在做的事一样。不要在这一步改 `StConnectionManager::FreePtr`。
- **验收**：`-k` 跑 2 万次请求时，`strace` 的 `connect` 和 `mmap` 接近并发数。响应里有 `Connection: keep-alive`。短连接曲线的命令行和结果定义不变。
- **改动面**：`app/st_httpserver/main.cpp`、`scripts/bench_curve.sh`、readme 里多一小节。不改库。

### O5 · `accept` 的新 fd 设为非阻塞，`st_recv` 可以先读一次

- **问题**：第 4.6、4.7 节。阻塞的 accepted fd 能冻住整个 OS 线程。`st_recv` 即使数据已经到达也先睡一次。
- **改动**：`src/st_sys.cc` 的 `st_accept` 在返回前 `fcntl(O_NONBLOCK)`（不要打上 hook 的「用户要求非阻塞」标志，那个标志会让 `sys_read` 绕过协程）。然后 `st_recv` / `st_read` 在 `WaitFdReady` 之前先做一次非阻塞读；`EAGAIN` 再睡。`st_send` 已经是这个顺序。
- **预期**：短连接再 **大约 +5% 到 +10%**，而且这是正确性修复。数据还没到时多一次失败的 `recv`，循环里已经处理 `EAGAIN`。
- **风险**：中等偏低。阻塞 fd 上「先读」会卡死，所以这一项必须和 `O_NONBLOCK` 一起上。macOS `accept` 同样不继承非阻塞，同一段代码两边都走。Android 也走 `st_accept`。
- **验收**：单测里 accepted fd 的标志带 `O_NONBLOCK`。一个故意不读的客户端不会让服务器的其他连接停在 1 秒以上。`strace` 里「数据已在缓冲」的请求少一次完整的 `epoll_ctl` 来回（用回环上的小请求抽查）。
- **改动面**：`src/st_sys.cc`，加一个单测。

### O6 · 兴趣保持到连接关闭，用 `MOD` 而不是每等一次就 `ADD`/`DEL`

- **问题**：第 4.6 节。`epoll_wait` 已经合并得很好，`epoll_ctl` 仍是每请求数次。
- **改动**：连接存活期间在 epoll/kqueue 里留着 fd。只在读写方向变化时 `MOD`（kqueue 则 `EV_ADD`/`EV_DELETE` 对应过滤器）。`Schedule` 不再在每次唤醒后把兴趣删光。`EPOLLET` 先不做：漏事件的代价比少一次 `ctl` 大。
- **预期**：在 O2、O3、O5 之后再量。单独看，`epoll_ctl` 占服务端 strace 时间大约 16%、客户端大约 22%，扣掉仍必须有的那部分，**大约 +5% 到 +10%**。
- **风险**：中。水平触发下「兴趣还在、但协程还没读」会立刻再次唤醒，必须保证读完或明确关掉兴趣，否则变回 O1 那种空转。kqueue 与 epoll 的结果掩码要仍一致。
- **验收**：每请求成功的 `epoll_ctl` 从约 3 降到约 1 或更少（方向不变时为 0）。空闲进程 CPU 仍接近 0。Linux 与 macOS 的 IO 单测都过。
- **改动面**：`src/st_thread.cc` 的 `Schedule`/`Delete`，`stlib/st_epoll.h` 与 `stlib/st_kqueue.h` 只在 O1 没覆盖到的地方补。

### O7 · 发布构建默认 `-O2`

- **问题**：第 4.1 节。库 `-O1`，app `-O0`，`http_parser.c` 只有 `-g`。
- **改动**：`make.inc` 增加 `OPT ?= 2`，`OPT=0` 时不加 `-O`（给 `COVERAGE` 用，coverage 今天已经强制 `-O0`）。`src/makefile` 去掉手写的 `-O1`，避免它盖住 `OPT`。app 的 `http_parser.c` 使用同一组 `ST_CXXFLAGS`。`DEBUG=1` 继续加 `-g2`。
- **预期**：这条短连接曲线 **0 到 +10%**，验收标准是「不明显变慢」，不是「翻倍」。keepalive 或更大 body 上再记一笔。
- **风险**：低。`-O2` 的 `-fstrict-aliasing` 若打到违规转型，表现为偶发错包而不是慢。用现有单测和 `fail=0` 的曲线看。不要开 `-O3` / LTO，除非 O7 落地后另有同机数据。
- **验收**：`make lib`、`make apps`、`make -C tests run`。`bench-curve` 中位数不低于同机改前的 85%（噪声下限）。`COVERAGE=1` 的编译命令里仍然能看到 `-O0` 且没有后写的 `-O2`。
- **改动面**：`make.inc`、`src/makefile`、`app/st_httpclient/makefile`。可以和 O1 并行，但不要和 O3 的 perf 混在同一次对比里。

### O8 · Linux 是否改用 libtask asm（先不要做）

- **问题**：第 4.5 节。服务端每请求约 4.1 次 `rt_sigprocmask`。
- **改动**：仅当 O3 之后这个数字仍大于大约 2 次/请求，再单独立项：Linux 也 `#undef USE_UCONTEXT`，走已有的 `stlib/ucontext/asm.S`，并且 **整份** `ucontext_t` 换成 libtask 布局。不能一半 glibc、一半 asm。
- **预期**：现在不能把 4.1 次都算成可省的切换。`getcontext` 占了新建栈的那部分。剩下的切换若仍贵，短连接上也许还有 **5% 到 10%**。
- **风险**：高。`InitContext` 的 `ty`/`tx`、`ss_sp + 16`、`ss_size - 64` 都不许顺手改。arm64 Linux 和 x86_64 都要测 `st_context` 单测。Android 已经在 asm 上，不要为了 Linux 去改它的布局。
- **验收**：`rt_sigprocmask` 降到每请求 0 或 1，协程单测和 `bench-curve` 不回退。
- **改动面**：`stlib/ucontext/ucontext.h` 的 Linux 分支。独立阶段。

### O9 · 小项，放在大头之后

| 项 | 改动 | 预期 | 风险 |
| --- | --- | --- | --- |
| `TCP_NODELAY` | `sys_socket` / `st_accept` 对 TCP 设 `TCP_NODELAY` | 这条 loopback 曲线大约 0。真实 RTT 上减少小包粘滞 | 低。可做成默认开 |
| 时钟 | daemon 循环里取一次时间，向下传；热路径少调 `gettimeofday`。不要改成会让超时早 4 ms 的 `CLOCK_MONOTONIC_COARSE`，除非单测锁住误差 | <5% | 超时语义不能变 |
| `dynamic_cast` | `Wakeup` 里堆顶已经是 `StThreadItem`，改成不需要 RTTI 的转换 | <3% | 低，堆里若有别的类型要先断言 |
| backlog | 样例 `Listen` 的 128 提到与 `somaxconn` 相称的值，或可配置 | 只影响高并发尾延迟 | 低，默认曲线的 c=10 平台期不动 |
| 多进程 | 样例层 `SO_REUSEPORT` + 多进程，每个进程仍是 1:N | 一颗核被 O1–O5 收拾干净之后，4 vCPU 上有机会再接近 2 倍，直到 conntrack 的自旋锁（perf 里已经能看到） | 不进库，不做 M:N |

`TRACE=1` 不单列阶段。压测保持 `TRACE=0`。真正在打的是 O2 的 `LOG_ERROR`，不是 `LOG_TRACE`。

---

## 6. 分阶段

```
O7 默认 -O2          （可与 O1 并行；对比性能时不要和 O3 叠在同一次）
        │
O1 epoll 掩码对齐 ──→ O2 去掉多余 DEL 和 ERR 日志
        │
        ▼
O5 accept 非阻塞
        │
        ▼
O3 栈复用  ──→  重测 rt_sigprocmask ──→ 仅此时考虑 O8（Linux asm）
        │
        ▼
O4 keepalive 曲线（样例，默认曲线仍是短连接）
        │
        ▼
O6 兴趣保持
        │
        ▼
O9 小项；多进程样例最后，且可选
```

| 阶段 | 内容 | 依赖 | 出口 |
| --- | --- | --- | --- |
| P0 | O7 | 无 | **延期**。三件套绿；同机曲线不低于改前中位数的 85%；coverage 仍是 `-O0` |
| P1 | O1 + O2 | 无 | **本轮**。空闲 DNS CPU ≈ 0；每请求失败 `epoll_ctl` 和多余 `write` ≈ 0 |
| P2 | O5 | P1 | **延期**。accepted fd 非阻塞；慢客户端不冻住服务器 |
| P3 | O3 | 无硬依赖 | **本轮**。VmSize 增量 < 64 MB / 5 万请求；`mmap` 不随请求数涨。默认栈同时改为 131072 |
| P4 | O4 | 短连接基线 | **延期**。新曲线的 `connect`/`mmap` ≈ 并发数；默认 `make bench-curve` 仍是短连接 |
| P5 | O6 | P1、P2 | **延期**。每请求 `epoll_ctl` 下降；空闲不空转；macOS 单测过 |
| P6 | O9 | 无 | **延期** |
| P7 | O8 | P3 之后的新 `strace` | **延期** |
| P8 | 多进程样例 | P3–P5 | **延期**。不进默认曲线，不进 CI 的性能门 |

D 项（做到哪一步算这一篇完成）不在本计划的落地范围内。本篇的完成是：计划进仓库、数字可追溯。实现时每个阶段自己的出口写在上表。

---

## 7. 怎样守住回归

1. **默认短连接曲线不变定义**：`make bench-curve`，`TRACE=0`，`n = max(50000, c*30)`，重复 3 次，中位数。服务端在 P3 之前继续每轮重启；P3 之后可以加一次「不重启、连续两个点」的对照，确认 VmSize 不再逼着重启。
2. **每一阶段若改变了默认曲线的中位数**，用同一次 `make bench-curve` 的产物更新三处，且只更新这一处工作负载：
   - `reports/baseline-curve.md` 与 `reports/baseline-curve.csv`（脚本生成，不手改数字）
   - `docs/perf/http-qps-vs-concurrency.svg` 与 `http-latency-vs-concurrency.svg`
   - `readme.md` / `readme_en.md` 的性能表，写上新的提交号和机器
3. **keepalive 是附加曲线**，文件名与短连接基线分开（例如 `reports/baseline-curve-keepalive.*`），不要覆盖短连接表。
4. **CI 不跑 QPS**。共享 VM 上 c=1000 可以差 1 万。P3 可以加一个功能单测：固定请求数之后 `VmSize` 低于阈值。那是泄漏断言，不是性能门。
5. 系统调用次数、VmSize、空闲 CPU 写进该阶段的分析记录（本篇这份是 `reports/perf-analysis-20261008.md`）。`perf.data` 不入库。
6. 对比时钉核或至少记录 `nproc`、conntrack 是否打开。这台机器的 loopback 过 netfilter，换一台没开 conntrack 的机器，绝对 QPS 会不一样。

---

## 8. 需要拍板的问题

下列每条都已拍板。推荐项里没被本轮选中的，整项延期，见 §11。

### D1. 默认优化级别改成 `-O2` 吗？

- **延期（O7）**。维持库 `-O1`、app `-O0`。不在本轮改 `make.inc`。

### D2. 回归曲线用短连接还是 keepalive？

- **(a) 维持**：短连接继续当 `make bench-curve` 和 readme 主表。keepalive 曲线（O4）延期。

### D3. Linux 现在就换 libtask asm 吗？

- **(a) 延期（O8）**。本轮不换。栈复用落地后若 `rt_sigprocmask` 仍高于约 2 次/请求再立项。

### D4. 07-D1 的真连接池放进这一轮吗？

- **(a) 不做**。O4 也延期，本轮不改样例持有连接。

### D5. 要不要做多 reactor？

- **(a) 不做**。P8 多进程样例延期。不做 M:N。

### D6. 默认栈大小

- **已改决定**：默认 `STACK = 131072`（128KB），取代原先「保持 260096」。`MEM_PAGE_SIZE`、`ty`/`tx`、`ss_sp + 16`、`ss_size - 64` 不动。实际 `malloc` 仍走 `InitStack` 公式（约 137216 字节）。验收：`make -C tests run`、`make apps`、`TRACE=1` 构建与一次短请求都不溢出。

### D7. 用 2026-10-08 的测量覆盖冻结基线吗？

- **(a) 不覆盖那次噪声曲线。** 本轮改完若默认曲线的中位数真的动了，再按第 7 节用同一次 `make bench-curve` 更新 `reports/baseline-curve.*`、`docs/perf/` 与 readme 表。

### D8. CI 里卡一个 QPS 下限吗？

- **(a) 不卡 QPS。** 可以卡 VmSize：5 万请求之后服务端增量 < 64MB。那是泄漏断言。

---

## 9. 风险

| # | 风险 | 缓解 |
| --- | --- | --- |
| R1 | 同机 QPS 漂移大于单项优化 | 用 VmSize、`strace` 次数、空闲 CPU 做硬验收；QPS 只看同机中位数 |
| R2 | 栈复用后协程返回，栈被拆掉 | 函数内循环，单测里反复复用同一批协程 |
| R3 | epoll 改成替换语义后，某处仍假设「Add 只加位」 | 改之前把 `Add(` 的调用点对一遍；单测锁住「只读」结果 |
| R4 | 兴趣保持做成忙等 | 空闲 CPU 是 P1 和 P5 的出口，不是可选项 |
| R5 | Linux asm 与 glibc `ucontext_t` 混用 | O8 要么全换，要么不动；禁止半套 |
| R6 | 本轮把 `STACK` 改成 131072 后深层调用溢出 | 跑完全部单测、`make apps`、`TRACE=1` 构建和一次短请求；枚举和 `st_wrk` 输出仍不改 |

---

## 10. 测量当时没有改库

测量用的是现有的 `make bench-curve`、`make bench-dns`、`strace`、`perf`。没有新的库 API，没有测量用的永久开关。`-O2` 对照是临时改 makefile 后还原的。决策之后的代码不在记录决策的这个 PR 里。

## 11. 2026-10-08 拍板

维护者选定本轮只做下面三项，其余全部延期。

| 项 | 决定 |
| --- | --- |
| P1 / O1 | **做**。`StIOState::AddEvent` 改成替换兴趣掩码，不再 `|=` 旧掩码。kqueue 本来就是替换，保持 macOS 行为正确 |
| P1 / O2 | **做**。去掉每次请求都打到 `ENOENT` 的第二次 `EPOLL_CTL_DEL`，以及随之而来的两行 `LOG_ERROR` |
| P3 / O3 | **做**。协程跑完回调后回到池里挂起，下一次 `CreateThread` 复用同一块栈。5 万请求后服务端 `VmSize` 增量 < 64MB |
| 栈默认值 | **做**。`STACK` 从 260096 改为 131072（128KB）。这取代 D6「保持原值」 |
| O7 / 默认 `-O2` | **延期** |
| O5 accept 非阻塞、`st_recv` 先读 | **延期** |
| O4 keepalive 曲线 | **延期**。默认曲线仍是短连接 |
| O6 兴趣保持到连接关闭 | **延期** |
| O9 时钟 / `TCP_NODELAY` / `dynamic_cast` / backlog | **延期** |
| O8 Linux libtask asm | **延期** |
| P8 `SO_REUSEPORT` 多进程样例 | **延期** |

验收仍是：空闲 `st_dnsserver` CPU 从约 100% 降到接近 0；短连接 `make bench-curve` 的 QPS 同机前后对比（噪声约 ±15%，不进 CI）；5 万请求的 `VmSize` 增量 < 64MB。QPS 不作为 CI 门禁。

## 12. 本轮落地（`a67127f` 一带）

代码在实现 PR，不在只记录决策的那个 PR 里。数字见 [`reports/perf-p1-p3-20261008.md`](../reports/perf-p1-p3-20261008.md)。

| 验收 | 结果 |
| --- | --- |
| 空闲 DNS CPU | 1 秒 0 tick（改前约 100%） |
| 5 万请求 VmSize | `-c 32`：12732 → 17792 KB，**+5060 KB** |
| `mmap` / 2000 请求 | 30（约等于并发；改前 2000） |
| 失败的 `epoll_ctl` 和两行错误日志 | 0 |
| 短连接 QPS | 落在改前同一条噪声带里，没有吃到计划里 +15%～+35% 的估计。客户端握手还在 |
| `STACK=131072` | `make -C tests run`、`make -C stlib/tests run`、`make apps TRACE=1` 和一次 `curl` 都过，没有溢出 |

冻结基线 `reports/baseline-curve.*` **不**用这次曲线覆盖（D7）。QPS 不进 CI。O4–O9 与 P8 仍延期。
