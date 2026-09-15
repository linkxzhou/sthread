sthread
---

# 简介

[sthread](https://github.com/linkxzhou/sthread) 是一个基于协程的高性能网络库，用 **C++98** 写成，提供非阻塞的 TCP/UDP 客户端与服务端能力。

最终交付物是一个库：`libmthread.a` / `libmthread.so`。使用方只需链接它。

# 特性

1. 不用依赖任何第三方运行时库
2. 基于支持多平台的协程调度（ucontext）
3. 支持 epoll（Linux）与 kqueue（macOS / OpenBSD）
4. 不用写异步调度代码：业务全部同步写法，框架内部异步处理
5. 提供非阻塞 TCP 客户端
6. 提供非阻塞 UDP 客户端
7. 跨平台；在内存与句柄足够时可以创建大量协程（见下方「性能」）
8. 使用简单，只需链接一个 `libmthread.a` 或 `libmthread.so`

示例应用：`app/st_dns`、`app/st_memcacheclient`、`app/st_wrk`。

# 环境要求

| 项 | 内容 |
| --- | --- |
| 语言标准 | C++98（`-std=c++98`） |
| Linux | g++；epoll 后端 |
| macOS | clang++；kqueue 后端 |
| 运行时依赖 | 无（仅系统库：libc / libstdc++ 或 libc++ / libpthread / libdl） |
| 可选开发期依赖 | gperftools（tcmalloc / profiler），默认关闭；见 [`thirdparty/readme.md`](thirdparty/readme.md) |

**Apple Silicon（arm64）说明**：已落地真实 `ucontext`/`asm`（`NEEDARM64CONTEXT`，`libthread_makecontext` 避开系统 `makecontext`）。协程切换可用；端到端 IO 冒烟见 [`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)。

# 快速开始

## 编译

在仓库根目录：

```bash
make lib                 # 产出 libmthread.a 与 libmthread.so（仓库根）
make apps                # 编译 app/st_dns、st_memcacheclient、st_wrk
make tests               # 编译 tests/ 下的 unittest
make -C tests run        # 运行核心单测
make clean
make help                # 目标与开关一览
```

可选开关（见 `make.inc`，默认：`TRACE=1` `DEBUG=1` `ASAN=0` `TCMALLOC=0` `PROFILER=0`）：

```bash
make lib TRACE=0         # 关闭 LOG_TRACE
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
  Frame::Loop(true);    /* 进入 daemon 事件循环 */
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

# 示例

## DNS 客户端

完整代码：`app/st_dns/`。协议解析在 `dns.cpp`；调度入口在 `main.cpp`。

要点：

```cpp
st_init_frame();
st_set_hook_flag();
Frame::CreateThread(func, s);   /* s 为域名字符串 */
Frame::Loop(true);
```

`dns_lookup` 内部通过 `udp_sendrecv` 发查询。请使用真实可解析域名做联调；readme 旧示例里的 `www.2000.com` 一类域名通常无结果。

```bash
make -C app/st_dns
# 需要外网 DNS（arm64 真实 ucontext 已可用）
./app/st_dns/main
```

## TCP / UDP 客户端 API

头文件：`app/st_c.h`（已编进 `libmthread`）。

```cpp
int udp_sendrecv(struct sockaddr_in *dst, void *pkg, int len,
                 void *recvbuf, int &bufsize, int timeout);

int tcp_sendrecv(struct sockaddr_in *dst, void *pkg, int len,
                 void *recvbuf, int &bufsize, int timeout,
                 CheckLengthCallback callback, bool keeplive = false);
```

示例应用还会用到精简版 `IMtAction` / `IMtActionClient`（`app/st_action.h`），其 `SendRecv` 建立在上述 API 之上：见 `app/st_memcacheclient`、`app/st_wrk`。

## HTTP 服务端（Listen）

服务端模板：`StServer<ConnectionT, ServerT>`（`src/st_server.h`）。连接回调请覆盖 `DoInput` / `DoOutput` / `DoProcess` / `DoError`（不是旧的 `Handle*`）。

可编译的 Listen 路径示例：`tests/st_server_unittest.cpp`（创建 socket + `Listen`；完整 `Loop` 依赖真实协程切换）。

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
| arm64 app 冒烟 | **已做**（wrk/memcache 通过；dns 合成域名超时，见 plan/04） |
| arm64 实测 QPS / 万级协程 | **底层切换已通**；万级协程 / QPS 仍待专项压测 |

# 已知限制

- **Apple Silicon**：真实 ucontext 已落地；app 冒烟结果见 plan/04 清单。
- **TCP keepalive 复用**：`eTCP_KEEPLIVE_CONN`（0x11）+ `Keeplive()`=`IS_KEEPLIVE`；连接池对 keepalive 类型按地址 hash 复用。
- **协程对象回收**：`StThread` 池回收仍有 `TODO`，长时间大量创建需关注内存。
- **`app/st_c.h`**：在 `extern "C"` 块里使用了 C++ 引用，**不能**被纯 C 编译器直接 include。
- **单进程内协程不可跨 OS 线程**（由 `Instance<T>()` 线程局部语义决定）。
- `app/st_c.*` / `app/st_sys.*` 语义上是库代码，物理路径仍在 `app/`（未搬迁）。

更多执行记录见 [`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)。

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
