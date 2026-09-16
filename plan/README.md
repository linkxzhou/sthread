# sthread 改进计划 · 总索引

本目录是 sthread 的**文档化改进计划**。本目录下所有文件**只描述计划，不包含任何功能代码变更**。

> 阅读顺序建议：先读本文「关键结论」与「硬约束」，再按「执行顺序」逐份阅读 01～05。

> **进度（2026-09-16）**：`01`～`07` 均已落地并合入 `origin/master`。
> - `01`：stlib 绿基线、根 makefile、D1/D2/D5/D6（COPYRIGHT）
> - `02`：协程调度可编译（PR #4）
> - `03`：IO/网络与 `libmthread`（PR #5）
> - `04`：apps/tests 可编译 + 回归清单
> - `05`：README / AGENTS / 关键类三件套注释
> - `06`：**✅ 已完成** — stlib 清理/优化（Phase 0–4，收尾 `cf62edd`）——[`06`](06-stlib-refactor-cleanup.md)
> - `07`：**✅ 已完成** — src 清理/优化（Phase 0–4，收尾 `ae4a040`/`43bc36f`）——[`07`](07-src-refactor-optimize.md)
>
> 各份 `0x-*.md` 正文含计划原文 + §9 落地记录；**当前状态以各文顶部「状态」行、根 README、AGENTS 为准**。

---

## 关键结论（先看这个）

> **更新（2026-09-16）**：下列「无法编译」结论是写 plan 当时的实测快照。`01`～`07` 落地后仓库已可 `make lib` / 三件套全绿；历史分析仍保留供对照。当前进度见上文「进度」块与 `06`/`07` 文首状态。

在编写本计划前，对仓库做了实际核对（逐文件读取 + `g++ -fsyntax-only` 试编译）。最重要的结论是：

**当前仓库处于一次未完成的重命名重构中间态，整棵源码树无法编译。**

这不是推测，是实测结果：

| 验证动作 | 结果 |
| --- | --- |
| `g++ -fsyntax-only src/st_thread.cc` | 在第一个 include 就失败：`ucontext/st_def.h: No such file or directory` |
| `g++ -fsyntax-only stlib/tests/st_*_test.cc` | 同样在 `ucontext/st_def.h` 失败 |
| 修正该头文件路径后重试 stlib | 失败于 `st_singleton.h:74: '__THREAD' does not name a type` |
| 再定义 `__THREAD=__thread` 后重试 stlib | **4 个 stlib 测试全部语法检查通过，`-std=c++98` 下亦为 0 error** |
| 同样条件下重试 `src/st_thread.cc` | 仍有大量 error（`eThreadType` / `Stack` / `STACK` 未声明、`st_epoll.h` 两处硬错误等） |
| `make -C src`（产出 `libmthread.a/.so`） | 无法产出：`LIB_O` 列的 10 个 `mt_*.o` 对应源文件全部不存在，`-I./include` 指向的 `include/` 目录也不存在 |

推论有三点，直接决定了后续所有计划的排序：

1. **`stlib/` 离可编译只差两个修复**（一个 include 路径 + 一个 `__THREAD` 宏），且已经是 C++98 干净的。它是唯一可以快速锚定为「绿色基线」的部分。
2. **`src/` 是真正的重灾区**，而它恰好是 readme 承诺的唯一交付物（`libmthread.a` / `libmthread.so`）的来源。新旧两套命名混杂在同一批文件里（详见 `02` 与 `03`）。
3. **`app/` 与 `tests/` 全部依赖 `src/`**，因此在 `src/` 恢复之前，它们连「验证」这个动作都无法进行。

### 对「不改变现有功能行为」这条约束的必要澄清

这条约束必须保留，但**不能按字面理解**：当前代码没有可观测的运行时行为可供对齐（编译都不通过，不存在可运行的基线）。

因此本计划统一采用如下口径，并在每份 plan 中复述：

> **基准不是「当前二进制的行为」，而是「当前代码明确表达出的语义意图」。**
> 修复动作只允许做「让代码达成它显然想达成的事」这一类改动；任何会改变设计意图的改动（换数据结构、换调度策略、改接口语义）都不属于修复，必须单独提案。

判定准则（可操作）：

- 允许：`ucontext/st_def.h` → `st_def.h`（路径笔误，意图无歧义）
- 允许：`__THREAD` → `__thread`（GNU TLS 关键字笔误，意图无歧义）
- 允许：`m_file_events_[fd].mask` → `m_file_[fd].mask`（同函数内另一处已用 `m_file_`，意图无歧义）
- **不允许**：把 `StThreadSchedule` 的四条 TAILQ 队列换成别的容器（改变设计）
- **不允许**：调整 `MEM_PAGE_SIZE`、超时默认值、`eConnType` 枚举值（改变行为，即使当前值可疑）
- **需提案**：新旧命名二选一（`Thread` vs `StThread` 等），因为两者都「存在」，意图不唯一 → 见「决策点 D3」

---

## 硬约束（适用于全部 plan，不可协商）

以下六条在 01～05 每份文档中都会复述一遍，便于单独阅读时不丢约束：

1. **不改变现有功能行为；对外 API 语义兼容。** 基准为「代码表达的语义意图」，口径见上一节。
2. **C++98。** 不使用 C++11 及以后的语言特性（`auto`、`nullptr`、右值引用、`>>` 模板闭合、range-for、`std::unique_ptr` 等一律禁止）。注意：`__thread`、`__builtin_expect`、语句表达式属于 GNU 扩展而非 C++11 特性，现有代码已在用，可继续使用。
3. **Linux + macOS 双平台可编译。** Linux 走 epoll，macOS 走 kqueue，分支只允许出现在 `stlib/st_epoll.h` / `stlib/st_kqueue.h` 与少量 `#if defined(__APPLE__)` 处，不得渗透到业务层。
4. **本仓库 style：LLVM 基线 + `m_x_` 成员命名。** `.clang-format` 是唯一权威。~~Google C++ Style~~ —— D1 已拍板保留 LLVM，不切 Google。
5. **零第三方运行时依赖。** 最终 `libmthread.a` / `libmthread.so` 不得链接任何第三方库。`gperftools`（tcmalloc/profiler）与 ASan 只能是可选的开发期开关，默认关闭。
6. **协程调度 + epoll/kqueue；业务层同步写法，框架内部异步。** 非阻塞 TCP/UDP 客户端；使用方只需链接 `libmthread.a` 或 `libmthread.so`。

---

## 执行顺序

严格串行，**每一步以「可编译 / 可运行」为出口条件**，未达标不进入下一步。理由：当前没有任何绿色基线，若并行推进则无法判断新错误由谁引入。

```
01 基础设施 ──→ 02 协程调度 ──→ 03 IO 多路复用与网络 ──→ 04 apps/tests/兼容 ──→ 05 文档与规范
   (stlib 绿)     (src 编译过)      (src 链接过, libmthread 产出)   (端到端可跑)      (对外可用)
```

| 序号 | 文档 | 范围 | 出口条件（Definition of Done） |
| --- | --- | --- | --- |
| 01 ✅ | [`01-foundation-build-style.md`](01-foundation-build-style.md) | 构建、目录、C++98、代码风格、libmthread 产物定义 | `stlib` 四个测试在 Linux + macOS 均编译通过并运行通过；`.clang-format` 与 style 约定一致；仓库无新增二进制 |
| 02 ✅ | [`02-coroutine-scheduler.md`](02-coroutine-scheduler.md) | ucontext、`StThreadItem`/`StThread`、`StThreadSchedule`、`StHeap`/`StHeapTimer` | `src/st_thread.cc` 编译通过；协程 create/yield/sleep/wakeup 单测通过 |
| 03 ✅ | [`03-io-multiplexing-net.md`](03-io-multiplexing-net.md) | `StIOState`(epoll/kqueue)、`StEventSchedule`、`StConnection`、`StServer`、sys hook | `libmthread.a` / `libmthread.so` 实际产出；TCP/UDP 回环收发通过 |
| 04 ✅ | [`04-apps-tests-compat.md`](04-apps-tests-compat.md) | `app/st_dns`、`app/st_memcacheclient`、`app/st_wrk`、`tests/` | 三个 app 编译通过；DNS / HTTP 示例可跑；回归清单全绿 |
| 05 ✅ | [`05-docs-agents-readme.md`](05-docs-agents-readme.md) | `readme.md`、代码注释、`AGENTS.md` | readme 示例与真实 API 一致且可复制运行；`AGENTS.md` 生效 |

---

## 风险总览

按「影响 × 概率」排序。每条风险的详细缓解措施在对应 plan 中展开。

| # | 风险 | 影响 | 概率 | 缓解 | 详见 |
| --- | --- | --- | --- | --- | --- |
| R1 | **新旧命名混杂**导致「修好一处、错开三处」，改动面失控 | 高 | 高 | 先冻结命名映射表并一次性机械替换，不边改边设计 | 02 |
| R2 | **`libmthread` 根本无法产出**：`src/makefile` 的 10 个 `mt_*.o` 源文件全不存在 | 高 | 已发生 | 按真实文件名重写 `LIB_O`，并新增根 `makefile` | 01 ✅ 源文件清单与产物路径已修好，链接仍阻塞于 02/03 |
| R3 | **两套同名 `extern "C" __*` 符号**（`src/st_sys.h` 带 timeout 版 vs `app/st_sys.h` POSIX 版）签名冲突 | 高 | 高 | 拆分命名空间：框架内部 API 与 syscall hook 必须改为不同前缀 | 03 |
| R4 | **macOS 无法验证**：本计划的实测只在 Linux + g++ 13 上做过，kqueue 路径与 `MAC_OS_X_VERSION_10_5` 下 `USE_UCONTEXT 0` 分支完全未验证 | 高 | 中 | 01 阶段就接入 macOS 验证手段，不留到最后 | 01 ⚠️ 已加 GitHub Actions（ubuntu + macos）跑 stlib 测试；但 `stlib/ucontext` 没有 arm64 分支，Apple Silicon 上 `make stlib` 仍编不过 → 02 |
| R5 | **协程栈与上下文切换**属未定义行为高发区（`MEM_PAGE_SIZE 2048`、`ss_sp + 8`、`ss_size - 64`、32/64 位指针拆分） | 高 | 中 | 该处按「原样保留 + 加注释 + 加断言」，不做优化 | 02 |
| R6 | `StEventSchedule::m_thread_schedule_` 在构造函数中未初始化，且 `Init()` 里赋值用的是不存在的 `ThreadSchedule` | 中 | 已发生 | 随 R1 命名统一一并修复，并补 NULL 断言 | 03 |
| R7 | `.clang-format` 全量格式化会产生巨大 diff，淹没真实改动 | 中 | 高 | 格式化独立成一个「纯格式」提交，并登记到 `.git-blame-ignore-revs` | 01 ✅ 改为只覆盖本仓库自己写的代码（`FORMAT_SRC`），实际只有 6 个文件 15 增 14 删，未做全库格式化，也就不需要 `.git-blame-ignore-revs`；`app/`、`tests/` 与 vendor 代码留给 04/05 |
| R8 | `thirdparty/gperftools` 是 gitlink 但**无 `.gitmodules`**，新 clone 得到空目录且 `git submodule status` 直接报错 | 中 | 已发生 | 需用户决策后处理 | D2 ✅ 已移除 gitlink（01） |
| R9 | 仓库内已跟踪二进制与垃圾文件（`.DS_Store`、`app/st_wrk/wrk` 等） | 低 | 已发生 | 补 `.gitignore` 并 `git rm --cached` | 01 ✅ 已完成（`memcache.pcap` 有意保留） |
| R10 | `readme.md` 通篇是已不存在的旧 API（`mt_init_frame`、`Frame`、`IMtAction`…），照做必然失败 | 中 | 已发生 | 05 阶段重写，且示例须真实编译过 | 05 |

---

## 验收总清单

汇总 01～05 的验收项。**建议作为 PR 模板的 checklist 使用。**

### A. 构建

- [ ] 根目录 `make` 成功，产出 `libmthread.a` 与 `libmthread.so`
- [ ] Linux（g++，epoll 路径）编译零 error
- [ ] macOS（clang++，kqueue 路径）编译零 error
- [ ] `-std=c++98` 下编译零 error
- [ ] `make clean` 后工作区 `git status` 干净（无残留产物）
- [ ] `libmthread.so` 的 `ldd` / `otool -L` 输出只含系统库（验证「零第三方运行时依赖」）

### B. 代码规范

- [ ] `.clang-format` 与所声明的 style 一致（D1 已决策并落地）
- [ ] `clang-format --dry-run --Werror` 全库通过
- [ ] 无 C++11 及以后特性（可用 `-std=c++98 -pedantic` 抽查）
- [ ] 不存在重复类定义（当前 `src/st_thread.h` 与 `app/thread.h` 均定义 `class StThread : public StThreadItem`）

### C. 功能

- [ ] 协程：创建 / `Yield` / `Sleep` / `Wakeup` / 父子协程唤醒（`WakeupParent`）行为正确
- [ ] 定时器：`StHeapTimer::CheckExpired` 到期触发，`Stop` 可取消
- [ ] epoll 与 kqueue 两条路径行为一致（同一套用例双平台跑）
- [x] TCP 客户端：短连接与 keepalive 连接（`eTCP_CONN` / `eTCP_KEEPLIVE_CONN`；L4 已修）
- [ ] UDP 客户端：`udp_sendrecv` 收发与超时
- [ ] 服务端：`StServer` accept → 收 → 处理 → 发 闭环
- [ ] 超时语义：连接 / 读 / 写 / 整体超时均按 `eERR_*` 错误码返回

### D. 应用与测试

- [ ] `app/st_dns` 编译并完成一次真实 DNS 查询
- [ ] `app/st_memcacheclient` 编译通过
- [ ] `app/st_wrk` 编译并能压测本地 HTTP server
- [ ] `tests/` 全部 unittest 可编译、可运行、结果为通过
- [ ] `tests/scripts/keepalive_unittest.py` 与 `udpsvr_unittest.py` 可执行
- [ ] 高并发冒烟：单进程创建 ≥ 10000 协程不崩溃、无 fd 泄漏

### E. 文档

- [ ] `readme.md` 中每段示例代码都真实编译过
- [ ] `readme.md` 的构建指令与实际 make 目标一致（当前写着 `make event`，但无此目标）
- [ ] `AGENTS.md` 存在且覆盖约定清单
- [ ] 公开头文件的关键类有用途 / 线程模型 / 所有权说明

---

## 需用户确认的决策点

以下 6 项**阻塞**对应阶段的开工，需要仓库维护者拍板。每项都给了推荐选项与理由。

### 拍板结果一览

| 决策点 | 结论 | 状态 |
| --- | --- | --- |
| D1 `.clang-format` 基线 | **(a)** 保留 LLVM + `m_x_` 命名，只把 `Standard: Latest` → `Cpp03`；文档不再声称 Google Style | ✅ 已落地（`plan/01`） |
| D2 `thirdparty/gperftools` | **(a)** 移除 gitlink，`-ltcmalloc` / `-lprofiler` 默认关、可选开 | ✅ 已落地（`plan/01`） |
| D3 新旧命名 / namespace | 统一到**新名**；**不加 namespace**（不把裸类塞进 `namespace`，即不采纳 (a) 里「把 `StConnection`/`StServer` 移入 `namespace sthread`」那部分）；不加 `typedef` 兼容层 | ⏳ 待 `plan/02` 执行 |
| D4 `extern "C"` 符号拆分 | 待定 | ⏳ 待 `plan/03` |
| D5 Bazel | **(a)** 移除残留（`WORKSPACE`、`stlib/ucontext/BUILD`、空 `stlib/tests/build.bzl`），构建只用 makefile | ✅ 已落地（`plan/01`） |
| D6 第三方示例代码定性 | 补 Russ Cox `COPYRIGHT` 与来源说明；**不**代选本仓库自身的 license；**不**删 `stlib/tests/ucontext/` 与 `stlib/tiny/`（即 (a)，不取 (b)） | ✅ 许可部分已落地（`plan/01`）；本仓库 license 仍待仓库所有者决定（`plan/05`） |

### D1. `.clang-format` 到底对齐哪个 style？（阻塞 01）

**事实**：任务与 readme 的语境都是「Google C++ Style」，但 `.clang-format` 实测内容是 LLVM 基线：

| 键 | 当前值 | Google 基线应为 |
| --- | --- | --- |
| 注释标记 | `# BasedOnStyle: LLVM` | `Google` |
| `AccessModifierOffset` | `-2` | `-1` |
| `PointerAlignment` | `Right`（`int *p`） | `Left`（`int* p`） |
| `DerivePointerAlignment` | `false` | `true` |
| `Standard` | `Latest` | — （与 C++98 约束矛盾） |

同时，现有代码的命名（`m_member_` 尾下划线前缀式、`StXxx` 类名、`ClassName()` 式方法名）**也不是 Google 命名规范**（Google 要求 `member_`、`ClassName`、`MethodName()`，其中成员变量只有尾下划线）。

**选项**：
- **(a) 推荐**：保留现有 LLVM 格式与现有命名习惯，把文档措辞从「Google Style」改为「本仓库 style（LLVM 基线 + `m_x_` 命名）」，仅把 `Standard: Latest` 改为 `Cpp03` 以匹配 C++98。**diff 最小、零行为风险。**
- (b) 真正切到 Google：改 `.clang-format` 为 Google 基线并全库重新格式化。产生超大 diff（R7），且与「不改行为」并不冲突但会彻底破坏 `git blame`。
- (c) 切 Google 格式但保留 `m_x_` 命名（折中）。

> **需要确认**：选 a / b / c？若选 b，是否接受一次覆盖全库的纯格式提交？

### D2. `thirdparty/gperftools` 怎么处理？（阻塞 01）

**事实**：`git ls-files -s thirdparty/gperftools` 返回 `160000 fe62a0baab87ba3abca12f4a621532bf67c9a7d2`，即它是一个 **gitlink（submodule 指针）**，但仓库**根本没有 `.gitmodules`**。后果：

- `git submodule status` 直接失败：`fatal: no submodule mapping found in .gitmodules for path 'thirdparty/gperftools'`
- 新 clone 只会得到一个空的 `thirdparty/gperftools/` 目录
- 而 `tests/Makefile` 与 `stlib/tests/Makefile` 里有 `-ltcmalloc -lprofiler`，依赖它

**选项**：
- **(a) 推荐**：**移除** gitlink（`git rm --cached thirdparty/gperftools`），改为在 `thirdparty/readme.md` 里说明「需要 tcmalloc 时请自行安装」，并把 `-ltcmalloc -lprofiler` 变成默认关闭的可选开关。最贴合「零第三方依赖」硬约束。
- (b) 补一个正确的 `.gitmodules`，把它作为**可选**的开发期 submodule 固定在 `fe62a0b`。
- (c) 保持现状（不推荐：`git submodule` 系列命令持续报错）。

> **需要确认**：选 a / b / c？若选 b，请确认 gperftools 的上游 URL。

### D3. 新旧命名二选一，是否允许 `namespace` 包装？（阻塞 02，**风险最高**）

**事实**：同一批文件里并存两套名字，且旧名字**没有任何定义**（只被引用）：

| 旧名（被引用，无定义） | 出现位置（举例） | 新名（有定义） |
| --- | --- | --- |
| `Thread` | `src/st_sys.h` 多处 | `StThread`（`src/st_thread.h`） |
| `ThreadSchedule` | `src/st_thread.cc:240` | `StThreadSchedule` |
| `EventSchedule` | `src/st_sys.h` | `StEventSchedule` |
| `Manager` | `src/st_server.h` | `StSysSchedule`（推测） |
| `StEventSuper` | `src/st_connection.h`、`src/st_server.h`、`app/st_c.h` | `StEventItem`（`src/st_poll.h`） |
| `StThreadSuper` | `src/st_server.h` | `StThreadItem` |
| `StNetAddress` | `src/st_server.h` | `StNetAddr`（`stlib/st_netaddr.h`） |
| `GlobalEventScheduler()` | `src/st_connection.h` 等 | `GlobalEventSchedule()`（`src/st_public.h`） |
| `GlobalThreadScheduler()` | `src/st_sys.h` 等 | `GlobalThreadSchedule()` |
| `ASSERT` | `src/st_sys.h` 等 | `LOG_ASSERT` |
| `HandleProcess` / `HandleError` / `CloseSocket` | `src/st_server.h` | `DoProcess` / `DoError` / `Close` |

另外命名空间本身也不统一：`stlib/` 全在 `namespace stlib`，`src/st_thread.h`、`src/st_poll.h`、`src/st_sys.h` 在 `namespace sthread`，但 `src/st_connection.h` 与 `src/st_server.h` **不在任何 namespace 里**（裸 `class StConnection`、`class StServer`）。

**选项**：
- **(a) 推荐**：统一到**新名**（`St*` 前缀 + `Global*Schedule()`），把 `src/st_connection.h`、`src/st_server.h` 一并纳入 `namespace sthread`。因为旧名无定义，这等价于「把未完成的重命名做完」，最符合「语义意图」口径。
- (b) 统一到旧名（回退重构）。需要新写大量缺失定义，工作量更大。
- (c) 保留新名，另加一层 `typedef` 兼容旧名（如 `typedef StEventItem StEventSuper;`）。对外更宽容，但会让两套名字长期共存。

> **需要确认**：(1) 选 a / b / c？(2) **是否允许把 `StConnection` / `StServer` 移入 `namespace sthread`？** 这会改变符号修饰名（ABI），严格说属于「对外 API」变化，需要你明确授权。(3) 是否需要 D3(c) 的 `typedef` 兼容层？

### D4. `extern "C"` 的 `__*` 符号冲突如何拆分？（阻塞 03）

**事实**：两个头文件用**相同的名字**声明了**不同签名**的 `extern "C"` 函数：

```
src/st_sys.h:  int __sendto(int fd, const void *msg, int len, int flags,
                            const struct sockaddr *to, int tolen, int timeout);   // 带 timeout
app/st_sys.h:  ssize_t __sendto(int fd, const void *message, size_t length, int flags,
                                const struct sockaddr *de__addr, socklen_t de__len); // POSIX 形状
```

`extern "C"` 不做重载，两者同时链接即符号冲突。更糟的是 `src/st_sys.cc` **自己也没对齐自己的头文件**：头里声明 `__recvfrom` / `__connect` / `__read` / `__recv` / `__send` / `__sleep` / `__accept` / `st_write`，而 `.cc` 里定义的是单下划线的 `_recvfrom` / `_connect` / `_read` / `_write` / `_recv` / `_send` / `_sleep` / `_accept`（只有 `__sendto` 是双下划线）。

补充：`__` 前缀在 C++ 中属于实现保留标识符，本就不宜自用。

**选项**：
- **(a) 推荐**：两套都改前缀——框架内部带超时的 API 用 `st_`（`st_sendto(..., timeout)`），syscall hook 用 `sys_`（`sys_sendto(...)`）。同时修正 `.cc` 与 `.h` 的名字对齐。
- (b) 只给 hook 层换前缀，框架层保留 `__`。
- (c) 用 `namespace` + 不导出 `extern "C"`（但 hook 层必须是 C 符号，不可行）。

> **需要确认**：(1) 选 a / b？(2) `app/st_c.h` 暴露的 `udp_sendrecv` / `tcp_sendrecv` / `st_set_hook_flag` / `st_set_private` / `st_get_private` 是**对外公开 API**，它们的名字必须保持不变（我按「必须不变」处理），请确认。

### D5. Bazel（`WORKSPACE`）是保留还是移除？（阻塞 01）

**事实**：Bazel 支持只搭了一半：

- `WORKSPACE` 只有一行：`workspace(name = "sthread")`
- 只有 `stlib/ucontext/BUILD` 一个 BUILD 文件，它 `glob(["*.cpp","*.cc","*.c","*.S"])` 并定义了 `cc_test(name="ucontext_test", srcs=["ucontext_test.c"])`
- `stlib/tests/build.bzl` 是**空文件（0 字节）**
- `src/`、`app/`、`tests/` 没有任何 BUILD 文件
- 主构建方式实际是 makefile

**选项**：
- **(a) 推荐**：**本轮移除** Bazel 残留（`WORKSPACE`、`stlib/ucontext/BUILD`、空的 `stlib/tests/build.bzl`），集中修好 makefile 这一条路。理由：维持两套构建系统会让 01～04 的每一步都要做两遍，而 Bazel 那套现在连一个完整 target 都没有。
- (b) 保留现状、不动（Bazel 继续处于不可用状态）。
- (c) 把 Bazel 补全为一等公民（每个目录补 BUILD + `.bazelrc` + 双平台 toolchain）。工作量显著增加，需独立排期。

> **需要确认**：选 a / b / c？如果你实际在用 Bazel（比如内部有未提交的 BUILD 文件），请务必告知——那会直接否掉 (a)。

### D6. `stlib/tests/ucontext/` 的第三方示例代码如何定性？（阻塞 04/05）

**事实**：`stlib/tests/ucontext/` 下是 Russ Cox 的 libtask 示例代码（`task.c`、`taskimpl.h`、`channel.c`、`primes.c`、`httpload.c`、`tcpproxy.c`、`rendez.c`、`qlock.c` 等），`stlib/ucontext/ucontext.h` 头部也写着 `Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT`——但仓库里**没有 `COPYRIGHT` 文件**。

另外 `stlib/tiny/`（`tiny_http_parse.h`、`tiny_util.h`）没有被任何 makefile 引用，`app/st_wrk/http_parser.c` 则是 nodejs http-parser 的 vendored 版本。

这与「零第三方运行时依赖」不矛盾（vendored 源码不是运行时依赖），但**许可证义务是真实存在的**。

**选项**：
- **(a) 推荐**：补齐 `COPYRIGHT` / `LICENSE`（MIT，Russ Cox）与 `thirdparty/readme.md` 的来源说明；`stlib/tests/ucontext/` 明确标注为「上游参考样例，不参与构建」。
- (b) 移除未使用的 `stlib/tests/ucontext/` 与 `stlib/tiny/`（减少混淆，但丢失参考价值）。

> **需要确认**：(1) 选 a / b？(2) **本仓库自身的 license 是什么？** 仓库根目录目前没有 LICENSE 文件，而源码头部是 `Copyright (C) zhoulv2000@163.com`。这需要你确定，我不能替你选。(3) `stlib/tiny/` 是正在开发中的东西还是废弃残留？

---

## 本目录文件一览

| 文件 | 内容 |
| --- | --- |
| `README.md` | 本文：总索引、硬约束、执行顺序、风险、验收总清单、决策点 |
| `01-foundation-build-style.md` | 构建系统 / 目录布局 / C++98 / 代码风格 / `libmthread` 产物定义 |
| `02-coroutine-scheduler.md` | ucontext 上下文切换 / `StThreadItem`·`StThread` / `StThreadSchedule` / 堆与定时器 |
| `03-io-multiplexing-net.md` | epoll·kqueue / `StEventSchedule` / `StConnection`·`StServer` / syscall hook / TCP·UDP |
| `04-apps-tests-compat.md` | `st_dns`·`st_memcacheclient`·`st_wrk` / `tests/` / 兼容回归 |
| `05-docs-agents-readme.md` | `readme.md` 重写 / 注释规范 / `AGENTS.md` |

根目录另有 [`../AGENTS.md`](../AGENTS.md)：面向 AI 助手与新贡献者的仓库约定。
