# 01 · 基础设施：构建 / 目录 / C++98 / 代码风格 / libmthread

> 阶段目标一句话：**让 `stlib/` 成为可编译、可运行、双平台、C++98 干净的绿色基线，并定义清楚 `libmthread` 到底是什么。**

## 硬约束（本阶段同样适用）

1. 不改变现有功能行为；对外 API 语义兼容。基准为「代码表达的语义意图」，详见 [`README.md`](README.md#对不改变现有功能行为这条约束的必要澄清)。
2. C++98。禁止 C++11+ 语言特性；`__thread` / `__builtin_expect` 等 GNU 扩展可用（现有代码已在用）。
3. Linux + macOS 双平台可编译。
4. Google C++ Style（`.clang-format` 已存在，但内容与声明矛盾 → 见决策点 D1）。
5. 零第三方运行时依赖。
6. 协程调度 + epoll/kqueue；业务同步写法、框架内部异步；非阻塞 TCP/UDP 客户端；链接 `libmthread.a`/`.so` 即可使用。

---

## 1. 目标

1. `stlib/` 的四个测试（`st_buffer_test`、`st_hash_list_test`、`st_heap_test`、`st_heap_timer_test`）在 Linux 与 macOS 上都能编译并运行通过。
2. 建立**根目录 `makefile`**，使 readme 承诺的「到当前目录下执行 `make`」真正成立。
3. `libmthread.a` / `libmthread.so` 有明确定义：包含哪些目标文件、导出哪些符号、依赖哪些系统库。
4. 统一 include 路径规范，消灭「头文件路径笔误」这一类问题的**根因**，而不只是逐个修。
5. 解决 `.clang-format` 与「Google Style」声明的矛盾（D1），并把格式化动作与功能改动隔离。
6. 清理仓库里已跟踪的构建产物与 `.DS_Store`，补 `.gitignore`。

## 2. 现状（点名真实文件与实测结论）

### 2.1 `stlib/` 只差两个修复就能编译 —— 这是本阶段最重要的事实

实测过程（Linux / g++ 13.3）：

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 1 | `g++ -fsyntax-only -I. -Istlib stlib/tests/st_heap_timer_test.cc` | `fatal error: ucontext/st_def.h: No such file or directory` |
| 2 | 用 include 垫片把 `ucontext/st_def.h` 指到真实的 `stlib/st_def.h` | `error: '__THREAD' does not name a type`（`st_singleton.h:74`，另见 45、79 行） |
| 3 | 追加 `-D__THREAD=__thread` | **4 个测试全部 0 error** |
| 4 | 步骤 3 加上 `-std=c++98` | **仍然 0 error** |

两个根因：

**(a) `ucontext/st_def.h` 路径错误。** 真实文件在 `stlib/st_def.h`，但被这样引用：

- `stlib/st_closure.h:8` → `#include "ucontext/st_def.h"`
- `stlib/st_util.h:22` → `#include "ucontext/st_def.h"`
- `stlib/st_log.h:4` → `#include "ucontext/st_def.h"`

由于 `st_util.h` 是 `stlib` 几乎所有头文件的共同依赖，这一处笔误让**整个 `stlib` 全线不可编译**。

**(b) `__THREAD` 宏从未定义。** 在 `stlib/st_singleton.h` 中被使用三次：

```
45:    static __THREAD T t;
74:  static __THREAD T *m_pinstance_;
79:  template <typename T> __THREAD T *Singleton<T>::m_pinstance_ = NULL;
```

全仓库 grep 不到任何 `#define __THREAD`。从用法看意图无歧义：应为 GNU 的 TLS 关键字 `__thread`。注意 `Singleton<T>` 是**线程局部单例**（配合 `pthread_key_create` 的 `Deleter` 做析构），这一点对理解 `Instance<T>()` 的语义很关键——每个 OS 线程一份实例。

### 2.2 `src/makefile` 无法产出 `libmthread`

`src/makefile` 的 `LIB_O` 列了 10 个目标文件：

```
LIB_O = mt_action.o mt_asm.o mt_c.o mt_connection.o mt_core.o \
        mt_ext.o mt_sys_hook.o mt_thread.o mt_ucontext.o mt_utils.o
```

实测：`src/` 下 `mt_action.*` … `mt_utils.*` **10 个源文件一个都不存在**。`src/` 的真实内容是：

```
src/st_connection.cc  src/st_sys.cc  src/st_thread.cc
src/st_connection.h   src/st_poll.h  src/st_public.h
src/st_server.h       src/st_sys.h   src/st_thread.h
src/makefile
```

同时 `INC = -I./include`，而**仓库根本没有 `include/` 目录**（`ls -d include` 失败）。

结论：`libmthread.a` / `libmthread.so`——readme 里唯一的交付物、也是 `app/*/makefile` 里 `LIBS = ../../libmthread.a` 指向的东西——**当前完全无法生成**。

### 2.3 `stlib/makefile` 同样指向不存在的文件

```
LIBO = st_ucontext.o st_log.o st_test.o st_asm.o
INC  = -I. -I./ucontext
```

实测：`st_log.cc` 与 `st_test.cc` 存在；`st_ucontext.*` 与 `st_asm.*` **不存在**。真实对应物在 `stlib/ucontext/` 下，名为 `ucontext.c` 与 `asm.S`。

注意 `-I./ucontext` 这个 include 路径的存在，说明 2.1(a) 的 `ucontext/st_def.h` 写法可能源自一次目录搬迁：`st_def.h` 曾在 `stlib/ucontext/` 下。这佐证了「路径笔误」的判断。

### 2.4 `stlib/tests/Makefile` 与真实测试文件基本对得上

这是仓库里**唯一状态较好**的 makefile。四个 target（`stlib_hash_list`、`stlib_buffer`、`stlib_heap`、`stlib_heap_timer`）对应的 `.cc` 文件都存在，编译命令也合理。只有两个小问题：

- `USE_TCMALLOC` / `USE_PROFILER` 已被注释掉（好事，符合零依赖约束）
- `-fsanitize=address` 默认开启。ASan 与协程自定义栈切换配合较差（ASan 需要 `__sanitizer_start_switch_fiber` 之类的标注才能正确追踪栈切换），在 02/03 阶段会成为噪声源

### 2.5 其它头文件路径笔误（同类根因）

除 2.1(a) 外，还有两组：

**`src/st_poll.h` 引用了不存在的 `stlib/ucontext/` 下的头文件：**

| 引用（`src/st_poll.h`） | 真实位置 |
| --- | --- |
| `stlib/ucontext/st_ucontext.h` | `stlib/ucontext/ucontext.h` |
| `stlib/ucontext/st_kqueue.h` | `stlib/st_kqueue.h` |
| `stlib/ucontext/st_epoll.h` | `stlib/st_epoll.h` |

**`stlib/ucontext/ucontext.h` 引用了带反向命名的架构头：**

| 引用 | 真实文件 |
| --- | --- |
| `386-ucontext.h` | `ucontext-386.h` |
| `amd64-ucontext.h` | `ucontext-amd64.h` |
| `mips-ucontext.h` | `ucontext-mips.h` |
| `power-ucontext.h` | `ucontext-power.h` |
| `sparc-ucontext.h` / `ucontext-sparc.h` | **两个都不存在**（SPARC 无支持） |

`tests/` 下还有一整组 `../include/st_*.h` 引用（`st_base.h`、`st_c.h`、`st_connection.h`、`st_manager.h`、`st_netaddr.h`、`st_server.h`、`st_singleton.h`、`st_sys.h`、`st_thread.h`、`st_util.h`、`ucontext/st_ucontext.h`），对应 2.2 里不存在的 `include/` 目录。这些属于 04 阶段的范围，但**根因同一个**：没有统一的 include 根。

### 2.6 `.clang-format` 是 LLVM 基线，不是 Google

实测关键键值：

```
3:  # BasedOnStyle:  LLVM
4:  AccessModifierOffset: -2        # Google 为 -1
60: ColumnLimit:     80             # 与 Google 一致
68: DerivePointerAlignment: false   # Google 为 true
107:IndentWidth:     2              # 与 Google 一致
133:PointerAlignment: Right         # Google 为 Left
176:Standard:        Latest         # 与 C++98 约束矛盾
184:UseTab:          Never          # 与 Google 一致
```

且现有代码命名（`m_active_thread_`、`StThreadSchedule`、`HeapPush()`）既有前缀 `m_` 又有尾 `_`，不符合 Google 的成员命名（Google 只要尾 `_`）。**格式与命名两条都与「Google Style」不符**，需 D1 决策。

### 2.7 仓库卫生

`.gitignore` 现有内容：

```
**/scp.sh
**/*.a
**/*.so
**/*.o
**/libtask
**/*_test
**/bazel-*
```

但 `git ls-files` 显示以下文件**已被跟踪**：

| 文件 | 性质 | `.gitignore` 是否覆盖 |
| --- | --- | --- |
| `.DS_Store` | macOS 垃圾 | 否 |
| `stlib/.DS_Store` | macOS 垃圾 | 否 |
| `app/st_dns/main` | 编译产物（可执行） | 否 |
| `app/st_wrk/wrk` | 编译产物（可执行） | 否 |
| `tests/st_server_unittest` | 编译产物（可执行） | 否（规则是 `*_test`，不匹配 `*_unittest`） |
| `app/st_dns/test.log` | 运行日志 | 否 |
| `app/st_memcacheclient/memcache.pcap` | 抓包样本 | 否（可能是有意保留的测试素材） |

注意：即使 `.gitignore` 写了规则，**已跟踪文件不受 `.gitignore` 影响**，必须 `git rm --cached`。

### 2.8 Bazel 残留

- `WORKSPACE`：仅 `workspace(name = "sthread")` 一行
- `stlib/ucontext/BUILD`：唯一的 BUILD 文件，`glob` 源码 + 一个 `cc_test(ucontext_test)`
- `stlib/tests/build.bzl`：**0 字节空文件**
- `src/`、`app/`、`tests/`：无 BUILD

→ 决策点 D5。

## 3. 问题与风险

| # | 问题 | 影响 | 缓解 |
| --- | --- | --- | --- |
| P1 | include 路径笔误散落多处，逐个改容易漏 | 反复返工 | 不逐个改，**先定 include 根规范**，再一次性核对（见 4.2 步骤 2） |
| P2 | `libmthread` 无法产出，导致 03/04 无法验证 | 阻塞下游 | 本阶段先把「产物定义」写死，03 阶段落地 |
| P3 | macOS 在本环境无法验证（R4） | 双平台约束落空 | 本阶段就引入 macOS 验证手段，不拖到 04 |
| P4 | 全量 `clang-format` 产生巨大 diff（R7） | 淹没真实改动 | 纯格式提交单独隔离 + `.git-blame-ignore-revs` |
| P5 | `-fsanitize=address` 与协程栈切换冲突 | 误报噪声 | 改为默认关闭的可选开关 |
| P6 | `git rm --cached` 会让本地仍有这些文件的协作者困惑 | 低 | 在 PR 描述里说明 |
| P7 | `__thread` 在 macOS 老版本 clang 上支持有限 | 中 | 若 macOS 报错，退化为 `pthread_getspecific`（属行为等价重构，需在 PR 说明） |

## 4. 做 / 不做

### 做

- 修 `ucontext/st_def.h` → `st_def.h`（3 处）
- 定义 `__THREAD`（或直接替换为 `__thread`）
- 修 `src/st_poll.h` 的 3 个 `stlib/ucontext/*` 路径
- 修 `stlib/ucontext/ucontext.h` 的 4 个架构头名字，并处理不存在的 SPARC 分支
- 新增根 `makefile`
- 重写 `src/makefile` 的 `LIB_O` 与 `INC`
- 修 `stlib/makefile` 的 `LIBO`
- 统一 include 根规范
- 定义 `libmthread` 产物内容
- 按 D1 结论处理 `.clang-format`
- 补 `.gitignore`，`git rm --cached` 已跟踪产物
- 按 D5 结论处理 Bazel 残留
- `-fsanitize=address`、`-ltcmalloc`、`-lprofiler` 改为默认关闭的开关

### 不做（明确排除，避免范围蔓延）

- **不**引入 CMake / Meson / 重写为 Bazel 一等公民（超出「仅修复」范围；如需请另立计划）
- **不**改任何类名、函数名、成员名（属 02 阶段 + D3）
- **不**改 `MEM_PAGE_SIZE`（2048）、`ST_MAX_FD`、`ST_RECV_BUFFSIZE`/`ST_SEND_BUFFSIZE`（8192）、各类默认超时（30000ms / 1000ms）等常量——即使可疑，改了就是改行为
- **不**改 `Singleton<T>` 的线程局部语义
- **不**删 `stlib/tiny/`、`stlib/tests/ucontext/`（待 D6）
- **不**动 `stlib/st_epoll.h` / `st_kqueue.h` 的实现 bug（属 03 阶段）
- **不**碰 `src/*.cc` 的函数体（属 02/03 阶段）

## 5. 分步步骤

### 步骤 1：确立 include 根规范（**先做这个，它是 P1 的根治手段**）

规范（建议）：

- 编译时只提供**一个** include 根：仓库根目录（`-I.`）
- 所有跨目录引用一律写成从仓库根出发的路径：`#include "stlib/st_def.h"`、`#include "src/st_thread.h"`
- 同目录内引用可用裸文件名：`stlib/st_heap.h` 里写 `#include "st_util.h"` —— **但**这要求同时提供 `-Istlib`，会带来歧义。更稳的做法是同目录也用全路径
- 禁止 `-I./include` 之类指向不存在目录的路径
- 禁止 `..` 相对路径穿越（`tests/` 里的 `../include/...` 全部按此规范重写，04 阶段执行）

出口：规范写入 `AGENTS.md`（05 阶段落地）与本文件，后续所有 include 修复都按它执行。

### 步骤 2：一次性核对所有 include（机械动作，可脚本化）

用如下方式列出「被引用但不存在」的头文件全集，避免漏改：

```bash
grep -rhno '#include "[^"]*"' src app stlib tests \
     --include=*.h --include=*.cc --include=*.cpp --include=*.c \
  | sed 's/.*#include "//;s/"//' | sort -u \
  | while read p; do
      [ -n "$(find . -path ./thirdparty -prune -o -path "*$p" -print | head -1)" ] \
        || echo "MISSING: $p"
    done
```

本阶段只修 `stlib/` 与 `src/st_poll.h`、`stlib/ucontext/ucontext.h` 三组；`tests/` 与 `app/` 的留给 04。

**出口**：`stlib` 范围内该脚本无 MISSING 输出。

### 步骤 3：修 `__THREAD`

两个等价选项，二选一并保持全库一致：

- (a) 在 `stlib/st_def.h` 增加 `#define __THREAD __thread`（改动最小，保留现有写法）
- (b) 把 `st_singleton.h` 里 3 处 `__THREAD` 直接替换为 `__thread`（更直白，少一层宏）

倾向 (b)：少一个自定义宏，且 `__THREAD` 这个名字本身是保留标识符风格。但若 P7（macOS 兼容）成为问题，(a) 更便于集中加平台分支——届时可回到 (a)。

**出口**：`g++ -std=c++98 -fsyntax-only -I. stlib/tests/*.cc` 零 error。

### 步骤 4：验证 stlib 绿色基线（编译 + 运行）

```bash
cd stlib/tests && make stlib_hash_list && ./st_hash_list_test
make stlib_buffer     && ./st_buffer_test
make stlib_heap       && ./st_heap_test
make stlib_heap_timer && ./st_heap_timer_test
```

注意：`stlib/tests/Makefile` 当前 `FLAGS` 含 `-fsanitize=address`。先按原样跑一遍（记录基线），再按步骤 8 改为可选。

`stlib/st_test.h` / `st_test.cc` 是仓库自带的轻量断言框架，运行结果以它的输出为准——**本阶段需先确认它的通过/失败判定方式和退出码**，否则「测试通过」无法自动化判定。

**出口**：4 个测试编译通过且运行通过；`echo $?` 为 0。

### 步骤 5：修 `stlib/makefile`

- `LIBO`：`st_ucontext.o` → 由 `ucontext/ucontext.c` 产出；`st_asm.o` → 由 `ucontext/asm.S` 产出；保留 `st_log.o`、`st_test.o`
- 由于源文件在子目录，现有的 `%.o: %.c` 模式规则需要能匹配 `ucontext/ucontext.c`（产出 `ucontext/ucontext.o`），相应调整 `LIBO` 写法为带目录前缀
- `INC` 按步骤 1 规范调整为 `-I..`（仓库根）
- 补 `.PHONY: all clean`
- `all` 目标当前有 `@rm -rf *.o`，会删掉刚生成的中间产物——保留此行为（不改行为），但注意它使增量编译失效

**出口**：`make -C stlib` 产出 `libst.a` 与 `libst.so`。

### 步骤 6：重写 `src/makefile` 并定义 `libmthread` 产物

产物定义（建议，需在 03 阶段确认完整性）：

```
libmthread = src/st_thread.o
             src/st_connection.o
             src/st_sys.o
             stlib/st_log.o
             stlib/st_test.o
             stlib/ucontext/ucontext.o
             stlib/ucontext/asm.o
```

依据：
- `src/` 的三个 `.cc` 是框架实现主体
- `stlib/` 的 `st_log.cc`、`st_test.cc` 是唯二的非头文件实现（其余 `stlib/*.h` 是 header-only 模板）
- `ucontext.c` + `asm.S` 提供 `getmcontext`/`setmcontext`/`swapcontext`/`makecontext`，是协程切换的底座

待确认项（留给 03）：
- `app/st_sys.cc`（syscall hook）与 `app/st_c.cc`（C 风格便捷 API）**是否应进 `libmthread`**？从 readme「只需引入一个 libmthread.a」和 `app/st_c.h` 暴露 `udp_sendrecv`/`tcp_sendrecv` 来看，**应该进**。但它们当前在 `app/` 下，与三个示例 app 混放。这涉及目录调整 → 记为本阶段**遗留决策**，见第 9 节。
- `src/st_thread.h` 里 `context_switch()` / `context_exit()` 被调用，但全仓库 grep 不到声明或定义（`stlib/ucontext/ucontext.h` 只提供 `getmcontext`/`setmcontext`/`swapcontext`/`makecontext`）。这是 02 阶段的核心问题，但会直接影响 `libmthread` 能否链接成功。

同时：
- `INC = -I./include` → 改为 `-I..`
- 补 `.PHONY`
- `C_ARGS` 加 `-std=c++98`（落实硬约束 2）
- 产物路径：`app/*/makefile` 里写的是 `LIBS = ../../libmthread.a`，即期望产物在**仓库根目录**，而 `src/makefile` 生成在 `src/` 下。需统一（建议根 makefile 负责把产物放到根目录，或改 app 的路径——后者改动更小但涉及 04）

**出口**：`make -C src` 至少能走到链接阶段（能否成功取决于 02/03，本阶段不强求成功）。

### 步骤 7：新增根 `makefile`

readme 明确写「编译.a或者.so，到当前目录下执行 `make`」，但**根目录没有 makefile**。新增：

```
.PHONY: all stlib lib apps tests clean format
all: lib
stlib:  ; $(MAKE) -C stlib
lib:    stlib ; $(MAKE) -C src
apps:   lib   ; $(MAKE) -C app/st_dns && $(MAKE) -C app/st_memcacheclient && $(MAKE) -C app/st_wrk
tests:  lib   ; $(MAKE) -C tests
clean:  ; $(MAKE) -C stlib clean; $(MAKE) -C src clean; ...
```

并提供统一的编译开关（默认全关，落实约束 5）：

| 开关 | 默认 | 作用 |
| --- | --- | --- |
| `DEBUG=1` | 关 | `-g -DTRACE`（注意现有 makefile 默认开着 `-DTRACE`，会输出大量 `LOG_TRACE`） |
| `ASAN=1` | 关 | `-fsanitize=address`（P5） |
| `TCMALLOC=1` | 关 | `-ltcmalloc`（D2） |
| `PROFILER=1` | 关 | `-lprofiler`（D2） |

注意 `-DTRACE` 当前在 `src/makefile`、`stlib/makefile`、`tests/Makefile`、`app/st_dns/makefile`、`app/st_memcacheclient/makefile` 里都是**默认开启**的（只有 `app/st_wrk/makefile` 注释掉了）。把它改为默认关闭**会改变可观测输出**——严格说属于行为变化。建议：保持默认开启以守住约束 1，仅新增 `TRACE=0` 关闭开关，并在 05 阶段的 readme 里说明。

**出口**：根目录 `make` 有意义且不报「No targets」。

### 步骤 8：双平台验证（P3 / R4）

本阶段必须解决「macOS 怎么验证」，否则约束 3 只是纸面约束。可选手段：

- (a) CI（GitHub Actions）加 `macos-latest` + `ubuntu-latest` 两个 job，只跑 `make` 与 `stlib` 测试。**推荐**：一次性投入，长期收益，且能守住后续所有阶段。
- (b) 维护者本地 macOS 手动验证 + 在 PR 勾选清单。
- (c) 仅静态检查 macOS 分支（读代码），不实际编译。**不可接受**，因为 kqueue 路径有实测 bug（见 03）。

同时注意 macOS 的两个特殊点：
- `stlib/ucontext/ucontext.h` 在 `defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_5)` 时把 `USE_UCONTEXT` 置为 0，即**不用系统 ucontext，走自带 asm 实现**。这条分支在现代 macOS 上必然命中，且完全未验证。
- `-fsanitize=address` 与 Apple clang 的组合、`-m64 -pthread` 在 clang 下的行为需确认。

**出口**：两个平台都有可复现的编译+测试记录。

### 步骤 9：代码风格落地（D1）

按 D1 结论执行。若选 (a)（推荐，保留 LLVM 基线）：

1. 把 `.clang-format` 的 `Standard: Latest` 改为 `Cpp03`（唯一必要改动，消除与 C++98 的矛盾）
2. 修正第 3 行注释 `# BasedOnStyle: LLVM` → 保持，但在 `AGENTS.md` 与 readme 中把措辞统一为「LLVM 基线 + `m_x_` 成员命名」，不再声称 Google
3. 跑 `clang-format --dry-run --Werror` 摸清当前偏差量

若选 (b)（切 Google）：格式化**必须独立成一个纯格式提交**，且：

```bash
git log -1 --format=%H > /tmp/fmt_sha     # 记录该提交
# 写入 .git-blame-ignore-revs
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

**出口**：`clang-format --dry-run --Werror` 全库通过；`.clang-format` 与文档声明一致。

### 步骤 10：仓库卫生

```bash
# 1) 补 .gitignore
cat >> .gitignore <<'EOF'
.DS_Store
**/.DS_Store
**/*_unittest
**/*.dSYM
app/st_dns/main
app/st_memcacheclient/main
app/st_wrk/wrk
*.log
EOF

# 2) 取消跟踪已提交的产物
git rm --cached .DS_Store stlib/.DS_Store
git rm --cached app/st_dns/main app/st_wrk/wrk tests/st_server_unittest
git rm --cached app/st_dns/test.log
```

注意三点：
- `app/st_memcacheclient/memcache.pcap` **不要删**，先确认它是不是有意保留的测试素材（它是抓包样本，可能是 04 阶段 memcache 协议验证的依据）
- `.gitignore` 里 `**/*_unittest` 会同时忽略源文件吗？不会——源文件是 `*_unittest.cpp`，模式 `*_unittest` 不匹配带扩展名的文件。但要小心 `**/*_test` 这条**已存在**的规则：`stlib/tests/st_buffer_test.cc` 的产物是 `st_buffer_test`（匹配，正确），源文件是 `.cc`（不匹配，正确）
- `git rm --cached` 后协作者本地仍有这些文件，需在 PR 描述说明

**出口**：`make && make clean` 后 `git status --porcelain` 为空。

### 步骤 11：Bazel 残留处理（D5）

按 D5 结论执行。若选 (a)（推荐移除）：`git rm WORKSPACE stlib/ucontext/BUILD stlib/tests/build.bzl`，并在 05 阶段 readme 中说明「本仓库使用 make 构建」。`.gitignore` 里的 `**/bazel-*` 可一并移除。

**出口**：不存在半可用的第二套构建系统。

## 6. 兼容策略

| 对象 | 策略 |
| --- | --- |
| 头文件路径 | 修复笔误属「意图无歧义」，但**对外可见**：使用方若曾写 `#include "ucontext/st_def.h"`（几乎不可能，因为那样也编不过）会受影响。风险可忽略 |
| `__THREAD` → `__thread` | 纯内部，不影响 ABI（TLS 存储类不改变符号名） |
| `libmthread` 产物内容 | **会变**（从「无法产出」到「可产出」）。这是修复而非破坏；但一旦发布，后续增删 `.o` 需谨慎 |
| `libmthread` 产物路径 | 需在 `src/` 下还是仓库根？`app/*/makefile` 期望根目录。**以 app 的期望为准**（不改 app） |
| `-std=c++98` 新增 | 可能暴露原先被默认 `-std=gnu++17` 容忍的写法。实测 `stlib` 在 C++98 下 0 error，`src`/`app` 待 02/03/04 确认 |
| `-DTRACE` 默认值 | **保持默认开启**，仅新增关闭开关（见步骤 7） |
| `.clang-format` | `Standard: Latest` → `Cpp03` 不影响代码语义，只影响格式化结果 |
| 常量与默认值 | 一律不改（见「不做」） |

## 7. 验收

- [ ] `stlib/tests` 四个测试在 Linux 编译通过并运行通过（退出码 0）
- [ ] 同上，在 macOS 编译通过并运行通过
- [ ] `-std=c++98` 下 `stlib` 零 error
- [ ] 步骤 2 的 MISSING 脚本在 `stlib/` + `src/st_poll.h` 范围内输出为空
- [ ] `make -C stlib` 产出 `libst.a` / `libst.so`
- [ ] 根目录存在 `makefile`，`make` 与 `make clean` 均可用
- [ ] `src/makefile` 的 `LIB_O` 只引用真实存在的源文件
- [ ] `libmthread` 的产物内容与路径有书面定义（写入本文件与 `AGENTS.md`）
- [ ] `clang-format --dry-run --Werror` 全库通过
- [ ] `.clang-format` 的 `Standard` 与 C++98 约束不矛盾
- [ ] `make && make clean` 后 `git status --porcelain` 为空
- [ ] `.DS_Store` 等垃圾文件不再被跟踪
- [ ] ASan / tcmalloc / profiler 默认关闭，且有开关可开
- [ ] D1 / D2 / D5 已决策并落地

## 8. 依赖与工作量

**前置依赖**：决策点 **D1**（clang-format 基线）、**D2**（gperftools）、**D5**（Bazel）必须先拍板，否则步骤 9/10/11 无法执行。步骤 1～8 不依赖任何决策，可立即开工。

**被依赖**：02、03、04、05 全部依赖本阶段的绿色基线与根 makefile。

**改动面**（按「涉及哪些子系统 / 侵入程度」描述）：

| 子系统 | 文件数 | 侵入程度 |
| --- | --- | --- |
| `stlib/` include 修复 | 3（`st_closure.h`、`st_util.h`、`st_log.h`） | 极低：各改 1 行 |
| `stlib/st_singleton.h` | 1 | 极低：3 处标识符替换 |
| `src/st_poll.h` include | 1 | 极低：3 行 |
| `stlib/ucontext/ucontext.h` | 1 | 低：4 个架构头名 + SPARC 分支处理 |
| makefile | 3 改 + 1 新增 | 中：`LIB_O`/`LIBO` 需按真实文件重写，新增开关体系 |
| `.clang-format` | 1 | 低（选 a）／极高（选 b，全库重排） |
| `.gitignore` + `git rm --cached` | 1 + 6 个文件 | 低，但会动到版本库跟踪状态 |
| CI 配置（若选 8(a)） | 1 新增 | 中：需调通双平台 |

**风险集中点**：步骤 8（macOS 首次验证，可能暴露成片的 kqueue / ucontext 问题）与步骤 9(b)（若选 Google 则产生全库 diff）。

**遗留决策**（本阶段暴露、需在 03 前确认）：`app/st_sys.cc` 与 `app/st_c.cc` 是否移出 `app/` 并纳入 `libmthread`？这关系到「只需链接一个 libmthread」能否成立，也关系到目录语义（`app/` 应只放示例 app）。建议移入 `src/`，但涉及文件移动 → 需用户授权。
