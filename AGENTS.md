# AGENTS.md

面向 AI 助手与新贡献者的 sthread 仓库约定。**改代码前请先读完本文。**

详细的改进计划见 [`plan/`](plan/README.md)。

---

## 这个项目是什么

sthread 是一个**基于协程的高性能网络库**，用 C++98 写成，提供非阻塞的 TCP/UDP 客户端与服务端能力。

最终交付物是**一个静态库或动态库**：`libmthread.a` / `libmthread.so`。使用方只需链接它。

---

## 重要：仓库当前状态

**`stlib/` 已经是绿色基线；`src/` 及其下游仍然编译不过。**

`plan/01` 已完成，当前实测状态：

| 范围 | 状态 |
| --- | --- |
| `stlib/`（含四个测试） | ✅ `-std=c++98` 下零 error，`make test` 四个测试全部运行通过 |
| `make -C stlib` | ✅ 产出 `libst.a` / `libst.so` |
| 根 `make` / `make clean` | ✅ 可用；`make clean` 后 `git status --porcelain` 为空 |
| `src/` → `libmthread` | ❌ 编译不过。`src/makefile` 的源文件清单已修好，会走到编译期才失败 |
| `app/`、`tests/` | ❌ 依赖 `src/`，无法验证 |

`src/` 剩下的问题（**不是你引入的**）：

- 新旧两套命名混杂（`Thread` vs `StThread`、`StEventSuper` vs `StEventItem`…），旧名**全都没有定义** → `plan/02`
- `st_manager.h` 被 `src/st_connection.cc` 与 `app/st_c.h` include，但这个文件**不存在** → `plan/02`
- `stlib/st_epoll.h` 有两处硬错误（`m_file_` 当结构体用、`m_file_events_` 未声明） → `plan/03`
- `tests/Makefile` 的 12 个 target 里 11 个引用了不存在的源文件 → `plan/04`

**因此**：如果你在 `src/`、`app/`、`tests/` 里的改动「编译不过」，请先确认是不是本来就编译不过。只有 `stlib/` 的基线是绿的，请不要把它弄红。

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

协程基于 ucontext + 手写汇编（`stlib/ucontext/asm.S`，含 386 / amd64 / mips / power 分支，**没有 arm64**），源自 Russ Cox 的 libtask（MIT，许可见根目录 [`COPYRIGHT`](COPYRIGHT)）。

Linux 上走的是 `USE_UCONTEXT 1`，即 glibc 的 `getcontext` 家族，`ucontext.c` 与 `asm.S` 展开为空目标文件；只有 `__APPLE__` 分支才用自带汇编。

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

具体包含哪些目标文件见下面的「libmthread 产物定义」。

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
| `app/st_c.*`、`app/st_sys.*` | **注意：这两组是库代码，不是示例**（对外 C API 与 syscall hook），已被 `src/makefile` 编进 `libmthread`。它们放在 `app/` 下属目录语义错误，是否物理移入 `src/` 留给 `plan/03`、`plan/04` |
| `tests/` | 框架的单元测试 + `tests/scripts/` 下的 Python 测试 |
| `thirdparty/` | 只剩一份 `readme.md`。gperftools 的 gitlink 已按 D2 移除，改为可选的开发期开关 |
| `plan/` | 改进计划文档 |
| `make.inc` | 公共编译开关，被根 `makefile` 与各子目录 makefile 共同 include |
| `COPYRIGHT` | vendor 进来的第三方代码（libtask、http-parser）的许可与来源。**不是**本仓库自身的 license |

---

## libmthread 产物定义

**这是本仓库唯一的对外交付物，内容必须是明确的。**

- 产物文件：`libmthread.a` 与 `libmthread.so`
- 产物路径：**仓库根目录**（`app/*/makefile` 都写着 `LIBS = ../../libmthread.a`）
- 生成者：`src/makefile`（根 `makefile` 的 `lib` 目标）
- 目标文件清单（`src/makefile` 的 `LIB_O`）：

| 目标文件 | 源文件 | 作用 |
| --- | --- | --- |
| `src/st_thread.o` | `src/st_thread.cc` | 协程、协程调度、事件调度 |
| `src/st_connection.o` | `src/st_connection.cc` | 连接对象 |
| `src/st_sys.o` | `src/st_sys.cc` | 框架内部**带超时**的 IO 封装 |
| `stlib/st_log.o` | `stlib/st_log.cc` | 日志 |
| `stlib/st_test.o` | `stlib/st_test.cc` | 自带的轻量断言 / 测试注册框架 |
| `stlib/ucontext/ucontext.o` | `stlib/ucontext/ucontext.c` | `makecontext` / `swapcontext` 的 C 部分 |
| `stlib/ucontext/asm.o` | `stlib/ucontext/asm.S` | `getmcontext` / `setmcontext` 的汇编 |
| `app/st_c.o` | `app/st_c.cc` | 对外 C 风格 API（`udp_sendrecv` / `tcp_sendrecv` …） |
| `app/st_sys.o` | `app/st_sys.cc` | syscall hook（`socket` / `close` / `read` / `write` …） |

依据与注意点：

- `stlib/*.h` 绝大多数是模板或 inline，只有 `st_log.cc`、`st_test.cc` 会产出目标文件
- Linux 上 `ucontext.c` 与 `asm.S` 展开为**空目标文件**（`USE_UCONTEXT 1`，走 glibc 的 `getcontext` 家族）；`__APPLE__` 分支才用自带汇编
- `app/st_c.cc` 与 `app/st_sys.cc` **在语义上是库代码，不是示例**。readme 承诺使用方只链接一个库，所以它们必须进 `libmthread`。当前只在 makefile 层面编进来，**没有做目录搬迁**；是否物理移入 `src/` 留给 `plan/03`、`plan/04`
- 因此归档里会出现两个基名相同的成员（`src/st_sys.o` 与 `app/st_sys.o`）。`ar`/`ld` 允许重名成员，但这两个文件用**相同的名字**声明了**签名不同**的 `extern "C"` 符号（决策点 D4），链接期必然冲突 → `plan/03` 必须先拆前缀
- 依赖的系统库：`-lpthread -ldl`（`ST_LDLIBS`）。**不得**出现任何第三方库

`libmthread` 自带 `stlib` 的目标文件，因此**不依赖** `stlib/libst.a`——`libst.a` 只是 `stlib` 自己的独立产物，给 `tests/` 用。

---

## 构建

根 `makefile` 已存在。

```bash
make               # = make stlib（当前阶段的绿色基线）
make stlib         # 产出 stlib/libst.a 与 stlib/libst.so
make stlib-tests   # 编译 stlib/tests 的四个测试
make test          # 编译并运行 stlib/tests 的四个测试
make lib           # 产出 libmthread.a / libmthread.so（到仓库根目录）
make apps          # 编译 app/ 下的三个示例
make tests         # 编译 tests/ 下的 unittest
make format        # 对本仓库自己的代码跑 clang-format -i
make format-check  # 只检查不改写
make clean
make help          # 目标与开关一览
```

**默认目标当前是 `stlib`，不是 `lib`。** 因为 `src/` 还编不过（`plan/02`、`plan/03`），让 `make` 默认失败没有意义。等 `src/` 打通后，把根 `makefile` 的 `all: stlib` 改成 `all: lib`。

`make lib` / `make apps` / `make tests` 现在会在编译期失败，这是**预期状态**，不是回归。

开关定义在 [`make.inc`](make.inc)，被根 `makefile` 与各子目录 makefile 共同 include：

| 开关 | 默认 | 作用 |
| --- | --- | --- |
| `TRACE=0\|1` | `1` | `-DTRACE`。历史上各 makefile 一律默认开启，为守住「不改变现有行为」这里保持默认开。`TRACE=0` 可关掉 `LOG_TRACE` 的大量输出 |
| `DEBUG=0\|1` | `1` | `-g2` |
| `ASAN=0\|1` | `0` | `-fsanitize=address`。**与协程自定义栈切换配合不佳**（需要 `__sanitizer_start_switch_fiber` 之类的标注），会有误报，所以默认关 |
| `TCMALLOC=0\|1` | `0` | `-ltcmalloc`，需自行安装 gperftools，见 [`thirdparty/readme.md`](thirdparty/readme.md) |
| `PROFILER=0\|1` | `0` | `-lprofiler`，同上 |
| `ARCH=32\|64` | `64` | `-m32` / `-m64`。只在 x86 上追加（arm64 的 clang 会直接报错） |

用法：`make ASAN=1 TRACE=0`、`make TCMALLOC=1 PROFILER=1`。开关会自动透传给子目录的 make。

`make.inc` 同时统一注入 `-std=c++98` 与唯一的 include 根，**不要在子 makefile 里再写死这两样**。

### include 路径规范

编译期只提供**仓库根**一个 include 根（`-I.` / `-I..`，由 `make.inc` 的 `ST_INC` 统一给出）。

- 跨目录引用：从仓库根出发写全路径，`#include "stlib/st_def.h"`、`#include "src/st_thread.h"`
- 同目录引用：裸文件名，`stlib/st_heap.h` 里写 `#include "st_util.h"`（引号形式会先在当前文件所在目录找，不需要额外 `-I`）
- **禁止** `-I./include` 之类指向不存在目录的路径（`include/` 目录根本不存在）
- **禁止** `../include/...` 这类 `..` 穿越写法（`tests/` 里还有一整组，属 `plan/04`）

核对「被引用但不存在」的头文件全集：

```bash
grep -rhno '#include "[^"]*"' src app stlib tests \
     --include=*.h --include=*.cc --include=*.cpp --include=*.c \
  | sed 's/.*#include "//;s/"//' | sort -u \
  | while read p; do
      [ -n "$(find . -path ./thirdparty -prune -o -path "*$p" -print | head -1)" ] \
        || echo "MISSING: $p"
    done
```

`stlib/` 与 `src/st_poll.h` 范围内该脚本已无输出。`app/`、`tests/` 仍有（`st_manager.h`、`../include/st_*.h`），属 `plan/02`、`plan/04`。

---

## 代码风格

本仓库的 style 是 **LLVM 基线 + `m_x_` 成员命名**。**不是 Google C++ Style**——[决策点 D1](plan/README.md#需用户确认的决策点) 已拍板保留现状，不切 Google。任何地方再看到「Google Style」的措辞都是过时描述。

**`.clang-format` 是唯一权威**：`# BasedOnStyle: LLVM`、`AccessModifierOffset: -2`、`PointerAlignment: Right`、`ColumnLimit: 80`、`IndentWidth: 2`、`UseTab: Never`、`Standard: Cpp03`。

`Standard: Cpp03` 不只是排版偏好：它会把嵌套模板的连续右尖括号拆成 `> >`，而 `Instance<StBufferPool<>>()` 在 `-std=c++98` 下本来就是语法错误。

提交前跑：

```bash
make format-check          # 只检查（--dry-run --Werror）
make format                # 就地格式化
clang-format -i <单个文件>  # 也可以只格式化你改过的文件
```

格式化结果与 clang-format 版本强相关，CI 钉的是 **clang-format-18**（`make format-check CLANG_FORMAT=clang-format-18`）。

`make format` / `make format-check` 的范围是根 `makefile` 的 `FORMAT_SRC`，即**本仓库自己写的代码**：`stlib/*.{h,cc}`、`src/*.{h,cc}`、`stlib/tests/*.cc`。这个范围目前 100% 干净，请保持。

**不在范围内**（不要顺手格式化，会淹没真实改动）：

- vendor 代码：`stlib/ucontext/`、`stlib/tests/ucontext/`、`app/st_wrk/http_parser.*`
- 尚未整理的历史代码：`app/`、`tests/`、`stlib/tiny/`（偏差从数十到数千处，属 `plan/04`、`plan/05`）

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

1. **先读 [`plan/README.md`](plan/README.md)**，特别是「决策点」一节。6 个决策点里 **D1（style）、D2（gperftools）、D5（Bazel）、D6（第三方许可）已拍板并落地**；**D3（新旧命名 / namespace）阻塞 `plan/02`，D4（`extern "C"` 符号拆分）阻塞 `plan/03`**，动手前先确认
2. **按阶段顺序推进**：01 基础设施 → 02 协程调度 → 03 IO 与网络 → 04 apps/tests → 05 文档。每阶段有明确的出口条件，未达标不要进下一阶段
3. **不要提交二进制与产物**：`.DS_Store`、`app/st_dns/main`、`app/st_wrk/wrk`、`tests/st_server_unittest`、`app/st_dns/test.log` 曾被误提交，`plan/01` 已 `git rm --cached` 并补齐 `.gitignore`。提交前确认 `make && make clean` 后 `git status --porcelain` 为空。唯一有意保留的二进制是 `app/st_memcacheclient/memcache.pcap`（抓包样本，`plan/04` 验证协议时要用）
4. **双平台都要验证**：Linux（epoll）+ macOS（kqueue）。CI 已覆盖 `ubuntu-latest` + `macos-latest`（见 `.github/workflows/ci.yml`），但 macOS 上只有 stlib 的四个测试是硬门禁。`asm.S` **没有 arm64 分支**，且 `stlib/ucontext/ucontext.h` 在非 i386/x86_64 的 `__APPLE__` 下会落到 `ucontext-power.h`（PowerPC），所以 `make stlib` 在 Apple Silicon 上编不过 → `plan/02`
5. **改 `stlib/st_epoll.h` 或 `st_kqueue.h` 时，两个文件一起改**（接口必须一致）
6. **如果 app 或 test 必须改业务逻辑才能编译**，那是库的 API 被改坏了 → 回头修库，不要改使用方。唯一例外是 include 路径与已授权的改名
7. **`// TODO:` 标记请保留**，它们标示真实的未完成项（如 `src/st_thread.h` 的「TODO: 回收sthread」——协程对象当前确实不回收）
8. **格式化与功能改动分开提交**。全量 `clang-format` 会产生巨大 diff，应独立成一个纯格式提交并登记到 `.git-blame-ignore-revs`
9. **写文档前先让代码跑起来**。`readme.md` 当前通篇是已不存在的旧 API（`mt_init_frame` / `Frame` / `IMtAction` / `IMtActionServer`…），照着做必然失败。不要重复这个错误——文档只写验证过的事实

---

## 已知问题速查

`plan/01` 已修掉的（保留在这里是为了让你知道它们已经不是问题）：

| 问题 | 位置 | 状态 |
| --- | --- | --- |
| `ucontext/st_def.h` 路径笔误（让整个 `stlib` 全线不可编译） | `stlib/` 6 个头文件 | ✅ 已修 |
| `__THREAD` 宏未定义 | `stlib/st_singleton.h` | ✅ 已改为 `__thread` |
| `src/st_poll.h` 三处 `stlib/ucontext/st_*.h` 路径错误 | `src/st_poll.h` | ✅ 已修 |
| `libmthread` 无法产出（`LIB_O` 的 10 个 `mt_*.o` 源文件全缺、`-I./include` 指向不存在的目录） | `src/makefile` | ✅ 源文件清单与产物路径已修好（**链接仍阻塞于 `plan/02`、`plan/03`**） |
| `stlib/makefile` 的 `LIBO` 指向不存在的 `st_ucontext.o` / `st_asm.o` | `stlib/makefile` | ✅ 已修，产出 `libst.a` / `libst.so` |
| 根目录没有 makefile（readme 承诺的 `make` 无从执行） | 根目录 | ✅ 已新增 |
| `.clang-format` 声明 Google 实为 LLVM，且 `Standard: Latest` 与 C++98 冲突 | `.clang-format` | ✅ D1 已拍板：LLVM + `m_x_`，`Standard: Cpp03` |
| ASan / tcmalloc / profiler 默认开启 | `stlib/tests/Makefile`、`tests/Makefile` | ✅ 一律默认关闭，改为开关 |
| `thirdparty/gperftools` 是 gitlink 但无 `.gitmodules` | `thirdparty/` | ✅ D2 已移除 gitlink |
| 半成品 Bazel（`WORKSPACE`、`BUILD`、0 字节 `build.bzl`） | 根目录、`stlib/` | ✅ D5 已移除 |
| 已跟踪的 `.DS_Store` 与编译产物 | 多处 | ✅ 已 `git rm --cached` + 补 `.gitignore` |
| 源码头部写 `see COPYRIGHT` 但无该文件 | `stlib/ucontext/` 等 | ✅ D6 已补 `COPYRIGHT` |

仍然存在的：

| 问题 | 位置 | 详见 |
| --- | --- | --- |
| `src/` 无法编译（重命名重构中间态） | `src/`、`app/`、`tests/` | `plan/README.md` |
| `st_manager.h` 被 include 但文件不存在 | `src/st_connection.cc`、`app/st_c.h` | `plan/02` |
| `context_switch` / `context_exit` / `STACK` / `Stack` / `eThreadType` 无定义 | `src/st_thread.h`、`src/st_poll.h` | `plan/02` |
| `stlib/st_test.h` 的 `StTester` 断言失败时 `exit(0)`，退出码无法作为通过/失败依据 | `stlib/st_test.h` | `plan/02` |
| `stlib/ucontext` 没有 arm64 分支，Apple Silicon 上落到 PowerPC 分支 | `stlib/ucontext/ucontext.h`、`asm.S` | `plan/02` |
| `StThread` 重复定义 | `src/st_thread.h` + `app/thread.h` | `plan/02` |
| epoll 的 `AddEvent` 从不更新 mask（事件注册根本失效） | `stlib/st_epoll.h` | `plan/03` |
| 两套同名不同签名的 `extern "C" __*` 符号 | `src/st_sys.h` vs `app/st_sys.h` | `plan/03` |
| `src/st_sys.cc` 与 `st_sys.h` 有 8 处函数名不一致 | `src/st_sys.*` | `plan/03` |
| `eTCP_KEEPLIVE_CONN` / `eUDP_UDPSESSION_CONN` 的值为 0（`&` 应为 `\|`），keepalive 完全失效 | `src/st_public.h` | `plan/03` |
| `Keeplive()` 硬编码 `return false` 且非虚 | `src/st_connection.h` | `plan/03` |
| `tests/Makefile` 12 个 target 里 11 个源文件缺失 | `tests/Makefile` | `plan/04` |
| 协程对象不回收 | `src/st_thread.h` | `plan/02` |
| `readme.md` 全是已不存在的 API（含「到当前目录下执行 `make`」之外的构建说明） | `readme.md` | `plan/05` |
| 本仓库自身没有 LICENSE（第三方成分的许可已在 `COPYRIGHT` 里说明，自身 license 待仓库所有者决定） | 根目录 | `plan/05` |
| `app/`、`tests/`、`stlib/tiny/` 与 vendor 代码不在 `make format-check` 范围内 | 多处 | `plan/04`、`plan/05` |
