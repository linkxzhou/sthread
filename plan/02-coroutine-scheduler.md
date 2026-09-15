# 02 · 协程与调度：ucontext / StThreadItem·StThread / StThreadSchedule / 堆与定时器

> 阶段目标一句话：**让 `src/st_thread.cc` 编译通过，并让「创建 / 让出 / 休眠 / 唤醒 / 父子协程」这五个动作有可运行的证据。**

## 硬约束（本阶段同样适用）

1. 不改变现有功能行为；对外 API 语义兼容。基准为「代码表达的语义意图」，详见 [`README.md`](README.md#对不改变现有功能行为这条约束的必要澄清)。
2. C++98。禁止 C++11+ 语言特性；`__thread` / `__builtin_expect` 等 GNU 扩展可用。
3. Linux + macOS 双平台可编译。**本阶段尤其关键**：macOS 会走 `USE_UCONTEXT 0` 的自带汇编分支。
4. Google C++ Style（见决策点 D1）。
5. 零第三方运行时依赖。
6. 协程调度 + epoll/kqueue；业务同步写法、框架内部异步；非阻塞 TCP/UDP 客户端；链接 `libmthread.a`/`.so` 即可使用。

---

## 1. 目标

1. `src/st_thread.cc` + `src/st_thread.h` + `src/st_poll.h` 编译通过（Linux 与 macOS）。
2. 解决 `context_switch()` / `context_exit()` **无声明、无定义**的问题——这是协程能否工作的命门。
3. 完成 D3 的命名统一，消灭新旧混杂。
4. 消除 `src/st_thread.h` 与 `app/thread.h` 的 `StThread` 重复定义。
5. 为协程核心行为建立单元测试（当前 `tests/st_thread_unittest.cpp` 与 `tests/st_ucontext_unittest.cpp` 存在但无法编译）。
6. 给上下文切换、栈分配这些「未定义行为高发区」补注释与断言，锁住现有语义。

## 2. 现状（点名真实类与文件）

### 2.1 协程的类型层次

```
stlib::StHeap                    (stlib/st_heap.h)      ← 可入堆元素，提供 HeapValue()
   └── sthread::StThreadItem     (src/st_poll.h:33)     ← 协程的「数据」部分，纯虚
          └── sthread::StThread  (src/st_thread.h:192)  ← 协程的「实现」部分（栈 + 上下文）

stlib::StHeap
   └── stlib::StTimer            (stlib/st_heap_timer.h:15)  ← 定时器元素

stlib::referenceable
   └── sthread::StEventItem      (src/st_poll.h:198)    ← fd 事件，03 阶段详述
```

`StThreadItem` 承载的状态（`src/st_poll.h:180-195`）：

```cpp
eThreadState  m_state_;      // eINITIAL/eRUNABLE/eRUNNING/eSLEEPING/ePENDING/eIOWAIT
eThreadType   m_type_;       // eNORMAL/ePRIMORDIAL/eDAEMON/eSUB_THREAD
eThreadFlag   m_flag_;       // 位掩码：eNOT_INLIST/eFREE_LIST/eIO_LIST/eSLEEP_LIST/eRUN_LIST/ePEND_LIST/eSUB_LIST
int64_t       m_wakeup_time_;// HeapValue() 返回它 → sleep 堆按唤醒时刻排序
Stack        *m_stack_;      // 栈与上下文
void         *m_private_;
StClosure    *m_callback_;   // 入口函数（stlib/st_closure.h）
StEventItemQueue m_fdset_;        // 本协程关心的 fd 集合
StThreadItemQueue m_sub_threadlist_; // 子协程链表
StThreadItem *m_parent_;
char          m_name_[64];
```

这些枚举定义在 `src/st_public.h:27-51`。**注意 `m_flag_` 是位掩码**（`eIO_LIST = 0x2`、`eSLEEP_LIST = 0x4`…），而 `m_state_` 是普通枚举值——两者语义不同，不能混用。

### 2.2 `StThreadSchedule`：四条 TAILQ + 一个 sleep 堆

`src/st_thread.h:16-120`。核心数据结构：

```cpp
StThreadItemQueue m_run_list_,      // 可运行队列
                  m_io_list_,       // IO 等待队列
                  m_pend_list_,     // 阻塞队列
                  m_reclaim_list_;  // 回收队列
StHeapList<StThreadItem> m_sleep_list_;  // 按 m_wakeup_time_ 排序的最小堆
StThreadItem *m_active_thread_, *m_daemon_, *m_primo_;
```

队列操作用 `stlib/st_tailq.h` 的 `CPP_TAILQ_*` 宏（`CPP_TAILQ_INIT` / `INSERT_TAIL` / `REMOVE` / `CONCAT` / `EMPTY` / `SIZE` / `REMOVE_SELF`）。

三种特殊协程：

| 角色 | 类型 | 创建处 | 职责 |
| --- | --- | --- | --- |
| primordial（主） | `ePRIMORDIAL` | `PrimoThread()`，`src/st_thread.h:47` | 代表最初的 OS 线程上下文，状态直接设为 `eRUNNING` |
| daemon（调度） | `eDAEMON` | `DaemonThread()`，`src/st_thread.h:35` | 回调为 `NewStClosure(Startup, this)`，即事件循环 |
| 普通 / 子协程 | `eNORMAL` / `eSUB_THREAD` | `CreateThread()` | 业务协程 |

调度 API：`SwitchThread` / `Yield` / `Sleep` / `Pend` / `Unpend` / `IOWaitToRunable` / `Insert*` / `Remove*` / `WakeupParent` / `Wakeup(now)` / `PopRunable` / `CreateThread` / `AllocThread`。

`AllocThread()` 走对象池：`Instance<UtilPtrPool<StThread>>()->AllocPtr()`（`stlib/st_util.h`）。配合 `Instance<T>()` 是**线程局部单例**（`stlib/st_singleton.h`，见 01 阶段 2.1(b)），意味着**每个 OS 线程有独立的调度器与对象池**——这是重要的设计语义，必须在注释里写明，且不能改。

### 2.3 `StThread`：栈分配与上下文初始化（**高危区**）

`src/st_thread.h:192-315`。三个关键函数：

**`InitStack()`（215-238 行）**

```cpp
m_stack_ = (Stack *)calloc(1, sizeof(Stack));
int memsize = MEM_PAGE_SIZE * 2 + (m_stack_size_ / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE;
void *vaddr = malloc(memsize * sizeof(uchar));
m_stack_->m_vaddr_ = (uchar *)vaddr;
m_stack_->m_vaddr_size_ = memsize;
m_stack_->m_stk_size_ = m_stack_size_;
m_stack_->m_id_ = Util::GetUniqid();
snprintf(m_name_, sizeof(m_name_) - 1, "T%u", m_stack_->m_id_);
sigset_t zero;
memset(&m_stack_->m_context_.uc, 0, sizeof(m_stack_->m_context_.uc));
sigemptyset(&zero);
sigprocmask(SIG_BLOCK, &zero, &m_stack_->m_context_.uc.uc_sigmask);
```

可疑点（**均不修改，只记录 + 加注释**）：
- `MEM_PAGE_SIZE` 定义为 **2048**（`src/st_public.h:85`），而 Linux/macOS 实际页大小是 4096/16384。这个名字有误导性，但它只是栈大小的对齐粒度，不是真页大小
- `malloc` 的栈内存**未做页对齐**，也**没有 guard page**（栈溢出会静默踩坏堆）
- `m_stack_size_` 初值为 `STACK`（`src/st_poll.h:38`），但 `STACK` 这个宏**全仓库 grep 不到定义** → 实测 `src/st_poll.h:38: error: 'STACK' was not declared in this scope`。这是一个**真实的编译错误**，且它决定默认栈大小，属必须解决项

**`InitContext()`（248-265 行）**

```cpp
uint32_t tx, ty;
uint64_t tz = (uint64_t)m_stack_;
ty = tz; tz >>= 16; tx = tz >> 16;
m_stack_->m_private_ = this;
getcontext(&m_stack_->m_context_.uc);
m_stack_->m_context_.uc.uc_stack.ss_sp   = m_stack_->m_vaddr_ + 8;
m_stack_->m_context_.uc.uc_stack.ss_size = m_stack_->m_vaddr_size_ - 64;
makecontext(&m_stack_->m_context_.uc, (void (*)())ActiveThreadStartUp, 2, ty, tx);
```

这是 libtask（Russ Cox）的经典手法：`makecontext` 的可变参数只能传 `int`，所以把 64 位指针拆成两个 32 位整数 `ty`/`tx`，在入口函数里再拼回来。`+ 8` 与 `- 64` 是为对齐与红区留的余量。

**这段代码属于「看起来可疑但实际有渊源」的类型，严禁在本阶段优化。** 唯一该做的是加注释说明 `ty`/`tx` 拆分原理。

**`ActiveThreadStartUp(uint ty, uint tx)`（284-314 行）**

```cpp
z = tx << 16; z <<= 16; z |= ty;     // 拼回指针
t = (Stack *)z;
StThread *thread = (StThread *)(t->m_private_);
StThreadSchedule *thread_schedule = Instance<StThreadSchedule>();
if (NULL != thread) {
  if (NULL != thread->m_callback_) thread->m_callback_->Run();
  if (thread->IsSubThread()) thread_schedule->WakeupParent(thread);
  thread_schedule->Yield(thread);
}
// ↓ 注意：上面 if (NULL != thread) 之外，这里直接解引用 thread
LOG_TRACE("... %s ...", thread->GetName());
if (thread == thread_schedule->DaemonThread()) {
  thread_schedule->SwitchThread(thread_schedule->PrimoThread(), thread);
} else {
  thread_schedule->SwitchThread(thread_schedule->DaemonThread(), thread);
}
context_exit(0);
```

问题：前面刚做了 `if (NULL != thread)` 的 NULL 检查，紧接着在 if 之外无条件 `thread->GetName()` 与 `thread == ...`。若 `thread` 为 NULL 则崩溃。属**意图无歧义**的缺陷（既然检查了就该一致），可修复，但修法需确认：是把后续逻辑移入 if 内，还是提前 return。

### 2.4 `context_switch` / `context_exit` 无声明（**本阶段最大阻塞**）

`src/st_thread.h` 中：

```
279: context_switch(&(thread->GetStack()->m_context_), &(this->GetStack()->m_context_));
313: context_exit(0);
```

实测：全仓库 grep `context_switch` / `context_exit` **只有这两处调用**，没有任何声明或定义。

而 `stlib/ucontext/ucontext.h` 实际提供的是：

```
59: extern int  getmcontext(mcontext_t *);
60: extern void setmcontext(const mcontext_t *);
61: #define setcontext(u) setmcontext(&(u)->uc_mcontext)
62: #define getcontext(u) getmcontext(&(u)->uc_mcontext)
63: extern int  swapcontext(ucontext_t *, const ucontext_t *);
64: extern void makecontext(ucontext_t *, void (*)(), int, ...);
```

`stlib/ucontext/asm.S` 导出的符号是 `GET` / `SET` 宏展开后的 `getmcontext`/`setmcontext`（多架构分支，实测 `.globl SET` / `.globl GET` 各出现 5 次，对应 386/amd64/mips/power 等）。

另外 `stlib/ucontext/uthread.h` / `uthread.c` 存在，可能是更高层封装——需在实施时确认它是否提供 `context_switch`。

**推断**：`context_switch(from, to)` 语义应等价于 `swapcontext(&from->uc, &to->uc)`；`context_exit(0)` 应等价于线程结束处理（可能是 `exit` 或回到调度器）。但**这是推断，不是事实**，需要确认 → 见第 9 节遗留问题。

### 2.5 命名混杂（D3 的具体证据）

`src/st_thread.cc:240`：

```cpp
m_thread_schedule_ = Instance<ThreadSchedule>();   // ThreadSchedule 无定义
```

`src/st_sys.h` 通篇用旧名：`Thread`（而非 `StThread`）、`EventSchedule` / `ThreadSchedule`、`StEventSuper`（而非 `StEventItem`）、`GlobalEventScheduler()` / `GlobalThreadScheduler()`（而 `src/st_public.h:89-90` 定义的是 `GlobalEventSchedule()` / `GlobalThreadSchedule()`）、`ASSERT`（而 `stlib/st_log.h` 提供 `LOG_ASSERT`）。

`src/st_sys.h:367-368` 还调用了 `SetDaemonThread()` / `SetPrimoThread()`，但 `StThreadSchedule` 提供的是 `DaemonThread()` / `PrimoThread()`（惰性创建的 getter，无 setter）——**语义还不一样**：`st_sys.h` 想「注入」自己 new 的 daemon/primo，而 `st_thread.h` 想「自己惰性创建」。这是两种设计在打架，属**必须由 D3 一并裁决**的内容。

完整映射表见 [`README.md` 决策点 D3](README.md#d3-新旧命名二选一是否允许-namespace-包装阻塞-02风险最高)。

### 2.6 `StThread` 重复定义

`src/st_thread.h:192` 与 `app/thread.h:1` **都定义了 `class StThread : public StThreadItem`**，成员签名几乎一致：

| | `src/st_thread.h` | `app/thread.h` |
| --- | --- | --- |
| `InitStack` / `FreeStack` / `InitContext` | 定义在类内（inline） | 只声明，无定义 |
| 入口函数 | `static void ActiveThreadStartUp(uint, uint)`（类内 static） | `static void ActiveThreadCallback(uint, uint)`（**文件级 static 函数**，类外） |
| namespace | `namespace sthread` | **无 namespace** |

因为 namespace 不同，严格说不是 ODR 冲突，但这显然是同一个类的两份拷贝（一份是重构残留）。`app/thread.h` 没有 include guard 之外的头部注释，且其 `ActiveThreadCallback` 与 `src` 版的 `ActiveThreadStartUp` 逻辑重复。

### 2.7 堆与定时器（`stlib/`，状态良好）

`stlib/st_heap.h`：`StHeap`（抽象元素，`HeapValue()` 纯虚）+ `StHeapList<T>`（`HeapPush`/`HeapPop`/`HeapTop`/`HeapDelete`/`HeapResize`/`HeapForeach`），`eOrderType { eOrderDesc, eOrderAsc }`。

`stlib/st_heap_timer.h`：
- `StTimer : StHeap` —— `m_time_expired_`、`SetExpiredTime`、`GetExpiredTime`、`IsExpired()`、虚 `Timeout()`
- `StHeapTimer` —— 持有 `StHeapList<StTimer>*`，提供 `Startup(timer, interval)` / `Stop(timer)` / `CheckExpired()`

**这部分在 01 阶段已实测编译通过且测试可跑**（`stlib/tests/st_heap_test.cc`、`st_heap_timer_test.cc`），是本阶段可以信赖的地基。

一个类型细节：`StTimer::m_time_expired_` 声明为 `uint64_t`，但初始化为 `-1`，且 `HeapValue()` 返回 `int64_t`。`IsExpired()` 里 `m_time_expired_ < Util::TimeMs()` 会做无符号比较。初值 `-1` 在无符号语义下是极大值（意为「永不过期」），配合 `HeapValue()` 转回 `int64_t` 变成 `-1`（意为「最早过期」）——**两处语义相反**。属可疑点，但涉及行为，记录不改。

### 2.8 `src/st_poll.h` 缺少必要 include（实测）

`src/st_poll.h` 用到 `eThreadType` / `eThreadState` / `eThreadFlag`（定义在 `src/st_public.h`）与 `Stack` / `STACK`（应来自 ucontext），但它**只 include 了** `stlib/st_util.h`、`stlib/ucontext/st_*.h`、`stlib/st_buffer.h`、`stlib/st_heap.h`、`stlib/st_netaddr.h`——**没有 include `st_public.h`**。

它靠「使用者先 include `st_public.h`」这种隐式顺序依赖工作（`src/st_thread.h` 确实是先 `st_poll.h` 再 `st_public.h`——顺序还是反的）。实测错误正是：

```
src/st_poll.h:59:23: error: 'eThreadFlag' has not been declared
src/st_poll.h:69:23: error: 'eThreadType' has not been declared
src/st_poll.h:91:10: error: 'Stack' does not name a type
src/st_poll.h:38:45: error: 'STACK' was not declared in this scope
src/st_poll.h:36:38: error: class 'sthread::StThreadItem' does not have any field named 'm_type_'
```

（最后一条是连带错误：`m_type_` 的类型 `eThreadType` 没声明成功，成员就不存在了。）

修复方向明确：头文件自包含（self-contained），即每个头 include 自己需要的一切。

## 3. 问题与风险

| # | 问题 | 影响 | 缓解 |
| --- | --- | --- | --- |
| P1 | `context_switch` / `context_exit` 无定义（2.4） | **完全阻塞**：协程无法切换 | 优先确认语义来源（`uthread.c`？上游 libtask？），不可凭猜实现 |
| P2 | `STACK` 宏无定义（2.3） | 编译错误 + 决定默认栈大小 | 需从上游或 git 历史找回原值；**不可随意取值**（影响内存占用与溢出风险） |
| P3 | 命名混杂（2.5），且 `SetDaemonThread` vs `DaemonThread()` 是设计冲突 | 改动面失控（R1） | 先冻结映射表 → 机械替换 → 再单独裁决设计冲突 |
| P4 | 上下文切换 / 栈布局是 UB 高发区（R5） | 崩溃难以定位 | 原样保留；只加注释与断言；ASan 默认关（见 01 P5） |
| P5 | macOS 走 `USE_UCONTEXT 0` 自带汇编分支，完全未验证（R4） | 双平台约束落空 | 01 阶段的 CI 必须先就位 |
| P6 | `ActiveThreadStartUp` 的 NULL 检查不一致（2.3） | 潜在崩溃 | 修复，但修法需确认 |
| P7 | `Instance<T>()` 是线程局部的，若使用方跨线程共享协程会静默出错 | 中 | 文档化（05 阶段）+ 加断言 |
| P8 | `StThreadSchedule` 析构里写着 `// TODO: 回收sthread`，协程对象实际不回收 | 内存泄漏 | **本阶段不修**（属功能补全，非修复）；登记为已知问题 |
| P9 | `m_reclaim_list_` 被 init 但 grep 全仓库无其它使用 | 死代码 | 记录，不删（可能是预留） |

## 4. 做 / 不做

### 做

- 确认并补上 `context_switch` / `context_exit` 的来源与声明
- 确认并补上 `STACK` 的定义
- 让 `src/st_poll.h` 自包含（补 `#include "st_public.h"` 等）
- 按 D3 结论统一命名（机械替换）
- 裁决 `SetDaemonThread`/`SetPrimoThread` vs `DaemonThread()`/`PrimoThread()` 的设计冲突
- 消除 `app/thread.h` 的 `StThread` 重复定义
- 修 `ActiveThreadStartUp` 的 NULL 检查不一致
- 给栈分配、`ty`/`tx` 指针拆分、线程局部单例语义加注释
- 加断言（`LOG_ASSERT`）守住不变量：`m_stack_ != NULL`、`m_active_thread_ != NULL`、状态迁移合法性
- 让 `tests/st_thread_unittest.cpp` 与 `tests/st_ucontext_unittest.cpp` 可编译可运行

### 不做

- **不**改 `MEM_PAGE_SIZE`、`+8`/`-64` 的偏移、栈大小计算公式
- **不**引入 guard page / mmap 栈 / 栈池（属优化，改行为）
- **不**把四条 TAILQ 换成其它容器
- **不**改 `m_sleep_list_` 的堆实现或排序语义
- **不**改 `StTimer::m_time_expired_` 的 `uint64_t` / 初值 `-1`（2.7 的可疑点，记录不改）
- **不**实现协程回收（P8，`m_reclaim_list_` 相关）
- **不**改 `Instance<T>()` 的线程局部语义
- **不**引入多线程（M:N）调度——当前是每 OS 线程一套调度器的 1:N 模型，改它是重新设计
- **不**碰 `StEventSchedule` / `StEventItem` 的实现（属 03）

## 5. 分步步骤

### 步骤 1：查清 `context_switch` / `context_exit` 与 `STACK`（**先做，它决定后面能否进行**）

这是本阶段唯一的「研究型」任务，必须先完成：

1. 检查 `stlib/ucontext/uthread.h` / `uthread.c` 是否定义了 `context_switch` / `context_exit`
2. 检查 `stlib/tests/ucontext/taskimpl.h` / `task.h` / `task.c`（libtask 上游样例）里的对应物——libtask 里有 `contextswitch(Context *from, Context *to)`，很可能就是原型
3. 检查 `Stack` 结构体的定义位置（`m_vaddr_`、`m_vaddr_size_`、`m_stk_size_`、`m_id_`、`m_private_`、`m_context_` 六个成员）与 `STACK` 宏
4. 查 git 历史：`git log --all -S 'context_switch' -- '*.h' '*.c' '*.cc'`、`git log --all -S 'define STACK'`

三种可能结果与对策：

| 结果 | 对策 |
| --- | --- |
| 在 `uthread.h` / `taskimpl.h` 里找到 | 补 include 即可，**最理想** |
| 在 git 历史里找到（被误删） | 恢复原定义，属「意图无歧义」的修复 |
| 完全找不到 | **需用户决策**：按 libtask 语义重新实现（`context_switch` ≈ `swapcontext`，`Stack::m_context_` 是含 `uc` 成员的联合/结构），但这已属「新写代码」，超出「不改行为」口径 |

**出口**：`context_switch`、`context_exit`、`Stack`、`STACK` 四者的来源全部有确定答案并记录在案。

### 步骤 2：冻结命名映射表（D3 落地的前置）

把 [`README.md` D3 的表格](README.md#d3-新旧命名二选一是否允许-namespace-包装阻塞-02风险最高)固化为一份可执行的替换清单，**并区分两类**：

**(A) 纯机械替换（旧名无定义，替换即修复）**

```
ThreadSchedule           → StThreadSchedule
EventSchedule            → StEventSchedule
Thread                   → StThread
StEventSuper             → StEventItem
StThreadSuper            → StThreadItem
StNetAddress             → StNetAddr
GlobalEventScheduler()   → GlobalEventSchedule()
GlobalThreadScheduler()  → GlobalThreadSchedule()
ASSERT(                  → LOG_ASSERT(
```

替换时必须注意 **`Thread` → `StThread` 不能无脑全文替换**：`StThread`、`StThreadItem`、`StThreadSchedule`、`DaemonThread`、`PrimoThread`、`CreateThread`、`AllocThread`、`SwitchThread`、`m_daemon_`… 里都含 `Thread` 子串。需用词边界正则（如 `\bThread\b`）并逐条复核。

**(B) 需设计裁决（两边都有定义，语义不同）**

| 冲突 | `src/st_thread.h` 的设计 | `src/st_sys.h` 的设计 | 建议 |
| --- | --- | --- | --- |
| daemon/primo 的所有权 | 惰性 getter `DaemonThread()` 自己 `new StThread()` | `StSysSchedule::Init()` 自己 `new Thread()` 再 `SetDaemonThread()` 注入 | 二者只能留一个。倾向保留 `st_thread.h` 的惰性 getter（它是新名侧、且自洽），`StSysSchedule` 改为调用 getter |
| 调度入口 | `StThreadSchedule::Startup(ss)` | `StSysSchedule::StartUp(schedule)` | 两个事件循环。需确认哪个是真正的 daemon 循环（`StSysSchedule::StartUp` 里有完整的 `Wait`→`Wakeup`→`CheckExpired`→`Yield` 循环，看起来更完整） |
| `Manager` | — | `src/st_server.h` 用 `Instance<Manager>()` | `Manager` 疑似 `StSysSchedule` 的旧名，需确认 |

**(B) 类冲突不属于「笔误」，必须由维护者裁决后才能动。**

**出口**：映射表定稿，(A) 类可执行，(B) 类有裁决结论。

### 步骤 3：头文件自包含化

按 01 阶段步骤 1 的 include 规范，让 `src/` 每个头文件自包含：

| 文件 | 需补 |
| --- | --- |
| `src/st_poll.h` | `st_public.h`（枚举）、ucontext 头（`Stack` / `STACK`） |
| `src/st_thread.h` | 检查 include 顺序（当前先 `st_poll.h` 后 `st_public.h`，顺序反了，自包含后无所谓） |
| `src/st_connection.h` | 它用了 `StEventSuper`/`GlobalEventScheduler`/`ST_RECV_BUFFSIZE`/`StTimer`/`StNetAddrKey`/`__close`，但只 include 了 `stlib/st_buffer.h`、`stlib/st_heap_timer.h`、`stlib/st_util.h` → 缺 `st_public.h`、`st_poll.h`、`app/st_sys.h`（`__close`）。**注意这是 03 阶段的文件**，此处仅登记 |

验证手段：对每个头文件单独做「孤立编译」：

```bash
for h in src/*.h; do
  echo "#include \"$h\"" > /tmp/selftest.cc
  echo "int main(){return 0;}" >> /tmp/selftest.cc
  g++ -std=c++98 -fsyntax-only -I. /tmp/selftest.cc || echo "NOT SELF-CONTAINED: $h"
done
```

**出口**：`src/st_poll.h`、`src/st_thread.h` 通过孤立编译。

### 步骤 4：执行 (A) 类机械替换 + (B) 类裁决落地

顺序要求：**先 (A) 后 (B)**。理由：(A) 完成后编译错误数量会大幅下降，(B) 的真实影响面才能看清。

每一步后都跑：

```bash
g++ -std=c++98 -fsyntax-only -I. src/st_thread.cc 2>&1 | grep -c error
```

记录 error 数递减曲线，作为进展证据。

**出口**：`src/st_thread.cc` 编译通过（Linux）。

### 步骤 5：消除 `app/thread.h` 重复定义

三个选项：

- (a) **推荐**：删除 `app/thread.h`（它的 `InitStack`/`FreeStack`/`InitContext` 只有声明没有定义，是不完整的残留），使用方改用 `src/st_thread.h`
- (b) 保留 `app/thread.h` 作为 `src/st_thread.h` 的转发头（`#include "src/st_thread.h"`）
- (c) 保留两份（不可接受）

需先 grep 确认有谁 include 了 `app/thread.h`（实测 include 清单里出现过 `thread.h`，需定位引用者）。

**出口**：全仓库只有一处 `class StThread` 定义。

### 步骤 6：加断言与注释（锁住语义，不改语义）

断言（用 `stlib/st_log.h` 的 `LOG_ASSERT`）：

- `StThread::InitStack()` 后 `m_stack_ != NULL && m_stack_->m_vaddr_ != NULL`
- `RestoreContext()` 入口 `thread != NULL && thread->GetStack() != NULL`
- `SwitchThread()` 入口 两个参数均非 NULL
- `Yield()` / `Sleep()` 入口 `thread == m_active_thread_`（若这是不变量，需先确认）
- `ActiveThreadStartUp()` 里 `t != NULL && t->m_private_ != NULL`

注释（关键的四处）：

1. `InitContext()` 的 `ty`/`tx` 拆分：说明 `makecontext` 可变参数只能传 `int`，故 64 位 `Stack*` 拆两半，入口再拼回
2. `ss_sp + 8` / `ss_size - 64`：说明是对齐与红区余量，来自 libtask，勿动
3. `Instance<T>()` 的线程局部性：说明每个 OS 线程一套调度器与对象池，协程不可跨 OS 线程迁移
4. `m_flag_` 是位掩码而 `m_state_` 是枚举值，两者不可混用

**注意**：加断言会**改变失败路径的行为**（原本静默 UB，现在触发断言）。这属于「让缺陷更早暴露」，但严格说是行为变化。建议断言只在 `DEBUG` 构建生效——需确认 `LOG_ASSERT` 在非 DEBUG 下是否被编译掉。

**出口**：注释与断言就位，且 release 构建行为不变。

### 步骤 7：建立协程单元测试

现有 `tests/` 下相关文件：`st_thread_unittest.cpp`、`st_ucontext_unittest.cpp`、`uthread_unittest.cpp`（+ `uthread.h`/`uthread.cpp`）、`st_base_unittest.cpp`、`st_singleton_unittest.cpp`。它们当前都因 `../include/...` 路径问题无法编译（01 阶段 2.5）。

本阶段需覆盖的行为（每条都要有可运行的断言）：

| # | 用例 | 断言 |
| --- | --- | --- |
| T1 | 创建 N 个协程并全部运行一遍 | 每个协程的回调都被调用恰好一次，顺序符合 `PopRunable` 的 FIFO 语义 |
| T2 | `Yield` 往返 | 主协程 → 子协程 → 回主协程，`GetActiveThread()` 正确切换 |
| T3 | `Sleep(ms)` | 唤醒时刻 ≥ 设定时刻；多个 sleep 协程按 `m_wakeup_time_` 升序唤醒（验证 sleep 堆） |
| T4 | `Wakeup(now)` | 只唤醒已到期的，未到期的仍在 `m_sleep_list_` |
| T5 | 父子协程 | `AddSubThread` / `RemoveSubThread` / `HasNoSubThread` / `WakeupParent` / `GetRootThread()` 多级向上 |
| T6 | `Pend` / `Unpend` | 状态与所在队列正确迁移 |
| T7 | 状态与 flag 一致性 | `m_state_` 与 `m_flag_` 不出现矛盾组合（如 `eRUNABLE` 但带 `eSLEEP_LIST`） |
| T8 | 大量协程冒烟 | 创建 10000 协程不崩溃（对应验收清单 C 的高并发项） |

T5 的 `GetRootThread()` 需注意：它的循环 `while (eSUB_THREAD == type)` 在链中出现非 `eSUB_THREAD` 时才停——需构造多级父子链验证。

**出口**：T1～T8 可编译、可运行、全部通过。

### 步骤 8：macOS 验证

在 01 阶段建好的 CI 上跑步骤 4 与步骤 7 的结果。重点关注：

- `stlib/ucontext/ucontext.h` 的 `__APPLE__ && MAC_OS_X_VERSION_10_5` → `USE_UCONTEXT 0` 分支，即用自带 `asm.S` 而非系统 `ucontext`
- Apple Silicon（arm64）：`asm.S` 实测只有 386/amd64/mips/power 分支，**没有 arm64**！这意味着 macOS on Apple Silicon 很可能根本不支持 → 需确认是否要求支持（见第 9 节）
- `sigprocmask` / `sigemptyset` 在 macOS 上的行为
- `__thread` 在 Apple clang 下的支持（01 阶段 P7）

**出口**：macOS 上的结论明确（通过 / 不支持某架构 / 需补分支）。

## 6. 兼容策略

| 对象 | 策略 |
| --- | --- |
| `StThreadItem` / `StThread` 的公开方法签名 | **不变**。这是使用方直接接触的 API |
| 命名统一（D3(a)） | 旧名无定义，替换不破坏任何**能编译**的使用方。若需保险，按 D3(c) 加 `typedef` 兼容层 |
| `namespace sthread` 归属 | `src/st_connection.h` / `st_server.h` 若移入 namespace 会改变 mangled name（ABI 变化）→ **需 D3 明确授权** |
| 枚举值（`eThreadType`/`State`/`Flag`/`eConnType`） | **数值一律不变**（`eNORMAL=0x1`…）。这些值可能被使用方硬编码 |
| 错误码 `eERR_*` | 数值不变（`eERR_NONE=0` … `eERR_DEST_ADDR_ERROR=-13`） |
| `STACK`（默认栈大小） | **必须找回原值**。随意取值会改变每协程内存占用 → 直接改变「能开多少协程」这一核心卖点 |
| `context_switch` 实现 | 若必须重写，需与 `asm.S` 的调用约定严格一致，且双平台双架构验证 |
| 断言 | 只在 DEBUG 生效，release 行为不变 |
| `app/thread.h` 删除 | 若有使用方 include 它，属破坏性变更 → 用 (b) 转发头兜底 |

## 7. 验收

- [ ] `context_switch` / `context_exit` / `Stack` / `STACK` 四者来源确定并记录
- [ ] `src/st_poll.h`、`src/st_thread.h` 通过孤立编译（自包含）
- [ ] `g++ -std=c++98 -fsyntax-only -I. src/st_thread.cc` 零 error（Linux）
- [ ] 同上（macOS / clang++）
- [ ] D3 的 (A) 类替换全部完成，全仓库 grep 不到 `ThreadSchedule`（不带 St 前缀的独立词）、`StEventSuper`、`StThreadSuper`、`StNetAddress`、`GlobalEventScheduler`、`GlobalThreadScheduler`
- [ ] D3 的 (B) 类设计冲突已裁决并落地
- [ ] 全仓库只有一处 `class StThread` 定义
- [ ] `ActiveThreadStartUp` 的 NULL 检查一致
- [ ] 六处关键断言就位，且 release 构建不受影响
- [ ] 四处关键注释就位
- [ ] T1～T8 单元测试全部通过（Linux + macOS）
- [ ] 10000 协程冒烟无崩溃、无 fd 泄漏
- [ ] 枚举值与错误码数值未变（可用 `git diff` 核验 `src/st_public.h`）
- [ ] Apple Silicon 支持与否有明确结论

## 8. 依赖与工作量

**前置依赖**：
- **01 阶段**必须完成（绿色 `stlib` 基线 + 根 makefile + CI）。本阶段的每一次验证都依赖 `stlib` 可编译
- **决策点 D3** 必须拍板（命名 + namespace 授权），否则步骤 2/4 无法执行
- **步骤 1 的研究结论**是内部前置，它可能反过来触发新的用户决策

**被依赖**：03 阶段（`StEventSchedule` 需要可用的协程切换）、04 阶段（所有 app 与 test）。

**改动面**：

| 子系统 | 文件 | 侵入程度 |
| --- | --- | --- |
| `src/st_poll.h` | 1 | 低：补 include；类定义本身不动 |
| `src/st_thread.h` | 1 | 中：命名替换 + 注释 + 断言 + NULL 检查修复 |
| `src/st_thread.cc`（545 行） | 1 | 中：命名替换为主 |
| `src/st_sys.h` | 1 | **高**：旧名密集，且含 (B) 类设计冲突 |
| `stlib/ucontext/*` | 1～3 | 视步骤 1 结论：若只需补 include 则极低；若需实现 `context_switch` 则**高** |
| `app/thread.h` | 1 | 低：删除或改转发 |
| `tests/st_thread_unittest.cpp` 等 | 3～5 | 中：需先修 include，再补 T1～T8 |

**风险集中点**（按严重度）：

1. **步骤 1 若「完全找不到」** → 需要新写上下文切换代码，这会突破「不改行为」口径，须用户重新授权。**这是本阶段最大的不确定性。**
2. **Apple Silicon 无 arm64 汇编分支** → 若要求支持 macOS on ARM，需新增 arm64 上下文切换汇编，属重大新增工作，应独立排期。
3. **(B) 类设计冲突** → `st_thread.h` 与 `st_sys.h` 存在两个 daemon 循环与两套 daemon 所有权模型，裁错方向会导致 03 阶段返工。

## 9. 遗留问题（需在本阶段内查清或上报）

| # | 问题 | 处理 |
| --- | --- | --- |
| L1 | `context_switch` / `context_exit` 的准确语义 | 步骤 1；若查不到 → **上报用户** |
| L2 | `STACK` 的原值 | 步骤 1；若查不到 → **上报用户**（不可随意取值） |
| L3 | `Stack` 结构体定义位置 | 步骤 1 |
| L4 | Apple Silicon (arm64) 是否在支持范围内 | **上报用户**：`asm.S` 无 arm64 分支 |
| L5 | `Manager` 是否等于 `StSysSchedule` | 步骤 2(B) |
| L6 | 真正的 daemon 事件循环是 `StThreadSchedule::Startup` 还是 `StSysSchedule::StartUp` | 步骤 2(B) |
| L7 | `m_reclaim_list_` 是预留还是死代码 | 记录，不删 |
| L8 | `StTimer::m_time_expired_` 的 `uint64_t` 初值 `-1` 与 `HeapValue()` 返回 `int64_t` 语义相反 | 记录，不改；05 阶段文档化 |
| L9 | 协程对象不回收（`// TODO: 回收sthread`） | 记录为已知问题，独立排期 |


## 10. 实现期更正（本地分支 impl-plan-02-scheduler，2026-09-15）

### 10.1 context_switch / context_exit / Stack / STACK 研究结论

| 符号 | 来源 | 处理 |
| --- | --- | --- |
| `STACK` | 历史 `stlib/ucontext/st_ucontext.h`：早期 `131072`，`478d209` 为 **`260096`（256K）** | 恢复为 `260096` |
| `Context` / `Stack` | 同上历史头文件 | 恢复 |
| `context_switch` / `context_exit` / `context_init` | 历史 `stlib/st_ucontext.cc`（`swapcontext` 封装） | 恢复实现到 `stlib/st_context.cc` |

厂商 `stlib/ucontext/ucontext.{h,c,asm.S}` 只提供 get/set/swap/makecontext；调度器面向的 Stack/context_switch 是项目层封装，在 ucontext 更新提交中被摘丢。本阶段新增：

- `stlib/st_context.h`
- `stlib/st_context.cc`

并编入 `stlib/makefile` 与 `src/makefile` 的 `libmthread`。

### 10.2 其它落地

- `src/st_poll.h`：补 `st_public.h` + `st_context.h`；`StEventItem` 调到 `StThreadItem` 之前；删除孤儿 `m_type_ = eEVENT_UNDEF`
- `st_epoll.h` / `st_kqueue.h`：删除非法 `memset(m_file_.data, …)`（已被整块 `memset(m_file_,0,…)` 覆盖）
- D3(A) 命名统一到 `St*` / `Global*Schedule` / `LOG_ASSERT`
- D3(B)：`StSysSchedule` 通过 getter 别名 daemon/primo，析构不再 delete；`StThreadSchedule::Startup` 转发 `StSysSchedule::StartUp`
- `app/thread.h` 改为转发 `src/st_thread.h`
- `ActiveThreadStartUp` NULL 检查改为早退
- flag/state：列表成员用 `SetFlag`/`UnsetFlag`/`HasFlag`，生命周期用 `SetState`

### 10.3 验证

- `g++ -std=c++98 -fsyntax-only -I. src/st_thread.cc` → **0 error**（本机 Apple clang；arm64 仍会走到 `ucontext-power.h` 警告，属已知限制）

### 10.4 遗留（交 03+）

- D4：`src/st_sys` 与 `app/st_sys` 双套 `extern "C"` 符号冲突
- Apple Silicon arm64 汇编缺失
- 协程单测 T1–T8 尚未在本机完整跑通（优先保证 `st_thread.cc` 可编译）
- L4 keepalive `&`→`|` 单独阶段
