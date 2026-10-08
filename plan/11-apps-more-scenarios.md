# 11 · 六个新样例：echo / 端口扫描 / HTTP 反代 / Redis / 聊天室 / POSIX hook

> **状态：📋 计划（未开工）** · 2026-10-08。基线：`master` = `486611f`（PR #15，`st_*` 超时统一为 `-1` 且 `errno == ETIME`）。
>
> **决策：D1–D14 未拍板**（§5）。推荐默认值写在表里；实现前按推荐值开工，有异议再改计划。
>
> 本文档**只描述计划，不包含任何功能代码变更**。和 [`08`](08-apps-bench-dnsserver.md) / [`10`](10-cross-platform-android-httpclient.md) 的关系：08 已经有 HTTP/DNS 压测闭环和 `st_dnsserver`；10 已经有 `st_httpclient`（`st_http_exchange`）和 Android 只编译。本篇只加 `app/` 样例，以及两处不补就写不出样例的库缺口（hook 的 `ST_FD_FLG_UNBLOCK`、`st_connect` / `Create` 的 errno）。调度模型、栈大小、枚举值不动。
>
> 硬约束沿用总索引：C++98；`.clang-format`（LLVM 基线 + `m_x_` 命名）+ CI 的 clang-format-18；**只用 makefile**；零第三方运行时依赖；新源文件头 `Copyright (C) zhoulv2000@163.com`；vendored 代码保留 [`COPYRIGHT`](../COPYRIGHT)；**不发明根 `LICENSE`**；注释用中文。Windows 不支持。三件套闸门：`make lib`、`make -C tests run`、`make -C stlib/tests run`，另加 `make apps`。`make clean` 之后 `git status --porcelain --ignored` 必须为空。

---

## 1. 目标与非目标

### 1.1 目标

在 `app/` 下增加六个样例，每个都是「同步写法、内部 Yield」的完整小程序，并带 README、冒烟脚本和 `make apps` / `make clean` 装配。编号沿用选题时的原号（缺 2/4/6/8/10，不是漏写）：

| # | 目录 | 演示什么 |
| --- | --- | --- |
| 1 | `app/st_echo` | 最短的 TCP 收发闭环，给 readme 当入口，也当最简单的压测基线 |
| 3 | `app/st_portscan` | 大量并发 `st_connect`；超时（`-1` / `ETIME`）和拒绝（`ECONNREFUSED`）分开计 |
| 5 | `app/st_httpproxy` | `StServer` 接 `st_http_exchange`；应用层后端槽位、超时、简单探活 |
| 7 | `app/st_redisclient` | 自己实现的 RESP；形态对齐 `st_memcacheclient`，另加 redis-benchmark 式压测模式 |
| 9 | `app/st_chat` | 一行消息从一条连接广播到其他连接；协程之间用邮箱叫醒，不碰对方的 socket 事件项 |
| 11 | `app/st_hookdemo` | 一份只调用 POSIX `socket` / `connect` / `read` / `write` 的客户端，原样放进协程跑 |

### 1.2 非目标（明确不做）

| # | 不做 | 理由 |
| --- | --- | --- |
| N1 | 不改调度模型、`STACK`（260096）、`MEM_PAGE_SIZE`（2048）、`eConnType` 数值、`st_*` / `tcp_sendrecv` / `udp_sendrecv` 的公开签名和状态码 | 公开 API 兼容 |
| N2 | 不把 keepalive **真连接池**做进 `StConnectionManager::FreePtr` | 仍是 07-D1。反代的槽位放在样例里，调用方式对齐 `st_httpclient -k` |
| N3 | 不引入 hiredis、libevent、CMake、gtest | makefile + 自带 `stlib/st_test.h`；RESP 自己解析 |
| N4 | 不把 `read` / `write` / `connect` 导出成 `libmthread.so` 的全局符号，也不把 LD_PRELOAD / `DYLD_INSERT_LIBRARIES` 当作默认跑法 | 会劫持进程里所有读，包括 hook 自己的 `dlsym` 回退。macOS 两级命名空间和 Android 只编译都接不住 |
| N5 | 不改 `sys_accept` | 它今天调用的是真实 `accept`（见 `app/st_sys.cc`），会堵住整个 OS 线程。六个样例里的服务端走 `st_accept` |
| N6 | 聊天室不加库级 channel / `st_notify`，不改 `StEventSchedule::Schedule` 的「没有 IO 事件就 `errno=ETIME`」 | `Pend` / `Unpend` 今天只为父子协程汇合服务。广播用应用层 pipe |
| N7 | 反代不做 TLS、HTTP/2、请求体流式转发、改 `ST_SEND_BUFFSIZE`（8192） | 与 `st_httpclient` 一样，只是样例 |
| N8 | Redis 压测不把 `redis-server` 装进 CI | 和 memcache 一样：本机有就用来压；冒烟用仓库里的 Python stub |
| N9 | 不发明根 `LICENSE`；不提交 `.session_tmps/`、样例二进制、一次性 `reports/echo-*.md` | 与 01/08 相同 |
| N10 | Android 不跑这些样例 | `build-android.yml` 继续只编译。新 makefile 必须能被 NDK clang++ 编过 |
| N11 | 六个样例都是单 OS 线程上的 1:N。不做 M:N，不开线程池 | `Instance<T>()` 仍是线程局部 |
| N12 | IPv6、`getaddrinfo` 放进协程 | `getaddrinfo` 不在 hook 表里，会堵住 OS 线程。地址用 `inet_pton` 的 IPv4 |

---

## 2. 现状盘点（2026-10-08，`486611f`，已对照源码）

下面是写计划时读过的接口，实现时按这些名字调用，不要再发明一套。

### 2.1 样例该用的入口

| 能力 | 位置 | 现状 |
| --- | --- | --- |
| 起调度器 | `st_init_frame()`（`app/st_c.cc`） | 拉起 `GlobalEventSchedule()` 和 `Instance<StSysSchedule>()` |
| 打开 hook | `st_set_hook_flag()` → `g_hook_flag = 1` | 不打开时，已登记的 fd 上 `sys_read` 仍走真实 `read` |
| 建协程 | `Frame::CreateThread(void (*)(void *), void *)`（`app/st_frame.h`） | 内部 `NewStClosure` + `StSysSchedule::CreateThread`。`ncoro==1` 时现有客户端直接在 primordial 上跑 |
| 等一组协程结束 | `st_sleep(10)` 轮询计数 | `app/st_dns/main.cpp`、`app/st_httpclient/main.cpp` 都这么做，避免 `Frame::Loop(true)` 不返回 |
| 契约式一次 TCP 收发 | `tcp_sendrecv(..., CheckLengthCallback, bool keeplive = false)` | 状态码在 `app/st_c.h`：成功 0，发送失败 `-2`，**接收超时 `-3`（`errno=ETIME`）**，对端关闭 `-5`。和 `st_*` 的 `-3`（只表示 Schedule/Add 失败）不是同一套 |
| 带超时的 syscall 包装 | `st_connect` / `st_send` / `st_recv` / `st_read` / `st_write` / `st_accept`（`src/st_sys.h`） | 超时一律 `-1` 且 `errno=ETIME`；`-2` 是没有事件项（`EINVAL`）；`-3` 只表示 Schedule/Add 失败 |
| 客户端连接 | `StExecClientConnection` : `StClientConnection<StEventItem>`；`StConnectionManager::AllocPtr` / `FreePtr` | `Create` 里 `sys_socket`、登记 `StEventItem`、再 `st_connect`。`SetTimeout` 默认 30000 ms |
| 服务端 | `StServer<Conn, ServerT>` 的 `CreateSocket` / `Listen` / `Loop`（`src/st_server.h`） | `Loop` 里 `st_accept`，每连接 `CreateThread(CallBack)`。`CallBack` 固定 `RecvData` → `DoProcess` → `SendData`，`while (conn->Keeplive())` |
| HTTP 交换 | `st_http_exchange`（`app/st_httpclient/http_client.h`） | 不用 `tcp_sendrecv`（要预先给死接收缓冲，不适合 chunked）。`-k` 时调用方自己拿着 `StHttpConn`，不走 `eTCP_KEEPLIVE_CONN` |
| HTTP 服务 | `app/st_httpserver` | `StServer<HttpConn, eTCP_CONN>`，固定 `hello from sthread`，`Connection: close` |
| hook 形状 | `sys_socket` / `sys_connect` / `sys_read` / `sys_write` / `sys_send` / `sys_recv` / `sys_setsockopt` / `sys_close`（`app/st_sys.cc`） | 失败时收成 libc 的 `-1` + 已设置的 errno。`SO_RCVTIMEO` / `SO_SNDTIMEO` 被折成毫秒，写进 `sys_fd.read_timeout` / `write_timeout` |
| 协程挂起 | `StThreadSchedule::Pend` / `Unpend`（`src/st_thread.cc`） | 父子协程：子协程结束时 `WakeupParent` → `Unpend`。`Unpend` 对不在 pend 队列上的线程直接 `CPP_TAILQ_REMOVE`，不能当通用 notify |
| 一次等多个 fd | `StEventSchedule::Schedule(thread, fdset, item, wakeup_timeout)` | `st_*` 的 `WaitFdReady` 只用单个 item。fdset 参数已经存在 |
| 一个 fd 一个事件项 | `StEventSchedule::m_event_[fd]` | `WaitFdReady` 会改这个 item 的读写兴趣和 `SetOwnerThread`。别的协程对同一个 fd 再 `st_send`，会把正在 `st_recv` 的那一方挤掉 |

### 2.2 和本篇直接相关的缺口（实现 Phase 0 先复核）

**G1 · `sys_socket` 把「库自己的非阻塞」标成了「用户要求非阻塞」。**

`sys_socket` 用 `sys_fcntl(F_SETFL, O_NONBLOCK)` 把内核 fd 设成非阻塞。`sys_fcntl` 看到 `O_NONBLOCK` 就置 `ST_FD_FLG_UNBLOCK`。此后 `sys_read` / `sys_write` / `sys_recv` / `sys_send` 发现这个标志就调用真实 syscall，**不会**进 `st_read`。`tests/st_hook_unittest.cpp` 的注释已经写明：「`sys_socket` 会顺手设 `O_NONBLOCK`，`st_*` 分支就走不到」，测试只好改走 `sys_new_fd`。

`sys_connect` 不看 `ST_FD_FLG_UNBLOCK`，会进 `st_connect`。`st_connect` → `WaitFdReady` 要求 `GetEventItem(fd)` 非空，否则 `-2` / `EINVAL`。`sys_socket` 今天不登记事件项。所以一份只调用 `socket` + `connect` + `read` 的代码，在现有 hook 上既不会 Yield，也连不上。

**G2 · 失败路径上的 errno 会被清掉。**

`StClientConnection::Create` 在 `Connect` 失败后调用 `ReleaseItem()` 和 `Close()`（`sys_close`）再返回 `-2`。`Connect` 把 `ETIME` 映射成 `-1`、其他错误映射成 `-2`，但返回到 `Create` 的调用方时，errno 已经不一定还是 `ECONNREFUSED` 或 `ETIME`。端口扫描如果只看 `Create()` 的返回值，分不开超时和拒绝。

`st_connect` 本身：非 `EINPROGRESS` / `EAGAIN` 的错误原样返回 `-1`（errno 保留）；可写之后再调一次 `connect`，`EISCONN` 当成成功。Linux 上拒绝通常会在第二次 `connect` 得到 `ECONNREFUSED`。macOS / kqueue 是否也这样，Phase 0 用 loopback 测，不靠回忆。若可写之后内核把失败也报成 `EISCONN`，就要在返回成功前 `getsockopt(SO_ERROR)`。

**G3 · `StServer::CallBack` 分不清「包还没收齐」和「这一轮成功」。**

`RecvData` 在 `DoInput` 返回 0（还要更多字节）和返回 `>0`（已收齐）时都返回 0。`CallBack` 只要 `RecvData() >= 0` 就 `DoProcess` + `SendData`。`eTCP_CONN`（0x20，末位不是 1）只转一圈就 `FreePtr`。要吃拆包，服务端模板参数得用 `eTCP_KEEPLIVE_CONN`（0x11），让 `while (Keeplive())` 再转；没收齐时 `DoOutput` 把长度设为 0。`st_send` 在 `nbyte==0` 时直接返回 0，不进 `WaitFdReady`。

`ST_CONN_RESET_RECVBUF`（`src/st_public.h`，值 `-65535`）会把 `have_recv_len` 整段清掉，**连 `\n` 后面的粘包一起丢**。没有「消费 N 字节、留下剩余」的接口。

`SendData` 不在每轮开始时把 `have_send_len` 清零。同一个连接上回显第二次，要在 `DoProcess` 里 `GetSendBuffer()->SetHaveSendLen(0)`。

这些都是样例里可以绕开的行为，不必改 `CallBack`。

**G4 · 没有「等 socket，或者等别的协程」的公开原语。**

`Pend` 把线程放进 pend 队列并 Yield。此时它不在 IO 队列上，socket 可读也不会醒。`st_recv` 走 `Schedule`，线程在 IO 队列和 sleep 堆上；别的协程 `Unpend` 它是未定义的（它不在 pend 队列）。`Schedule` 被提前叫醒且 `recv_num==0` 时，一律 `errno=ETIME`，调用方分不出「超时」和「有人找我」。

一个 fd 只能有一个 `StEventItem`。聊天室不能从 A 的协程里对 B 的 socket 调用 `st_send`。

**G5 · hook 表里没有的调用会堵住整个进程。**

已包装：`socket` `close` `connect` `read` `write` `send` `recv` `sendto` `recvfrom` `setsockopt` `fcntl` `ioctl` `accept`。`sleep` 只在 `SyscallCallbackTab` 里有函数指针，**没有** `sys_sleep`。`getaddrinfo`、`poll`、`select`、`readv` / `writev` 都没有。`sys_new_fd` 把读写超时默认设成 **512 ms**。

**G6 · 真连接池仍然不存在。**

`StConnectionManager::FreePtr` 对 `IS_KEEPLIVE` 仍是 HashRemove 再 `Reset()`→`Close()`（`src/st_connection.h` 里的 D1/B11 注释）。`st_httpclient -k` 的做法是协程自己持有 `StExecClientConnection*`，请求之间不 `FreePtr`。

**G7 · 协程栈大约 266240 字节。**

`StThread::InitStack`：`MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE`。`-c 2000` 光栈就要大约 500MB，再加上每个在飞连接一个 fd。CI 的 `NOFILE` 常常是 1024。冒烟不能起几千个协程。

**G8 · `FORMAT_SRC` 是白名单。**

根 `makefile` 的 `format-check` 只覆盖列出来的文件。新样例的 `.h` / `.cc` / `.cpp` 和对应单测不写进这张表，clang-format-18 就不会看它们。`app/` 下历史文件本来就不在表里；本篇新文件要加进去。

### 2.3 现有装配（新样例要接上的地方）

| 位置 | 今天 |
| --- | --- |
| 根 `makefile` 的 `apps` / `clean` / `help` | dns、memcache、wrk、httpserver、dnsserver、httpclient |
| `.gitignore` | 上述二进制路径逐条列出。新二进制不写进去的话，`git status --ignored` 在 `make clean` 之前会看到，`make clean` 漏删则会红 |
| `.github/workflows/_build.yml` | `make apps`、`make smoke-httpclient`、`make -C tests run`、最后 `make clean` + `git status --porcelain --ignored` |
| `build-android.yml` | `make android` → `stlib apps tests stlib-tests`，只编译 |
| `format.yml` | `make format-check CLANG_FORMAT=clang-format-18` |
| 冒烟脚本形态 | `scripts/smoke_httpclient.sh`：PID、`trap`、`lsof` 看端口、退出码断言 |

默认端口避开已经占用的：httpserver `8765`，httpclient 冒烟 `18765`，dnsserver `5353`。

---

## 3. 六个样例

共同约定：源文件头 `Copyright (C) zhoulv2000@163.com`；注释中文；C++98（可以用 `__thread`、`__builtin_expect`、`__sync_fetch_and_add`，和 `st_dns` 一样）；`LOG_LEVEL(LLOG_CRIT)`，避免 `LOG_ERROR` 把 SUMMARY 打花；`signal(SIGPIPE, SIG_IGN)`；单协程走 primordial，多协程 `Frame::CreateThread`，主协程 `st_sleep(10)` 等到计数归零或截止时间。二进制用 `st_*` 这种显式名字，不再用 `main`。每个目录一份 `makefile`（照 `app/st_httpserver/makefile`：`include make.inc`，只链 `libmthread.a` 和 `$(ST_LDLIBS)`）和 `README.md`。

### 3.1 TCP echo（#1）

**目标。** 一条连接、一次「发出一行、收回同一行」。readme 的入口示例用它，而不是再讲一遍 DNS。同时给一个比 HTTP 更短的吞吐基线：没有解析，只有 `tcp_sendrecv`。

**目录。**

```
app/st_echo/server.cpp      # 产物 st_echoserver
app/st_echo/client.cpp      # 产物 st_echoclient
app/st_echo/makefile
app/st_echo/README.md
```

**CLI。**

```
st_echoserver [ip] [port]          # 默认 0.0.0.0 7707
st_echoclient [-c CONC] [-n TOTAL] [-t MS] [-s PAYLOAD] host port
  -c 并发协程，默认 1；-n 总请求，默认等于 -c；-t 单次超时，默认 3000
  -s 负载，默认 "ping"（程序自己补 '\n'）
输出一行：
  SUMMARY ok=.. fail=.. qps=.. elapsed_ms=..
退出码：0 全部 ok；1 有失败或超时仍有 pending；2 参数错误
```

**核心流程。**

服务端：`st_init_frame` + `st_set_hook_flag`，`StServer<EchoConn, eTCP_KEEPLIVE_CONN>`。`CreateSocket` + `Listen` + `Loop`。用 `eTCP_KEEPLIVE_CONN` 只为了 `CallBack` 的 `while (Keeplive())` 能多转几圈，把拆包收齐；**不是**客户端连接池。对端关掉之后 `RecvData` 返回 `-1`，`FreePtr` 仍会 HashRemove（G6）。

`EchoConn` 成员：`char line_[ST_RECV_BUFFSIZE]`、`int line_len_`、`int have_line_`。

- `DoInput`：在接收缓冲里找 `\n`。没有就返回 0。有就把这一行（含 `\n`）拷进 `line_`，置 `have_line_`，返回 `ST_CONN_RESET_RECVBUF`。`\n` 后面的字节会被清掉，协议因此定成「一次只保证一行」。
- `DoProcess`：`GetSendBuffer()->SetHaveSendLen(0)`。
- `DoOutput`：没有完整行时把 `len` 设为 0；有则拷贝并清 `have_line_`。
- `DoError`：打一条日志，返回 0。

客户端：每个请求一次 `tcp_sendrecv`，`keeplive=false`（短连接，和 `FreePtr` 一致）。`CheckLengthCallback` 见到 `\n` 返回该下标 + 1，缓冲里还没有就返回 0，满了还没有就返回负数（`tcp_sendrecv` 会把它收成 `-6` / `-7`）。超时看返回值 `-3` 且 `errno==ETIME`，不要和 `st_recv` 的 `-1` 混用。并发结构照 `app/st_dns/main.cpp` 的 `worker` + `g_pending`。

**数据结构。** 服务端除了 `EchoConn` 的那三个字段，没有共享表。客户端用 `BenchArg`（index、次数、地址、超时、payload）和两个 `volatile` 计数，`__sync_fetch_and_add`。

**库缺口。** 不改 `src/`。G3 的三点（keepalive 循环、空 `DoOutput`、`SetHaveSendLen(0)`、整段清缓冲）在样例里消化，并写进 README 的「限制」。

**冒烟 / 测试。**

- `scripts/smoke_echo.sh`，根目标 `make smoke-echo`（强制 `TRACE=0`，PID / trap 照 `smoke_httpclient.sh`）。端口默认 `17707`。
- 一次 `-c 1 -n 1`，stdout 的回显与 payload 一致，退出码 0。
- `-c 8 -n 40`，`ok=40 fail=0`。
- 连一个没人听的端口，`fail` 增加、退出码 1。
- `tests/` 里不必再单测 `CheckLengthCallback`（逻辑几十行）。若要锁住「没有 `\n` 返回 0」，放在 `tests/st_echo_unittest.cpp`，用 `stlib/st_test.h`，并挂上 `tests/Makefile` 的 `all` 和 `run`。
- Android：跟着 `make apps` 编译，不跑。

**文档。** `readme.md` / `readme_en.md` 的样例清单加上 echo；在「最小使用样例」后面加一节，命令是真实的 `st_echoclient` / `st_echoserver`，**保留**现有 DNS 那节。`AGENTS.md` 的 `make apps` 那一行在实现时补上名字。

**改动面与风险。** 只动 `app/st_echo`、根 makefile、`.gitignore`、脚本、readme。风险是 `eTCP_KEEPLIVE_CONN` 被读成「已经有连接池」——README 写清楚 `FreePtr` 仍关闭 fd。另一风险是粘包被 `ST_CONN_RESET_RECVBUF` 丢掉，冒烟每次只发一行。

### 3.2 端口扫描（#3）

**目标。** 几十到几千个协程同时 `st_connect`，把结果分成 open / refused / timeout / other。演示超时是 `-1` 且 `errno==ETIME`，拒绝不是超时。

**目录。** `app/st_portscan/{main.cpp,makefile,README.md}`，产物 `st_portscan`。

**CLI。**

```
st_portscan [-c CONC] [-t MS] [-p SPEC] host
  -c  同时在飞的协程数，默认 64
  -t  单次 connect 超时，默认 300
  -p  端口表，如 22,80,8000-8010；默认 1-1024 不作为默认值（太慢）。
      不写 -p 时默认 80,443
  host  只接受 IPv4 字面量（inet_pton）
输出：
  SUMMARY open=.. refused=.. timeout=.. other=.. elapsed_ms=..
  每个 open 端口一行 "OPEN <port>"
退出码：0 扫完（open 可以为 0）；2 参数错误
```

**核心流程。** `-c` 个工人协程，用 `__sync_fetch_and_add` 从端口数组里领任务。每个任务：

1. `StConnectionManager<StExecClientConnection>::AllocPtr(eTCP_CONN)`。
2. `SetTimeout(t)`。
3. `Create(StNetAddr)`。成功（返回值 `>=0`）计 open，立刻 `FreePtr`（不要把 fd 留到扫描结束）。
4. 失败则看 **保存过的 errno**（见下面的库改动）：`ETIME` → timeout，`ECONNREFUSED` → refused，其余（`ENETUNREACH`、`EHOSTUNREACH`、`ECONNRESET`）→ other。

不在失败路径上再 `connect` 一次。分类以 `st_connect` 的 errno 为准，不以 `Create` 的 `-1`/`-2` 为准（`-2` 把超时以外的失败叠在一起）。

工人数就是在飞连接数，不是「每个端口一个协程」。`-c 2000` 对 1–65535 就是两千个并发 `st_connect`，栈大约 500MB，还要 `ulimit -n` 大于 `-c`。README 写上这句。CI 只用 `-c 32`。

**数据结构。** 端口数组（启动时把 `-p` 展开到 `int *`，上限 65535 项，超出直接退出码 2）。四个 `volatile` 计数。没有共享连接。

**库缺口（本样例要补，否则分类是错的）。**

1. `StClientConnection::Create`：`Connect` 失败时先把 errno 存下来，`ReleaseItem` / `Close` 之后写回去。返回值仍是 `-2`（`ETIME` 也继续是 `-2`，因为今天 `Create` 对两种 `Connect` 失败都返回 `-2`）。不改 `Connect` 里「`ETIME` → `-1`，其他 → `-2`」这层，避免动已经依赖它的单测（`tests/st_coverage_extra_unittest.cpp` 附近有这条断言）。扫描器在 `Create` 返回后读 errno。
2. Phase 0 若证明「可写之后拒绝被当成 `EISCONN` 成功」：在 `st_connect` 里，`WaitFdReady` 返回之后、把连接当成成功之前，`getsockopt(SO_ERROR)`。非 0 则 `errno` 设成该值并返回 `-1`。这是行为修正，必须有 loopback 单测：监听端口成功、未监听端口 `ECONNREFUSED`、两种都不许是 `ETIME`。Phase 0 若已经能分开，这条就不改。

不新增扫描专用 API。

**冒烟 / 测试。**

- `scripts/smoke_portscan.sh`，`make smoke-portscan`。先起 `st_echoserver`（或一个只 `accept` 的桩；用 echo 即可），再扫「这个端口 + 一个确定没人听的端口」。断言 open 里有 echo 的端口，refused ≥ 1，timeout 为 0，退出码 0。
- **CI 不断言 `ETIME`。** loopback 上没人听是 `ECONNREFUSED`，不是超时。打到 `192.0.2.1`（TEST-NET-1）在 GitHub runner 上经常是 `ENETUNREACH`，几毫秒就返回。样例把这类计进 other。真正的黑洞超时留给手工：README 写一条「对一个丢弃 SYN 的地址加 `-t 200`，应看到 timeout」。
- 单测 `tests/st_connect_errno_unittest.cpp`：loopback open 与 refused，锁住 G2 的修复。挂进 `tests/Makefile`。
- Android 只编译。

**文档。** `app/st_portscan/README.md` 说明四类计数、栈大小、`NOFILE`、为什么 CI 不测 `ETIME`。根 readme 样例清单加一行，并指回 `st_*` 返回值表（超时是 `-1`/`ETIME`）。

**改动面与风险。** `app/st_portscan` + `StClientConnection::Create` 的 errno 保存（几行）+ 可能的 `st_connect` SO_ERROR。风险：SO_ERROR 改动碰到「已经成功的连接被误判失败」。缓解：只在 Phase 0 复现了误判时才改；单测覆盖 open 和 refused；不动超时数值和 `st_*` 的返回码约定。几千协程在 CI 里 OOM 或耗尽 fd：冒烟固定 `-c 32`。

### 3.3 HTTP 反代 / 简单负载均衡（#5）

**目标。** 一个进程里前面是 `StServer`，后面用已经落地的 `st_http_exchange` 转发。演示应用层连接槽位、上游超时、以及一个会把死后端摘掉的探活协程。

**目录。**

```
app/st_httpproxy/main.cpp
app/st_httpproxy/makefile    # 同时编译 ../st_httpclient/http_client.cc 和 ../st_wrk/http_parser.c
app/st_httpproxy/README.md
```

产物 `st_httpproxy`。`http_client.cc` 和 `http_parser.c` **不**编进 `libmthread`。`http_parser.c` 一个字节不改。

**CLI。**

```
st_httpproxy [-l HOST:PORT] [-b HOST:PORT]... [-t MS] [-s SLOTS] [-H MS]
  -l  监听，默认 0.0.0.0:18080
  -b  后端，可重复，至少 1 个，最多 8 个；只接受 IPv4 字面量
  -t  上游单次交换超时，默认 3000，传给 StHttpRequest.timeout_ms
  -s  每个后端的槽位数，默认 2，最大 8
  -H  探活间隔毫秒，默认 1000；0 表示不探活（全部当作活着）
```

**核心流程。**

监听：`StServer<ProxyConn, eTCP_CONN>`。一请求一连接，和 `st_httpserver` 一样，转发完就 `FreePtr`。请求行和头必须落在 `ST_RECV_BUFFSIZE`（8192）里；`DoInput` 找到 `\r\n\r\n` 返回该长度，否则返回 0。超过缓冲返回 `-1`，`DoOutput` 写一个很小的 `400`。

`DoProcess`：

1. 从接收缓冲取出方法和 path（只做第一行 + `Host`）。`CONNECT` 和绝对 URL 的 https 直接 `400`。
2. 轮询选一个 `alive==1` 的后端。下标 `next_` 在调用 `st_http_exchange` **之前**递增；这两步之间没有 Yield，单 OS 线程上不用锁。
3. 在该后端的槽位里找 `in_use==0` 的 `StHttpConn`。找到就置 `in_use=1`，再调用 `st_http_exchange`（`keepalive=1`）。返回后清 `in_use`。交换失败则 `st_http_conn_close` 清掉这个槽位里的 fd。
4. 槽位都忙：`st_sleep(1)` 直到 `-t` 耗尽，然后 `502`。这是轮询，不是条件变量。
5. 没有活着的后端：`502`。
6. 上游响应体 `body_len` 大于 `ST_SEND_BUFFSIZE` 减去响应头余量：丢掉正文，回 `502`（缓冲常量不动，见 N7）。成功则把状态行、少数头和 body 放进 `ProxyConn` 的发送侧缓冲，`DoOutput` 一次写出。

探活：`Loop()` 之前 `Frame::CreateThread` 一个协程。它 `st_sleep(H)`，对每个后端用**自己的**短连接 `st_http_exchange`（`keepalive=0`）请求 `GET /`。2xx 把 `alive` 置 1，否则置 0。不用业务槽位，避免和 `in_use` 打架。`Loop` 不返回，但 `st_accept` 会 Yield，探活协程能跑到。

**数据结构。**

```text
Backend
  host[64], port, ip_be, alive, rr 不放这里
  StHttpConn slot[8]
  int in_use[8]
ProxyConn
  解析出的 method/path、要写回客户端的字节和长度
```

固定数组，不用 `std::vector` 的扩容来表达上限。全局 `Backend backends_[8]`、`int nbackend_`、`int next_`。

**库缺口。** 连接池仍然不在库里（G6）。本样例不改 `FreePtr`，不使用 `eTCP_KEEPLIVE_CONN` 当作池。槽位满时没有 notify，只有 `st_sleep`。响应体上限是 `ST_SEND_BUFFSIZE`，因为 `CallBack` 只会 `SendData` 一次，不能从 `DoProcess` 里再对客户端 fd `st_send`（和监听协程共用一个 `StEventItem`，见 §2.1）。

**冒烟 / 测试。**

- `scripts/smoke_httpproxy.sh`，`make smoke-proxy`。起两个 `st_httpserver`（不同端口），反代 `-b` 两个都配上。`curl` 经反代拿到 `hello from sthread`。杀掉其中一个 httpserver，等一个探活间隔，再 `curl`，仍是 200。两个都杀掉，再请求，状态是 502。
- 不把反代放进 `make bench`。可选的手工对比写在 README，不进 CI。
- Android 只编译（会一起编 `http_client.cc` / `http_parser.c`）。

**文档。** 根 readme 加一节：反代是样例，槽位不是 `StConnectionManager` 的真复用，限制写明 8192 和 IPv4。`app/st_httpproxy/README.md` 写编译依赖的那两个已有文件。

**改动面与风险。** 只在 `app/st_httpproxy` 和脚本。风险：`st_http_exchange` 的签名或 `StHttpConn` 以后若改了，反代要跟着改——它是编译期复用，不是把 HTTP 客户端链进库。探活和请求抢同一个后端时，靠「探活用自己的短连接」避开槽位。`DoInput` 只看头，不读 body；冒烟用 GET。带 body 的 POST 若超过第一次 `RecvData`，会 400，README 写明。

### 3.4 Redis 客户端（RESP）（#7）

**目标。** 和 `st_memcacheclient` 同一类：协议在样例里，传输用 `StExecClientConnection` + `st_send` / `st_recv`。加上一个 redis-benchmark 式的模式（并发、总请求、SUMMARY、分位）。不链接 hiredis。本机 `redis-server` 只用于可选压测。

**目录。**

```
app/st_redisclient/main.cpp
app/st_redisclient/resp.h
app/st_redisclient/resp.cc
app/st_redisclient/makefile
app/st_redisclient/README.md
scripts/redis_stub.py          # 冒烟用，不是库依赖
```

产物 `st_redisclient`。

**CLI。**

```
st_redisclient [-h HOST] [-p PORT] [-c CONC] [-n TOTAL] [-t MS] [-q] command [args...]
  默认 127.0.0.1:6379，-c 1，-n 等于 -c，-t 3000
  command：ping | set | get | incr
    ping
    set <key> <value>
    get <key>
    incr <key>
  -c 1 且 -n 1 且没有 -q：把回复打到 stdout（批量字符串打正文，整数打十进制，空批量打 (nil)）
  总是一行：
    SUMMARY ok=.. fail=.. qps=.. elapsed_ms=.. p50_ms=.. p99_ms=..
退出码：0 全部 ok；1 有失败；2 参数或未知命令
```

分位照 `st_httpclient`：`int` 数组 + `qsort`，不要 `std::chrono`。

**核心流程。** 不走 `tcp_sendrecv`。RESP 的长度在 bulk 头里，和 HTTP chunked 一样不适合「预先一个回调判整包」那种一次收满；做法对齐 `st_memcacheclient` 的 `memcache_exchange` 和 `st_http_exchange`：

1. `AllocPtr(eTCP_CONN)` → `SetTimeout` → `Create`。
2. `resp_encode` 把命令写成 `*<n>\r\n` + 若干 `$<len>\r\n...<r\n`，`st_send` 发出去。剩余时间照 `app/st_c.cc` 的 `time_left`。
3. `st_recv` 喂给 `resp_parse`。状态：等类型字节 → `+`/`-`/`:` 读到 `\r\n` → `$` 读长度再读正文和 `\r\n` → `*` 读元素个数再递归。`$-1` 是空批量。嵌套数组只要能接住 `*1` 这种回复即可，不追求完整 RESP3。
4. `+`/`:`/`$` 计 ok，`-` 计 fail（这是 Redis 的错误回复，不是传输失败）。传输失败也计 fail。
5. `FreePtr`。压测模式每个请求一条短连接，和 memcache 样例一致。同一协程里复用连接不做进第一期（避免踩 G6）；README 写一句。

**数据结构。** `RespValue`：`type`（`kSimple` / `kError` / `kInt` / `kBulk` / `kArray` / `kNull`）、`int i`、`char *buf`（`malloc`，调用方 `resp_free`）、数组用 `RespValue *elem` + `int n`（第一期 `n` 只保证 0 或 1，多元素留在解码器里但客户端命令用不到）。编码缓冲是栈上 4096 或按参数长度 `malloc`。不引入 `std::string` 的必须性；用 `char *` 和长度，和 memcache 样例一样。

**库缺口。** 没有。协议和压测都在 `app/`。不要把 RESP 放进 `libmthread`。

**冒烟 / 测试。**

- `tests/st_redis_resp_unittest.cpp`：对固定字节串做 encode/decode（`PING` 的请求、`+PONG\r\n`、`$3\r\nbar\r\n`、`$-1\r\n`、`:1\r\n`、`-ERR x\r\n`）。不需要网络。挂进 `tests/Makefile`，CI 的 `make -C tests run` 会跑到。
- `scripts/smoke_redis.sh`，`make smoke-redis`。优先如果 `redis-server` 在 `PATH` 里，就用 `--save "" --appendonly no --bind 127.0.0.1 --port $PORT --protected-mode no` 起一个，trap 杀掉。否则用 `python3 scripts/redis_stub.py`（只实现 PING/SET/GET/INCR，内存 dict）。然后 `st_redisclient ping`、`set k v`、`get k`、`incr n`，以及 `-c 4 -n 20 ping` 的 SUMMARY `ok=20`。
- CI **不** `apt-get install redis`。stub 是脚本，不是运行时依赖。
- 可选压测：本机已有 redis 时，`st_redisclient -c 50 -n 5000 ping`。不新增 `make bench-redis`，避免 `make bench` 变长。README 给出命令即可。
- Android 只编译客户端，不跑 stub。

**文档。** 根 readme 把 Redis 和 Memcache 放在相邻的一节：都是协议样例，端到端依赖外部服务或 stub。`app/st_redisclient/README.md` 写 RESP 子集和「没有 hiredis」。

**改动面与风险。** `app/st_redisclient` + 一个 Python stub + 一个不联网的单测。风险是 RESP 解析在拆包边界上错（`$` 的长度和一个字节一个字节的 `st_recv`）。单测喂切片式的 `resp_parse` 两次调用，锁住「第一次只拿到类型字节」。stub 和真实 redis 的空白差异：冒烟只断言我们发出的那四条命令。

### 3.5 聊天室 / 广播（#9）

**目标。** 一条连接上的一行字，出现在其他已连接的客户端上。演示协程之间怎么交接数据，以及为什么不能直接写对方的 socket。

**目录。**

```
app/st_chat/server.cpp     # st_chatserver
app/st_chat/client.cpp     # st_chatclient
app/st_chat/makefile
app/st_chat/README.md
```

**CLI。**

```
st_chatserver [ip] [port]          # 默认 0.0.0.0 7700
st_chatclient [-t MS] [-n NICK] host port message...
  每条 message 发一行。发完再读，直到累计收到至少 1 行广播或超时。
  默认 nick 是 pid。默认超时 3000。
  把读到的行打到 stdout，一行一条。
退出码：0 读到了至少一行；1 超时或连接失败；2 参数错误
```

不做交互式 stdin。fd 0 不在 `sys_fd` 表里，`read(0)` 会堵住整个 OS 线程（G5）。

**协议。** 一行一个 UTF-8 文本，`\n` 结尾，单行不超过 512 字节。

- 客户端第一条是 nick，服务端回 `* joined <nick>\n`，并向其他人发 `* <nick> joined\n`。
- 之后每行变成 `<nick>: <text>\n` 发给其他人，不回显给发送者。
- 行 `quit` 或对端关闭：向其他人发 `* <nick> left\n`，然后关掉。

单房间。多频道不做（N，见 D11 的范围）。

**核心流程。** **不用 `StServer::Loop`。** `CallBack` 只会 `RecvData`，空闲连接收不到别人的消息（G3、G4）。监听段仍然用库里的同一套：`StServer` 的 `CreateSocket` + `Listen`（监听 fd 已经登记了事件项），然后自己的循环调用 `st_accept`。每个 connfd：`sys_new_fd` 或不经过 hook、直接 `::fcntl(O_NONBLOCK)`，分配自己的 `StEventItem` 并 `Add`，再 `CreateThread`。

每个连接协程独占自己的 socket。旁边有一根非阻塞 pipe（`pipe` + `O_NONBLOCK`）：

- socket 和 pipe 的读端各一个 `StEventItem`（两个 fd，不违反「一 fd 一项」）。
- 协程调用 `StEventSchedule::Schedule`，fdset 里放这两项，超时用 `Util::TimeMs() + idle`。`Schedule` 返回后看哪一项 `GetRecvEvents() != 0`。
- pipe 可读：读掉字节（合并多次唤醒），把邮箱里的行 `st_send` 到**自己的** socket。
- socket 可读：`st_recv` 已经不适合再套一层（会再 `Schedule` 一次）。这里直接 `::recv`，因为 `Schedule` 刚确认可读；读到的字节拼进行缓冲，满一行就广播。
- `recv_num==0`：这是 `Schedule` 的超时（`ETIME`）。空闲超时只继续循环，不断开（聊天室要一直等）。截止时间由样例自己算，不靠把 `ETIME` 当成「有人广播」。

广播方**只**做两件事：把一行拷进对方的邮箱，若对方 pipe 的「已投递」标志为 0，就写 1 字节并置标志。不调用对方 socket 上的 `st_send` / `WaitFdReady`。对方协程醒来后清标志。pipe 写满时（标志已经是 1）不再写。

临界区里不要 Yield：改邮箱、改标志、`write(pipe)` 都在让出之前做完。单 OS 线程、协作式调度，这里不用互斥锁。

**数据结构。**

```text
Session （固定数组，最多 64 个）
  int used
  int fd
  int pipe_wr, pipe_rd
  int poke          # pipe 里已经有字节则为 1
  StThread *self
  char nick[32]
  Mail mail         # 定长环：char text[8][512]，int head，int tail
```

满员时 `st_accept` 出来的 fd 直接 `sys_close`，不创建协程。邮箱满时丢掉这一行并给发送者回 `* dropped\n`（写自己的 socket，不写别人的）。

**库缺口。** 公开 API 里没有 channel。`Pend` / `Unpend` 不能在「正堵在 socket 上」的时候把协程叫醒（G4）。本篇**不**加 `st_notify`，**不**改 `Schedule` 的 `ETIME` 语义。pipe + 已有的 fdset `Schedule` 足够把样例跑通。`stlib/tests/ucontext/channel.c` 是 Russ Cox 的参考，不参与构建，也不要抄进 `src/`。

`Session.self` 只用于调试打印名字，不参与叫醒。

**冒烟 / 测试。**

- `scripts/smoke_chat.sh`，`make smoke-chat`。端口 `17700`。
- 起 server。起 client B：`-n bob`，消息省略，只等一行，超时 3s（后台）。等 server 打出 `listening`。起 client A：`-n ada hello`。
- B 的 stdout 含 `ada: hello` 或 `* ada joined`（实现时选定一种作为断言，推荐断言 `ada: hello`，joined 可以在 A 连接后、发 hello 之前到达，B 要能读多行直到看到 hello 或超时）。
- A 退出后 B 若还活着应能看到 `* ada left`；冒烟可以不断言 left，避免和进程退出赛跑。断言 hello 即可。
- Android 只编译。

**文档。** README 用一小段说明「为什么是 pipe」：一个 fd 一个事件项；`Pend` 等不到 socket；`Unpend` 不能用在 IO 队列上的线程。根 readme 只给命令和这句限制，不展开调度器。

**改动面与风险。** 只在 `app/st_chat`。风险集中在 `Schedule` 的 fdset 路径（生产代码里 `WaitFdReady` 只传单个 item，多 fd 路径单测少）。冒烟就是这条路径的回归。第二个风险是 pipe 标志和「字节还在 pipe 里」不一致，导致再也不写 pipe、广播丢了：标志只在读完 pipe 之后清，写之前用非阻塞写，写失败 `EAGAIN` 就保持 `poke=1`。

### 3.6 未改过的阻塞代码 + syscall hook（#11）

**目标。** 库最想给人看的一点：业务代码按阻塞 POSIX 来写，跑在协程里时一次慢 `read` 不会堵住其他协程。今天没有这个样例。`app/st_sys.cc` 提供的是 `sys_*`，不是 libc 的 `read`。

**目录。**

```
app/st_hookdemo/blocking_client.c   # 只有 POSIX 调用，不出现 St* / st_*
app/st_hookdemo/st_posix_alias.h    # 仅宏 + sys_* 声明；业务 .c 自己不 include 它
app/st_hookdemo/main.cpp            # 起框架、建协程、调 blocking_exchange
app/st_hookdemo/makefile
app/st_hookdemo/README.md
```

产物：`st_hookdemo`（协程跑法），以及 `blocking_client`（同一份 `.c`，不 `-include`，不链 `libmthread`，用来证明它真是阻塞客户端）。

**CLI。**

```
st_hookdemo [-c CONC] [-n TOTAL] [-t MS] [-s PAYLOAD] host port
  默认 -c 4 -n 4 -t 3000，payload "hook"
  SUMMARY ok=.. fail=.. elapsed_ms=..
退出码：0 全部 ok；1 有失败；2 参数或 host 不是 IPv4 字面量

blocking_client host port payload
  一次交换，退出码 0/1。给冒烟做对照。
```

**业务文件里允许的调用（写在 README 顶部）。** `socket`、`connect`、`setsockopt`、`write`、`read`、`close`。用 `inet_pton` 填地址。超时用 `setsockopt(SO_RCVTIMEO / SO_SNDTIMEO)`。不要 `sleep`、`getaddrinfo`、`poll`、`select`、`readv`。

`blocking_exchange(ip, port, payload, timeout_ms)` 的形状：

1. `socket(AF_INET, SOCK_STREAM, 0)`。
2. `setsockopt` 两个 `timeval`（毫秒换成 `tv_sec` / `tv_usec`）。hook 打开时 `sys_setsockopt` 会把它们写成 `sys_fd` 的毫秒超时，盖过默认的 512 ms。
3. `connect`。
4. `write` 完整的 `payload\n`。
5. `read` 直到见到 `\n` 或对端关闭或错误。
6. `close`。成功返回 0。

**编译。** `blocking_client.c` 用仓库的 `$(CC)` 编译（C++ 编译器吃这份 C 源，这样 `-include` 的头可以带 `extern "C"` 声明）。协程目标：

```make
$(CC) -include st_posix_alias.h -c blocking_client.c
```

`st_posix_alias.h` 把 `socket` `connect` `read` `write` `close` `setsockopt` 定义成 `sys_*`，并声明这些 `sys_*`。业务文件的文本里仍然是 POSIX 名字。对照目标不加 `-include`，只链 libc。

`main.cpp`：`st_init_frame`、`st_set_hook_flag`、`Frame::CreateThread` 调 `blocking_exchange`。对端用 `st_echoserver`。`-c` 个协程同时读时，慢的那一个在 `sys_read` → `st_read` 里 Yield，其他协程继续。

**库缺口（不修的话样例不成立，见 G1）。** 建议改动，限制在 hook 层：

1. `sys_socket` 设置内核 `O_NONBLOCK` 时走 `::fcntl` / `REAL_FUNC(fcntl)`，**不要**走会打上 `ST_FD_FLG_UNBLOCK` 的 `sys_fcntl`。用户自己 `fcntl(F_SETFL, O_NONBLOCK)` 或 `ioctl(FIONBIO)` 仍然置 `ST_FD_FLG_UNBLOCK`，hook 让开，行为与现在的注释一致。
2. `sys_connect` / `sys_read` / `sys_write` / `sys_recv` / `sys_send`：fd 已在 `sys_fd` 表、hook 开着、没有 `ST_FD_FLG_UNBLOCK`、且 `GetEventItem(fd)==NULL` 时，hook 层 `AllocPtr<StEventItem>`、`SetOsfd`、`Add`，把指针记在 `sys_fd` 里（新增一个字段，表示「这项是 hook 自己登记的」）。
3. `sys_close`：如果这项是 hook 登记的，`ClearItem` + `UtilPtrPoolFree`，再 `sys_free_fd`。`StClientConnection::Create` 自己 `Add` 的项不是 hook 登记的，`ReleaseItem` 仍由连接对象释放，避免双重释放。
4. `StClientConnection::Create` 的顺序是 `sys_socket` 然后自己 `Add`。只要 `sys_socket` 不登记事件项，现有客户端路径只是「内核非阻塞但不再误标 `UNBLOCK`」。现有样例走 `st_recv` / `st_send`，不走 `sys_read`，这条标志本来也不影响它们。

`sys_accept` 本期不改（N5）。hook 样例是客户端。

不在 `libmthread.so` 里定义 `read` 符号（N4）。

**冒烟 / 测试。**

- 更新 `tests/st_hook_unittest.cpp`：补一条「`sys_socket` 出来的 fd，在 hook 打开且对端先不写数据时，`sys_read` 会挂起协程直到超时，errno 为 `ETIME`，而不是立刻 `EAGAIN`」。现有「用 `sys_new_fd` 绕开 `sys_socket`」的用例保留，避免把两条路径绑死。
- `scripts/smoke_hook.sh`，`make smoke-hook`。起 `st_echoserver`。先跑不链接库的 `blocking_client`，确认回显。再跑 `st_hookdemo -c 4 -n 4`，`ok=4`。
- Android 只编译 `st_hookdemo`。对照用的纯 libc 二进制也编出来即可，不在设备上跑。

**文档。** 根 readme 用单独一节讲 hook，因为这是和其他网络库不一样的地方。说明三件事：业务文件长什么样、`-include` 做了什么、为什么不覆盖全局 `read`。`AGENTS.md` 里 hook 那句（`sys_*` 是 libc 形状）可以加半句「样例见 `app/st_hookdemo`」，留到实现时再改。本计划现在只在样例清单那一行加索引（见文末对 `AGENTS.md` 的改动）。

**改动面与风险。** `app/st_sys.cc` / `app/st_sys.h` 的 `sys_socket`、`sys_close` 和「缺事件项时补登记」。这是本篇最大的行为改动。风险：

- 误标去掉之后，旧测试若依赖「`sys_socket` + `sys_read` 立即 `EAGAIN`」，会失败。Phase 0 先 grep `sys_socket` 的调用点（目前主要是库内部 `Create` / `CreateSocket` 和 hook 单测），单测按新契约改。
- hook 自己登记的 `StEventItem` 若和 `Create` 的项叠在同一 fd 上，会泄漏或双重释放。规则：只有 `GetEventItem==NULL` 时才登记，并打上所有权标志；`Add` 里「item replace」那条日志在冒烟里应当不出现。
- 默认 512 ms 太短，业务文件必须 `setsockopt`。README 和样例都这么做。

---

## 4. 分阶段执行

依赖关系：

```
Phase 0 实测（无功能代码）
    │
    ▼
Phase 1 echo ──────────────┬──────────────► Phase 5 反代（还依赖已有的 httpserver / httpclient）
    │                      │
    ├─► Phase 2 端口扫描（可能改 Create / st_connect）
    │
    └─► Phase 3 hook（改 sys_socket；对端用 echo）

Phase 4 Redis（与上面无关，RESP 单测可与 Phase 1 并行）

Phase 6 聊天室（自己的 accept 循环；echo 只提供「服务端怎么 Listen」的参照）
```

每个阶段的出口都包含：现有 POSIX workflow + `format.yml` 全绿；`make apps` 编过新目标；`make clean` 后 `git status --porcelain --ignored` 为空。Linux `st_context_unittest` 的 64 KiB `makecontext` SIGABRT 仍按 plan/09 如实记录，本篇不改 `STACK`。Android workflow 只要求新样例交叉编译通过。

### Phase 0 · 实测锚点

不提交功能代码。把结果写进 §9。

1. loopback：`st_connect` 对正在 `listen` 的端口、以及对没人听的端口，分别记录返回值和 errno（Linux 与 macOS 各一次；macOS 用 CI 即可）。
2. `StClientConnection::Create` 失败返回之后，errno 还是不是 `ECONNREFUSED` / `ETIME`。
3. `sys_socket` + hook + `sys_read`（对端不写）是立刻 `EAGAIN`，还是挂起。对照 `tests/st_hook_unittest.cpp` 的现有注释。
4. 确认 `FORMAT_SRC`、`.gitignore`、根 `clean` 的改法，列进 Phase 1 的文件清单。

**出口**：§9 有一张表。G1/G2 与源码不符时，先改本文再写代码。

### Phase 1 · echo

按 §3.1 实现。根 `makefile` 的 `apps` / `clean` / `help`，`FORMAT_SRC`，`.gitignore`，`scripts/smoke_echo.sh`，`_build.yml` 增加 `make smoke-echo`。readme 两份加上入口示例。

**出口**：`make smoke-echo` 在 Linux 上 `ok` 全过；`st_echoclient` 对没人听的端口退出码 1。

### Phase 2 · 端口扫描

按 §3.2。先做 errno 保存（D3）。SO_ERROR（D4）仅当 Phase 0 证明有误判。`make smoke-portscan` 进 `_build.yml`。

**出口**：冒烟 open/refused 分类稳定；单测锁住 errno；CI 不要求出现 `ETIME`。

### Phase 3 · hook 样例

按 §3.6，依赖 Phase 1 的 echo。先改 hook（D6、D7），再写样例。更新 hook 单测。`make smoke-hook` 进 `_build.yml`。

**出口**：同一份 `blocking_client.c`，纯 libc 二进制和 `st_hookdemo -c 4` 都能对 echo 回显；`sys_read` 超时单测为 `ETIME`。

### Phase 4 · Redis

按 §3.4。与 Phase 1–3 无代码依赖，可以紧跟 Phase 1。`make smoke-redis` 进 `_build.yml`（stub，不装 redis）。

**出口**：RESP 单测通过；冒烟四条命令加 `-c 4 -n 20 ping`。

### Phase 5 · 反代

按 §3.3。依赖已有的 `st_httpserver` 和 `st_http_exchange`，不依赖 Phase 1 的二进制（冒烟直接起 httpserver）。`make smoke-proxy` 进 `_build.yml`。

**出口**：双后端 200；停掉一个仍 200；两个都停则 502。

### Phase 6 · 聊天室

按 §3.5。`make smoke-chat` 进 `_build.yml`。

**出口**：B 收到 A 的 `ada: hello`。`Schedule` 多 fd 没有「item replace」日志。

### 收尾

`plan/README.md` 把 11 标成完成（实现 PR 里做，不是本计划 PR）。`AGENTS.md` 的 `make apps` 行补上六个名字。不把 echo/redis 塞进默认 `make bench`（那个目标今天是 `bench-http` + `bench-dns`）。

---

## 5. 决策点（实现前需拍板）

未回复时按「建议」列实现。

| # | 问题 | 选项 | 建议 |
| --- | --- | --- | --- |
| D1 | readme 入口示例 | a) 加 echo 一节，保留 DNS；b) 用 echo 换掉 DNS 那节 | **a** |
| D2 | echo 的连接模型 | a) 只做短连接，拆包算限制；b) 服务端 `eTCP_KEEPLIVE_CONN` 把一行收齐，客户端仍短连接 | **b**。`FreePtr` 仍关 fd，README 写明这不是连接池 |
| D3 | `Create` 失败后是否保存 errno | a) 改 `StClientConnection::Create`，返回值不变；b) 扫描器自己 `socket` + `st_connect`，不改库 | **a**。调用方终于能区分 `ETIME` 和 `ECONNREFUSED` |
| D4 | `st_connect` 是否查 `SO_ERROR` | a) Phase 0 已能区分就不改；b) 无论如何都查 | **a** |
| D5 | 扫描并发 | a) 默认 `-c 64`，CI `-c 32`；b) CI 里起 2000 个协程 | **a**。栈大约 266KB，2000 个只写在 README 的手工命令里 |
| D6 | 「不改业务源码」做到哪一步 | a) `.c` 里是 POSIX 名字，makefile `-include` 换成 `sys_*`，另编一份纯 libc 二进制；b) `LD_PRELOAD` 一份 so；c) 在 `libmthread.so` 里导出 `read` | **a** |
| D7 | 是否修 G1（`UNBLOCK` + 缺事件项时由 hook 登记） | a) 修，否则样例不成立；b) 样例改成直接调用 `sys_*`，并在文档里承认它不是 POSIX 名字 | **a**。`sys_accept` 不动 |
| D8 | 反代的池放哪 | a) 只在 `app/st_httpproxy` 的槽位里，池满 `st_sleep(1)`；b) 顺手做掉 07-D1 的 `FreePtr` 真复用 | **a** |
| D9 | 反代响应体 | a) 超过 `ST_SEND_BUFFSIZE` 回 502，不改常量；b) 加大缓冲 | **a** |
| D10 | Redis 冒烟 | a) Python stub，有 `redis-server` 就用它；b) CI 安装 redis；c) 只编译，像 memcache | **a** |
| D11 | 聊天室叫醒 | a) 每连接一根 pipe，`Schedule` 的 fdset 同时等 socket 和 pipe；b) 新做 `st_notify` 并改 `ETIME` 语义 | **a**。单房间、最多 64 人 |
| D12 | CI 跑哪些冒烟 | a) 六个 `smoke-*` 都进 `_build.yml`；b) 只跑 echo 和 hook | **a**。都是 loopback、数秒级。Android 仍只编译。不放进 `make bench` |
| D13 | 二进制名字 | a) `st_echoserver` 这类显式名字，写入 `.gitignore` 和 `make clean`；b) 继续叫 `main` | **a** |
| D14 | 新文件进不进 `format-check` | a) 把本篇新的 `.h/.cc/.cpp` 和单测写进 `FORMAT_SRC`；b) 维持「app 历史代码不格式化」 | **a**。旧的 `app/st_dns` 等仍然不在表里 |

---

## 6. 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R1 | G1 的修法让「`sys_socket` 后立刻 `sys_read`」从 `EAGAIN` 变成挂起 | 中 | Phase 0 列出调用点；hook 单测改成断言 `ETIME`；`Create` 不走 `sys_read` |
| R2 | hook 登记的 `StEventItem` 和 `Create` 的项双重释放或泄漏 | 高 | 只在 `GetEventItem==NULL` 时登记；所有权标志；`sys_close` 只释放自己的 |
| R3 | D4 的 `SO_ERROR` 把成功连接判失败 | 高 | 默认不改；要改就必须先有失败复现和 open/refused 单测 |
| R4 | `eTCP_KEEPLIVE_CONN` 被当成池已经做好 | 中 | 文案写明 `FreePtr` 仍 `HashRemove`；echo 客户端用 `keeplive=false` |
| R5 | 聊天室 `Schedule` fdset 路径很少被现有单测走到 | 中 | `smoke-chat` 进 CI；不在这条路径上改 `Schedule` 本身 |
| R6 | `-c` 很大时栈和 `NOFILE` 把 CI 打满 | 中 | 冒烟 ≤ 32；README 写 266KB 和 `ulimit` |
| R7 | 反代和 `st_http_exchange` 编译期绑在一起 | 低 | makefile 显式列出 `http_client.cc`；不把 HTTP 客户端塞进库 |
| R8 | Python stub 和真实 RESP 有差别 | 低 | 单测覆盖解码；冒烟只打四条命令；本机 redis 可选 |
| R9 | 新二进制没进 `.gitignore` / `clean`，CI 最后一步失败 | 中 | D13；Phase 1 就把六条路径的位置写进清单，后续阶段只填文件 |
| R10 | `FORMAT_SRC` 漏了新文件，格式债留到以后 | 低 | D14 |
| R11 | 粘包、POST body、512 ms 默认超时被当成库 bug | 低 | 每个 README 的「限制」写明；hook 样例一定 `setsockopt` |

---

## 7. 验收清单

### A. 六个样例

- [ ] `make apps` 产出 `st_echoserver`、`st_echoclient`、`st_portscan`、`st_httpproxy`、`st_redisclient`、`st_chatserver`、`st_chatclient`、`st_hookdemo`、`blocking_client`
- [ ] 每个目录有 README：编译、运行、限制
- [ ] `make smoke-echo` `smoke-portscan` `smoke-hook` `smoke-redis` `smoke-proxy` `smoke-chat` 在 Linux 上退出码 0
- [ ] 上述冒烟写进 `_build.yml`（Android workflow 不跑它们）
- [ ] `make android` 仍只编译，且新 makefile 能过 NDK（arm64-v8a 与 x86_64）

### B. 契约

- [ ] echo 客户端用 `tcp_sendrecv`，超时按 `app/st_c.h` 的 `-3` 解释
- [ ] 端口扫描把 `ETIME` 和 `ECONNREFUSED` 分进不同计数；CI 断言 open 与 refused
- [ ] hook 业务文件源码中不出现 `st_` / `St`；`-include` 只出现在 makefile
- [ ] 反代不调用 `eTCP_KEEPLIVE_CONN` 来表示池；槽位在样例内
- [ ] 聊天室广播不在别的协程的 socket 上调用 `st_send`

### C. 工程

- [ ] C++98，无第三方运行时；`ldd` / `otool -L` 只有系统库和 `libmthread`
- [ ] 新源文件在 `FORMAT_SRC` 里，`clang-format-18` 通过
- [ ] `make clean` 后 `git status --porcelain --ignored` 为空
- [ ] 未新增根 `LICENSE`；未提交 `.session_tmps/`
- [ ] `STACK`、`MEM_PAGE_SIZE`、枚举值、`st_*` 签名未改
- [ ] RESP 单测、connect errno 单测、hook 超时单测进 `make -C tests run`

---

## 8. 建议目录落点（实现时）

```
app/st_echo/                 # st_echoserver, st_echoclient
app/st_portscan/             # st_portscan
app/st_httpproxy/            # st_httpproxy（编译期用 st_httpclient、http_parser.c）
app/st_redisclient/          # st_redisclient + resp.*
app/st_chat/                 # st_chatserver, st_chatclient
app/st_hookdemo/             # blocking_client.c, st_posix_alias.h, st_hookdemo
scripts/smoke_echo.sh
scripts/smoke_portscan.sh
scripts/smoke_httpproxy.sh
scripts/smoke_redis.sh
scripts/redis_stub.py
scripts/smoke_chat.sh
scripts/smoke_hook.sh
tests/st_redis_resp_unittest.cpp
tests/st_connect_errno_unittest.cpp
# tests/st_hook_unittest.cpp 增补，不新建
# 可能改动的库文件：
#   src/st_connection.h      Create 失败路径保存 errno（D3）
#   src/st_sys.cc            仅当 D4 触发时，st_connect 查 SO_ERROR
#   app/st_sys.h app/st_sys.cc   sys_socket / sys_close / 懒登记事件项（D7）
makefile                     apps / clean / help / FORMAT_SRC / smoke-*
.gitignore
.github/workflows/_build.yml
readme.md readme_en.md
AGENTS.md                    实现完成时补 make apps 列表
```

---

## 9. 落地记录（实现时填写）

> 本文件提交时还没有功能代码。Phase 0 的实测表、每个 Phase 的提交号和冒烟结果写在这里。

| Phase | 提交 | 结果 |
| --- | --- | --- |
| 0 |  |  |
| 1 echo |  |  |
| 2 portscan |  |  |
| 3 hook |  |  |
| 4 redis |  |  |
| 5 proxy |  |  |
| 6 chat |  |  |

---

## 10. 与需求的对照

| 需求 | 落点 |
| --- | --- |
| 1. TCP echo，最小收发，readme 入口，最简单的 bench 基线 | §3.1，Phase 1。基线是样例自己的 SUMMARY，不并进 `make bench` |
| 3. 端口扫描，并发 `st_connect`，超时 vs 拒绝 | §3.2，Phase 2。CI 断言 open/refused；`ETIME` 靠 errno 分类，不靠外网黑洞 |
| 5. HTTP 反代，组合 httpserver 与 httpclient，池、超时、探活 | §3.3，Phase 5。池在样例槽位里 |
| 7. Redis RESP + benchmark 模式 | §3.4，Phase 4。无 hiredis；redis-server 可选 |
| 9. 聊天室广播，协程之间叫醒 | §3.5，Phase 6。pipe + `Schedule` fdset |
| 11. 不改的阻塞代码经 hook 跑在协程里 | §3.6，Phase 3。`-include` 换名；要修 G1 |
| 只写计划、不改功能代码 | 本文 + `plan/README.md` 索引 |
