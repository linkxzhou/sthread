# 11 · 六个新样例：echo / 端口扫描 / HTTP 反代 / Redis / 聊天室 / POSIX hook

> **状态：📋 计划（未开工）** · 2026-10-08。基线：`master` = `486611f`（PR #15，`st_*` 超时统一为 `-1` 且 `errno == ETIME`）。
>
> **决策：D1–D14 已决定**（§6）。用户拍板 5 项（readme 保留 DNS、必须先修 hook、聊天室改用库内通知原语、反代池只放样例、六个冒烟不进 CI）；其余按原推荐定稿。
>
> 本文档**只描述计划，不包含任何功能代码变更**。和 [`08`](08-apps-bench-dnsserver.md) / [`10`](10-cross-platform-android-httpclient.md) 的关系：08 已经有 HTTP/DNS 压测闭环和 `st_dnsserver`；10 已经有 `st_httpclient`（`st_http_exchange`）和 Android 只编译。本篇加六个样例，以及两个库前置：hook 的 `ST_FD_FLG_UNBLOCK`（Phase 1，然后才是 hookdemo），协程通知 `st_notify` / `st_wait`（Phase 2，然后才是聊天室）。另有 `Create` 失败路径保存 errno。调度模型、栈大小、已有枚举数值不动。
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
| 9 | `app/st_chat` | 一行消息广播到其他连接；用库里的 `st_notify` / `st_wait` 叫醒，不占 fd，不写对方的 socket |
| 11 | `app/st_hookdemo` | 一份只调用 POSIX `socket` / `connect` / `read` / `write` 的客户端，原样放进协程跑 |

### 1.2 非目标（明确不做）

| # | 不做 | 理由 |
| --- | --- | --- |
| N1 | 不改调度模型、`STACK`（260096）、`MEM_PAGE_SIZE`（2048）、`eConnType` 数值、`st_*` / `tcp_sendrecv` / `udp_sendrecv` 的公开签名和状态码 | 公开 API 兼容 |
| N2 | 不把 keepalive **真连接池**做进 `StConnectionManager::FreePtr` | 仍是 07-D1。反代的槽位放在样例里，调用方式对齐 `st_httpclient -k` |
| N3 | 不引入 hiredis、libevent、CMake、gtest | makefile + 自带 `stlib/st_test.h`；RESP 自己解析 |
| N4 | 不把 `read` / `write` / `connect` 导出成 `libmthread.so` 的全局符号，也不把 LD_PRELOAD / `DYLD_INSERT_LIBRARIES` 当作默认跑法 | 会劫持进程里所有读，包括 hook 自己的 `dlsym` 回退。macOS 两级命名空间和 Android 只编译都接不住 |
| N5 | 不改 `sys_accept` | 它今天调用的是真实 `accept`（见 `app/st_sys.cc`），会堵住整个 OS 线程。六个样例里的服务端走 `st_accept` |
| N6 | 不把 `stlib/tests/ucontext/channel.c` 抄进 `src/`，通知原语不占用 fd | 用本篇的 `st_notify` / `st_wait`（§3.2）。`Pend` / `Unpend` 仍只给父子协程。通知位未置时，`Schedule` 没有 IO 事件仍报 `ETIME` |
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

这个缺口由 §3.2 的 `st_notify` / `st_wait` 补上，并且排在聊天室样例之前。`Pend` / `Unpend` 继续只服务父子协程，不拿来做广播。

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

**CI 边界（D12，已决定）。** 六个样例的 `scripts/smoke_*.sh` 和根 makefile 的 `make smoke-*` 只给本地手动跑，不写进 `_build.yml`。CI 继续用已有的 `make apps`、`make android`（只编译）和 `make -C tests run`。hook 修复、`st_notify` / `st_wait`、`Create` 的 errno 这些库层单测放在 `tests/`，因此会进 CI。样例冒烟不进。

---

## 3. 库前置

样例动手之前先落地下面两段。Phase 1 修完 hook，Phase 4 的 hookdemo 才有意义。Phase 2 的通知原语落地后，Phase 8 的聊天室才开始写。两段都带 `tests/` 单测，跟着现有的 `make -C tests run` 进 CI。不依赖 echo 二进制。

### 3.1 Hook 修复（Phase 1，hookdemo 的前置）

**要修的行为。** G1：`sys_socket` 经 `sys_fcntl(F_SETFL, O_NONBLOCK)` 置了 `ST_FD_FLG_UNBLOCK`，`sys_read` / `sys_write` / `sys_recv` / `sys_send` 就走真实 syscall，协程不 Yield。同时 `sys_socket` 不登记 `StEventItem`，`st_connect` / `st_read` 会得到 `-2` / `EINVAL`。

**改动（只动 hook 层）。**

1. `app/st_sys.cc` 的 `sys_socket`：内核 `O_NONBLOCK` 用 `::fcntl` 或 `REAL_FUNC(fcntl)` 设置，不走会打上 `ST_FD_FLG_UNBLOCK` 的 `sys_fcntl`。
2. 用户自己 `fcntl(F_SETFL, O_NONBLOCK)` 或 `ioctl(FIONBIO)` 仍然置 `ST_FD_FLG_UNBLOCK`。这条保持：用户要非阻塞时，hook 让开。
3. `sys_connect` / `sys_read` / `sys_write` / `sys_recv` / `sys_send`：fd 已在 `sys_fd` 表中、hook 已打开、没有 `ST_FD_FLG_UNBLOCK`、且 `GetEventItem(fd)==NULL` 时，hook 层 `AllocPtr<StEventItem>`、`SetOsfd`、`Add`，指针记在 `sys_fd` 的新字段里，表示「这项是 hook 自己登记的」。
4. `sys_close`：仅当该项是 hook 登记的，才 `ClearItem` + `UtilPtrPoolFree`，然后 `sys_free_fd`。`StClientConnection::Create` 自己 `Add` 的项不由 `sys_close` 释放，`ReleaseItem` 仍归连接对象，避免双重释放。
5. `Create` 的顺序仍是 `sys_socket` 然后自己 `Add`。`sys_socket` 不在这里登记事件项，所以不会和 `Create` 抢同一项。现有样例走 `st_recv` / `st_send`，不走 `sys_read`。
6. `sys_accept` 不改（N5）。不在 `libmthread.so` 里定义 `read`（N4）。

**验收。**

- `sys_socket` 返回后，`sys_find_fd(fd)->sock_flag` 不含 `ST_FD_FLG_UNBLOCK`，内核标志仍是 `O_NONBLOCK`。
- hook 打开、对端不写数据：`sys_read` 挂起协程，超时返回 `-1` 且 `errno==ETIME`，不是立刻 `EAGAIN`。
- 用户再 `sys_fcntl(F_SETFL, O_NONBLOCK)` 之后，同一次 `sys_read` 立刻 `EAGAIN`，不挂起。
- 未登记事件项的 fd，第一次 `sys_read` / `sys_connect` 会自己 `Add`；`sys_close` 之后 `GetEventItem(fd)==NULL`，再关一次不双重释放。
- 现有 `StClientConnection::Create` / `StServer::CreateSocket` 路径的单测仍然通过，日志里不出现 `item replace`。
- `make -C tests run` 含下面的用例；`_build.yml` 不用加新步骤。

**单测。** 改 `tests/st_hook_unittest.cpp`（已在 `FORMAT_SRC` 和 `tests/Makefile` 里），不新建文件：

| 用例 | 断言 |
| --- | --- |
| `sys_socket` + hook + 对端暂不写 | `sys_read` 超时，`errno==ETIME` |
| 同上，但调用方又 `fcntl` 了 `O_NONBLOCK` | `sys_read` 立刻失败，`errno==EAGAIN` |
| `sys_socket` + `sys_connect` 到 loopback 上正在 listen 的端口 | 返回 0；期间发生过 Yield（另一个协程的计数增加） |
| `sys_close` 两次 | 第二次是 `EBADF` 一类，进程不崩 |
| 现有 `sys_new_fd` 绕开 `sys_socket` 的用例 | 保留，两条路径都在 |

### 3.2 通知原语 `st_notify` / `st_wait`（Phase 2，聊天室的前置）

**要补的能力。** 同一 OS 线程上，协程 A 可以在超时之内等「有人叫我」或「fd 可读/可写」，协程 B 叫它时不占用 fd、不加 pipe。实现落在已有的睡眠堆和可运行队列上：`RemoveSleep` / `RemoveIOWait` / `InsertRunable`（`src/st_thread.cc`）。不新开一种队列，不改 `eThreadFlag` / `eThreadState` 已有数值。

**状态。** `StThreadItem`（`src/st_poll.h`）加一个 `int m_notified_`，0 或 1。`Reset()` 清零，避免对象池把粘滞通知带给下一个协程。这不是队列成员标志，所以不进 `eThreadFlag`。

**放哪。**

| 内容 | 文件 |
| --- | --- |
| 声明、`ST_WAIT_FD` / `ST_WAIT_NOTIFY`、返回值注释 | `src/st_sys.h`（和 `st_sleep` 同一头；使用方已经为带超时 IO include 它） |
| `st_notify_wait` / `st_notify` / `st_wait` 的定义 | `src/st_sys.cc` |
| 把目标从睡眠堆或 IO 队列移到可运行队列 | `StThreadSchedule::Notify`，声明 `src/st_thread.h`，定义 `src/st_thread.cc` |

三个函数是 C++ 函数，**不**放进 `extern "C"`：参数里有 `StThread*`。声明在全局作用域，调用写法和 `st_sleep` 一样，不用加命名空间。`src/st_sys.h` 已经 include `st_thread.h`。

**API 草案（C++98）。**

```cpp
#define ST_WAIT_FD     0x1
#define ST_WAIT_NOTIFY 0x2

/* 当前协程等到通知，或超时。
 *  0  被 st_notify 叫醒，含调用前已经记下的粘滞通知
 * -1  超时，errno=ETIME；当前没有协程，errno=EINVAL
 * timeout_ms < 0：一直等（内部收成 0x7fffffff，与 NormalizeTimeoutMs 相同）
 * timeout_ms == 0：不挂起；有粘滞则 0，否则 -1 且 errno=ETIME
 */
int st_notify_wait(int timeout_ms);

/* 给 target 记一次通知。只限当前 OS 线程的 StThreadSchedule。
 *  0  已记下。target 正在 st_notify_wait 或 st_wait 中，则离开睡眠堆
 *     或 IO 队列，进入可运行队列。没在等则只留粘滞位，下次 wait 立即成功。
 *     多次 notify 合并成一次。
 * -1  target==NULL，或 target 不属于当前调度器，errno=EINVAL
 * 不调用 Unpend，不跨 OS 线程。
 */
int st_notify(sthread::StThread *target);

/* 当前协程同时等 fd 和通知。
 * want_read 非 0 等可读，0 等可写。
 * fd 必须已经在事件表里，否则 -2 且 errno=EINVAL（与 st_read 的 -2 相同）。
 *  >0  位掩码：ST_WAIT_FD 与 ST_WAIT_NOTIFY 可以同时置位
 *  -1  超时，errno=ETIME；没有协程，errno=EINVAL
 *  -2  没有事件项，errno=EINVAL
 *  -3  Schedule / Add 失败
 * 返回值含 ST_WAIT_FD 时，用一次非阻塞 recv/send 把字节取走。
 * 不要接着再调 st_recv / st_send：那会再进一次 Schedule。
 */
int st_wait(int fd, int want_read, int timeout_ms);
```

**和睡眠堆的配合。**

`st_notify_wait`：粘滞位已置则清掉并返回 0。否则 `StThread::Sleep(timeout_ms)` 写好 `m_wakeup_time_`，再走现有的 `StThreadSchedule::Sleep`（入睡眠堆并 Yield）。醒来后若 `m_notified_` 为 1，清掉并返回 0；否则 `-1` 且 `errno=ETIME`。

`st_notify`：先把 `m_notified_` 置 1。然后只做下面一件，并且先看标志再摘队列（`RemoveIOWait` / `RemoveSleep` 对不在队列上的线程不能调）：

- `HasFlag(eIO_LIST)`：`RemoveIOWait`（它内部会 `RemoveSleep`）然后 `InsertRunable`。
- 否则 `HasFlag(eSLEEP_LIST)`：`RemoveSleep` 然后 `InsertRunable`。
- 已经在跑、在可运行队列、或在 pend 队列：只留粘滞位。pend 队列是父子协程的，这里不 `Unpend`。
- `target` 就是当前协程：只留粘滞位，不把自己 `InsertRunable`。

直接移到可运行队列，而不是只把 `m_wakeup_time_` 改成 now。只改时间不 `HeapDelete` 会破坏堆序；改完再等 daemon 的 `epoll` 返回，通知会被埋到下一次超时。通知方下次 Yield 时，运行队列里已经有等待方。

**和 `Schedule` / fd 事件同时等。**

`st_wait` 自己调用 `StEventSchedule::Schedule`（单 item，和 `WaitFdReady` 相同）。`Schedule` 在 `recv_num==0` 时仍然 `errno=ETIME` 并返回 false。这条不改：`st_read` / `st_recv` / `st_accept` 不置通知位，超时语义保持。

`st_wait` 在 `Schedule` 返回之后看 `m_notified_`：

| `Schedule` | 通知位 | `st_wait` 返回 |
| --- | --- | --- |
| true（fd 有事件） | 0 | `ST_WAIT_FD` |
| true | 1（清掉） | `ST_WAIT_FD \| ST_WAIT_NOTIFY` |
| false，`errno==ETIME` | 1（清掉） | `ST_WAIT_NOTIFY`（不要把这次 `ETIME` 交给调用方） |
| false，`errno==ETIME` | 0 | `-1`，`errno=ETIME` |
| false，其他 errno | — | `-3`（Add/Schedule 失败）或 `-2`（没有事件项，在进 `Schedule` 之前就返回） |

进入 `st_wait` 时若粘滞位已经是 1：先清掉并记住，然后用超时 0 再 `Schedule` 一次。fd 上已经有事件就或上 `ST_WAIT_FD`，没有也不再睡。这样「先 notify、缓冲里又有字节」不会把字节丢到下一次循环。

协作式调度，没有第二个 OS 线程同时改队列。典型顺序：等待方在 `Schedule` 里 Yield 给 daemon；daemon `epoll` 之后才跑到通知方。fd 先就绪时，`Dispatch` 已经 `IOWaitToRunable` 并写好 `revents`，通知方只看到「不在 IO 队列」并置位，`st_wait` 两个位都返回。通知先到时，`RemoveIOWait` 把等待方放进运行队列，`revents` 仍是 0，`st_wait` 只返回 `ST_WAIT_NOTIFY`；socket 缓冲里随后到达的字节留到下一次 `st_wait`。

**单测。** 新文件 `tests/st_notify_unittest.cpp`，用 `stlib/st_test.h`，挂上 `tests/Makefile` 的 `all` 和 `run`，并写进根 `FORMAT_SRC`。两个协程用 `Frame::CreateThread` 或 `StSysSchedule::CreateThread`，主协程 `st_sleep` 让他们跑起来。

| 用例 | 断言 |
| --- | --- |
| A `st_notify_wait(500)`，B `st_notify(A)` | A 返回 0，耗时明显小于 500 ms |
| 没人通知，`st_notify_wait(50)` | `-1`，`errno==ETIME` |
| 先 `st_notify` 再 `st_notify_wait(1000)` | 立刻返回 0 |
| 连续两次 `st_notify`，然后两次 `st_notify_wait` | 第一次 0；第二次超时（粘滞只消费一次） |
| socketpair 已 `Add`，对端不写，只 `st_notify` | `st_wait` 返回 `ST_WAIT_NOTIFY`，不含 `ST_WAIT_FD` |
| 对端写入 1 字节并且 `st_notify` | 返回值两个位都在；非阻塞 `recv` 读到那一字节 |
| 只写入、不 `st_notify` | 返回 `ST_WAIT_FD`，不含 `ST_WAIT_NOTIFY` |
| `st_wait` 的 fd 没有 `Add` | `-2`，`errno==EINVAL` |
| `st_notify(NULL)` | `-1`，`errno==EINVAL` |
| 不涉及通知的 `st_read` 超时 | 仍是 `-1` / `ETIME`（回归） |

**文档（和原语一起改，不拖到聊天室）。**

- `src/st_sys.h` 上表就是注释。
- `readme.md` / `readme_en.md` 在「`st_*` 返回值」后加一小节「协程通知」：三个函数、同一 OS 线程、不占 fd、超时是 `-1` 且 `errno==ETIME`。
- `AGENTS.md` 推荐头文件表加一行：协程之间 wait/notify → `src/st_sys.h` 的 `st_notify` / `st_wait`。

**验收。** 上表单测在 `make -C tests run` 里通过；`st_read` 超时单测不回归；不新增 fd；`Pend` 的现有单测（`tests/st_scheduler_unittest.cpp`）仍然通过。

---

## 4. 六个样例

共同约定：源文件头 `Copyright (C) zhoulv2000@163.com`；注释中文；C++98（可以用 `__thread`、`__builtin_expect`、`__sync_fetch_and_add`，和 `st_dns` 一样）；`LOG_LEVEL(LLOG_CRIT)`，避免 `LOG_ERROR` 把 SUMMARY 打花；`signal(SIGPIPE, SIG_IGN)`；单协程走 primordial，多协程 `Frame::CreateThread`，主协程 `st_sleep(10)` 等到计数归零或截止时间。二进制用 `st_*` 这种显式名字，不再用 `main`。每个目录一份 `makefile`（照 `app/st_httpserver/makefile`：`include make.inc`，只链 `libmthread.a` 和 `$(ST_LDLIBS)`）和 `README.md`。

### 4.1 TCP echo（#1）

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

**冒烟 / 测试。** 脚本只供本地手动跑，不写进 `_build.yml`。CI 用 `make apps` / `make android` 编译这个目标。

- `scripts/smoke_echo.sh`，根目标 `make smoke-echo`（强制 `TRACE=0`，PID / trap 照 `smoke_httpclient.sh`）。端口默认 `17707`。
- 一次 `-c 1 -n 1`，stdout 的回显与 payload 一致，退出码 0。
- `-c 8 -n 40`，`ok=40 fail=0`。
- 连一个没人听的端口，`fail` 增加、退出码 1。
- `tests/` 里不必再单测 `CheckLengthCallback`（逻辑几十行）。若要锁住「没有 `\n` 返回 0」，放在 `tests/st_echo_unittest.cpp`，用 `stlib/st_test.h`，并挂上 `tests/Makefile` 的 `all` 和 `run`。
- Android：跟着 `make apps` 编译，不跑。

**文档。** `readme.md` / `readme_en.md` 的样例清单加上 echo；在「最小使用样例」后面加一节，命令是真实的 `st_echoclient` / `st_echoserver`，**保留**现有 DNS 那节。`AGENTS.md` 的 `make apps` 那一行在实现时补上名字。

**改动面与风险。** 只动 `app/st_echo`、根 makefile、`.gitignore`、脚本、readme。风险是 `eTCP_KEEPLIVE_CONN` 被读成「已经有连接池」——README 写清楚 `FreePtr` 仍关闭 fd。另一风险是粘包被 `ST_CONN_RESET_RECVBUF` 丢掉，冒烟每次只发一行。

### 4.2 端口扫描（#3）

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

工人数就是在飞连接数，不是「每个端口一个协程」。`-c 2000` 对 1–65535 就是两千个并发 `st_connect`，栈大约 500MB，还要 `ulimit -n` 大于 `-c`。README 写上这句。本地冒烟用 `-c 32`。

**数据结构。** 端口数组（启动时把 `-p` 展开到 `int *`，上限 65535 项，超出直接退出码 2）。四个 `volatile` 计数。没有共享连接。

**库缺口（本样例要补，否则分类是错的）。**

1. `StClientConnection::Create`：`Connect` 失败时先把 errno 存下来，`ReleaseItem` / `Close` 之后写回去。返回值仍是 `-2`（`ETIME` 也继续是 `-2`，因为今天 `Create` 对两种 `Connect` 失败都返回 `-2`）。不改 `Connect` 里「`ETIME` → `-1`，其他 → `-2`」这层，避免动已经依赖它的单测（`tests/st_coverage_extra_unittest.cpp` 附近有这条断言）。扫描器在 `Create` 返回后读 errno。
2. Phase 0 若证明「可写之后拒绝被当成 `EISCONN` 成功」：在 `st_connect` 里，`WaitFdReady` 返回之后、把连接当成成功之前，`getsockopt(SO_ERROR)`。非 0 则 `errno` 设成该值并返回 `-1`。这是行为修正，必须有 loopback 单测：监听端口成功、未监听端口 `ECONNREFUSED`、两种都不许是 `ETIME`。Phase 0 若已经能分开，这条就不改。

不新增扫描专用 API。

**冒烟 / 测试。** 冒烟脚本本地手动跑，不进 CI。errno 单测进 `make -C tests run`。

- `scripts/smoke_portscan.sh`，`make smoke-portscan`。先起 `st_echoserver`（或一个只 `accept` 的桩；用 echo 即可），再扫「这个端口 + 一个确定没人听的端口」。断言 open 里有 echo 的端口，refused ≥ 1，timeout 为 0，退出码 0。并发用 `-c 32`。
- loopback 上没人听是 `ECONNREFUSED`，不是超时。打到 `192.0.2.1`（TEST-NET-1）经常是 `ENETUNREACH`。样例把这类计进 other。黑洞超时写在 README：对一个丢弃 SYN 的地址加 `-t 200`，应看到 timeout。这条不进 CI。
- 单测 `tests/st_connect_errno_unittest.cpp`：loopback open 与 refused，锁住 G2 的修复。挂进 `tests/Makefile`，随现有 CI 跑。
- Android 只编译。

**文档。** `app/st_portscan/README.md` 说明四类计数、栈大小、`NOFILE`、为什么 CI 不测 `ETIME`。根 readme 样例清单加一行，并指回 `st_*` 返回值表（超时是 `-1`/`ETIME`）。

**改动面与风险。** `app/st_portscan` + `StClientConnection::Create` 的 errno 保存（几行）+ 可能的 `st_connect` SO_ERROR。风险：SO_ERROR 改动碰到「已经成功的连接被误判失败」。缓解：只在 Phase 0 复现了误判时才改；单测覆盖 open 和 refused；不动超时数值和 `st_*` 的返回码约定。本地冒烟固定 `-c 32`，避免栈和 `NOFILE`。

### 4.3 HTTP 反代 / 简单负载均衡（#5）

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

**库缺口。** 连接池仍然不在库里（G6，D8 已定）。本样例不改 `FreePtr`，不使用 `eTCP_KEEPLIVE_CONN` 当作池。槽位满时用 `st_sleep(1)` 轮询，不用 §3.2 的 `st_notify`（样例故意保持这么简单）。响应体上限是 `ST_SEND_BUFFSIZE`，因为 `CallBack` 只会 `SendData` 一次，不能从 `DoProcess` 里再对客户端 fd `st_send`（和监听协程共用一个 `StEventItem`，见 §2.1）。

**冒烟 / 测试。** 本地手动，不进 CI。CI 只编译。

- `scripts/smoke_httpproxy.sh`，`make smoke-proxy`。起两个 `st_httpserver`（不同端口），反代 `-b` 两个都配上。`curl` 经反代拿到 `hello from sthread`。杀掉其中一个 httpserver，等一个探活间隔，再 `curl`，仍是 200。两个都杀掉，再请求，状态是 502。
- 不把反代放进 `make bench`。可选的手工对比写在 README。
- Android 只编译（会一起编 `http_client.cc` / `http_parser.c`）。

**文档。** 根 readme 加一节：反代是样例，槽位不是 `StConnectionManager` 的真复用，限制写明 8192 和 IPv4。`app/st_httpproxy/README.md` 写编译依赖的那两个已有文件。

**改动面与风险。** 只在 `app/st_httpproxy` 和脚本。风险：`st_http_exchange` 的签名或 `StHttpConn` 以后若改了，反代要跟着改——它是编译期复用，不是把 HTTP 客户端链进库。探活和请求抢同一个后端时，靠「探活用自己的短连接」避开槽位。`DoInput` 只看头，不读 body；冒烟用 GET。带 body 的 POST 若超过第一次 `RecvData`，会 400，README 写明。

### 4.4 Redis 客户端（RESP）（#7）

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

**冒烟 / 测试。** 端到端脚本本地手动跑，不进 CI，也不在 CI 里安装 redis。不联网的 RESP 单测挂在 `tests/`，会跟着现有 `make -C tests run` 跑。

- `tests/st_redis_resp_unittest.cpp`：对固定字节串做 encode/decode（`PING` 的请求、`+PONG\r\n`、`$3\r\nbar\r\n`、`$-1\r\n`、`:1\r\n`、`-ERR x\r\n`）。拆包边界喂两次 `resp_parse`。挂进 `tests/Makefile`。
- `scripts/smoke_redis.sh`，`make smoke-redis`。优先如果 `redis-server` 在 `PATH` 里，就用 `--save "" --appendonly no --bind 127.0.0.1 --port $PORT --protected-mode no` 起一个，trap 杀掉。否则用 `python3 scripts/redis_stub.py`（只实现 PING/SET/GET/INCR，内存 dict）。然后 `st_redisclient ping`、`set k v`、`get k`、`incr n`，以及 `-c 4 -n 20 ping` 的 SUMMARY `ok=20`。
- stub 是脚本，不是运行时依赖。
- 可选压测：本机已有 redis 时，`st_redisclient -c 50 -n 5000 ping`。不新增 `make bench-redis`，避免 `make bench` 变长。README 给出命令即可。
- Android 只编译客户端，不跑 stub。

**文档。** 根 readme 把 Redis 和 Memcache 放在相邻的一节：都是协议样例，端到端依赖外部服务或 stub。`app/st_redisclient/README.md` 写 RESP 子集和「没有 hiredis」。

**改动面与风险。** `app/st_redisclient` + 一个 Python stub + 一个不联网的单测。风险是 RESP 解析在拆包边界上错（`$` 的长度和一个字节一个字节的 `st_recv`）。单测喂切片式的 `resp_parse` 两次调用，锁住「第一次只拿到类型字节」。stub 和真实 redis 的空白差异：冒烟只断言我们发出的那四条命令。

### 4.5 聊天室 / 广播（#9）

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

单房间，最多 64 人。多频道不做。

**核心流程。** 依赖 §3.2，不自己做 pipe。**不用 `StServer::Loop`。** `CallBack` 只会 `RecvData`，空闲连接收不到别人的消息。监听段用 `StServer` 的 `CreateSocket` + `Listen`（监听 fd 已经登记了事件项），然后自己的循环调用 `st_accept`。每个 connfd 分配自己的 `StEventItem` 并 `Add`，再 `CreateThread`。该协程记下 `StThread *self` 供别人 `st_notify`。`GetActiveThread()` 的静态类型是 `StThreadItem*`；`CreateThread` 放进去的是 `StThread`，记下时转成 `StThread*`。

连接协程独占自己的 socket，循环：

1. 把邮箱里已经有的行 `st_send` 到**自己的** fd。此时自己不在 `st_wait` 里，事件项归自己。
2. `rc = st_wait(fd, 1, idle_ms)`。
3. `rc == -1` 且 `errno==ETIME`：空闲超时，回到 1，不断开。
4. `rc & ST_WAIT_NOTIFY`：回到 1，把新到的邮箱行发出去。
5. `rc & ST_WAIT_FD`：fd 已经可读，用一次非阻塞 `recv` 把字节拼进行缓冲。凑满一行就广播。不要再套 `st_recv`。

广播只做两件事，并且在 Yield 之前做完：把一行拷进对方邮箱，然后 `st_notify(peer->self)`。不调用对方 socket 上的 `st_send`。对方若正停在 `st_wait`，会带着 `ST_WAIT_NOTIFY` 醒来；若正停在自己的 `st_send` 里，粘滞位让它下一次 `st_wait` 立刻返回。单 OS 线程，改邮箱和调用 `st_notify` 之间不 Yield，不用锁。

邮箱满时丢掉这一行，给发送者自己的 socket 回 `* dropped\n`。满员时 `st_accept` 出来的 fd 直接关掉，不建协程。

**数据结构。**

```text
Session （固定数组，最多 64 个）
  int used
  int fd
  StThread *self     # st_notify 的目标
  char nick[32]
  Mail mail          # 定长环：char text[8][512]，int head，int tail
```

没有 pipe，没有额外 fd。

**库缺口。** 由 Phase 2 补上。聊天室只调用 `st_notify` / `st_wait`，不改 `Schedule`。`stlib/tests/ucontext/channel.c` 仍不参与构建，也不抄进 `src/`。

**冒烟 / 测试。** 本地手动，不进 CI。叫醒行为的回归在 `tests/st_notify_unittest.cpp`。

- `scripts/smoke_chat.sh`，`make smoke-chat`。端口 `17700`。
- 起 server。起 client B：`-n bob`，只等一行，超时 3s（后台）。等 server 打出 `listening`。起 client A：`-n ada hello`。
- B 的 stdout 含 `ada: hello`。`* ada joined` 可能更早到达，B 要能读多行直到看到 hello 或超时。
- `* ada left` 不作为冒烟断言，避免和进程退出赛跑。
- Android 只编译。

**文档。** 样例 README 写明用的是 `st_notify` / `st_wait`，广播不写别人的 socket。根 readme 只给命令；原语本身的说明在 Phase 2 已经写进 `st_*` 返回值后面那一节。

**改动面与风险。** 样例只在 `app/st_chat`。风险在「`st_wait` 返回 `ST_WAIT_FD` 之后又调用了 `st_recv`」导致多等一轮，以及邮箱在 Yield 之后才写入、通知已经先被消费。约定：先入邮箱，再 `st_notify`；读侧先排空邮箱，再 `st_wait`。

### 4.6 未改过的阻塞代码 + syscall hook（#11）

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

**库缺口。** 修法、验收和单测在 §3.1（Phase 1）。本样例只消费修好的 hook，不再改 `sys_socket`。`sys_accept` 不改（N5）。不在 `libmthread.so` 里定义 `read`（N4）。

**冒烟 / 测试。** 库行为由 §3.1 的 `tests/st_hook_unittest.cpp` 在 CI 里锁住。样例冒烟本地手动跑，不进 `_build.yml`。

- `scripts/smoke_hook.sh`，`make smoke-hook`。起 `st_echoserver`（Phase 3）。先跑不链接库的 `blocking_client`，确认回显。再跑 `st_hookdemo -c 4 -n 4`，`ok=4`。
- Android 只编译 `st_hookdemo`。对照用的纯 libc 二进制也编出来即可，不在设备上跑。

**文档。** 根 readme 用单独一节讲 hook，因为这是和其他网络库不一样的地方。说明三件事：业务文件长什么样、`-include` 做了什么、为什么不覆盖全局 `read`。实现时在 `AGENTS.md` 的 hook 那句加上「样例见 `app/st_hookdemo`」。通知原语的头文件说明在 Phase 2 写。

**改动面与风险。** 样例本身只在 `app/st_hookdemo`。hook 行为改动的风险见 §7 的 R1、R2，在 Phase 1 收掉。默认 512 ms 太短，业务文件必须 `setsockopt`。README 和样例都这么做。

---

## 5. 分阶段执行

依赖：

```
Phase 0 实测（无功能代码）
    │
    ├─► Phase 1 hook 修复（§3.1，tests/ 进 CI）
    │         │
    │         └─► Phase 4 hookdemo（还要等 Phase 3 的 echo 当对端）
    │
    └─► Phase 2 通知原语 st_notify / st_wait（§3.2，tests/ 进 CI）
              │
              └─► Phase 8 聊天室

Phase 3 echo ──► Phase 5 端口扫描（errno 单测进 CI；本地冒烟用 echo）
Phase 6 Redis（RESP 单测进 CI；与 1/2 无代码依赖）
Phase 7 反代（池在样例里；依赖已有的 httpserver / httpclient）
```

每个阶段的出口都包含：现有 POSIX workflow + `format.yml` 全绿；`make clean` 后 `git status --porcelain --ignored` 为空。库阶段还要求 `make -C tests run` 里的新单测通过。样例阶段要求 `make apps` 编过，Android 的 `make android` 只编译。Linux `st_context_unittest` 的 64 KiB `makecontext` SIGABRT 仍按 plan/09 如实记录，本篇不改 `STACK`。

**六个 `make smoke-*` 都不写进 `_build.yml`。** 脚本留在仓库里，给人本地跑。CI 不新增冒烟步骤。

### Phase 0 · 实测锚点

不提交功能代码。把结果写进 §10。

1. loopback：`st_connect` 对正在 `listen` 的端口、以及对没人听的端口，分别记录返回值和 errno（Linux 与 macOS 各一次；macOS 用 CI 即可）。
2. `StClientConnection::Create` 失败返回之后，errno 还是不是 `ECONNREFUSED` / `ETIME`。
3. 对照 `tests/st_hook_unittest.cpp`：`sys_socket` 今天会把 `ST_FD_FLG_UNBLOCK` 置上。Phase 1 按这个事实改，不再当成可选项。
4. 确认 `FORMAT_SRC`、`.gitignore`、根 `clean` 要加的路径。

**出口**：§10 有一张表。G1/G2 与源码不符时，先改本文再写代码。

### Phase 1 · 前置：hook 修复

按 §3.1。只动 `app/st_sys.h`、`app/st_sys.cc` 和 `tests/st_hook_unittest.cpp`。不写 hookdemo。

**出口**：§3.1 的验收表全部满足；`make -C tests run` 通过；现有连接/服务端单测不回归。这是 Phase 4 的前置。

### Phase 2 · 前置：通知原语

按 §3.2。`src/st_poll.h`、`src/st_thread.h`、`src/st_thread.cc`、`src/st_sys.h`、`src/st_sys.cc`、`tests/st_notify_unittest.cpp`，以及 readme / `AGENTS.md` 里那一小节。不写聊天室。

**出口**：§3.2 的单测表通过；`st_read` 超时仍是 `ETIME`；`Pend` 单测仍过。这是 Phase 8 的前置。

### Phase 3 · echo

按 §4.1。根 `makefile` 的 `apps` / `clean` / `help` / `smoke-echo`，`FORMAT_SRC`，`.gitignore`，`scripts/smoke_echo.sh`。readme 两份在「最小使用样例」后**新增** echo 一节，DNS 那节留着。

**出口**：本地 `make smoke-echo` 的 `ok` 全过（不作为 CI 步骤）。CI 上 `make apps` 编过 `st_echoserver` / `st_echoclient`。

### Phase 4 · hookdemo

按 §4.6。依赖 Phase 1 的 hook 修复和 Phase 3 的 echo。不再改 `sys_socket`。

**出口**：本地 `make smoke-hook`：纯 libc 的 `blocking_client` 和 `st_hookdemo -c 4` 都能对 echo 回显。CI 只编译。`sys_read` 超时单测已在 Phase 1 进 CI。

### Phase 5 · 端口扫描

按 §4.2。errno 保存在 `StClientConnection::Create`（D3）。`SO_ERROR`（D4）仅当 Phase 0 证明有误判。

**出口**：`tests/st_connect_errno_unittest.cpp` 在 CI 里锁住 open 与 refused。本地 `make smoke-portscan` 用 `-c 32`。CI 不跑这个脚本，也不要求出现 `ETIME`。

### Phase 6 · Redis

按 §4.4。与 Phase 1–5 无代码依赖，可以紧跟 Phase 3。

**出口**：RESP 单测进 `make -C tests run`。本地 `make smoke-redis` 打四条命令。CI 不安装 redis，不跑 stub。

### Phase 7 · 反代

按 §4.3。槽位只在 `app/st_httpproxy`（D8）。依赖已有的 `st_httpserver` 和 `st_http_exchange`。

**出口**：本地 `make smoke-proxy`：双后端 200，停一个仍 200，都停则 502。CI 只编译。

### Phase 8 · 聊天室

按 §4.5。依赖 Phase 2。样例只调用 `st_notify` / `st_wait`。

**出口**：本地 `make smoke-chat` 里 B 收到 `ada: hello`。CI 不跑这个脚本；叫醒的回归已在 Phase 2 的单测里。

### 收尾

`plan/README.md` 把 11 标成完成（实现 PR 里做，不是本计划 PR）。`AGENTS.md` 的 `make apps` 行补上六个名字。不把 echo/redis 塞进默认 `make bench`（那个目标今天是 `bench-http` + `bench-dns`）。不把 `smoke-*` 塞进 `_build.yml`。

---

## 6. 已决定

2026-10-08 定稿。标「用户」的是这次拍板；标「推荐」的是沿用原稿建议、一并定下来。

| # | 结论 | 谁定的 |
| --- | --- | --- |
| D1 | readme **新增** echo 一节，**保留** DNS 示例 | 用户（与推荐一致） |
| D2 | echo 服务端用 `eTCP_KEEPLIVE_CONN` 把一行收齐；客户端 `tcp_sendrecv` 仍短连接。`FreePtr` 仍关 fd，README 写明这不是连接池 | 推荐 |
| D3 | `StClientConnection::Create` 失败路径保存 errno，返回值仍是 `-2` | 推荐 |
| D4 | `st_connect` 的 `SO_ERROR` 只在 Phase 0 证明「拒绝被当成成功」时才加 | 推荐 |
| D5 | 扫描默认 `-c 64`。本地冒烟 `-c 32`。`-c 2000` 只写在 README，不进 CI | 推荐 |
| D6 | 业务 `.c` 里是 POSIX 名字，makefile `-include` 换成 `sys_*`，另编一份纯 libc 二进制。不做 LD_PRELOAD，不在 `libmthread.so` 里导出 `read` | 推荐 |
| D7 | **必须**修 G1：`sys_socket` 不再把库自己的非阻塞标成 `ST_FD_FLG_UNBLOCK`；缺事件项时 hook 自己登记，`sys_close` 释放自己的项。`sys_accept` 不动。单独立成 Phase 1，带 §3.1 的验收和单测 | 用户 |
| D8 | 反代连接池只在 `app/st_httpproxy` 的槽位里，池满 `st_sleep(1)`。不改 `FreePtr` | 用户（与推荐一致） |
| D9 | 反代响应体超过 `ST_SEND_BUFFSIZE` 回 502，不改 8192 | 推荐 |
| D10 | Redis 本地冒烟：有 `redis-server` 就用它，否则 Python stub。CI 不安装 redis | 推荐 |
| D11 | 聊天室不用 pipe。库里新增 `st_notify` / `st_notify_wait` / `st_wait`（§3.2），同一 OS 线程、带超时、不占 fd，走睡眠堆和可运行队列。`Schedule` 在通知位未置时仍把「没有 IO 事件」报成 `ETIME`。这是 Phase 2，聊天室（Phase 8）只调用它 | 用户（改掉了原稿的 pipe 建议） |
| D12 | 六个样例冒烟**不进 CI**。保留 `scripts/smoke_*.sh` 和 `make smoke-*` 供本地跑。CI 只保证 `make apps`、Android 只编译，以及 `tests/` 里的库层单测（hook、通知原语、connect errno；RESP 单测同样挂在 `tests/`，不联网） | 用户（改掉了「六个冒烟都进 `_build.yml`」的建议） |
| D13 | 二进制用 `st_echoserver` 这类显式名字，写入 `.gitignore` 和 `make clean` | 推荐 |
| D14 | 本篇新的 `.h` / `.cc` / `.cpp` 和单测写进 `FORMAT_SRC`。旧的 `app/st_dns` 等仍然不在表里 | 推荐 |

---

## 7. 风险

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R1 | G1 的修法让「`sys_socket` 后立刻 `sys_read`」从 `EAGAIN` 变成挂起 | 中 | Phase 1 的单测改成断言 `ETIME`；用户自己 `fcntl(O_NONBLOCK)` 仍是 `EAGAIN`；`Create` 不走 `sys_read` |
| R2 | hook 登记的 `StEventItem` 和 `Create` 的项双重释放或泄漏 | 高 | 只在 `GetEventItem==NULL` 时登记；所有权标志；`sys_close` 只释放自己的 |
| R3 | D4 的 `SO_ERROR` 把成功连接判失败 | 高 | 默认不改；要改就必须先有失败复现和 open/refused 单测 |
| R4 | `eTCP_KEEPLIVE_CONN` 被当成池已经做好 | 中 | 文案写明 `FreePtr` 仍 `HashRemove`；echo 客户端用 `keeplive=false` |
| R5 | `st_notify` 在 IO 队列上 `RemoveIOWait` 时，对方已经被 `Dispatch` 移走，重复摘队列 | 高 | 先 `HasFlag(eIO_LIST)` / `HasFlag(eSLEEP_LIST)` 再摘；Phase 2 单测覆盖「fd 就绪和 notify 同一轮」 |
| R6 | `-c` 很大时栈和 `NOFILE` 在本地冒烟里打满 | 中 | 本地冒烟 ≤ 32；README 写 266KB 和 `ulimit`。CI 不跑这些脚本 |
| R7 | 反代和 `st_http_exchange` 编译期绑在一起 | 低 | makefile 显式列出 `http_client.cc`；不把 HTTP 客户端塞进库 |
| R8 | Python stub 和真实 RESP 有差别 | 低 | 单测覆盖解码；本地冒烟只打四条命令；本机 redis 可选 |
| R9 | 新二进制没进 `.gitignore` / `clean`，CI 最后一步失败 | 中 | D13；Phase 3 起就把路径写进清单 |
| R10 | `FORMAT_SRC` 漏了新文件，格式债留到以后 | 低 | D14 |
| R11 | 粘包、POST body、512 ms 默认超时被当成库 bug | 低 | 每个 README 的「限制」写明；hook 样例一定 `setsockopt` |
| R12 | `st_wait` 把 `Schedule` 的 `ETIME` 误判成通知，或反过来 | 高 | 只有 `m_notified_` 为 1 才返回 `ST_WAIT_NOTIFY`；不改 `Schedule` 本身的 `ETIME`；`st_read` 超时单测作回归 |

---

## 8. 验收清单

### A. 库前置（进 CI）

- [ ] Phase 1：`tests/st_hook_unittest.cpp` 覆盖 §3.1 的表（`ETIME` 挂起、用户 `O_NONBLOCK` 仍 `EAGAIN`、hook 自己登记的事件项在 `sys_close` 释放）
- [ ] Phase 2：`tests/st_notify_unittest.cpp` 覆盖 §3.2 的表（纯通知、超时、粘滞只消费一次、fd 与通知的三种组合、未登记 fd 返回 `-2`）
- [ ] `st_read` 超时仍是 `-1` / `ETIME`；`Pend` 单测仍过
- [ ] `Create` 失败后 errno 仍是 `ECONNREFUSED` 或 `ETIME`（`tests/st_connect_errno_unittest.cpp`）
- [ ] 上述单测在现有 `make -C tests run` 里，不给 `_build.yml` 加新 job

### B. 六个样例（CI 只编译）

- [ ] `make apps` 产出 `st_echoserver`、`st_echoclient`、`st_portscan`、`st_httpproxy`、`st_redisclient`、`st_chatserver`、`st_chatclient`、`st_hookdemo`、`blocking_client`
- [ ] 每个目录有 README：编译、运行、限制
- [ ] 根 makefile 有 `smoke-echo` / `smoke-portscan` / `smoke-hook` / `smoke-redis` / `smoke-proxy` / `smoke-chat`，对应脚本可在本地跑
- [ ] 这些 `smoke-*` **没有**写进 `_build.yml`
- [ ] `make android` 仍只编译，且新 makefile 能过 NDK（arm64-v8a 与 x86_64）

### C. 契约

- [ ] readme 有 echo 一节，DNS 示例还在
- [ ] echo 客户端用 `tcp_sendrecv`，超时按 `app/st_c.h` 的 `-3` 解释
- [ ] 端口扫描把 `ETIME` 和 `ECONNREFUSED` 分进不同计数
- [ ] hook 业务文件源码中不出现 `st_` / `St`；`-include` 只出现在 makefile
- [ ] 反代不调用 `eTCP_KEEPLIVE_CONN` 来表示池；槽位在样例内
- [ ] 聊天室用 `st_notify` / `st_wait`，广播不在别的协程的 socket 上调用 `st_send`，不建 pipe

### D. 工程

- [ ] C++98，无第三方运行时；`ldd` / `otool -L` 只有系统库和 `libmthread`
- [ ] 新源文件在 `FORMAT_SRC` 里，`clang-format-18` 通过
- [ ] `make clean` 后 `git status --porcelain --ignored` 为空
- [ ] 未新增根 `LICENSE`；未提交 `.session_tmps/`
- [ ] `STACK`、`MEM_PAGE_SIZE`、已有枚举值、`st_*` 既有签名未改
- [ ] `readme.md` / `readme_en.md` / `AGENTS.md` 写了 `st_notify` / `st_wait` 的入口（`src/st_sys.h`）

---

## 9. 建议目录落点（实现时）

```
# Phase 1
app/st_sys.h app/st_sys.cc       # UNBLOCK + hook 自己登记事件项
tests/st_hook_unittest.cpp       # 增补，不新建

# Phase 2
src/st_poll.h                    # StThreadItem::m_notified_
src/st_thread.h src/st_thread.cc # StThreadSchedule::Notify
src/st_sys.h src/st_sys.cc       # st_notify_wait / st_notify / st_wait
tests/st_notify_unittest.cpp
readme.md readme_en.md           # 「协程通知」小节；echo 一节（Phase 3）保留 DNS
AGENTS.md                        # 推荐头文件加 st_notify / st_wait

# Phase 3–8 样例
app/st_echo/                     # st_echoserver, st_echoclient
app/st_portscan/                 # st_portscan
app/st_httpproxy/                # 槽位在样例内
app/st_redisclient/              # resp.*
app/st_chat/                     # 调用 st_notify / st_wait
app/st_hookdemo/                 # blocking_client.c, st_posix_alias.h
scripts/smoke_*.sh               # 本地；不进 _build.yml
scripts/redis_stub.py
tests/st_redis_resp_unittest.cpp
tests/st_connect_errno_unittest.cpp
# src/st_connection.h            Create 失败路径保存 errno（D3）
# src/st_sys.cc                  仅当 D4 触发时，st_connect 查 SO_ERROR
makefile                         apps / clean / help / FORMAT_SRC / smoke-*
.gitignore
```

`_build.yml` 不改冒烟步骤。它已经会 `make apps` 和 `make -C tests run`。

---

## 10. 落地记录（实现时填写）

> 本文件这次只改定稿，没有功能代码。Phase 0 的实测表、每个 Phase 的提交号写在这里。样例冒烟是本地结果，不作为 CI 绿灯。

| Phase | 提交 | 结果 |
| --- | --- | --- |
| 0 实测 |  |  |
| 1 hook 修复 |  |  |
| 2 通知原语 |  |  |
| 3 echo |  |  |
| 4 hookdemo |  |  |
| 5 portscan |  |  |
| 6 redis |  |  |
| 7 proxy |  |  |
| 8 chat |  |  |

---

## 11. 与需求的对照

| 需求 | 落点 |
| --- | --- |
| 1. TCP echo，readme 入口，最简单的 bench 基线 | §4.1，Phase 3。新增 echo 一节，保留 DNS。基线是样例自己的 SUMMARY，不并进 `make bench` |
| 3. 端口扫描，并发 `st_connect`，超时 vs 拒绝 | §4.2，Phase 5。errno 单测进 CI；冒烟本地跑 |
| 5. HTTP 反代，池、超时、探活 | §4.3，Phase 7。池只在样例槽位里 |
| 7. Redis RESP + benchmark 模式 | §4.4，Phase 6。无 hiredis；redis-server 可选 |
| 9. 聊天室广播，协程之间叫醒 | §4.5，Phase 8。前置是 Phase 2 的 `st_notify` / `st_wait` |
| 11. 不改的阻塞代码经 hook 跑在协程里 | §4.6，Phase 4。前置是 Phase 1 的 hook 修复 |
| 冒烟不进 CI | D12，§5、§6、§8 |
| 只写计划、不改功能代码 | 本文 |
