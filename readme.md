**中文** | [English](readme_en.md)

sthread
---

[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-22.04.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-22.04.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04-arm.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04-arm.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-14.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-14.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15-intel.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15-intel.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-26.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-26.yml)
[![build](https://github.com/linkxzhou/sthread/actions/workflows/build-android.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-android.yml)
[![format](https://github.com/linkxzhou/sthread/actions/workflows/format.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/format.yml)

# 简介

[sthread](https://github.com/linkxzhou/sthread) 是一个基于协程的高性能网络库，用 **C++98** 写成，提供非阻塞的 TCP/UDP 客户端与服务端能力。

最终交付物是一个库：`libmthread.a` / `libmthread.so`。使用方只需链接它。

# 特性

1. 不用依赖任何第三方运行时库
2. 多平台协程调度（ucontext + `asm.S`：386 / amd64 / mips / power / **arm64**）。Android（arm64-v8a、x86_64）使用 vendored libtask asm（ELF 符号）+ epoll
3. 支持 epoll（Linux / Android）与 kqueue（macOS / OpenBSD）
4. 不用写异步调度代码：业务全部同步写法，框架内部异步处理
5. 提供非阻塞 TCP / UDP 客户端（短连接与 TCP keepalive 复用）
6. 跨平台；在内存与句柄足够时可以创建大量协程（见下方「性能」）
7. 使用简单，只需链接一个 `libmthread.a` 或 `libmthread.so`

示例应用：`app/st_dns`、`app/st_memcacheclient`、`app/st_wrk`、`app/st_httpserver`、`app/st_dnsserver`、`app/st_httpclient`、`app/st_echo`、`app/st_portscan`、`app/st_httpproxy`、`app/st_redisclient`、`app/st_chat`、`app/st_hookdemo`。

# 环境要求

| 项 | 内容 |
| --- | --- |
| 语言标准 | C++98（`-std=c++98`） |
| Linux | g++；epoll 后端；glibc `get/make/swapcontext` |
| macOS | clang++；kqueue 后端；**Apple Silicon 已支持真实 ucontext** |
| Android | NDK clang；API 21；**arm64-v8a 与 x86_64**；epoll + vendored libtask asm。只交叉编译，不在模拟器上跑测试。armeabi-v7a / x86 未支持 |
| 运行时依赖 | 无（仅系统库：libc / libstdc++ 或 libc++ / libpthread / libdl） |
| 可选开发期依赖 | gperftools（tcmalloc / profiler），默认关闭；见 [`thirdparty/readme.md`](thirdparty/readme.md) |

# 编译状态

每次 push / PR 由 [GitHub Actions](https://github.com/linkxzhou/sthread/actions) 在下列平台上编译 stlib、libmthread、apps 与全部测试，并运行 `stlib/tests`；`tests/` 的 unittest 目前为非阻塞项。每个平台一个 workflow（`.github/workflows/build-*.yml`，POSIX 平台共享 `_build.yml`），另有 `format.yml` 做 clang-format-18 格式检查。Android 用单独的 `build-android.yml`，只交叉编译。

| 平台 | 架构 | 编译器 | 状态 |
| --- | --- | --- | --- |
| Ubuntu 22.04 | x86_64 | g++ / clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-22.04.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-22.04.yml) |
| Ubuntu 24.04 | x86_64 | g++ / clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04.yml) |
| Ubuntu 24.04 | arm64 (aarch64) | g++ / clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04-arm.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-ubuntu-24.04-arm.yml) |
| macOS 14 | arm64 | Apple clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-14.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-14.yml) |
| macOS 15 | arm64 | Apple clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15.yml) |
| macOS 15 | x86_64 (Intel) | Apple clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15-intel.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-15-intel.yml) |
| macOS 26 | arm64 | Apple clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-26.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-macos-26.yml) |
| Android | arm64-v8a、x86_64（NDK，API 21，只编译） | NDK clang++ | [![build](https://github.com/linkxzhou/sthread/actions/workflows/build-android.yml/badge.svg?branch=master)](https://github.com/linkxzhou/sthread/actions/workflows/build-android.yml) |

## Apple Silicon（arm64）

- 实现：`stlib/ucontext/ucontext-arm64.h` + `asm.S`（`NEEDARM64CONTEXT`）
- 符号：`makecontext` / `swapcontext` 重命名为 `libthread_*`，**避开** Apple `libsystem` 同名函数（布局不兼容，会 SIGSEGV）
- 栈：`makecontext` 保持 SP 16 字节对齐；`InitContext` 使用 `ss_sp + 16`
- 冒烟与单测记录见 [`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)

# 验证状态（plan/08 起：HTTP/DNS 压测闭环）

| 项 | 状态 |
| --- | --- |
| `make lib` / `make -C tests run` / `make -C stlib/tests run` | 三件套门禁 |
| `make apps` | dns / memcache / wrk / httpserver / dnsserver / httpclient / echo / portscan / hookdemo / redis / httpproxy / chat |
| `make bench-http` | 起 `st_httpserver`，`st_wrk` 矩阵，报告 `reports/http-*.md` |
| `make bench-dns` | 起 `st_dnsserver` `:5353`，`st_dns` 协程压测后**退出** |
| 冻结基线 | [`reports/baseline-http.md`](reports/baseline-http.md) / [`reports/baseline-dns.md`](reports/baseline-dns.md)（标 OS/arch/commit） |
| 历史 macOS 冒烟（2026-09-15） | `st_wrk -n 3 -c 3 -d 2s` → 约 832 req/s（连接极少，**不能**当性能结论） |
| L4 keepalive | **已修**：`eTCP_KEEPLIVE_CONN = 0x11`，`Keeplive()` = `IS_KEEPLIVE(m_type_)` |

旧 `st_dns` 默认 `www.2000–2149.com` 合成域名 + `Frame::Loop(true)` 挂死已修：请用 `-s 127.0.0.1 -p 5353` 打本地权威。

# 快速开始

## stlib

基础组件说明、线程模型约定、裸宏清单与最小可运行示例见 [`stlib/README.md`](stlib/README.md)。

## 编译

在仓库根目录：

```bash
make lib                 # 产出 libmthread.a 与 libmthread.so（仓库根）
make apps                # dns / memcache / wrk / httpserver / dnsserver / httpclient / echo / portscan / hook / redis / proxy / chat
make tests               # 编译 tests/ 下的 unittest
make -C tests run        # 运行核心单测（含 keepalive）
make bench-http          # HTTP 压测闭环（默认 BENCH_PROFILE=smoke）
make bench-dns           # DNS 压测闭环（本地 5353，不依赖公网）
make bench               # bench-http + bench-dns
make help                # 目标与开关一览
make -C tests coverage TRACE=0 COVERAGE=1 \
  LLVM_PROFDATA=/opt/homebrew/opt/llvm/bin/llvm-profdata \
  LLVM_COV=/opt/homebrew/opt/llvm/bin/llvm-cov
                         # 行覆盖率门禁（默认阈值80%；需 Homebrew llvm）
make clean
make help                # 目标与开关一览
```

可选开关（见 `make.inc`，默认：`TRACE=1` `DEBUG=1` `ASAN=0` `TCMALLOC=0` `PROFILER=0`）：

```bash
make lib TRACE=0         # 关闭编译期 -DTRACE（日志级别仍可能打 PVERB）
make lib ASAN=1          # AddressSanitizer（与协程栈切换配合较差，默认关）
make lib TCMALLOC=1      # 需自行安装 gperftools
```

验证零第三方运行时依赖：

```bash
# Linux
ldd libmthread.so
# macOS
otool -L libmthread.so
```

## HTTP server 样例

```bash
make lib && make -C app/st_httpserver
./app/st_httpserver/main          # 监听 0.0.0.0:8765
curl http://127.0.0.1:8765/
# 一键压测（自动起停 server，写 reports/http-*.md）
make bench-http
# 或手动：./app/st_wrk/wrk -n 2 -c 10 -d 3s --json http://127.0.0.1:8765/
```

详见 [`app/st_httpserver/README.md`](app/st_httpserver/README.md)。

## HTTP client 样例

```bash
make apps
./app/st_httpserver/main 18765 &
./app/st_httpclient/st_httpclient http://127.0.0.1:18765/
# 或一键冒烟（起停 server，检查退出码与响应体）
make smoke-httpclient
```

详见 [`app/st_httpclient/README.md`](app/st_httpclient/README.md)。

## Android 交叉编译

需要 NDK（CI 使用 ubuntu-24.04 预装的 NDK，`ANDROID_NDK_HOME`）。只编译，不运行：

```bash
export ANDROID_NDK_HOME=/path/to/ndk   # 或 ANDROID_NDK
make android ABI=arm64-v8a API=21
make android ABI=x86_64 API=21
```

产物是 Android ELF：`libmthread.a` / `libmthread.so`、`app/*/main`（httpclient 为 `st_httpclient`）、`tests/` 与 `stlib/tests/` 的测试二进制。`libmthread.so` 的 `NEEDED` 只有 `libc.so`、`libm.so`、`libdl.so`（C++ 运行时静态链入，不带 `libc++_shared.so`）。

## DNS server / client 样例

```bash
make lib && make -C app/st_dnsserver && make -C app/st_dns
./app/st_dnsserver/main 127.0.0.1 5353
# 另开终端
./app/st_dns/main -s 127.0.0.1 -p 5353 www.1.bench.local
make bench-dns
```

详见 [`app/st_dnsserver/README.md`](app/st_dnsserver/README.md)、[`app/st_dns/README.md`](app/st_dns/README.md)。

## 最小使用样例

```cpp
#include "app/st_c.h"
#include "app/st_frame.h"

void worker(void *arg) {
  (void)arg;
  /* 在协程里调用 udp_sendrecv / tcp_sendrecv 等同步 API */
}

int main() {
  st_init_frame();      /* 拉起事件调度 + StSysSchedule */
  st_set_hook_flag();   /* 启用 syscall hook（可选，视场景） */

  Frame::CreateThread(worker, NULL);
  Frame::Loop(true);    /* 进入 daemon 事件循环（默认不返回） */
  return 0;
}
```

完整可编译示例见 `app/st_echo/`（下面这一节）、`app/st_dns/main.cpp`（DNS 客户端，查询完退出）、`app/st_dnsserver/main.cpp`（UDP 权威样例）与 `tests/st_server_unittest.cpp`（Listen 路径）。

## TCP echo

最短的一行回显。DNS 示例仍在后面，这一节只是多一个更短的入口。

```bash
make lib && make -C app/st_echo
./app/st_echo/st_echoserver                 # 0.0.0.0:7707
./app/st_echo/st_echoclient 127.0.0.1 7707
./app/st_echo/st_echoclient -c 8 -n 40 -s ping 127.0.0.1 7707
```

`-c 1 -n 1` 先打回显，再打 `SUMMARY ok=.. fail=.. qps=.. elapsed_ms=..`。客户端每个请求一次 `tcp_sendrecv`，短连接（`keeplive=false`）。接收超时是 `app/st_c.h` 里的 **-3**（`errno=ETIME`），不要和 `st_recv` 的 `-1` 混用。

服务端用 `eTCP_KEEPLIVE_CONN` 只为了把拆开的一行收齐。`FreePtr` 仍然关掉 fd，这不是连接池。一次只保证一行，`\n` 后面的粘包会被清掉。详见 [`app/st_echo/README.md`](app/st_echo/README.md)。

## 对外头文件（推荐）

使用方链接 `libmthread.a` / `.so` 后，按场景 include：

| 场景 | Include |
| --- | --- |
| 协程框架最小入口 | `#include "app/st_c.h"` + `#include "app/st_frame.h"` |
| TCP/UDP `StServer` 服务 | `#include "src/st_server.h"` |
| 客户端连接 / 连接池 | `#include "src/st_connection.h"` |
| 带超时的 `st_read` / `st_write` / `st_accept` / … | `#include "src/st_sys.h"` |
| 同一 OS 线程上的协程通知 `st_notify` / `st_wait` | `#include "src/st_sys.h"` |
| 连接类型枚举、`ST_CONN_RESET_RECVBUF` 等 | `#include "src/st_public.h"` |

说明：`app/st_c.*` / `app/st_sys.*` 已编进库，但 **hook 头 `app/st_sys.h` 非稳定对外 API**；业务优先走 `st_c` / `st_frame` / `src/st_*.h`。`stlib/` 多为被上述头间接包含的基础设施。


# 核心概念

## 协程模型（每 OS 线程 1:N）

- `Instance<T>()` 是**线程局部**单例（`stlib/st_singleton.h`）。
- 每个 OS 线程各有一套 `StThreadSchedule` / `StEventSchedule` / `StSysSchedule`。
- **协程不能跨 OS 线程迁移。**
- 角色：primordial（主）/ daemon（事件循环）/ 普通业务协程。

## 「同步写法、内部异步」

业务调 `RecvData()` / `udp_sendrecv()` 看起来像阻塞；内部在数据未就绪时：

1. 注册 fd 事件（`StEventSchedule::Schedule`）
2. `Yield` 让出当前协程
3. daemon 在 `epoll_wait` / `kevent` 上等待
4. 就绪后 `IOWaitToRunable` 唤醒，从原调用点继续

**不要**引入回调式 / future 式对外 API——业务层看不到异步是设计目标。

## 双后端

`src/st_poll.h` 编译期选择 `stlib/st_epoll.h` 或 `stlib/st_kqueue.h`。两边同名 `StIOState`，公开接口必须一致；业务代码无感。

## 连接类型（`eConnType`）

规则：末位 `0x1` 表示需要保存 / 复用状态（`IS_KEEPLIVE`）。

| 枚举 | 值 | 含义 |
| --- | --- | --- |
| `eUNDEF_CONN` | `0x0` | 未定义 / 错误 |
| `eUDP_CONN` | `0x10` | UDP |
| `eTCP_CONN` | `0x20` | TCP 短连接（兼容宏 `eTCP_SHORT_CONN`） |
| `eTCP_KEEPLIVE_CONN` | `0x11` | TCP keepalive（连接池按地址 hash 复用） |
| `eUDP_UDPSESSION_CONN` | `0x21` | UDP session |

`StConnection::Keeplive()` 返回 `IS_KEEPLIVE(m_type_)`。服务端 `do { ... } while (conn->Keeplive())` 仅在 keepalive 类型上循环。

# 示例

## DNS 客户端

完整代码：`app/st_dns/`。协议解析在 `dns.cpp`；调度入口在 `main.cpp`。

要点：

```cpp
st_init_frame();
st_set_hook_flag();
Frame::CreateThread(func, s);   /* s 为域名字符串 */
Frame::Loop(true);              /* 不返回；联调请 Ctrl-C / kill */
```

`dns_lookup` 内部通过 `udp_sendrecv` 发查询。**请改成真实可解析域名**再联调；仓库默认循环 `www.2000.com` … `www.2149.com` 为合成名，通常无 A 记录，会超时失败（用于压调度路径，不是正确性证明）。

```bash
make -C app/st_dns
./app/st_dns/main          # Frame::Loop(true) 需手动结束
```

## Memcache / wrk

```bash
# memcache：先启动本机 memcached（默认 127.0.0.1:11211）
make -C app/st_memcacheclient
./app/st_memcacheclient/main

# wrk：先起一个 HTTP 服务，再压测
make -C app/st_wrk
./app/st_wrk/wrk -n 3 -c 3 -d 2s http://127.0.0.1:8765/
```

`st_wrk` 和 `st_memcacheclient` 直接使用 `StExecClientConnection`（`StClientConnection` + `st_send` / `st_recv`），写法和 `app/st_httpclient` 一样：短连接上发请求，读到协议报文结束。

## Redis

和 Memcache 一样，协议在样例里，不进库。`app/st_redisclient` 自己解析 RESP 子集，不链接 hiredis。端到端需要本机 `redis-server`，或者冒烟脚本里的 `scripts/redis_stub.py`。

```bash
make -C app/st_redisclient
./app/st_redisclient -h 127.0.0.1 -p 6379 ping
./app/st_redisclient -c 4 -n 20 ping
```

`SUMMARY` 里带 `p50_ms` / `p99_ms`。详见 [`app/st_redisclient/README.md`](app/st_redisclient/README.md)。

## HTTP 反代

`app/st_httpproxy` 前面是 `StServer`，后面用 `st_http_exchange` 转发。每个后端的槽位只活在这个样例里，不是 `StConnectionManager` 的真复用。请求头和正文受 8192 字节缓冲限制，只接受 IPv4 字面量。详见 [`app/st_httpproxy/README.md`](app/st_httpproxy/README.md)。

## 端口扫描 / 聊天室

```bash
./app/st_portscan/st_portscan -c 32 -t 300 -p 80,443 127.0.0.1
./app/st_chat/st_chatserver
./app/st_chat/st_chatclient -n ada 127.0.0.1 7700 hello
```

扫描把 `ETIME` 和 `ECONNREFUSED` 分成 timeout 和 refused。`st_connect` 超时仍是 `-1` 且 `errno=ETIME`。聊天室用下一节的 `st_notify` / `st_wait` 广播，不在别人的 socket 上 `st_send`。

## TCP / UDP 客户端 API

头文件：`app/st_c.h`（已编进 `libmthread`）。

```cpp
int udp_sendrecv(struct sockaddr_in *dst, void *pkg, int len,
                 void *recvbuf, int &bufsize, int timeout);

int tcp_sendrecv(struct sockaddr_in *dst, void *pkg, int len,
                 void *recvbuf, int &bufsize, int timeout,
                 CheckLengthCallback callback, bool keeplive = false);
```

`keeplive == true` 时走 `eTCP_KEEPLIVE_CONN` 连接池路径。

## HTTP 服务端（Listen）

服务端模板：`StServer<ConnectionT, ServerT>`（`src/st_server.h`）。连接回调请覆盖 `DoInput` / `DoOutput` / `DoProcess` / `DoError`（不是旧的 `Handle*`）。

可编译的 Listen 路径示例：`tests/st_server_unittest.cpp`（创建 socket + `Listen`；完整 `Loop` 会进入 daemon 事件循环）。

```bash
make -C tests server
./tests/st_server_unittest
```

# 架构

```
        业务协程 (同步写法)
              |  RecvData / SendData / udp_sendrecv
              v
      StConnection / StServer / st_c API
              |  数据未就绪 → st_* / WaitFdReady → 注册事件 + Yield
              v
      StEventSchedule ── StEventItem(fd) ──┐
              ^                             |
              |  IOWaitToRunable            |  AddEvent
              |                             v
      StThreadSchedule <── daemon ──── StIOState (epoll / kqueue)
        run / io / pend / sleep               ^
              |                               |  Poll
              └── sleep 最小堆 ──────── StHeapTimer
```

`src/st_sys.cc` 内 8 个 `st_*` 共用内部 `WaitFdReady`（plan/07 C1）。fd 事件表容量取 `min(rlim_cur, 65535)`（D4）。keepalive 连接 **当前不在 hash 中真复用**（D1，见 `FreePtr` 注释）。

## `st_*` 返回值

`st_read` / `st_write` / `st_recv` / `st_send` / `st_recvfrom` / `st_sendto` / `st_connect` / `st_accept`（`src/st_sys.h`）：

| 返回值 | 含义 |
| --- | --- |
| `>0` | 成功字节数；`st_accept` 为新 connfd。`st_connect` 成功多为 `0` |
| `0` | 对端关闭，或历史的 `n==0` |
| `-1` | 硬错误，或超时（`errno == ETIME`）。等到唤醒但没有 IO 事件也是超时 |
| `-2` | 该 fd 没有事件 item（`errno == EINVAL`） |
| `-3` | `Schedule` / `Add` 失败。**不是超时** |

`app/st_sys.cc` 里 hook 出去的 `sys_read` / `sys_recv` / `sys_send` / `sys_write` / `sys_connect` 对调用方保持 libc 形状：失败一律 `-1`，并且 `errno` 已设置。超时仍是 `errno == ETIME`。

`udp_sendrecv` / `tcp_sendrecv` 用另一套状态码（接收超时在 TCP 上是 `-3`），见 `app/st_c.h`。不要和 `st_*` 的 `-3`（调度失败）混用。

## 协程通知

同一 OS 线程上叫醒另一个协程，不占 fd，也不建 pipe。声明在 `src/st_sys.h`：`st_notify`、`st_notify_wait`、`st_wait`。

| 调用 | 返回 |
| --- | --- |
| `st_notify(target)` | `0` 已记下。`target` 正在等，就离开睡眠堆或 IO 队列，进入可运行队列。没在等则只留粘滞位，下次 wait 立刻成功。多次 notify 合并成一次。`target` 为空或不属于当前调度器：`-1`，`errno=EINVAL` |
| `st_notify_wait(ms)` | `0` 被叫醒（含调用前已经记下的粘滞）。超时 `-1` 且 `errno=ETIME` |
| `st_wait(fd, want_read, ms)` | `>0` 是位掩码：`ST_WAIT_FD`（0x1）和 `ST_WAIT_NOTIFY`（0x2）可以同时置上。超时 `-1`/`ETIME`。fd 没有事件项 `-2`/`EINVAL`。调度失败 `-3` |

`st_wait` 返回值含 `ST_WAIT_FD` 时，用一次非阻塞 `recv` / `send` 把字节取走，不要接着再调 `st_recv` / `st_send`。进入时若粘滞位已经是 1，先清掉，再用 `poll(timeout=0)` 看 fd 是否已经有事件。不走 `Schedule(超时 0)`：`StEventSchedule::Wait(0)` 会 `Poll(NULL)`，那是一直阻塞。

聊天室样例：[`app/st_chat/README.md`](app/st_chat/README.md)。

## syscall hook

业务文件可以按阻塞 POSIX 来写（`socket` / `connect` / `read` / `write` / `close` / `setsockopt`）。协程目标在编译时 `-include app/st_hookdemo/st_posix_alias.h`，把这些名字换成 `sys_*`。不在 `libmthread.so` 里定义全局 `read`，也不做 `LD_PRELOAD`。同一份 `blocking_client.c` 不加 `-include`、不链库，就是普通阻塞客户端。

```bash
make -C app/st_hookdemo
./app/st_hookdemo/blocking_client 127.0.0.1 7707 hook
./app/st_hookdemo/st_hookdemo -c 4 -n 4 127.0.0.1 7707
```

`sys_socket` 会把内核 fd 设成非阻塞，但不再打上 `ST_FD_FLG_UNBLOCK`。这样 hook 打开时 `sys_read` 会进 `st_read` 并 Yield。用户自己的 `fcntl(O_NONBLOCK)` 仍然是立刻 `EAGAIN`。超时用 `setsockopt(SO_RCVTIMEO / SO_SNDTIMEO)`，不要依赖默认的 512 ms。详见 [`app/st_hookdemo/README.md`](app/st_hookdemo/README.md)。

# 性能

单协程栈分配（实现公式，`StThread::InitStack`）：

```
MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE
```

当前常量：`STACK = 131072`（128KB），`MEM_PAGE_SIZE = 2048` → 约 **137216 字节 / 协程**。

| 指标 | 状态 |
| --- | --- |
| 单协程栈占用 | 上式（静态可算） |
| 高并发创建上限 | 受内存与 `RLIMIT_NOFILE` 限制；事件表容量为 `min(rlim_cur, 65535)`，`setrlimit` 失败仅告警（plan/07 D4） |
| arm64 协程切换 | 已通（`libthread_makecontext` + asm） |
| arm64 app 冒烟 | wrk / memcache 通过；DNS 请走本地 `st_dnsserver`，见 `make bench-dns` |
| HTTP / DNS QPS | 短连接曲线见下方「性能曲线」（本轮提交 `a67127f`）。[`reports/baseline-curve.md`](reports/baseline-curve.md) 仍是改前冻结曲线（`e62c158`）。冒烟点仍是 `reports/baseline-http.md` / `baseline-dns.md`。短连接不可当 keepalive 结论 |

## 性能曲线

loopback 上 `st_httpclient` 对 `st_httpserver` 的**短连接**曲线（服务端 `Connection: close`）。并发 1、10、50、100、200、500、1000，每个点 50000 次请求、重复 3 次，图上是中位数。单 OS 线程事件循环。测量环境：Linux 6.12.94+、x86_64、4 vCPU KVM、g++ 13.3.0、提交 `a67127f`、`TRACE=0`。同一台机器上 QPS 大约在 ±15% 噪声内（相对改前同机中位数大约 −10% 到 +23%）。DNS 仍须 `-n == -c`，墙钟大约 1–16 ms 的一次性突发，没有画进曲线。

同一轮的硬结果：空闲 `st_dnsserver` 的 CPU 从大约 100% 降到 0；5 万次短连接后服务端 VmSize 大约增加 5 MB（改前是 GB 量级）；默认协程栈是 128KB（`STACK = 131072`）。

![HTTP 吞吐与并发](docs/perf/http-qps-vs-concurrency.svg)

![HTTP 延迟与并发](docs/perf/http-latency-vs-concurrency.svg)

| 并发 | QPS（req/s） | p50（ms） | p99（ms） | 错误 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 18463.81 | 0 | 1 | 0 |
| 10 | 32051.28 | 0 | 1 | 0 |
| 50 | 34364.26 | 1 | 2 | 0 |
| 100 | 33875.34 | 3 | 4 | 0 |
| 200 | 28835.06 | 6 | 9 | 0 |
| 500 | 29274.00 | 13 | 20 | 0 |
| 1000 | 27808.68 | 23 | 46 | 0 |

每个点 50000 次请求、3 次重复取中位数；客户端计时精度 1 ms，p50 为 0 表示低于 1 ms。

本轮三次重复与对照见 [`reports/perf-p1-p3-20261008.md`](reports/perf-p1-p3-20261008.md)。[`reports/baseline-curve.md`](reports/baseline-curve.md) 的表和 CSV 仍是改前冻结曲线（提交 `e62c158`）；它嵌入同一份 `docs/perf/*.svg`，图会显示本轮曲线。重新生成（时间戳结果在 `reports/curve-*`，不入库）：

```bash
make bench-curve
```

# 已知限制

- **协程对象回收**：`StThread` 池回收仍有 `TODO`，长时间大量创建需关注内存。
- **keepalive 真复用**：`FreePtr` 仍 `HashRemove` 再入池；仅保证 fd/`Keeplive` 语义诚实（plan/07 D1），完整池复用另立项。
- **`app/st_c.h`**：在 `extern "C"` 块里使用了 C++ 引用，**不能**被纯 C 编译器直接 include。
- **单进程内协程不可跨 OS 线程**（由 `Instance<T>()` 线程局部语义决定）。
- **`Frame::Loop(true)`**：进入 daemon 后默认不返回。`st_dns` 压测已改为 `st_sleep` 等待 worker 后退出，不再依赖 Loop。
- **同 fd 多 action**：memcache 示例可能打出 `item conflict` 告警，属示例用法问题，不是 ucontext 回归。
- `app/st_c.*` / `app/st_sys.*` 语义上是库代码，物理路径仍在 `app/`（未搬迁）；`StServer` 已不再 include `app/st_c.h`（plan/07 C4）。
- **LICENSE**：根目录尚未发布；vendored 许可见 [`COPYRIGHT`](COPYRIGHT)。

更多执行记录见 [`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)。贡献约定见 [`AGENTS.md`](AGENTS.md)。

# 代码风格

本仓库使用 **LLVM 基线的 `.clang-format`**，并约定成员命名 `m_x_`；语言标准在格式配置里为 `Cpp03`。

请以 `.clang-format` 为准：

```bash
make format
make format-check
```

不要按「严格 Google Style」理解本仓库（与 Google 的差异例如 `AccessModifierOffset: -2`、`PointerAlignment: Right`、成员 `m_x_`）。

# 贡献

请先读 [`AGENTS.md`](AGENTS.md)。改进计划在 [`plan/`](plan/)。

# 许可证

- **第三方 vendored 代码**（libtask ucontext、nodejs http-parser）的许可见根目录 [`COPYRIGHT`](COPYRIGHT)。
- **sthread 自身**源码头部为 `Copyright (C) zhoulv2000@163.com`，根目录**尚未**发布 `LICENSE`（由维护者决定，见决策 D6）。使用 / 分发前请与作者确认。
