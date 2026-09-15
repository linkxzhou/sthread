sthread
---

# 简介

[sthread](https://github.com/linkxzhou/sthread) 是一个基于协程的高性能网络库，用 **C++98** 写成，提供非阻塞的 TCP/UDP 客户端与服务端能力。

最终交付物是一个库：`libmthread.a` / `libmthread.so`。使用方只需链接它。

# 特性

1. 不用依赖任何第三方运行时库
2. 多平台协程调度（ucontext + `asm.S`：386 / amd64 / mips / power / **arm64**）
3. 支持 epoll（Linux）与 kqueue（macOS / OpenBSD）
4. 不用写异步调度代码：业务全部同步写法，框架内部异步处理
5. 提供非阻塞 TCP / UDP 客户端（短连接与 TCP keepalive 复用）
6. 跨平台；在内存与句柄足够时可以创建大量协程（见下方「性能」）
7. 使用简单，只需链接一个 `libmthread.a` 或 `libmthread.so`

示例应用：`app/st_dns`、`app/st_memcacheclient`、`app/st_wrk`、`app/st_httpserver`。

# 环境要求

| 项 | 内容 |
| --- | --- |
| 语言标准 | C++98（`-std=c++98`） |
| Linux | g++；epoll 后端 |
| macOS | clang++；kqueue 后端；**Apple Silicon 已支持真实 ucontext** |
| 运行时依赖 | 无（仅系统库：libc / libstdc++ 或 libc++ / libpthread / libdl） |
| 可选开发期依赖 | gperftools（tcmalloc / profiler），默认关闭；见 [`thirdparty/readme.md`](thirdparty/readme.md) |

## Apple Silicon（arm64）

- 实现：`stlib/ucontext/ucontext-arm64.h` + `asm.S`（`NEEDARM64CONTEXT`）
- 符号：`makecontext` / `swapcontext` 重命名为 `libthread_*`，**避开** Apple `libsystem` 同名函数（布局不兼容，会 SIGSEGV）
- 栈：`makecontext` 保持 SP 16 字节对齐；`InitContext` 使用 `ss_sp + 16`
- 冒烟与单测记录见 [`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)

# 验证状态（本机 Apple Silicon，2026-09-15）

| 项 | 状态 |
| --- | --- |
| `make lib` / `make -C tests run` | 通过（含 `st_thread_unittest` `Wait(10)`、`st_keepalive_unittest`） |
| `st_httpserver` | 样例：`app/st_httpserver`，默认 `:8765`，可与 `st_wrk` / curl 联调 |
| `st_wrk` | **通过**：`./wrk -n 3 -c 3 -d 2s http://127.0.0.1:8765/` → 3 requests，约 832 req/s |
| `st_memcacheclient` | **通过**：本机 memcached `:11211`，可见 `STORED` / `VALUE k1`（有同 fd `item conflict` 告警） |
| `st_dns` | **部分通过**：150 协程进 IO wait、切换正常；默认 `www.2000–2149.com` 为合成域名，查询超时属预期；`Frame::Loop(true)` 不退出，需手动结束进程 |
| L4 keepalive | **已修**：`eTCP_KEEPLIVE_CONN = 0x11`，`Keeplive()` = `IS_KEEPLIVE(m_type_)` |
| 万级协程 / QPS 专项 | 待测 |

# 快速开始

## 编译

在仓库根目录：

```bash
make lib                 # 产出 libmthread.a 与 libmthread.so（仓库根）
make apps                # 编译 app/st_dns、st_memcacheclient、st_wrk、st_httpserver
make tests               # 编译 tests/ 下的 unittest
make -C tests run        # 运行核心单测（含 keepalive）
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
# 另开终端可压测：./app/st_wrk/wrk -n 1000 -c 50 -d 5s http://127.0.0.1:8765/
```

详见 [`app/st_httpserver/README.md`](app/st_httpserver/README.md)。

## 最小使用轮廓

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

完整可编译示例见 `app/st_dns/main.cpp`（DNS）与 `tests/st_server_unittest.cpp`（Listen 路径）。

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

示例应用还会用到精简版 `IMtAction` / `IMtActionClient`（`app/st_action.h`），其 `SendRecv` 建立在 `tcp_sendrecv` / `udp_sendrecv` 之上。

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
              |  数据未就绪 → 注册事件 + Yield
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

# API 参考（对外）

| API | 位置 |
| --- | --- |
| `st_init_frame` / `st_set_hook_flag` / `st_set_private` / `st_get_private` | `app/st_c.h` |
| `udp_sendrecv` / `tcp_sendrecv` | `app/st_c.h` |
| `Frame::CreateThread` / `Frame::Loop` | `app/st_frame.h`（薄封装） |
| `StSysSchedule::CreateThread` | `src/st_sys.h` |
| `StClientConnection` / `StConnection` | `src/st_connection.h` |
| `StServer` | `src/st_server.h` |
| `IMessage` / `IMtAction` / `IMtActionClient` | `app/st_action.h`（示例兼容层） |
| `Instance<T>()` | `stlib/st_singleton.h` |
| `eConnType` / `IS_KEEPLIVE` / `IS_TCP_CONN` | `src/st_public.h` |

## 旧名 → 新名（迁移）

| 旧（readme / 历史） | 现在 |
| --- | --- |
| `mt_init_frame` | `st_init_frame`（仍提供宏别名） |
| `mt_set_hook_flag` | `st_set_hook_flag`（仍提供宏别名） |
| `mt_set_timeout` | 无直接等价物；超时在连接 / `tcp_sendrecv` 参数里 |
| `Util::system_ms` | `Util::TimeMs` |
| `safe_delete` / `safe_free` | `st_safe_delete` / `st_safe_free`（`st_action.h` 仍有兼容宏） |
| `eTCP_SHORT_CONN` | `eTCP_CONN`（`st_action.h` 有兼容宏） |
| `IMtActionServer` | `StServer<ConnectionT, ServerT>` |
| `HandleEncode` / `HandleInput` / …（连接层） | `DoOutput` / `DoInput` / `DoProcess` / `DoError` |
| `Manager` | `StSysSchedule` |
| `StEventSuper` | `StEventItem` |
| `Thread` / `ThreadScheduler` | `StThread` / `StThreadSchedule` |

# 性能

单协程栈分配（实现公式，`StThread::InitStack`）：

```
MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE
```

当前常量：`STACK = 260096`，`MEM_PAGE_SIZE = 2048` → 约 **266240 字节 / 协程**（约 260 KiB）。

| 指标 | 状态 |
| --- | --- |
| 单协程栈占用 | 上式（静态可算） |
| 高并发创建上限 | 受内存与 `RLIMIT_NOFILE` 限制；`StEventSchedule::Init` 会尝试把 fd 上限提到 65535（非 root 可能失败） |
| arm64 协程切换 | 已通（`libthread_makecontext` + asm） |
| arm64 app 冒烟 | wrk / memcache 通过；dns 合成域名超时，见「验证状态」 |
| 万级协程 / QPS 专项 | 待测 |

# 已知限制

- **协程对象回收**：`StThread` 池回收仍有 `TODO`，长时间大量创建需关注内存。
- **`app/st_c.h`**：在 `extern "C"` 块里使用了 C++ 引用，**不能**被纯 C 编译器直接 include。
- **单进程内协程不可跨 OS 线程**（由 `Instance<T>()` 线程局部语义决定）。
- **`Frame::Loop(true)`**：进入 daemon 后默认不返回；示例进程需外部结束。
- **同 fd 多 action**：memcache 示例可能打出 `item conflict` 告警，属示例用法问题，不是 ucontext 回归。
- `app/st_c.*` / `app/st_sys.*` 语义上是库代码，物理路径仍在 `app/`（未搬迁）。
- **LICENSE**：根目录尚未发布；vendored 许可见 [`COPYRIGHT`](COPYRIGHT)（决策 D6）。

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
