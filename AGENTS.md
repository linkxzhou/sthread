# AGENTS.md

面向 AI 助手与新贡献者的 sthread 仓库约定。**改代码前请先读完本文。**

详细计划见 [`plan/`](plan/README.md)。对外说明见 [`README.md`](README.md)。

---

## 这个项目是什么

sthread 是一个**基于协程的高性能网络库**，C++98，提供非阻塞 TCP/UDP 客户端与服务端。

交付物：`libmthread.a` / `libmthread.so`（仓库根目录）。使用方只链接这一个库。

---

## 仓库当前状态（plan/01～04 后）

| 范围 | 状态 |
| --- | --- |
| `stlib/` | 绿：`-std=c++98`，`make test`（stlib/tests）可跑 |
| `make lib` | 绿：产出 `libmthread.a` / `.so`，无第三方运行时依赖 |
| `make apps` | 绿：dns / memcache / wrk / httpserver **可编译** |
| `make -C tests` | 绿：核心 unittest 可编译；多数可跑 |
| Apple Silicon arm64 | **真实 ucontext/asm**（`NEEDARM64CONTEXT`） |
| keepalive（L4） | **已修**：`eTCP_KEEPLIVE_CONN=0x11`，`Keeplive()`=`IS_KEEPLIVE` |
| 本仓库 `LICENSE` | **未发布**（仅有 vendored 的 [`COPYRIGHT`](COPYRIGHT)） |

回归记录：[`plan/04-regression-checklist.md`](plan/04-regression-checklist.md)。

---

## 九条核心约定

### 1. 不用第三方运行时库

`libmthread` 零第三方运行时依赖。`ldd` / `otool -L` 只应见系统库。

gperftools / ASan 仅为可选开发期开关，默认关（`make.inc`）。测试用自带 `stlib/st_test.h` / `st_test.cc`（**仅测试构建**，不进 libmthread），不要引入 gtest。

### 2. 多平台协程

ucontext + `stlib/ucontext/asm.S`（386 / amd64 / mips / power；**含 arm64**）。来源 Russ Cox libtask，见 [`COPYRIGHT`](COPYRIGHT)。

勿擅动：`InitContext` 的 `ty`/`tx` 拆分、`ss_sp`/`ss_size` 余量、`STACK`（260096）、`MEM_PAGE_SIZE`（2048）。

### 3. epoll + kqueue

`src/st_poll.h` 编译期二选一。`StIOState` 两边公开接口必须完全一致。平台分支勿渗入业务层。

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

C++：`StClientConnection`、`StServer`。示例兼容层：`app/st_action.h`、`app/st_frame.h`。

### 6. 高性能

单协程栈：`MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE`（约 266240 B）。改动勿增大该占用。

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
| `app/st_c.*` `app/st_sys.*` `app/st_action.*` | **库代码**（物理仍在 app/） |
| `app/st_dns` 等 | 示例 |
| `tests/` | 框架 unittest + scripts |
| `thirdparty/` | 仅说明文档；无运行时依赖 |
| `plan/` | 分阶段计划（含 `07-src-refactor-optimize.md`） |
| `make.inc` | 公共编译开关 |
| `COPYRIGHT` | vendored 第三方许可（非本仓库 LICENSE） |

---

## libmthread 产物

| 目标 | 源 |
| --- | --- |
| `src/st_thread.o` `st_connection.o` `st_sys.o` | `src/*.cc` |
| `stlib/st_log.o` `st_context.o` | stlib（**不含** `st_test.o`，D6） |
| `stlib/ucontext/ucontext.o` + `asm.o` （含 arm64） | ucontext |
| `app/st_c.o` `st_sys.o` `st_action.o` | app 下的库文件 |

系统库：`-lpthread -ldl`。不得链第三方。

---

## 构建

```bash
make lib / apps / tests / clean / format / format-check / help
make -C tests run
make -C stlib/tests run    # stlib 单测
```

开关：`TRACE` `DEBUG` `ASAN` `TCMALLOC` `PROFILER`（见 `make.inc`）。

---

## 贡献注意

1. API 兼容：对外符号与枚举数值勿 silently 改
2. 不提交二进制 / `.dSYM` / 本地 log（见 `.gitignore`）
3. 注释保持**中文**（与现有代码一致）
5. 改文档时示例必须来自真实可编译路径（`app/` / `tests/`），勿再写已删除的 `IMtActionServer` / `mt_set_timeout` 等

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
| 高并发 / wrk QPS 缺 Linux 实测数字 | 文档诚实标注 |

### 推荐对外 include（libmthread 使用方）

| 场景 | 推荐头 |
| --- | --- |
| 最简 C 风格入口（init / hook / udp·tcp_sendrecv） | `app/st_c.h` + `app/st_frame.h` |
| 自写 `StServer` / 连接派生类 | `src/st_server.h`（会拉 `st_connection.h` / `st_sys.h`） |
| 仅用带超时 `st_read`/`st_write`/… | `src/st_sys.h` |
| 基础类型 / 连接枚举 | `src/st_public.h`、`stlib/st_netaddr.h` |

不要直接 include `app/st_sys.h`（hook 实现细节），除非自己做 syscall 表扩展。不要 include `stlib/st_test.h`（仅测试）。

完整清单与验收见各 `plan/0x-*.md`、`plan/07-src-refactor-optimize.md`。
