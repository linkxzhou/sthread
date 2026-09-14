# AGENTS.md

面向 AI 助手与新贡献者的 sthread 仓库约定。**改代码前请先读完本文。**

详细的改进计划见 [`plan/`](plan/README.md)。

---

## 这个项目是什么

sthread 是一个**基于协程的高性能网络库**，用 C++98 写成，提供非阻塞的 TCP/UDP 客户端与服务端能力。

最终交付物是**一个静态库或动态库**：`libmthread.a` / `libmthread.so`。使用方只需链接它。

---

## 重要：仓库当前状态

**当前代码处于一次未完成的重命名重构中间态，整棵源码树无法编译。**

这不是推测，是实测结论（`g++ -fsyntax-only`）：

- `stlib/` 离可编译只差两个修复（一个 include 路径笔误 + 一个未定义的 `__THREAD` 宏），修好后 `-std=c++98` 下零 error
- `src/` 里新旧两套命名混杂（如 `Thread` vs `StThread`、`StEventSuper` vs `StEventItem`），且旧名**全都没有定义**
- `src/makefile` 引用的 10 个 `mt_*.o` 源文件全不存在 → **`libmthread` 当前完全无法产出**
- `tests/Makefile` 的 12 个 target 里 11 个引用了不存在的源文件

**因此**：如果你的改动「编译不过」，请先确认是不是本来就编译不过。基线不是绿的。

完整的现状分析与修复计划见 [`plan/README.md`](plan/README.md)，**按 01 → 05 的顺序执行**。

---

## 九条核心约定

这些是项目的设计主张，也是 review 的红线。

### 1. 不用第三方库

`libmthread` 必须**零第三方运行时依赖**。验证方式：

```bash
ldd libmthread.so          # Linux：只应有 libc / libstdc++ / libpthread / libm / libdl
otool -L libmthread.dylib  # macOS：只应有系统库
```

gperftools（tcmalloc / profiler）与 AddressSanitizer 只能是**可选的开发期开关，默认关闭**。不要把它们写进默认编译选项。

不要引入 gtest / catch2 等测试框架——仓库自带 `stlib/st_test.h`。

### 2. 多平台协程

协程基于 ucontext + 手写汇编（`stlib/ucontext/asm.S`，含 386 / amd64 / mips / power 分支），源自 Russ Cox 的 libtask（MIT）。

**上下文切换与栈布局的代码请勿擅动。** 具体地：

- `StThread::InitContext()` 里把 64 位 `Stack*` 拆成两个 32 位 `int`（`ty` / `tx`）再在入口拼回，是因为 `makecontext` 的可变参数只能传 `int`。这不是 bug
- `ss_sp = m_vaddr_ + 8` 与 `ss_size = m_vaddr_size_ - 64` 是对齐与红区余量，来自上游
- `MEM_PAGE_SIZE` 定义为 2048，它是栈大小的对齐粒度，**不是**系统页大小

这一块是未定义行为高发区，改动前请先有可复现的失败用例。

### 3. epoll + kqueue

两个后端，**编译期二选一**（`src/st_poll.h`）：

```cpp
#if defined(__APPLE__) || defined(__OpenBSD__)
  // kqueue → stlib/st_kqueue.h
#else
  // epoll  → stlib/st_epoll.h
#endif
```

两个文件各自定义**同名**的 `class StIOState`。**它们的公开接口必须完全一致**（`Create` / `ApiResize` / `Free` / `AddEvent` / `DelEvent` / `Poll` / `EventList` / `ApiName`）——只改一边会让另一个平台编译失败。

平台分支只允许出现在这两个文件与少量 `#if defined(__APPLE__)` 处，**不得渗透到业务层**。

### 4. 同步 API，内部异步

**这是本项目最核心的价值主张，不要破坏它。**

业务代码写起来是同步的：

```cpp
conn->RecvData();    // 看起来阻塞
conn->DoProcess();
conn->SendData();
```

框架内部是异步的：数据没就绪时，会注册 fd 事件（`StEventSchedule::Schedule`）并 `Yield` 让出协程；daemon 协程 `epoll_wait` / `kevent` 拿到就绪事件后，通过 `Dispatch` → `IOWaitToRunable` 唤醒原协程，业务代码从 `RecvData()` 内部继续往下走。

**不要引入回调式 / future 式 / 显式 async 的对外 API。** 业务层看不到异步是设计目标，不是缺陷。

### 5. 非阻塞 TCP/UDP 客户端

对外的客户端 API：

```cpp
// app/st_c.h — 这些名字是对外契约，不要改
int  udp_sendrecv(struct sockaddr_in *dst, void *pkg, int len,
                  void *recvbuf, int &bufsize, int timeout);
int  tcp_sendrecv(struct sockaddr_in *dst, void *pkg, int len,
                  void *recvbuf, int &bufsize, int timeout,
                  CheckLengthCallback callback, bool keeplive = false);
void st_set_private(void *data);
void *st_get_private();
void st_set_hook_flag();
```

C++ 类形式：`StClientConnection<T>`（`src/st_connection.h`）。服务端：`StServer<ConnectionT, ServerT>`（`src/st_server.h`）。

### 6. 高性能：内存与句柄足够就能开大量协程

单协程内存占用由 `STACK` 与 `MEM_PAGE_SIZE` 决定：

```
MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE
```

**任何改动都不应增加单协程内存占用**——这直接决定「能开多少协程」。

`StEventSchedule::Init()` 会用 `setrlimit` 把 fd 上限提到 65535（非 root 下可能失败）。

### 7. 只用 `libmthread.a` 或 `.so`

使用方**只链接一个库**，不应直接编译 `src/*.cc`。

产物位置：**仓库根目录**（`app/*/makefile` 都写着 `LIBS = ../../libmthread.a`）。

如果你发现某个 app 或 test 必须直接编译 `src/*.cc` 才能工作，那是库的导出符号不全，应该修库而不是修使用方。

### 8. C++98

禁止使用：`auto`、`nullptr`、右值引用与 move、range-for、lambda、`std::unique_ptr` / `shared_ptr`、`std::thread`、变参模板、`= delete` / `= default`、`override` / `final`、初始化列表 `{}`、`constexpr`、`static_assert`、连续右尖括号 `>>`（模板嵌套要写 `> >`）。

可以使用（GNU 扩展，现有代码已在用）：`__thread`、`__builtin_expect`（`likely` / `unlikely` 宏）。

没有 lambda，所以回调用 `stlib/st_closure.h` 的 `StClosure` 家族 + `NewStClosure(...)` 工厂。

验证：`g++ -std=c++98 -fsyntax-only ...`

### 9. 不改变现有功能行为，对外 API 语义兼容

因为基线编译不过，「行为」以**代码表达的语义意图**为准，而不是「当前二进制的行为」（当前没有）。

**允许**（意图无歧义的笔误修复）：

- `#include "ucontext/st_def.h"` → `"st_def.h"`（文件就在那）
- `__THREAD` → `__thread`（GNU TLS 关键字）
- `m_file_events_[fd].mask` → `m_file_[fd].mask`（同函数另一处已用 `m_file_`）

**不允许**（改变设计或行为）：

- 改枚举数值：`eThreadType` / `eThreadState` / `eThreadFlag` / `eConnType` / `eERR_*`
- 改常量：`MEM_PAGE_SIZE`(2048)、`ST_RECV_BUFFSIZE`/`ST_SEND_BUFFSIZE`(8192)、`ST_MAX_FD`、各类默认超时（30000 / 1000 ms）
- 换数据结构：`StThreadSchedule` 的四条 TAILQ、sleep 最小堆、`StHashList`
- 改 `Instance<T>()` 的**线程局部**语义
- 引入多线程（M:N）调度——当前是每 OS 线程一套调度器的 1:N 模型

**需要先问**（两边都有定义，意图不唯一）：新旧命名二选一、是否把 `StConnection`/`StServer` 移入 `namespace sthread`（会改 ABI）。见 [`plan/README.md` 决策点 D3](plan/README.md#需用户确认的决策点)。

---

## 目录结构

| 目录 | 职责 |
| --- | --- |
| `stlib/` | 基础库。大部分是 header-only 模板：`st_def.h`（宏与错误码）、`st_buffer.h`、`st_heap.h`、`st_heap_timer.h`、`st_hash_list.h`、`st_tailq.h`、`st_singleton.h`、`st_closure.h`、`st_util.h`、`st_netaddr.h`、`st_log.h`、`st_test.h`。非头文件实现只有 `st_log.cc` 与 `st_test.cc` |
| `stlib/st_epoll.h` / `st_kqueue.h` | 两个多路复用后端（同名 `StIOState`，编译期二选一） |
| `stlib/ucontext/` | 协程上下文切换：`ucontext.c` + `asm.S` + 各架构头（`ucontext-386.h` / `-amd64.h` / `-mips.h` / `-power.h`） |
| `stlib/tests/` | `stlib` 的单元测试（`st_*_test.cc`）。`stlib/tests/ucontext/` 是 libtask 上游参考样例，**不参与构建** |
| `stlib/tiny/` | 未被任何构建引用（去向待定） |
| `src/` | 框架核心 → `libmthread`。`st_poll.h`（`StThreadItem` / `StEventItem`）、`st_thread.h/.cc`（`StThread` / `StThreadSchedule` / `StEventSchedule`）、`st_connection.h/.cc`、`st_server.h`、`st_sys.h/.cc`、`st_public.h`（枚举与错误码） |
| `app/` | 示例应用：`st_dns/`、`st_memcacheclient/`、`st_wrk/` |
| `app/st_c.*`、`app/st_sys.*` | **注意：这两组是库代码，不是示例**（对外 C API 与 syscall hook）。它们放在 `app/` 下属目录语义错误，见 `plan/01` 的遗留决策 |
| `tests/` | 框架的单元测试 + `tests/scripts/` 下的 Python 测试 |
| `thirdparty/` | gperftools（可选开发期依赖，见决策点 D2） |
| `plan/` | 改进计划文档 |

---

## 构建

```bash
make          # 产出 libmthread.a 与 libmthread.so（到仓库根目录）
make apps     # 编译 app/ 下的示例
make tests    # 编译测试
make clean
```

**注意**：根 makefile 目前**还不存在**，这是 `plan/01` 的交付项之一。当前只能分目录 `make -C stlib` / `make -C src`（且后者无法成功）。

计划中的可选开关（默认全关，落实「零第三方依赖」）：

| 开关 | 作用 |
| --- | --- |
| `DEBUG=1` | `-g` |
| `TRACE=0` | 关闭 `LOG_TRACE` 输出（注意：现有各 makefile 默认开着 `-DTRACE`） |
| `ASAN=1` | AddressSanitizer。**与协程自定义栈切换配合不佳**，会有误报 |
| `TCMALLOC=1` / `PROFILER=1` | 需自行安装 gperftools |

include 路径规范：只提供**仓库根**一个 include 根（`-I.`），跨目录引用一律从根出发写全路径（`#include "stlib/st_def.h"`）。禁止 `../include/...` 这类穿越写法（`include/` 目录根本不存在）。

---

## 代码风格

**`.clang-format` 是唯一权威**，提交前跑：

```bash
clang-format -i <你改过的文件>
clang-format --dry-run --Werror <文件>    # 检查
```

注意一个待解决的矛盾（[决策点 D1](plan/README.md#需用户确认的决策点)）：文档里写的是「Google C++ Style」，但 `.clang-format` 实际是 **LLVM 基线**（`# BasedOnStyle: LLVM`、`AccessModifierOffset: -2`、`PointerAlignment: Right`），且 `Standard: Latest` 与 C++98 约束冲突。在 D1 拍板之前，**以 `.clang-format` 文件的实际内容为准**。

现有命名习惯（与 Google 不同，但请保持一致）：

| 对象 | 写法 | 例 |
| --- | --- | --- |
| 类 | `St` 前缀 + 大驼峰 | `StThreadSchedule`、`StEventItem` |
| 方法 | 大驼峰 | `HeapPush()`、`GetActiveThread()` |
| 成员变量 | `m_` 前缀 + 尾 `_` | `m_active_thread_`、`m_osfd_` |
| 枚举值 | `e` 前缀 | `eRUNABLE`、`eTCP_CONN` |
| 宏 | `ST_` 前缀全大写 | `ST_READABLE`、`ST_OK` |
| 命名空间 | `stlib`（基础库）、`sthread`（框架） | 注意 `src/st_connection.h` 与 `st_server.h` 目前**不在任何 namespace 内** |

注释用**中文**（与现有代码一致）。

头文件应**自包含**（self-contained）：每个头 include 自己需要的一切，不依赖使用方的 include 顺序。可用这个方法检查：

```bash
echo '#include "src/st_poll.h"' > /tmp/t.cc && echo 'int main(){return 0;}' >> /tmp/t.cc
g++ -std=c++98 -fsyntax-only -I. /tmp/t.cc
```

---

## 贡献注意

1. **先读 [`plan/README.md`](plan/README.md)**，特别是「决策点」一节。有 6 个决策点会阻塞对应阶段的开工
2. **按阶段顺序推进**：01 基础设施 → 02 协程调度 → 03 IO 与网络 → 04 apps/tests → 05 文档。每阶段有明确的出口条件，未达标不要进下一阶段
3. **不要提交二进制与产物**：`.DS_Store`、`app/st_dns/main`、`app/st_wrk/wrk`、`tests/st_server_unittest` 等目前**已被误提交**，`plan/01` 会清理。提交前确认 `make clean` 后 `git status --porcelain` 为空
4. **双平台都要验证**：Linux（epoll）+ macOS（kqueue）。注意 `asm.S` **没有 arm64 分支**，Apple Silicon 支持状态待确认
5. **改 `stlib/st_epoll.h` 或 `st_kqueue.h` 时，两个文件一起改**（接口必须一致）
6. **如果 app 或 test 必须改业务逻辑才能编译**，那是库的 API 被改坏了 → 回头修库，不要改使用方。唯一例外是 include 路径与已授权的改名
7. **`// TODO:` 标记请保留**，它们标示真实的未完成项（如 `src/st_thread.h` 的「TODO: 回收sthread」——协程对象当前确实不回收）
8. **格式化与功能改动分开提交**。全量 `clang-format` 会产生巨大 diff，应独立成一个纯格式提交并登记到 `.git-blame-ignore-revs`
9. **写文档前先让代码跑起来**。`readme.md` 当前通篇是已不存在的旧 API（`mt_init_frame` / `Frame` / `IMtAction` / `IMtActionServer`…），照着做必然失败。不要重复这个错误——文档只写验证过的事实

---

## 已知问题速查

| 问题 | 位置 | 详见 |
| --- | --- | --- |
| 整树无法编译（重命名重构中间态） | 全仓库 | `plan/README.md` |
| `libmthread` 无法产出（`mt_*.o` 源文件全缺） | `src/makefile` | `plan/01` |
| `__THREAD` 宏未定义 | `stlib/st_singleton.h` | `plan/01` |
| `ucontext/st_def.h` 路径笔误 | `stlib/st_closure.h`、`st_util.h`、`st_log.h` | `plan/01` |
| `context_switch` / `context_exit` / `STACK` 无定义 | `src/st_thread.h` | `plan/02` |
| `StThread` 重复定义 | `src/st_thread.h` + `app/thread.h` | `plan/02` |
| epoll 的 `AddEvent` 从不更新 mask（事件注册根本失效） | `stlib/st_epoll.h` | `plan/03` |
| 两套同名不同签名的 `extern "C" __*` 符号 | `src/st_sys.h` vs `app/st_sys.h` | `plan/03` |
| `src/st_sys.cc` 与 `st_sys.h` 有 8 处函数名不一致 | `src/st_sys.*` | `plan/03` |
| `eTCP_KEEPLIVE_CONN` / `eUDP_UDPSESSION_CONN` 的值为 0（`&` 应为 `\|`），keepalive 完全失效 | `src/st_public.h` | `plan/03` |
| `Keeplive()` 硬编码 `return false` 且非虚 | `src/st_connection.h` | `plan/03` |
| `tests/Makefile` 12 个 target 里 11 个源文件缺失 | `tests/Makefile` | `plan/04` |
| 协程对象不回收 | `src/st_thread.h` | `plan/02` |
| `readme.md` 全是已不存在的 API | `readme.md` | `plan/05` |
| 无 LICENSE，但含 MIT 第三方成分 | 根目录 | `plan/05` |
| `thirdparty/gperftools` 是 gitlink 但无 `.gitmodules` | `thirdparty/` | `plan/README.md` D2 |
