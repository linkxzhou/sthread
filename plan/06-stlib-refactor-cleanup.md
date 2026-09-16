# 06 · stlib 重构计划：清理 + 优化，打磨通用 server + 协程库底座

> 本文档**只描述计划，不包含任何代码变更**。实现按 Phase 0 → 4 串行推进，每个 Phase 以「构建 + 单测全绿」为出口条件。
>
> 与 plan/01～05 的关系：01～05 把仓库从「编译不过」修到「全绿可用」；本计划做**减法与优化**——删掉死代码、修掉潜伏 bug、把热点数据结构从「能跑」优化到「高效」，最终让 stlib 成为一个干净、可独立使用的通用 server + 协程基础库。

---

## 1. 目标与非目标

### 1.1 目标

sthread 的定位是「基于协程的高性能网络库」，stlib 是它的底座：协程上下文（ucontext）、IO 多路复用（epoll/kqueue）、调度容器（TAILQ/堆/哈希）、缓冲区池、日志、测试框架。本计划完成后：

1. **stlib 无死代码**：每个文件、每个公开接口都有真实消费方或被明确标记为 vendored 参考。
2. **stlib 无已知潜伏 bug**：第 4 节 P-B 清单逐项修复并配回归单测。
3. **stlib 热点路径性能正确**：堆操作从 O(n²) 全量重建改为 O(log n) sift；日志多线程安全。
4. **stlib 可独立使用**：`stlib/tests` 只依赖 stlib 自身源码即可构建运行（现状已满足，保持）；生产库产物（`libst.a`/`libmthread`）不链测试框架等非生产代码。

### 1.2 非目标（明确不做）

- 不改任何对外语义：枚举数值、`STACK`（260096）、`MEM_PAGE_SIZE`（2048）、buffer 默认尺寸、超时默认值、`Instance<T>()` 线程局部语义。
- 不引入 C++11+ 语言特性（保持 `-std=c++98`；`__thread`、`__builtin_expect` 等 GNU 扩展可继续用）。
- 不引入任何第三方运行时依赖。
- 不动 `src/` 的架构（调度模型、M:N 等不在本期范围）；`src/` 仅作为 stlib 的**消费方**参与兼容性验证。
- 不换构建系统（沿用现有 makefile 体系）。

---

## 2. 现状盘点（逐文件实测）

### 2.1 组件清单与消费矩阵

| 文件 | 职责 | 消费方 | 判定 |
| --- | --- | --- | --- |
| `st_def.h` | 基础宏 / 错误码 / 常量 | 全局 | 保留，需瘦身（P-B16、P-C2） |
| `st_log.h/.cc` | 日志（`StLogger` 进程级单例） | 全局 | 保留，需修 bug（P-B4~7） |
| `st_singleton.h` | 线程局部单例 `Instance<T>()` | `src/` 核心 | 保留，清理空宏（P-A8） |
| `st_util.h` | `Util` + `UtilPtrPool` + `referenceable` + `Any`/`any_cast` | `src/` 核心 | 保留，优化（P-C3/C4） |
| `st_closure.h` | C++98 回调封装 `NewStClosure` | `app/st_frame.h`、`src/st_thread` | 保留 |
| `st_tailq.h` | `CPP_TAILQ_*` 侵入式队列 | 全局（调度队列） | 保留，清理 BSD 原生宏（P-A9） |
| `st_hash_list.h` | `StHashKey`/`StNetAddrKey`/`StHashList` | `st_buffer.h`、`src/st_connection.h` | 保留，修 bug（P-B8~10） |
| `st_buffer.h` | `StBuffer`/`StBufferBucket`/`StBufferPool` | `src/st_connection.h` | 保留，修析构 bug（P-B9） |
| `st_heap.h` | `StHeap`/`StHeapList` 最小堆 | `st_heap_timer.h`、`src/` | 保留，**性能重写**（P-C1） |
| `st_heap_timer.h` | `StTimer`/`StHeapTimer` | `src/st_sys.h` 等 | 保留，类型修正（P-B13） |
| `st_netaddr.h` | `StNetAddr` IPv4/IPv6 地址 | `src/st_server.h` 等 | 保留，修 bug（P-B1~3） |
| `st_epoll.h` / `st_kqueue.h` | IO 后端 `StIOState`（编译期二选一） | `src/st_poll.h` | 保留，对齐语义（P-B20/21、P-A10） |
| `st_context.h/.cc` | `Context`/`Stack`/`context_switch` | `src/st_poll.h`、`st_thread` | 保留，拼写修正（P-B17） |
| `st_test.h/.cc` | 轻量测试框架 `TEST`/`ASSERT_*` | `stlib/tests`、`src/st_public.h`（异常耦合） | 保留，解耦（P-A13、D6） |
| `tiny/`（2 个头文件） | HTTP 解析 + 字符串工具 | **全仓库零引用** | 死代码 → D1 |
| `ucontext/`（vendored libtask） | 协程上下文汇编 + makecontext | 构建仅取 `ucontext.c` + `asm.S` | 保留底座，清死件（P-A2~5） |
| `ucontext/uthread.c/.h` | 教学级协程 demo | **不进 LIBO，零引用** | 死代码 → D2 |
| `ucontext/ucontext_test.c` | 孤立测试 | **无构建引用** | 死代码 → D2 |
| `ucontext/ucontext_stub_arm64.c` | 空 stub（`#if 0` 占位） | 无 | 死代码，直接删 |
| `tests/ucontext/` | libtask 上游示例 | `thirdparty/readme.md` 声明「不参与构建」 | 参考件 → D2 |

### 2.2 关键依赖事实（grep 实测）

- `src/st_public.h:8` include 了 `stlib/st_test.h`，但 `src/` 全目录**没有** `ASSERT_*`/`TEST` 的任何使用 —— 生产头文件白白依赖测试框架（连带 `<sstream>`/`<string>`）。
- `ApiResize()`（epoll/kqueue 两版）在全仓库**无任何调用方**，且实现本身有泄漏 + 状态丢失 bug（P-B14）。
- `Any` 的 `Holder`/`Clone`/`GetType`/`operator()` 机制全仓库**无真实使用**；所有 `any_cast<T>(p)` 调用点走的都是「类型不匹配时退化为 `static_cast`」的兜底路径。
- `st_buffer.h:170` 的 `ST_ALGIN(8192)` = **8200**（已对齐时仍 +8），即连接默认 buffer 实际是 8200 字节 —— 行为已固化，本期只注释不改（D4）。
- BSD 原生 `TAILQ_*` 宏（非 `CPP_` 前缀）与 `typedef long long time64_t` 在 `st_tailq.h` 定义但全仓库使用方为零。
- `stlib/tests` 现有 4 个测试（buffer/hash_list/heap/heap_timer），是本次重构的回归锚点；`tests/`（框架级 unittest）是 src 兼容性的回归锚点。

---

## 3. 硬约束（继承 AGENTS.md，逐条复述）

1. **C++98**：禁止 `auto`/`nullptr`/右值引用/range-for/`std::unique_ptr` 等；`__thread`、`__builtin_expect` 可用。
2. **Linux + macOS 双平台可编译**：epoll/kqueue 分支只出现在 `st_epoll.h`/`st_kqueue.h`。
3. **零第三方运行时依赖**。
4. **行为兼容**：`src/`、`app/`、`tests/` 现有调用点在重构后**源码级兼容**（允许「编译期等价替换」，如 `any_cast` 语义退化为 `static_cast` 但调用点不动）。枚举数值与尺寸常量一律不动。
5. **风格**：`.clang-format`（LLVM 基线 + `m_x_`）为唯一权威；注释保持中文。
6. **每个 Phase 独立可验证**：串行推进，出口条件不满足不进入下一阶段。

---

## 4. 问题清单（重构素材，全部经 grep/阅读核实）

### P-A · 死代码（纯删除，零行为变化）

| # | 位置 | 内容 | 证据 |
| --- | --- | --- | --- |
| A1 | `stlib/tiny/` | `tiny_http_parse.h` + `tiny_util.h` 整个目录 | 全仓库 include 零匹配 → **D1** |
| A2 | `stlib/ucontext/uthread.c/.h` | 教学 demo（`uthread_create_schedule` 等） | 不在 `stlib/makefile` 的 `LIBO`；全仓库零 include → **D2** |
| A3 | `stlib/ucontext/ucontext_test.c` | 孤立测试 | 无任何 makefile 引用 → **D2** |
| A4 | `stlib/ucontext/ucontext_stub_arm64.c` | 空文件（`#if 0` 占位） | 无构建引用；直接删 |
| A5 | `stlib/tests/ucontext/` | libtask 上游示例 13 个文件 | `thirdparty/readme.md` 已声明不参与构建 → **D2** |
| A6 | `st_def.h:78,95-98` | `ST_MAXTIME`、`ST_SQUARE`、`ST_VAR`、`ST_STDDEV` | 全仓库零使用 |
| A7 | `st_log.h` | `LOGA`、`LOG_FUNCMARK` 宏；`Stacktrace()` 空实现；`StringIndexOf()`（实现错误：找不到时返回串长而非 -1） | 零使用 |
| A8 | `st_singleton.h:87-105` | `DECLARE_SINGLETON` / `DECLARE_VIRTUAL_SINGLETON` / `IMPLEMENT_SINGLETON`（空宏） | 零使用（实现前先 grep 复核一次） |
| A9 | `st_tailq.h:8-31` | BSD 原生 `TAILQ_*` 宏组 + `typedef long long time64_t` | 全仓库只用 `CPP_TAILQ_*`（实现前 grep 复核） |
| A10 | `st_epoll.h:58` / `st_kqueue.h:58` | `ApiResize()`：无调用方，且 `m_file_`/`m_fired_` 用 `malloc` 覆盖旧指针（泄漏 + mask 状态丢失） | grep 零调用；两后端**同时删**，接口一致性不受影响 |
| A11 | `st_heap.h:151-160` | `HeapUp()` 从未被调用 | 仅 `HeapDown()` 有调用点；随 P-C1 重写一并消化 |
| A12 | `st_epoll.h` / `st_kqueue.h` | `StFiredEvent.data[DATA_SIZE=1]` 字段从未被填充/读取 | 阅读确认；删字段或注释保留（随 D7 一起定） |
| A13 | `src/st_public.h:8` | `#include "stlib/st_test.h"` | `src/` 无 `ASSERT_*`/`TEST` 使用 → 移除该 include（跨目录唯一改动点，需编译验证） |

### P-B · 潜伏 bug（逐项修复 + 回归单测）

| # | 位置 | 问题 | 修法要点 |
| --- | --- | --- | --- |
| B1 | `st_netaddr.h:28-36` | `operator==` 中端口比较写成**赋值** `=`（IPv4/IPv6 各一处）：永远判等成功且污染 `this`。直接影响 `StNetAddrKey::HashCmp` → 连接四元组判重错误 | `=` → `==`；配 IPv4/IPv6 相等性单测 |
| B2 | `st_netaddr.h:72-74` | IPv6 分支 `sin6_family = AF_INET`（应 `AF_INET6`），`inet_pton(AF_INET, …)`（应 `AF_INET6`） | 修正族与解析；配 IPv6 地址构造单测 |
| B3 | `st_netaddr.h:87-119` | `IP()`/`IPPort()` 返回 `static char[64]` —— 多 OS 线程下互相踩踏，与库的线程局部模型冲突 | 改 `__thread` 缓冲（C++98 兼容）；`IPPort` 补 IPv6 分支 |
| B4 | `st_log.cc:102,150` | `__log`/`__loga` 用函数级 `static char buf[8192]` —— 多线程写日志必然交织 | 改 `__thread`（→ **D5**） |
| B5 | `st_log.cc:9-13,15-20` | 构造函数未初始化 `m_name_`/`m_nerror_`；析构里 `::free(m_name_)` 依赖未初始化指针 | 成员全部显式初始化；析构判空 |
| B6 | `st_log.h:76-82` | `LOG_ERROR` 映射到 `LLOG_ALERT`（级别语义错位） | 映射改 `LLOG_ERR`；排查是否有人依赖旧级别过滤 |
| B7 | `st_log.cc:66-77` | `StringIndexOf` 找不到返回串长而非 -1 | 随 A7 删除（零使用），不留坏代码 |
| B8 | `st_hash_list.h:106` | 哑元桶节点用 `malloc(sizeof(value_type))` 分配 C++ 对象，**跳过构造函数**（UB，靠手工赋字段掩盖） | 随 P-C6 移除哑元设计时一并消化 |
| B9 | `st_buffer.h:148-158` | `~StBufferPool()` 清理路径错乱：`HashGetFirst()` 返回的是哑元头节点而非业务元素；`HashRemove` 内部 `st_safe_delete` 后调用方再次 `st_safe_delete(_bucket)` → **double free** 风险（进程退出时被 OS 回收掩盖） | 重写析构：遍历桶、逐个释放 `StBufferBucket`；配「池析构不挂」单测 |
| B10 | `st_hash_list.h:162-188` | `HashRemove` 删除即 `delete`（所有权语义与调用方冲突，B9 的根源之一）；while 循环删所有匹配项（键唯一时多余） | 拆成「摘除节点」与「释放节点」两步语义，所有权归调用方 |
| B11 | `st_util.h:59-72` | `GetUniqid` 的 `static pthread_mutex_t mutex` **未初始化**（应 `PTHREAD_MUTEX_INITIALIZER`）；`rid = id;` 为多余赋值 | 初始化 + 删多余语句；配并发取 id 单测 |
| B12 | `st_tailq.h:73-76` | `CPP_TAILQ_FOREACH_SAFE` 续行符 `\` 后跟 trailing space —— 严格编译选项下报错 | 去尾空格（含同文件其他行） |
| B13 | `st_heap_timer.h:15-38` | `m_time_expired_`（`uint64_t`）初始化 `-1`；`SetExpiredTime(int64_t)` vs `GetExpiredTime()→uint64_t` 符号混乱；未 `Startup` 的 timer 若入堆会以「最早过期」置顶 | 统一 `int64_t`；初始 0 表示未启动；`IsExpired` 语义不变 |
| B14 | `st_epoll.h:42` | `epoll_create(EVENT_SIZE)` 而非 `size`（现代内核忽略该参数，仅整洁性问题） | 改传 `size` |
| B15 | `st_epoll.h`/`st_kqueue.h` | `Poll()` 语义漂移：epoll 映射 `EPOLLERR/EPOLLHUP → ST_EVERR`，kqueue 完全不产生 `ST_EVERR` | 对齐两后端（→ **D7**，kqueue 侧用 `EV_ERROR`/`EV_EOF` 映射） |
| B16 | `st_def.h:71-75` | `#if ST_DEBUG` 在宏未定义时按 0 处理，写法颠倒（先用后定义） | 改 `#ifdef`/`#ifndef` 标准写法，行为不变 |
| B17 | `st_context.cc:18` | `g_context_runing` 拼写错误（runing→running） | 纯重命名（文件内 static，无 ABI 影响） |
| B18 | `st_kqueue.h:90-104` | `AddEvent` 中 `EV_DELETE` 失败被空 `if` 静默吞掉 | 至少加注释说明容忍 `ENOENT`；或统一错误处理 |
| B19 | `st_singleton.h:46-50` | C++11 分支 `static __thread T t` 与 C++98 分支指针语义不一致；`InstanceDestroy` 后同线程再次 `Instance()` 会触发 `Deleter.Set` 的 assert（pthread_setspecific 仍指向已释放对象） | 本期仅注释标注限制；统一实现留待后续（涉及 `Instance<T>()` 语义，谨慎） |
| B20 | `st_buffer.h:51-61` | `SetBuffer` 边界 `len >= m_max_len_` 使「恰好写满」失败；返回值 `int` 与 `uint32_t` 混用 | 修边界为 `len > m_max_len_`；返回值语义保持（调用点检查） |

### P-C · 性能与结构优化（行为等价为前提）

| # | 位置 | 问题 | 方案 |
| --- | --- | --- | --- |
| C1 | `st_heap.h` | `HeapPush/HeapPop/HeapDelete` 每次调用 `HeapDown()` 做**双重循环全堆重建**（O(n²) 级，实为堆排序套路）；定时器与协程 sleep 队列都压在这上面，高并发下是热点 | 重写为标准 sift-up/sift-down：push 上浮 O(log n)、pop/delete 下沉 O(log n)；保持最小堆语义与 `GetIndex/SetIndex` 约定不变。**现有 `st_heap_test`/`st_heap_timer_test` 即回归锚点**，另补「10 万元素 push/pop 单调性」单测 |
| C2 | `st_def.h` | 头文件膨胀：18 个系统/STL include 摊给所有包含者 | 只保留宏所需的公共头；平台相关 include 下沉到真实使用方（`st_log.h`/`st_netaddr.h` 等已各自包含）。逐个 include 验证编译 |
| C3 | `st_util.h` `Any`/`any_cast` | `Holder`/`Clone`/`GetType`/`operator()` 机制零真实使用，`any_cast` 全部走 `static_cast` 兜底 —— 重量级设计纯负担 | `any_cast` 简化为 `static_cast` 内联别名（**调用点零改动**）；`StHashKey`/`StHeap` 不再继承 `Any`（→ **D3**） |
| C4 | `st_util.h` `referenceable` | `m_ref_count_` public；基类无虚析构 | 成员转 private + 访问器；补 `virtual ~referenceable()`（有虚函数，多态 delete 目前是 UB） |
| C5 | `st_singleton.h` | C++98 分支双检锁 + `pthread_key` 析构；C++11 分支另一套 | 保留 C++98 路径；标注 B19 限制（不本期改语义） |
| C6 | `st_hash_list.h` | 哑元头节点设计：`malloc` 未构造（B8）、`m_hash_value_` 被复用为「链长计数」、`HashGetFirst` 返回哑元 —— 三层语义混淆 | 移除哑元：桶直接存首元素指针；桶计数显式字段或干脆去掉（计数只在 ST_DEBUG 用）；同步重写 `~StBufferPool`（B9） |
| C7 | `st_buffer.h` | `ST_BUFFER_BUCKET_SIZE`/`ST_MAX_SIZE` 命名误导（128 是桶数/默认 max_free，不是字节）；`ST_ALGIN` 拼写 | 重命名内部常量（保持数值）；`ST_ALGIN` 值语义不动（→ **D4**），仅注释拼写 |
| C8 | `stlib/ucontext/ucontext.h:96-104` | `#if 0 && defined(__sun__)` 永久关闭的 SPARC 死块 | 删除死块，注释说明「SPARC 未 vendor」 |
| C9 | 全 stlib | 头文件自包含性（include-what-you-use）：部分头依赖包含顺序 | 每个头独立可编译（`g++ -fsyntax-only -std=c++98` 逐个验证），纳入 CI/回归 |
| C10 | `st_epoll.h`/`st_kqueue.h` | `Create` 里三段 `malloc` 失败时只 `Free()` 一半（`m_epfd_` 未建时 Free 分支安全但重复代码） | 整理为统一失败路径（行为不变） |

---

## 5. 分阶段执行

> 严格串行。每个 Phase 末尾跑「基线三件套」：
> `make -C stlib/tests run`（stlib 单测）、`make lib`（libmthread 产出）、`make -C tests run`（框架级回归）。
> 平台验证：本机 macOS arm64（kqueue 路径）必跑；Linux（epoll 路径）通过 CI 或虚拟机验证。

### Phase 0 · 基线锚定（无代码改动）

1. 在当前 HEAD 跑通三件套并记录输出；记录 `libmthread.a/.so` 体积、`stlib` 各测试通过数。
2. 记录 `tests/` 覆盖率现状（上一节点 ~74%）。
3. 输出：基线记录追加到本文档「落地记录」节（或单独 `06-baseline-notes.md`）。

**出口条件**：三件套全绿；任何一项当前即失败的，先定位是历史遗留还是环境问题，记入风险表再继续。

### Phase 1 · 死代码清理（P-A 全部）

- 直接删除：A4、A6、A7、A10、A12、A13。
- grep 复核后删除：A8、A9（复核 `TAILQ_` 非 `CPP_` 前缀的使用为零再删）。
- 决策项：A1（D1）、A2/A3/A5（D2）。
- A11 并入 Phase 3 的 C1 重写。

**出口条件**：三件套全绿；`nm`/`otool` 对比库符号无意外丢失（`src/` 用到的 stlib 符号全部保留）；库体积只减不增。

### Phase 2 · 潜伏 bug 修复（P-B 全部）

- 每个 bug 一个独立提交：修复 + 对应回归单测（放 `stlib/tests/`，沿用 `st_test.h` 框架）。
- 顺序建议：先纯本地的（B1、B2、B5、B7、B11、B12、B14、B16、B17、B20），再结构性的（B3、B4、B6、B8、B9、B10、B13、B15、B18、B19）。
- B8/B9/B10 与 Phase 3 的 C6 强耦合：可提前与 C6 合并为一个「HashList 重写 + 池析构修复」提交，但必须挂在 Phase 2 名下完成（先修对，再谈优化）。
- 每个修复都要答一句：**「这个 bug 当前被谁触发？」**（多数潜伏 bug 是「路径走不到」，修复后需确认新路径有单测覆盖）。

**出口条件**：三件套全绿；新增回归单测全部通过；`tests/` 覆盖率不低于基线。

### Phase 3 · 性能与结构优化（P-C 全部）

- C1（堆 sift 化）是性能主线，单独提交，附简易基准（10 万 timer push/pop 耗时对比，写入落地记录）。
- C2/C9（头文件卫生）单独提交：逐头 `g++ -fsyntax-only -std=c++98 -I.` 独立编译验证。
- C3（Any 简化，D3 拍板后）、C4、C6、C7、C8、C10 各自独立提交。
- C5/B19 只做注释，不动语义。

**出口条件**：三件套全绿；基准数据证明堆操作复杂度改善（10 万量级 push+pop 耗时显著下降）；库导出符号与 Phase 1 结束时的清单做 diff，差异逐项解释。

### Phase 4 · 通用库收尾

1. **stlib 自述文档**：`stlib/README.md`（或并入根 readme 的 stlib 章节）——组件清单、线程模型约定、「如何只用 stlib 写一个最小协程 echo server」的指引性示例（示例代码须来自真实可编译路径，如 `stlib/tests/` 新增一个 demo 测试）。
2. **测试框架剥离（D6 拍板后）**：`st_test.o` 从 `libst`/`libmthread` 产物中移除，只在测试构建时编译；同步改 `src/makefile` 与 `stlib/tests/Makefile`。
3. **宏命名空间卫生说明**：`ST_*`/`LOG_*`/`ASSERT_*` 等裸宏清单文档化（改名不在本期，避免调用点地震）。
4. 更新 `AGENTS.md`「目录结构」与 `thirdparty/readme.md`（D1/D2 删除项需同步抹掉记录）。

**出口条件**：三件套全绿；文档中的示例真实编译通过；`ldd`/`otool -L` 确认产物零第三方依赖。

---

## 6. 决策点（实现前需拍板）

| # | 问题 | 选项 | 建议 |
| --- | --- | --- | --- |
| D1 | `stlib/tiny/`（HTTP 解析）去留 | a) 删除；b) 保留并接进 `app/st_httpserver`；c) 移到 `examples/` | **a)**：当前零引用，且 mongoose 风格实现与库风格不一致；将来做 HTTP 时再按库风格重写 |
| D2 | `uthread.*`、`ucontext_test.c`、`tests/ucontext/` 去留 | a) 全删；b) 保留 `tests/ucontext/` 作上游参考，删前两个；c) 全保留 | **b)**：uthread 是教学 demo 与库定位不符；上游示例留作协议参考成本低 |
| D3 | `Any`/`any_cast` 体系 | a) 简化 `any_cast` 为 `static_cast` 别名、移除 Holder 机制与 `Any` 继承；b) 原样保留 | **a)**：零真实使用，调用点零改动，纯减负 |
| D4 | `ST_ALGIN` 已对齐时仍 +8（8192→8200） | a) 修为标准向上对齐；b) 保持 + 注释 | **b)**：池分桶尺寸已固化，改动会影响 buffer 池行为，收益极低 |
| D5 | 日志多线程安全 | a) `__thread` 缓冲区；b) 加锁；c) 不改 | **a)**：C++98 兼容、无锁开销；注意 `__log` 里 `localtime` 也非线程安全，一并换 `localtime_r` |
| D6 | `st_test.o` 编进 `libst`/`libmthread` | a) 剥离，仅测试构建时编译；b) 保持 | **a)**：生产库不应链测试框架；需同步两个 makefile + `src/st_public.h` 去耦（A13） |
| D7 | kqueue 不产生 `ST_EVERR`，与 epoll 语义漂移 | a) kqueue 补 `EV_ERROR`/`EV_EOF → ST_EVERR` 映射；b) 维持差异并文档化 | **a)**：对齐语义，但需先确认 `src/StEventSchedule` 对 `ST_EVERR` 的消费逻辑，避免改出行为差异 |

---

## 7. 风险总览

| # | 风险 | 影响 | 概率 | 缓解 |
| --- | --- | --- | --- | --- |
| R1 | 删除「死代码」时被漏检的隐藏调用方打脸（宏/模板 grep 漏报） | 中 | 中 | Phase 1 每删一项即全量重编三件套；grep 模式含 `##` 拼接、`#include` 间接路径两类复核 |
| R2 | 堆 sift 重写引入边界错误（`GetIndex/SetIndex` 约定被 `StHeapTimer`/`src` 依赖） | 高 | 中 | 现有 `st_heap_test`/`st_heap_timer_test` + 新增 10 万级随机单测 + `tests/` 全量回归；逐提交可回滚 |
| R3 | B1（`operator==` 赋值 bug）修复后，连接四元组判重逻辑行为变化（以前「永远相等」掩盖了某些路径） | 中 | 低 | 修复后重点跑 `st_conn_manager_unittest` 等连接管理回归；如有失败，分析旧路径是否依赖了错误判等 |
| R4 | `HashList` 重写（C6）与 `StBufferPool` 析构（B9）耦合，改动面扩散 | 中 | 中 | 合并为一个提交一次到位；先写「池析构/桶复用」单测再动手 |
| R5 | Linux 端验证缺失（本机 macOS） | 中 | 中 | Phase 0 即建立 Linux 验证手段（CI 或 Docker），不留到最后 |
| R6 | D6 剥离 `st_test.o` 后 `tests/` 链接失败（unittest 可能依赖库内符号） | 低 | 中 | Phase 4 才做；改前 grep `tests/` 对 `StTester`/`StStatus` 的链接依赖并同步调整其 Makefile |
| R7 | 日志级别映射修正（B6）改变现有日志输出量 | 低 | 低 | 默认级别 `LLOG_PVERB` 下 `ALERT(1)<ERR(3)` 均可输出，实际无差；落地记录中说明 |

---

## 8. 验收总清单（可作 PR checklist）

### A. 构建与测试

- [ ] `make -C stlib/tests run` 全绿（macOS + Linux）
- [ ] `make lib` 产出 `libmthread.a/.so`，零第三方依赖（`otool -L`/`ldd`）
- [ ] `make -C tests run` 全绿，覆盖率不低于基线
- [ ] stlib 每个头文件可独立编译（`-std=c++98`）

### B. 清理

- [ ] P-A 清单全部落地（含 D1/D2 决策项）
- [ ] 全仓库 grep 无已删符号残留引用
- [ ] 库导出符号清单 diff 逐项有解释

### C. 修复

- [ ] P-B 清单逐项修复并各有回归单测
- [ ] 无新增 `-Wall` 警告（在现有基线之上只减不增）

### D. 优化

- [x] 堆操作 sift 化完成，基准数据写入落地记录
- [x] `Any` 简化后调用点零改动（D3）
- [x] 日志多线程安全（D5）

### E. 文档

- [x] `stlib` 组件文档更新；`AGENTS.md`、`thirdparty/readme.md` 同步
- [x] 本文档补「落地记录」节：每 Phase 的实际结果与偏差

---

## 9. 落地记录（实现时填写）

> 每完成一个 Phase，在此追加：日期、提交号、三件套结果、偏差说明。

### Phase 0 · 基线锚定（2026-09-16）

- HEAD: `418650f`（`master` = `origin/master`）
- 平台: macOS arm64（kqueue）
- `make -C stlib/tests run`: **ALL STLIB TESTS PASSED**（4 binaries: buffer / hash_list / heap / heap_timer）
- `make lib`: `libmthread.a` 1643152 bytes，`libmthread.so` 220112 bytes；`otool -L` 仅 `libc++` + `libSystem`（无第三方）
- `nm -gU libmthread.a` 导出符号约 **618**
- `tests/` 关键回归（singleton/base/thread/util/buffer/heap/hash/scheduler/harvest/sysapi/covextra）: **全绿**
- 覆盖率现状（上一节点）: ~74%（gate 70，长期目标 80）
- 风险表 R5（Linux 验证）: 本期仍仅本机 macOS；Linux 留 CI/后续

出口条件满足 → 可进入 Phase 1（待 D1–D7 拍板）。

### Phase 1 · 死代码清理落地（2026-09-16）

- 决策：D1–D7 全按建议拍板（本 Phase 落地 D1/D2；D3–D7 留给后续 Phase）
- 删除：`stlib/tiny/`（D1）；`stlib/ucontext/{uthread.c,uthread.h,ucontext_test.c,ucontext_stub_arm64.c}`（D2，保留 `stlib/tests/ucontext/`）
- A6：去掉 `ST_MAXTIME`/`ST_SQUARE`/`ST_VAR`/`ST_STDDEV`
- A7：去掉 `LOGA`/`LOG_FUNCMARK`/`Stacktrace`/`StringIndexOf`/`__loga`；保留 `StringLastOf`（`__log` 路径截断仍用）；同步改 `tests/st_log_buffer_extra_unittest.cpp`
- A8：去掉空宏 `DECLARE_*_SINGLETON` / `IMPLEMENT_SINGLETON`
- A9：去掉 BSD `TAILQ_*`；**偏差**：`time64_t` 仍被 `app/st_c.cc` 使用 → 迁到 `stlib/st_def.h`，未删除类型
- A10/A12：删 `ApiResize`；`StFiredEvent` 去掉未用 `data[]`/`DATA_SIZE`
- A13：`src/st_public.h` 去掉 `#include "stlib/st_test.h"`
- 文档：`makefile` 注释、`COPYRIGHT`、`thirdparty/readme.md` 同步抹掉已删项
- 三件套：stlib tests / `make lib` / 全部 `st_*_unittest` **全绿**；`format-check` 通过
- 体积：`libmthread.a` 1643152→1639776；`.so` 220112→219600；`nm -gU` 符号 618→615（只减不增）
- A11（`HeapUp`）按计划并入 Phase 3 C1

出口条件满足 → 可进 Phase 2。

### Phase 2 · 潜伏 bug 修复落地（2026-09-16）

- B1/B2/B3：`operator==` 端口赋值→比较；IPv6 `AF_INET6`+`inet_pton`；`IP`/`IPPort` 改 `__thread`，补 IPv6 端口；`PortNetEndian` 分族
- B4/B5/B6：日志缓冲 `__thread` + `localtime_r`；构造全员初始化、析构判空；`LOG_ERROR`→`LLOG_ERR`
- B7：随 Phase 1 A7 已删 `StringIndexOf`
- B8/B9/B10/C6：`StHashList` 去哑元；`HashRemove` 返回摘除节点（调用方释放）；`~StBufferPool` 重写；同步 `src/st_connection.h` / 单测
- B11：`GetUniqid` `PTHREAD_MUTEX_INITIALIZER`
- B12：`CPP_TAILQ_FOREACH_SAFE` 去续行尾空格
- B13：timer 统一 `int64_t`，未启动=0，`IsExpired`/`CheckExpired` 跳过 0
- B14：`epoll_create(size)`
- B15/D7：kqueue `EV_ERROR|EV_EOF`→`ST_EVERR`
- B16：`ST_DEBUG` 改 `#ifdef` 写法
- B17：`g_context_running` 重命名
- B18：kqueue `EV_DELETE` ENOENT 注释
- B19：singleton 限制注释（不改语义）
- B20：`SetBuffer` 边界 `len > max`
- 新增：`stlib/tests/st_netaddr_test`、`st_buffer_pool_test`；扩展框架 netaddr/hash/harvest 单测
- 三件套全绿；`format-check` 通过

出口条件满足 → 可进 Phase 3。

### Phase 3 · 性能与结构优化落地（2026-09-16）

- C1：`st_heap.h` 重写为标准最小堆 sift-up/sift-down（O(log n)）；删除全堆重建/`HeapUp`/`eOrderType`；`GetIndex`/`SetIndex` 约定不变。`st_heap_test` 加强断言 + `HeapMonotonic100k`；`tests/st_heap_unittest` 补单调性。
  - 基准（arm64，N=100000，本机一次）：`push_us≈10349–13502`，`pop_us≈45912–46065`（约 10–14 ms push / ~46 ms pop）。
- C3/D3：`Any`/`Holder` 移除；`any_cast` 退化为 `static_cast` 别名；`StHeap`/`StHashKey` 不再继承 `Any`；调用点未改。
- C4：`referenceable` 析构改为 `virtual`；`m_ref_count_` 改 private。
- C2：`st_def.h` 瘦身为宏所需（`stddef`/`stdlib`）；`math.h`/`queue` 下沉到 `st_util.h`。
- C7：`ST_BUFFER_HASH_BUCKETS` / `ST_BUFFER_DEFAULT_MAX_FREE`（数值仍 128）；`ST_ALGIN` 仅注释拼写（D4）。
- C8：删除 `ucontext.h` 中 `#if 0 && __sun__` SPARC 死块。
- C9：各 stlib 头 `g++ -fsyntax-only -std=c++98` 独立通过（mac 跳过 `st_epoll.h`）；为 `st_kqueue`/`st_epoll`/`st_tailq` 补齐自包含 include。
- C10：epoll/kqueue `Create` 统一失败走 `Free()`；fd 失败路径清 `-1`。
- C5/C6：此前 Phase 2 已覆盖（singleton 注释 / HashList）。
- 测试：`Any` 单测改为 `any_cast` + `referenceable`。
- 三件套：`make test` / `make lib`+`make -C tests run` / `make apps` 全绿；`format-check` 通过。**未 push**（按用户要求）。

出口条件满足 → 可进 Phase 4（文档 / D6 剥离 `st_test`）。

### Phase 4 · 通用库收尾落地（2026-09-16）

- D6：`st_test.o` 从 `libst` / `libmthread` 移除；`stlib/tests` 与 `tests/` 构建时单独编入 `st_test.cc`。
  - 验证：`ar t` / `nm` 产物中无 `StTester`；unittest / stlib tests 全绿。
- 文档：新增 [`stlib/README.md`](../stlib/README.md)（组件清单、线程模型、裸宏清单、最小示例指引）；根 `README.md` / `AGENTS.md` 同步（产物表去掉 `st_test.o`）；`thirdparty/readme.md` 已无 tiny/uthread 残留。
- 示例：`stlib/tests/st_demo_usage_test.cc`（堆 + 缓冲池 + `TimeMs`，可编译运行）；完整 HTTP 样例仍指向 `app/st_httpserver`。
- 依赖：`otool -L libmthread.so` / `libst.so` 仅系统库（libc++、libSystem）。
- 三件套 + `format-check` 全绿。**未 push**。

plan/06 全部 Phase 0–4 出口条件满足。
