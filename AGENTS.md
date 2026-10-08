# AGENTS.md

面向 AI 助手与新贡献者的 sthread 仓库约定。**改代码前请先读完本文。**

详细计划见 [`plan/`](plan/README.md)。对外说明见 [`readme.md`](readme.md)。

---

## 这个项目是什么

sthread 是一个**基于协程的高性能网络库**，C++98，提供非阻塞 TCP/UDP 客户端与服务端。

交付物：`libmthread.a` / `libmthread.so`（仓库根目录）。使用方只链接这一个库。

---

## 仓库当前状态（plan/01～12）

| 范围 | 状态 |
| --- | --- |
| `stlib/` | 绿：`-std=c++98`，`make test`（stlib/tests）可跑 |
| `make lib` | 绿：产出 `libmthread.a` / `.so`，无第三方运行时依赖 |
| `make apps` | 绿：dns / memcache / wrk / httpserver / dnsserver / httpclient / echo / portscan / hookdemo / redis / httpproxy / chat |
| Android | NDK 交叉编译 arm64-v8a / x86_64（API 21，`make android`，只编译）。无 armv7 / x86，不跑模拟器 |
| `make bench-http` / `make bench-dns` | 绿：脚本起停 server（PID），写 `reports/`；冻结基线见 `reports/baseline-*.md` |
| `make -C tests run` | macOS arm64 回归见 [`plan/09-main-bugfix-cleanup.md`](plan/09-main-bugfix-cleanup.md)；Linux 待验证 |
| Apple Silicon arm64 | **真实 ucontext/asm**（`NEEDARM64CONTEXT`） |
| keepalive（L4） | **已修**：`eTCP_KEEPLIVE_CONN=0x11`，`Keeplive()`=`IS_KEEPLIVE` |
| 本仓库 `LICENSE` | **未发布**（仅有 vendored 的 [`COPYRIGHT`](COPYRIGHT)） |
| 六个新样例（echo / 端口扫描 / 反代 / Redis / 聊天室 / hook） | **已落地**见 [`plan/11-apps-more-scenarios.md`](plan/11-apps-more-scenarios.md)。冒烟是 `make smoke-*`，只在本地跑，不进 CI |
| 短连接性能（`make bench-curve`） | **本轮已落地**：epoll 掩码替换、去掉多余 DEL、协程栈复用、`STACK=131072`。QPS 仍在噪声里。其余延期。见 [`plan/12`](plan/12-performance-optimization.md) §12 与 [`reports/perf-p1-p3-20261008.md`](reports/perf-p1-p3-20261008.md)。QPS 不进 CI |

回归记录：[`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)。

---

## 九条核心约定

### 1. 不用第三方运行时库

`libmthread` 零第三方运行时依赖。`ldd` / `otool -L` 只应见系统库。

gperftools / ASan 仅为可选开发期开关，默认关（`make.inc`）。测试用自带 `stlib/st_test.h` / `st_test.cc`（**仅测试构建**，不进 libmthread），不要引入 gtest。

### 2. 多平台协程

ucontext + `stlib/ucontext/asm.S`（386 / amd64 / mips / power；**含 arm64**）。来源 Russ Cox libtask，见 [`COPYRIGHT`](COPYRIGHT)。

平台选择在 `stlib/st_platform.h`（编译期宏，无虚接口）。后端：

| 平台 | 上下文 | 事件 |
| --- | --- | --- |
| Linux | glibc `get/make/swapcontext`（不改走 asm） | epoll |
| macOS | vendored libtask asm（Mach-O `_` 符号） | kqueue |
| Android arm64-v8a / x86_64 | 同一套 libtask asm，ELF 符号名，无系统 ucontext | epoll |
| OpenBSD | libtask（既有分支） | kqueue |

新平台接入：在 `st_platform.h` 加 `ST_OS_*`；上下文走系统 ucontext 或 `stlib/ucontext/` 的 asm（不要引入 boost.context）；事件后端只在 `src/st_poll.h` 按 `ST_POLL_*` 选择；`make.inc` 用 `$(CC) -dumpmachine` 推导 `ST_OS` / `ST_ARCH`。Android 只支持 arm64-v8a 与 x86_64（`make android ABI= API=`）。

勿擅动：`InitContext` 的 `ty`/`tx` 拆分、`ss_sp`/`ss_size` 余量、`MEM_PAGE_SIZE`（2048）。默认 `STACK` 为 **131072**（128KB，plan/12）。

### 3. epoll + kqueue

`src/st_poll.h` 按 `ST_POLL_KQUEUE` / `ST_POLL_EPOLL` 编译期二选一（Apple / OpenBSD 为 kqueue，其余含 Android 为 epoll）。`StIOState` 两边公开接口必须完全一致。平台分支勿渗入业务层。

### 4. 同步 API，内部异步

业务 `RecvData` / `udp_sendrecv` 等同步写法；内部 Yield + daemon 多路复用。

**不要**引入回调式 / future 式对外 API。

### 5. 非阻塞 TCP/UDP 客户端

契约 API（`app/st_c.h`，已进 lib）：

```cpp
st_init_frame();
st_set_hook_flag();
udp_sendrecv(...);
tcp_sendrecv(..., CheckLengthCallback, bool keeplive = false);
```

`st_*` I/O 包装（`src/st_sys.h`）超时一律返回 `-1` 且 `errno == ETIME`；`-3` 只表示 `Schedule` / `Add` 失败。`app/st_sys.cc` 的 hook（`sys_read` / `sys_recv` / …）对调用方是 libc 形状：失败 `-1` 且 errno 已设置。样例见 `app/st_hookdemo`。`tcp_sendrecv` / `udp_sendrecv` 的状态码是另一套（TCP 接收超时为 `-3`），见 `app/st_c.h`，不要和 `st_*` 的 `-3` 混用。同一 OS 线程的协程通知是 `st_notify` / `st_notify_wait` / `st_wait`（`src/st_sys.h`），不占 fd。

C++：`StClientConnection`、`StServer`。示例里的 `Frame` 在 `app/st_frame.h`；HTTP 样例见 `app/st_httpclient`。

### 6. 高性能

单协程栈：`MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE`（`STACK=131072` 时约 137216 B）。改动勿增大该占用。

### 7. 只用 `libmthread.a` / `.so`

使用方不要直接编译 `src/*.cc`。缺符号应修库导出，而不是让 app 编进源文件。

### 8. C++98

禁止 C++11+ 语言特性。可用 `__thread`、`__builtin_expect`。回调用 `NewStClosure`。

### 9. 不改变现有功能行为

- 勿改枚举数值、`MEM_PAGE_SIZE`、buffer 大小、默认超时
- 勿改 `Instance<T>()` 的线程局部语义
- 勿改成 M:N 跨线程调度
- 笔误级 include / 明显拼写修复可以做；设计分歧先问

代码风格：以 `.clang-format`（LLVM 基线 + `m_x_`）为准，**不是**严格 Google Style。`make format` / `format-check`。

---

## 目录结构

| 路径 | 职责 |
| --- | --- |
| `stlib/` | 基础库（多为 header-only）+ ucontext；说明见 [`stlib/README.md`](stlib/README.md) |
| `src/` | 框架核心 → 编进 libmthread |
| `app/st_c.*` `app/st_sys.*` | **库代码**（物理仍在 app/） |
| `app/st_dns` 等 | 示例（含 `st_dnsserver`） |
| `scripts/` | `bench_http.sh` / `bench_dns.sh` |
| `reports/` | 压测报告；提交冻结 `baseline-*.md` |
| `tests/` | 框架 unittest + scripts |
| `thirdparty/` | 仅说明文档；无运行时依赖 |
| `plan/` | 分阶段计划（含 `08-apps-bench-dnsserver.md`） |
| `make.inc` | 公共编译开关 |
| `COPYRIGHT` | vendored 第三方许可（非本仓库 LICENSE） |

---

## libmthread 产物

| 目标 | 源 |
| --- | --- |
| `src/st_thread.o` `st_connection.o` `st_sys.o` | `src/*.cc` |
| `stlib/st_log.o` `st_context.o` | stlib（**不含** `st_test.o`，D6） |
| `stlib/ucontext/ucontext.o` + `asm.o` （含 arm64） | ucontext |
| `app/st_c.o` `st_sys.o` | app 下的库文件 |

系统库：Linux / macOS 为 `-lpthread -ldl`；Android 只有 `-ldl`（pthread 在 bionic libc）。不得链第三方。

---

## 构建

```bash
make lib / apps / tests / android / clean / format / format-check / help
make bench-http / bench-dns / bench    # 默认 BENCH_PROFILE=smoke
make -C tests run
make -C stlib/tests run    # stlib 单测
```

开关：`TRACE` `DEBUG` `ASAN` `TCMALLOC` `PROFILER`（见 `make.inc`）。

---

## 贡献注意

1. API 兼容：对外符号与枚举数值勿 silently 改
2. 不提交二进制 / `.dSYM` / 本地 log（见 `.gitignore`）
3. 注释保持**中文**（与现有代码一致）
5. 改文档时示例必须来自真实可编译路径（`app/` / `tests/`），勿再写已删除的接口名

---

## 已知问题（摘要）

| 项 | 归类 |
| --- | --- |
| L4 / Keeplive 枚举与 `Keeplive()` | **已修**（plan/05–06；见 keepalive 单测） |
| plan/07 P-A/P-B/P-C（死代码、泄漏、`st_*` 骨架、头依赖） | **已修**（见 `plan/07-src-refactor-optimize.md` §9） |
| keepalive **真连接池复用** | **未做**（D1：仅诚实注释；`FreePtr` 仍 HashRemove） |
| `StThread` 池回收 TODO | 待办 |
| `app/st_c.h` 非纯 C 可用 | 已知限制 |
| `app/st_c|st_sys` 未搬入 `src/` | 遗留目录语义（C4 已去掉 server→`st_c.h`） |
| 根 `LICENSE` 未定 | 待维护者 |
| 高并发 / wrk QPS | 冻结基线见 `reports/baseline-*.md`（plan/08，Linux smoke）；短连接、单 OS 线程；`-d` 为时长标签 |
| 同进程连续 UDP | macOS arm64 的 4 次 loopback 已通过；历史 Linux 故障仍待复现，DNS 压测继续用 `-n == -c` |
| Linux `st_context_unittest` | 64 KiB `makecontext` 栈 SIGABRT（未改 `STACK`） |
| Android | **已交叉编译** arm64-v8a / x86_64（NDK，API 21，`build-android.yml` 只编译）。无 armeabi-v7a / x86，不跑模拟器 |

### 推荐对外 include（libmthread 使用方）

| 场景 | 推荐头 |
| --- | --- |
| 最简 C 风格入口（init / hook / udp·tcp_sendrecv） | `app/st_c.h` + `app/st_frame.h` |
| 自写 `StServer` / 连接派生类 | `src/st_server.h`（会拉 `st_connection.h` / `st_sys.h`） |
| 仅用带超时 `st_read`/`st_write`/…，或 `st_notify` / `st_wait` | `src/st_sys.h` |
| 基础类型 / 连接枚举 | `src/st_public.h`、`stlib/st_netaddr.h` |

不要直接 include `app/st_sys.h`（hook 实现细节），除非自己做 syscall 表扩展。不要 include `stlib/st_test.h`（仅测试）。

完整清单与验收见各 `plan/0x-*.md`、`plan/07-src-refactor-optimize.md`。
