# 03 · IO 多路复用与网络：epoll·kqueue / StEventSchedule / StConnection·StServer / sys hook / TCP·UDP

> 阶段目标一句话：**让 `libmthread.a` / `libmthread.so` 真正产出，并让 TCP/UDP 的非阻塞收发在两个平台上都有可运行的证据。**

## 硬约束（本阶段同样适用）

1. 不改变现有功能行为；对外 API 语义兼容。基准为「代码表达的语义意图」，详见 [`README.md`](README.md#对不改变现有功能行为这条约束的必要澄清)。
2. C++98。禁止 C++11+ 语言特性；`__thread` / `__builtin_expect` 等 GNU 扩展可用。
3. Linux + macOS 双平台可编译。**本阶段是双平台差异的集中地**：epoll vs kqueue。
4. 本仓库代码风格（LLVM 基线 + `m_x_`，见决策点 D1）。
5. **零第三方运行时依赖**。本阶段产出 `libmthread`，是这条约束的最终验证点（`ldd` / `otool -L` 只应含系统库）。
6. 协程调度 + epoll/kqueue；**业务同步写法、框架内部异步**；非阻塞 TCP/UDP 客户端；链接 `libmthread.a`/`.so` 即可使用。

---

## 1. 目标

1. 修复 `stlib/st_epoll.h` 与 `stlib/st_kqueue.h` 中**实测存在的硬编译错误**。
2. 让 `src/st_connection.cc`、`src/st_sys.cc` 编译通过，`src/st_connection.h`、`src/st_server.h` 自包含。
3. 解决 D4：两套同名 `extern "C" __*` 符号的签名冲突。
4. 修正 `src/st_sys.cc` 与 `src/st_sys.h` 之间的函数名不一致。
5. **实际产出** `libmthread.a` 与 `libmthread.so`，并确认其符号表与依赖。
6. 建立 TCP/UDP 回环收发测试，双平台一致性验证。
7. 补上 `StEventSchedule::m_thread_schedule_` 的初始化缺陷（R6）。

## 2. 现状（点名真实类与文件）

### 2.1 双后端 `StIOState`：同名类、同头保护宏、条件编译二选一

| 文件 | 后端 | `ApiName()` | 句柄成员 |
| --- | --- | --- | --- |
| `stlib/st_epoll.h` | epoll（Linux） | `"epoll"` | `m_epfd_` |
| `stlib/st_kqueue.h` | kqueue（macOS/BSD） | `"kqueue"` | `m_kqfd_` |

两者都在 `namespace stlib` 里定义**同名**的 `class StIOState`，接口一致：

```cpp
int32_t Create(int32_t size);
int32_t ApiResize(int32_t setsize);
void    Free();
int32_t AddEvent(int32_t fd, int32_t mask);
int32_t DelEvent(int32_t fd, int32_t delmask);
int32_t Poll(struct timeval *tvp = NULL);
StFiredEvent *EventList();
const char   *ApiName();
```

选择逻辑在 `src/st_poll.h:11-15`：

```cpp
#if defined(__APPLE__) || defined(__OpenBSD__)
#include "stlib/ucontext/st_kqueue.h"     // 路径错误，见 01 阶段 2.5
#else
#include "stlib/ucontext/st_epoll.h"      // 路径错误
#endif
```

这个设计是合理的（编译期二选一，零运行时开销，符合约束 6）。但有三个问题：

**(a) 两个文件用同一个 include guard `_ST_EVENT_H_`**（`st_epoll.h:160` 与 `st_kqueue.h:335` 行处均为 `#ifndef _ST_EVENT_H_`）。因为条件编译保证只进一个，当前不会出错，但这是脆弱设计——一旦有人同时 include（比如为了写对比测试），后 include 的会被静默跳过，产生极难排查的错误。

**(b) `DATA_SIZE` 在两个文件里都 `#define DATA_SIZE 1`**，`st_epoll.h` 还额外定义 `EVENT_SIZE 1024`。无 `#undef`，无前缀，属宏污染。

**(c) 类型 `StFiredEvent` / `StFileEvent` 在两个文件里重复定义**（成员相同）。同理靠条件编译规避。

### 2.2 `st_epoll.h` 的两个硬编译错误（实测）

实测命令与输出：

```
$ g++ -fsyntax-only -D__THREAD=__thread -I. -Isrc -Istlib <shims> src/st_thread.cc
stlib/st_epoll.h:48:20: error: request for member 'data' in
  '((stlib::StIOState*)this)->stlib::StIOState::m_file_',
  which is of pointer type 'stlib::StFileEvent*' (maybe you meant to use '->' ?)
stlib/st_epoll.h:97:5: error: 'm_file_events_' was not declared in this scope;
  did you mean 'm_events_'?
```

**错误 1（`Create()` 内）**：

```cpp
memset(m_file_, 0, sizeof(StFileEvent) * size);
memset(m_fired_, 0, sizeof(StFiredEvent) * size);
memset(m_file_.data, 0, sizeof(void *) * DATA_SIZE);   // ← m_file_ 是指针，且无 data 成员
```

两个问题叠加：`m_file_` 是 `StFileEvent*`（该用 `->`），而 `StFileEvent` 的成员只有 `mask` 与 `client_data`——**根本没有 `data` 成员**。有 `data[DATA_SIZE]` 的是 `StFiredEvent`。

而且前两行 `memset` 已经把 `m_file_` 与 `m_fired_` 整块清零了，第三行是**完全多余**的。意图分析：这一行想清零的 `data` 数组已被第二行覆盖 → **删除该行即为正确修复**，语义无损失。

**错误 2（`AddEvent()` 内）**：

```cpp
int32_t op = (m_file_[fd].mask == ST_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
...
mask |= m_file_[fd].mask;         // ← 读的是 m_file_
...
m_file_events_[fd].mask = mask;   // ← 写的是 m_file_events_（不存在）
```

同一函数内读用 `m_file_`、写用 `m_file_events_`，而类成员只声明了 `m_file_`。且 `DelEvent()` 里的对应写法是 `m_file_[fd].mask = mask;`（正确）。**意图无歧义：应为 `m_file_[fd].mask = mask;`**。

**这个 bug 的后果值得强调**：`AddEvent` 从不更新 `m_file_[fd].mask`，导致
- 开头的 `if (m_file_[fd].mask == mask) return ST_OK;` 去重永远不生效
- `op` 永远算成 `EPOLL_CTL_ADD`（因为 mask 恒为 `ST_NONE`），第二次对同一 fd 加事件会返回 `EEXIST`
- `mask |= m_file_[fd].mask` 的「合并旧事件」逻辑失效

即：**读写事件的注册在 epoll 路径上从根本上是坏的。** 这也解释了为什么这个仓库的 IO 部分没有可运行的证据。

### 2.3 `st_kqueue.h` 的同类错误与额外问题

**错误 1（同 2.2）**：`Create()` 里同样有 `memset(m_file_.data, 0, sizeof(void *) * DATA_SIZE);`。修复同理（删除该行）。

kqueue 的 `AddEvent` 没有 `m_file_events_` 错误（它正确地写 `m_file_[fd].mask = mask;`），所以 kqueue 路径的 mask 跟踪是对的。

**额外问题（均需确认是否修复）**：

| 位置 | 问题 |
| --- | --- |
| `AddEvent()` 的 `EV_DELETE` 分支 | `if (kevent(...) == -1) { }` —— **空的 if 体**。意图可能是「删除失败可忽略（本来就没注册）」，但空块会触发编译器告警，且意图不明确。应加注释说明「忽略 ENOENT」 |
| `Free()` | 不像 epoll 版那样把 `m_size_ = 0`。epoll 版的 `Free()` 有 `m_size_ = 0;`，kqueue 版没有 → **两平台行为不一致** |
| `ApiResize()`（两个版本都有） | `m_file_` 与 `m_fired_` 用 `malloc` **覆盖**旧指针（不是 `realloc`），旧内存泄漏，且旧的 mask 状态全部丢失。epoll 版对 `m_events_` 用了 `realloc`，对另两个用 `malloc` → 前后不一致 |
| 缺少 include | 两个文件都只 include `st_def.h`，但用到 `fcntl` / `F_SETFD` / `FD_CLOEXEC`（需 `<fcntl.h>`）、`::close`（需 `<unistd.h>`，`st_def.h` 有）、`malloc`/`realloc`（`st_def.h` 有 `<stdlib.h>`）。`fcntl.h` 是真缺的——当前靠使用方先 include `stlib/st_util.h`（它有 `<fcntl.h>`）兜底 |
| `AddEvent` 越界 | `m_file_[fd]` 无边界检查。`Create(size)` 分配 `size` 个，但 `fd` 可以是任意值。`StEventSchedule` 侧有 `IsValidFd` 但 `StIOState` 自己没有 |

### 2.4 `StEventSchedule`：事件调度器

`src/st_thread.h:122-190`。构造：

```cpp
StEventSchedule(int32_t max_num = 1024)
    : m_maxfd_(65535), m_iostate_(new StIOState()), m_event_(NULL),
      m_timeout_(30000)     // 默认超时 30s
{ int32_t r = Init(max_num); LOG_ASSERT(r >= 0); }
```

**注意初始化列表里没有 `m_thread_schedule_`** —— 它在 `src/st_thread.cc:240` 的 `Init()` 里赋值：

```cpp
m_thread_schedule_ = Instance<ThreadSchedule>();   // ThreadSchedule 无定义（02 阶段 D3）
LOG_ASSERT(m_thread_schedule_ != NULL);
```

这就是 R6：**成员未在构造列表初始化，且赋值语句用的是不存在的类型名**。修复 = D3 命名统一 + 加入初始化列表（置 NULL）。它在 `Dispatch()`（`st_thread.cc:456,468`）与 `Schedule()`（`522-524`）里被解引用，一旦为空即崩溃。

`Init()` 的 fd 容量逻辑（`st_thread.cc:211-241`）：

```cpp
m_maxfd_ = ST_MAX(max_num, m_maxfd_);          // 65535 vs 1024 → 取 65535
int rc = m_iostate_->Create(m_maxfd_);
m_event_ = (StEventItemPtr *)malloc(sizeof(StEventItemPtr) * m_maxfd_);
memset(m_event_, 0, sizeof(StEventItemPtr) * m_maxfd_);
// 随后用 getrlimit/setrlimit 把 RLIMIT_NOFILE 提到 m_maxfd_
if ((int)rlim.rlim_max < m_maxfd_) { rlim.rlim_cur = m_maxfd_; rlim.rlim_max = m_maxfd_; ... }
```

这部分逻辑是自洽的（`m_event_` 数组与 `StIOState` 内部数组都按 `m_maxfd_` = 65535 分配），与 `IsValidFd(fd)`（`fd >= m_maxfd_ || fd < 0` 为非法）一致。**这是 IO 层少见的正确部分。**

一个可疑点：`src/st_def.h` 定义 `ST_MAX_FD 65535 * 2`（无括号的宏，`65535 * 2` = 131070），但 `StEventSchedule` 用的是硬编码 `65535`，两者不一致。且 `ST_MAX_FD` 未加括号，在 `ST_MAX_FD / 2` 这类表达式里会算错——属宏卫生问题，记录。

关键方法：
- `Schedule(thread, item_list, item, wakeup_timeout)` —— 把协程挂到 IO 等待并让出
- `Add` / `Delete`（单个与队列两个重载）、`AddFd` / `DeleteFd`
- `BindItem(fd, item)` / `GetEventItem(fd)` / `ClearItem(item)`
- `Dispatch(fdnum)` —— 把就绪事件派发回协程（`EvInput`/`EvOutput`/`EvHangup`）
- `Wait(timeout)` —— 调 `StIOState::Poll` 后转 `Dispatch`

**`BindItem` 有一处逻辑疑点**：

```cpp
inline void BindItem(int32_t fd, StEventItem *item) {
  if (unlikely(IsValidFd(fd))) { m_event_[fd] = item; }
}
```

用 `unlikely()` 包裹「fd 合法」这个**正常路径**。`unlikely` 是给编译器的分支预测提示，用在这里是反的（合法 fd 才是常态）。不影响正确性，只影响性能提示。对比 `ClearItem` 里是 `if (unlikely(!IsValidFd(osfd)))`（用在非法路径，正确）。属可疑点，记录。

### 2.5 `StEventItem`：fd 事件

`src/st_poll.h:198-266`，继承 `stlib::referenceable`。

```cpp
int32_t m_fd_, m_events_, m_revents_;
StThreadItem *m_thread_;      // 所属协程
StEventItemNext m_next_;      // TAILQ 链接
```

虚回调 `EvInput()` / `EvOutput()` / `EvHangup()`（默认只打 LOG_TRACE，由使用方派生覆盖）。事件掩码操作：`EnableInput` / `EnableOutput` / `DisableInput` / `DisableOutput`，用 `ST_READABLE`(1) / `ST_WRITEABLE`(2) / `ST_EVERR`(4)（`stlib/st_def.h`）。

**缺陷**：`Reset()` 里写 `m_type_ = eEVENT_UNDEF;`，但

- `StEventItem` **没有 `m_type_` 成员**（成员只有 `m_fd_`/`m_events_`/`m_revents_`/`m_thread_`/`m_next_`）
- `eEVENT_UNDEF` **全仓库无定义**

→ 又一处编译错误，且属「残留」性质（某个版本有过 `m_type_`）。修法需确认：是补回 `m_type_` + 枚举，还是删掉这行。倾向删除（因为其它地方都不用 `m_type_`），但需确认没有使用方依赖事件类型。

### 2.6 `StConnection` 家族

`src/st_connection.h`（223 行）+ `src/st_connection.cc`（113 行）。**该文件不在任何 namespace 内**（裸 `class StConnection`）——与 `src/st_thread.h` 的 `namespace sthread` 不一致，属 D3 的 namespace 授权范围。

```
referenceable
  └── StConnection                        ← 基类：fd + 收发 buffer + 地址 + 超时
        ├── StClientConnection<ConnectionT>  (: StConnection, StTimer)  ← 客户端，带定时器
        └── StServerConnection<ConnectionT>  (src/st_server.h)          ← 服务端（当前是空壳）
StConnectionManager<ConnectionT>          ← 连接池，用 StHashList<StNetAddrKey> 做 keepalive 复用
```

`StConnection` 的成员与方法：

```cpp
int m_osfd_;
StBuffer *m_sendbuf_, *m_recvbuf_;     // 来自 Instance<StBufferPool<> >()（stlib/st_buffer.h）
StNetAddr m_addr_, m_destaddr_;
eConnType m_type_;
int32_t   m_timeout_;                   // 默认 30000
StEventSuper *m_item_;                  // ← 旧名，应为 StEventItem（D3）

int32_t SendData();  int32_t RecvData();
virtual int32_t DoOutput(void*, int32_t&);  virtual int32_t DoInput(void*, int32_t);
virtual int32_t DoProcess();                virtual int32_t DoError(int32_t);
```

buffer 尺寸来自 `ST_RECV_BUFFSIZE` / `ST_SEND_BUFFSIZE`（均 8192，`stlib/st_def.h`）。

**「业务同步写法、框架内部异步」的落点就在这里**：`SendData()` / `RecvData()` 对业务是同步阻塞语义，内部实现是「发不完就注册 `ST_WRITEABLE` 事件 + `Yield`，被 `Dispatch` 唤醒后继续」。这是整个框架的核心价值主张，**必须在实施中重点验证，并在 05 阶段文档化**。

`StClientConnection::Create(addr)` 的流程（`st_connection.h:195-230`）：

```cpp
m_destaddr_ = addr;
int protocol = IS_UDP_CONN(m_type_) ? SOCK_DGRAM : SOCK_STREAM;
m_osfd_ = __socket(addr.IsIPV6() ? AF_INET6 : AF_INET, protocol, 0);
m_item_ = Instance<UtilPtrPool<ConnectionT> >()->AllocPtr();
m_item_->SetOsfd(m_osfd_); m_item_->EnableOutput(); m_item_->DisableInput();
GlobalEventScheduler()->Add(m_item_);              // ← 旧名 + 代码里带 "// TODO:"
if (IS_TCP_CONN(m_type_)) {
  int32_t rc = Connect(addr);
  if (rc < 0) { GlobalEventScheduler()->Close(m_item_); /* TODO: */ ... }
}
```

问题清点：
- `GlobalEventScheduler()` → 应为 `GlobalEventSchedule()`（D3）
- `GlobalEventSchedule()->Close(item)` —— `StEventSchedule` **没有 `Close` 方法**（只有 `Delete` / `ClearItem`）。需确认意图是哪个（倾向 `ClearItem`，它做了 `Delete` + 清 `m_event_[fd]`）
- 代码里有两处 `// TODO:` 注释，说明作者自己也知道这里未完成
- `ASSERT` → `LOG_ASSERT`（D3）
- UDP 分支**不调用 `Connect`**，符合 UDP 语义（正确）

`Connect()` 的错误码处理有一处可疑：`EINPROGRESS` / `EALREADY` / `EINTR` 返回 `-1`，其它错误返回 `-2`，`EISCONN` 返回 0。但对非阻塞 socket，`EINPROGRESS` 是**正常路径**（应该去等 `ST_WRITEABLE` 事件），这里当错误返回。需确认 `__connect(fd, addr, len, timeout)` 这个带超时的封装是否已在内部完成了等待——若是，则到达此处的 `EINPROGRESS` 确实算超时失败，逻辑成立。**这是理解「框架内部异步」的关键，必须查清。**

`StConnectionManager<T>` 的 keepalive 复用依据 `IS_KEEPLIVE(type)`：

```cpp
#define KEEPLIVE_VALUE 0x1
#define IS_KEEPLIVE(type) (((type) & KEEPLIVE_VALUE) == KEEPLIVE_VALUE)
```

而 `eConnType`（`src/st_public.h:61-67`）：

```cpp
eUNDEF_CONN          = 0x0,
eUDP_CONN            = 0x10,
eTCP_CONN            = 0x20,
eTCP_KEEPLIVE_CONN   = (0x10 & KEEPLIVE_VALUE),   // = 0x10 & 0x1 = 0x0  ← ！
eUDP_UDPSESSION_CONN = (0x20 & KEEPLIVE_VALUE),   // = 0x20 & 0x1 = 0x0  ← ！
```

**这是一个实打实的逻辑 bug**：用 `&` 而非 `|`，导致两个 keepalive 类型的值**都等于 0**，即等于 `eUNDEF_CONN`。后果：

- `eTCP_KEEPLIVE_CONN == eUDP_UDPSESSION_CONN == eUNDEF_CONN == 0`
- `IS_KEEPLIVE(eTCP_KEEPLIVE_CONN)` = `(0 & 1) == 1` = **false** → keepalive 连接复用**完全不生效**
- `IS_TCP_CONN(eTCP_KEEPLIVE_CONN)` = `(0 == 0x20) || (0 == 0)` = true（歪打正着）
- `IS_UDP_CONN(eUDP_UDPSESSION_CONN)` = `(0 == 0x10) || (0 == 0)` = true（同样歪打正着）
- 但两者**无法互相区分**，UDP session 和 TCP keepalive 是同一个值

显然作者意图是 `(0x10 | KEEPLIVE_VALUE)` = 0x11 和 `(0x20 | KEEPLIVE_VALUE)` = 0x21，配合注释「规则是最后一位0x1则表示需要保存状态」——注释已明确说明了意图，**`&` 是 `|` 的笔误**。

**但这属于「改枚举值」**，直接违反 01/02 阶段「枚举数值不变」的纪律，且会改变可观测行为（keepalive 从不生效变为生效）。→ **必须上报用户决策**，见第 9 节 L4。

### 2.7 `StServer`：服务端（旧名最密集的文件）

`src/st_server.h`（153 行）。**不在任何 namespace 内**。include 了四个头，其中三个路径有问题：

```cpp
#include "st_connection.h"   // OK
#include "st_manager.h"      // ← 不存在（D3：疑为 st_sys.h 的旧名）
#include "st_netaddr.h"      // ← 应为 stlib/st_netaddr.h
#include "st_util.h"         // ← 应为 stlib/st_util.h
```

旧名清单（全部无定义）：`Manager`、`StNetAddress`、`StEventSuper`、`StThreadSuper`、`st_socket`、`::_accept`、`ASSERT`、`HandleProcess`、`HandleError`、`CloseSocket`、`GlobalEventScheduler()`、`GlobalThreadScheduler()`、`SetHookFlag`、`ConnetionT::ServerStEventSuperT`（注意模板参数名 `ConnetionT` 本身是 `ConnectionT` 的拼写错误）。

服务端主循环 `Loop()`：

```cpp
while (true) {
  connfd = ::_accept(m_osfd_, &clientaddr, &addrlen);   // ← ::_accept 不存在
  if (connfd <= 0) continue;                            // ← 见下方缺陷
  StNetAddress addr(*((struct sockaddr_in *)&clientaddr));
  StConnection *conn = Instance<StConnectionManager<ConnetionT> >()
                          ->AllocPtr((eConnType)ServerT, &addr);
  conn->SetOsfd(connfd); conn->SetDestAddr(addr);
  m_manager_->CreateThread(NewStClosure(CallBack, conn, this));   // 每连接一协程
}
```

**`if (connfd <= 0) continue;` 是 busy-loop 隐患**：accept 失败时无条件 continue，不区分 `EAGAIN`（应 yield 等事件）、`EINTR`（应重试）、其它致命错误（应退出）。在非阻塞 fd 上会变成 100% CPU 占用的忙等。属真实缺陷，但修它涉及设计（要不要挂事件），需确认。

`CallBack` 是每连接协程体，闭环为 `RecvData` → `HandleProcess` → `SendData`，`do {...} while (conn->Keeplive())`。

**注意 `StConnection::Keeplive()` 硬编码 `return false;`**（`st_connection.h:162`，非虚函数）：

```cpp
inline bool Keeplive() { return false; }
```

所以 `do-while` 永远只跑一轮，keepalive 长连接**在服务端也不生效**。与 2.6 的枚举 bug 是同一个功能的两处失效。这是非虚函数，派生类无法覆盖（只能 shadow）。→ 同样上报（L4）。

### 2.8 syscall hook 与两套 `__*` 符号（D4 的完整证据）

**三个文件，两套语义，名字互撞：**

| 文件 | 语义 | 示例签名 |
| --- | --- | --- |
| `src/st_sys.h` + `src/st_sys.cc` | 框架 API：**带 timeout**，内部会 yield 协程 | `int __sendto(int fd, const void *msg, int len, int flags, const struct sockaddr *to, int tolen, int timeout);` |
| `app/st_sys.h` + `app/st_sys.cc` | syscall hook：**POSIX 形状**，用 `dlfcn.h` / `dlsym` 拿真实符号 | `ssize_t __sendto(int fd, const void *message, size_t length, int flags, const struct sockaddr *de__addr, socklen_t de__len);` |

`extern "C"` 不支持重载 → 同时链接必然符号冲突。

**更糟的是 `src/st_sys.cc` 和它自己的头文件也不一致**（实测）：

| `src/st_sys.h` 声明 | `src/st_sys.cc` 定义（行号） | 一致？ |
| --- | --- | --- |
| `__sendto` | `__sendto`（5） | 一致 |
| `__recvfrom` | `_recvfrom`（63） | **不一致** |
| `__connect` | `_connect`（119） | **不一致** |
| `__read` | `_read`（175） | **不一致** |
| `st_write` | `_write`（228） | **不一致**（名字完全不同） |
| `__recv` | `_recv`（288） | **不一致** |
| `__send` | `_send`（344） | **不一致** |
| `__sleep` | `_sleep`（409） | **不一致** |
| `__accept` | `_accept`（417） | **不一致** |

即 9 个函数里 8 个名字对不上 → 链接时全部 undefined reference。而 `src/st_server.h` 调用的 `::_accept` 恰好匹配 `.cc` 里的 `_accept`（说明 `.cc` 的单下划线版本才是「现役」的，头文件是旧的）。

补充：`app/st_sys.h` 的 `sys_fd` 结构体很可疑：

```cpp
typedef struct {
  uint32_t read_timeout : 4;      // 4 bit → 最大值 15
  uint32_t write_timeout : 4;
} sys_fd;
```

用 4 位存超时值，最大 15。若单位是毫秒则毫无意义，若是秒则上限 15 秒。属可疑点，记录（L6）。

另外 `app/st_c.h` 有：

```cpp
class StExecClientConnection : public StClientConnection<StEventSuper> {};
```

`StEventSuper` 是旧名（D3），且 `app/st_c.h` include 的 `st_connection.h` / `st_manager.h` / `st_thread.h` / `st_util.h` 都是无前缀路径（需按 01 规范修正）。

**`app/st_c.h` 暴露的才是真正的对外 C API**，必须保持名字不变：

```cpp
int udp_sendrecv(struct sockaddr_in *dst, void *pkg, int len, void *recvbuf, int &bufsize, int timeout);
int tcp_sendrecv(struct sockaddr_in *dst, void *pkg, int len, void *recvbuf, int &bufsize, int timeout,
                 CheckLengthCallback callback, bool keeplive = false);
void  st_set_private(void *data);
void *st_get_private();
void  st_set_hook_flag();
```

（注意：`int &bufsize` 是 C++ 引用，却放在 `extern "C"` 块里——C 语言没有引用。这个头**不能被 C 编译器使用**，只能给 C++ 用。属设计缺陷，记录 L7。）

### 2.9 `libmthread` 产出现状

见 01 阶段 2.2：`src/makefile` 的 `LIB_O` 列的 10 个 `mt_*.o` 全部无对应源文件，`-I./include` 指向不存在的目录 → **产物完全无法生成**。而 `app/st_dns/makefile`、`app/st_memcacheclient/makefile`、`app/st_wrk/makefile` 都写 `LIBS = ../../libmthread.a`，期望它在仓库根目录。

## 3. 问题与风险

| # | 问题 | 影响 | 缓解 |
| --- | --- | --- | --- |
| P1 | epoll 的 `AddEvent` 不更新 mask（2.2 错误 2） | **epoll 路径事件注册根本性失效** | 修复（意图无歧义）；修后必须验证 `EPOLL_CTL_MOD` 路径 |
| P2 | 两套 `__*` 符号冲突 + `.cc`/`.h` 名字全面不一致（2.8） | 链接必失败 | D4 决策后一次性对齐，**不要边改边试** |
| P3 | `eTCP_KEEPLIVE_CONN` / `eUDP_UDPSESSION_CONN` 值为 0（2.6） | keepalive 功能完全失效，且两类型无法区分 | **上报用户**（L4）：修它等于改行为 |
| P4 | `Keeplive()` 硬编码 false 且非虚（2.7） | 服务端长连接失效 | 同 P3，一并决策 |
| P5 | `m_thread_schedule_` 未初始化（R6） | 空指针崩溃 | 加入构造初始化列表 + D3 命名修正 |
| P6 | `StEventItem::Reset()` 用不存在的 `m_type_` / `eEVENT_UNDEF`（2.5） | 编译错误 | 修法需确认（删除 vs 补回） |
| P7 | `GlobalEventSchedule()->Close()` 方法不存在（2.6） | 编译错误 | 确认意图为 `ClearItem` 还是 `Delete` |
| P8 | `StServer::Loop()` 的 `connfd <= 0 → continue` 是 busy-loop（2.7） | 100% CPU | 修它涉及设计，需确认 |
| P9 | kqueue 路径完全未验证（R4） | 双平台约束落空 | 依赖 01 阶段 CI |
| P10 | 两个 poller 共用 include guard 与宏（2.1） | 脆弱 | 低成本修复（换 guard 名、宏加前缀），但属「改动无行为影响的细节」 |
| P11 | `ApiResize` 泄漏 + 丢失 mask 状态（2.3） | 内存泄漏 | 需确认 `ApiResize` 是否有调用方；若无，可仅加注释 |
| P12 | `StIOState::AddEvent` 无 fd 边界检查（2.3） | 越界写 | 加断言（DEBUG 下） |

## 4. 做 / 不做

### 做

- 修 `st_epoll.h` 的两个硬错误（删多余 `memset`、`m_file_events_` → `m_file_`）
- 修 `st_kqueue.h` 的多余 `memset`
- 补两个 poller 的 `<fcntl.h>`
- 换掉重复的 include guard，给 `DATA_SIZE` / `EVENT_SIZE` 加前缀
- 修 `StEventSchedule` 的 `m_thread_schedule_` 初始化
- 修 `StEventItem::Reset()` 的 `m_type_` / `eEVENT_UNDEF`
- 按 D4 统一 `__*` 符号命名，并让 `src/st_sys.cc` 与 `src/st_sys.h` 完全对齐
- 修 `src/st_connection.h` / `src/st_server.h` 的 include 路径与旧名（D3）
- 确认 `GlobalEventSchedule()->Close()` 的真实意图
- 让 `src/st_connection.cc`、`src/st_sys.cc` 编译通过
- 重写 `src/makefile` 使 `libmthread.a` / `libmthread.so` 真正产出（含 01 阶段的产物定义）
- 验证 `libmthread` 的符号表与外部依赖（约束 5 的最终验证）
- 建立 TCP/UDP 回环收发测试，双平台跑
- 给「业务同步 / 内部异步」的关键路径加注释
- 给 `StIOState::AddEvent` / `DelEvent` 加 DEBUG 期 fd 边界断言

### 不做

- **不**改 `eConnType` 的枚举值（P3 → 上报 L4）
- **不**改 `Keeplive()` 的返回值或虚性（P4 → 上报 L4）
- **不**改 `ST_RECV_BUFFSIZE` / `ST_SEND_BUFFSIZE`（8192）
- **不**改默认超时（`StEventSchedule` 30000ms、`StConnection` 30000ms、`StSysSchedule` 1000ms）
- **不**改 `m_maxfd_ = 65535` 或 `ST_MAX_FD`
- **不**改 `eERR_*` 错误码数值
- **不**换 poller 后端（不加 `io_uring` / `select` / `poll` 回退）
- **不**改 `StConnectionManager` 的 `StHashList` 实现或哈希策略
- **不**改 `StBufferPool` 的分配策略
- **不**引入多线程 accept / SO_REUSEPORT（属新功能）
- **不**修 `StServer::Loop()` 的 busy-loop（P8，需确认后单独处理）
- **不**重构 `ApiResize`（P11，先确认有无调用方）

## 5. 分步步骤

### 步骤 1：修两个 poller 的硬错误（最小、最确定，先做）

`stlib/st_epoll.h`：

1. 删除 `Create()` 里的 `memset(m_file_.data, 0, sizeof(void *) * DATA_SIZE);`
   —— 前两行 memset 已覆盖，删除无语义损失
2. `AddEvent()` 里 `m_file_events_[fd].mask = mask;` → `m_file_[fd].mask = mask;`
3. 补 `#include <fcntl.h>`
4. include guard `_ST_EVENT_H_` → `_ST_EPOLL_H_`
5. `DATA_SIZE` → `ST_EPOLL_DATA_SIZE`，`EVENT_SIZE` → `ST_EPOLL_EVENT_SIZE`（或在 `st_def.h` 统一定义一次）

`stlib/st_kqueue.h`：

1. 删除 `Create()` 里同样的多余 `memset`
2. 补 `#include <fcntl.h>`
3. include guard → `_ST_KQUEUE_H_`
4. `DATA_SIZE` 加前缀
5. `Free()` 补 `m_size_ = 0;`（与 epoll 版对齐）—— **注意这是行为变化**，需确认；若不确认则仅加注释说明差异
6. `AddEvent()` 的两个空 `if` 体加注释说明「删除不存在的注册会失败，可忽略」

**关键验证**：步骤 1 之后，epoll 的 mask 跟踪才开始工作。必须写一个针对 `StIOState` 的**直接单元测试**（不经过协程），验证：

| 用例 | 断言 |
| --- | --- |
| `AddEvent(fd, ST_READABLE)` 两次 | 第二次因去重返回 `ST_OK` 且不产生 `EEXIST` |
| `AddEvent(fd, ST_READABLE)` 后 `AddEvent(fd, ST_WRITEABLE)` | 合并为读写皆有，且第二次用的是 `EPOLL_CTL_MOD` |
| `DelEvent(fd, ST_READABLE)` 后只剩写 | `EPOLL_CTL_MOD`；再删写则 `EPOLL_CTL_DEL` |
| `Poll()` 返回就绪数与 `EventList()` 内容一致 | `m_fired_[j].fd` / `.mask` 正确 |
| 同一套用例在 kqueue 上 | 结果一致（双平台一致性） |

这个测试是**本阶段最有价值的产出**之一：它是唯一能证明 IO 底座正确的东西，且不依赖协程（可在 02 阶段未完成时独立验证）。

**出口**：两个 poller 编译通过；`StIOState` 单元测试在双平台通过。

### 步骤 2：修 `StEventSchedule` 与 `StEventItem`

1. `StEventSchedule` 构造初始化列表补 `m_thread_schedule_(NULL)`
2. `Init()` 里 `Instance<ThreadSchedule>()` → `Instance<StThreadSchedule>()`（D3）
3. `StEventItem::Reset()` 的 `m_type_ = eEVENT_UNDEF;` —— 先 grep 确认全仓库无人读 `m_type_`（`StEventItem` 的），确认后删除该行
4. `BindItem` 的 `unlikely` 方向（2.4）—— 属性能提示，可修可不修；若修需在 PR 说明这是提示而非逻辑改动
5. `Dispatch` / `Schedule` 里对 `m_thread_schedule_` 加 `LOG_ASSERT` 非空

**出口**：`src/st_thread.cc` 的 `StEventSchedule` 部分编译通过。

### 步骤 3：执行 D4，统一 `__*` 符号（**本阶段最大的机械工作量**）

按 D4 结论（推荐方案 a）：

| 层 | 新前缀 | 举例 |
| --- | --- | --- |
| 框架 API（带 timeout，会 yield 协程） | `st_` | `st_sendto(fd, msg, len, flags, to, tolen, timeout)` |
| syscall hook（POSIX 形状，dlsym 真实符号） | `sys_` | `sys_sendto(fd, msg, len, flags, addr, addrlen)` |

需同步修改的调用点（必须全查）：

- `src/st_sys.h`（9 个声明）与 `src/st_sys.cc`（9 个定义）→ 全部改为 `st_*`，并**消除 2.8 表格里的 8 处名字不一致**
- `app/st_sys.h`（17 个声明）与 `app/st_sys.cc` → 全部改为 `sys_*`
- `src/st_connection.h` 里的 `__close`、`__socket`、`__connect` → 判断该调哪一层：
  - `__socket` / `__close` 是无 timeout 的 → 应为 `sys_*`
  - `__connect(fd, addr, len, timeout)` 带 timeout → 应为 `st_*`
  - **这个判断必须逐个做，不能机械替换**，因为两层有同名函数
- `src/st_server.h` 里的 `st_socket`、`::_accept`
- `app/st_c.cc`、`app/st_dns/dns.cpp`、`app/st_memcacheclient/*.cpp`、`app/st_wrk/wrk.cpp`、`tests/*.cpp` 的所有调用点（04 阶段范围，但名字改动会波及 → 需在本阶段一并完成，否则 04 无法编译）

**对外 API 保持不变**（D4 已确认）：`udp_sendrecv`、`tcp_sendrecv`、`st_set_private`、`st_get_private`、`st_set_hook_flag`。

注意 `st_` 前缀与现有 `st_set_hook_flag` 等对外 API 撞前缀。若担心混淆，框架内部层可用 `stio_` 或 `st_io_`。→ 需在 D4 落地时一并确定。

**出口**：`src/st_sys.cc` 编译通过；`nm libmthread.a` 中不存在 undefined 的 `_*` / `__*` 符号。

### 步骤 4：修 `src/st_connection.h` / `.cc` 与 `src/st_server.h`

include 路径（按 01 规范）：

| 文件 | 修正 |
| --- | --- |
| `src/st_connection.h` | 补 `st_public.h`、`st_poll.h`；`stlib/*` 路径已正确 |
| `src/st_server.h` | `st_manager.h` → `st_sys.h`（待 D3 的 L5 确认）；`st_netaddr.h` → `stlib/st_netaddr.h`；`st_util.h` → `stlib/st_util.h` |

旧名替换（D3）+ 逐项确认：

- `StEventSuper` → `StEventItem`
- `StThreadSuper` → `StThreadItem`
- `StNetAddress` → `StNetAddr`
- `GlobalEventScheduler()` → `GlobalEventSchedule()`
- `Manager` → `StSysSchedule`（待 L5）
- `ASSERT` → `LOG_ASSERT`
- `HandleProcess` → `DoProcess`，`HandleError` → `DoError`，`CloseSocket` → `Close`
- `ConnetionT` → `ConnectionT`（拼写）
- `ConnetionT::ServerStEventSuperT` → 需确认这个内嵌 typedef 该叫什么、由谁定义
- `GlobalEventSchedule()->Close(item)` → 确认为 `ClearItem(item)`（它做了 `Delete` + 清 `m_event_[fd]`，语义最贴近「关闭并清理」）
- namespace：按 D3 授权决定是否移入 `namespace sthread`

**出口**：`src/st_connection.cc` 编译通过；`src/st_server.h` 通过孤立编译。

### 步骤 5：产出 `libmthread` 并验证依赖

按 01 阶段步骤 6 的产物定义构建，并确认 01 遗留决策（`app/st_sys.cc` / `app/st_c.cc` 是否纳入）。

从 readme 的承诺「使用简单，只需要引入一个 libmthread.a 或者 libmthread.so」与 `app/*/makefile` 只链接 `libmthread.a` 来看，**必须纳入**——否则三个 app 里的 `udp_sendrecv` / `st_set_hook_flag` 会 undefined。

验证：

```bash
# 1) 产物存在且在 app 期望的位置（仓库根）
ls -l libmthread.a libmthread.so

# 2) 零第三方运行时依赖（约束 5 的最终验证）
ldd libmthread.so          # Linux：应只有 libc/libstdc++/libpthread/libm/libdl
otool -L libmthread.dylib  # macOS：应只有系统库

# 3) 对外符号齐备
nm -g --defined-only libmthread.a | grep -E 'udp_sendrecv|tcp_sendrecv|st_set_hook_flag'

# 4) 无 undefined（除系统符号）
nm -u libmthread.a
```

**出口**：`libmthread.a` 与 `libmthread.so` 产出；4 项验证全过。

### 步骤 6：TCP/UDP 回环收发测试

现有素材：`tests/st_connection_unittest.cpp`、`tests/server_unittest.cpp`、`tests/st_server_unittest.cpp`、`tests/scripts/keepalive_unittest.py`、`tests/scripts/udpsvr_unittest.py`。

需覆盖（每条都要双平台跑）：

| # | 用例 | 断言 |
| --- | --- | --- |
| N1 | UDP 回环：`udp_sendrecv` 发一个包收一个包 | 收到的内容与发出一致 |
| N2 | UDP 超时 | 对不存在的端口，按 timeout 返回，错误码为 `eERR_RECV_TIMEOUT` |
| N3 | TCP 短连接（`eTCP_CONN`）：connect → send → recv → close | 内容正确，fd 被关闭 |
| N4 | TCP connect 超时 | 对黑洞地址按 timeout 返回 `eERR_CONNECT_FAIL` 或 `eERR_RECV_TIMEOUT`（需先确认预期码） |
| N5 | TCP 对端关闭 | 返回 `eERR_PEER_CLOSE` |
| N6 | 大包分片：> 8192（`ST_SEND_BUFFSIZE`） | **重点**：验证「发不完就注册写事件 + yield」的内部异步路径 |
| N7 | 并发客户端：N 个协程同时请求 | 全部成功，无串包（验证每协程独立 buffer） |
| N8 | `StServer` accept 闭环 | 收→处理→发 正确 |
| N9 | fd 泄漏 | 反复 1000 次连接后 `/proc/self/fd`（或 `lsof`）计数稳定 |
| N10 | epoll / kqueue 一致性 | N1～N9 在两平台结果一致 |

N6 是**验证核心价值主张的关键用例**：只有大于 buffer 的收发才会真正触发「同步写法 → 内部 yield → 事件唤醒 → 继续」这条路径。

keepalive 相关用例（`tests/scripts/keepalive_unittest.py`）需等 P3/P4（L4）决策后才能有意义地跑——当前 keepalive 功能是失效的。

**出口**：N1～N10 通过（keepalive 项按 L4 结论处理）。

### 步骤 7：注释关键路径

给「业务同步、内部异步」这条主线加注释，位置：

1. `StConnection::SendData()` / `RecvData()` —— 说明它对业务是同步语义，内部会 yield
2. `StEventSchedule::Schedule()` —— 说明它是「挂 IO 等待 + 让出协程」的汇合点
3. `StEventSchedule::Dispatch()` —— 说明它把就绪事件转成 `IOWaitToRunable`，即唤醒协程
4. `StSysSchedule::WaitEvents()` —— 说明这是带超时的等待循环
5. `src/st_poll.h` 的 `#if defined(__APPLE__)` 分支 —— 说明双后端二选一，且两者接口必须保持一致
6. `StIOState::AddEvent` 的 mask 合并语义 —— 说明为何要 `mask |= m_file_[fd].mask`

**出口**：六处注释就位。

## 6. 兼容策略

| 对象 | 策略 |
| --- | --- |
| `udp_sendrecv` / `tcp_sendrecv` / `st_set_hook_flag` / `st_set_private` / `st_get_private` | **名字与签名一律不变**（D4 已确认）。这是真正的对外 API |
| `__*` → `st_*` / `sys_*` | 这些是框架内部符号。严格说它们出现在头文件里即「半公开」，改名属破坏性变更。但因为当前**根本无法链接**（8/9 名字不一致），不存在能用它们的使用方 → 风险可接受。仍建议在 05 阶段 readme 里记一条 migration note |
| `eConnType` 数值 | **不改**（P3 → L4 上报）。若用户批准修 `&`→`|`，则 `eTCP_KEEPLIVE_CONN` 从 0 变 0x11，属破坏性变更，须单独 PR + 明确 release note |
| `Keeplive()` | **不改**（P4 → L4） |
| `eERR_*` 数值 | 不改 |
| 超时默认值 | 不改（30000 / 30000 / 1000） |
| buffer 尺寸 | 不改（8192） |
| `StIOState` 的公开接口 | 不变。两个后端必须保持**完全相同**的接口签名，否则 `src/st_poll.h` 的条件编译会在一个平台上失败 |
| `StEventItem` 的虚函数签名 | 不变（`EvInput`/`EvOutput`/`EvHangup` 由使用方派生覆盖） |
| `StConnection` 的虚函数 | `DoOutput`/`DoInput`/`DoProcess`/`DoError` 签名不变 |
| include guard / 宏改名 | 纯内部，无兼容影响 |
| `libmthread` 产物位置 | 以 `app/*/makefile` 的 `../../libmthread.a` 为准（仓库根） |

## 7. 验收

- [ ] `stlib/st_epoll.h` 与 `stlib/st_kqueue.h` 编译零 error（双平台）
- [ ] 两个 poller 不再共用 include guard，宏已加前缀
- [ ] `StIOState` 单元测试（步骤 1 的 5 条）在 Linux（epoll）通过
- [ ] 同上，在 macOS（kqueue）通过，且结果与 epoll 一致
- [ ] `StEventSchedule::m_thread_schedule_` 在构造列表中初始化
- [ ] `StEventItem::Reset()` 不再引用不存在的 `m_type_` / `eEVENT_UNDEF`
- [ ] `src/st_sys.h` 与 `src/st_sys.cc` 的 9 个函数名完全一致
- [ ] 全仓库不存在两个同名不同签名的 `extern "C"` 函数（可用 `nm` 交叉核验）
- [ ] `src/st_connection.cc`、`src/st_sys.cc` 编译零 error（双平台）
- [ ] `src/st_connection.h`、`src/st_server.h` 通过孤立编译
- [ ] **`libmthread.a` 与 `libmthread.so` 在仓库根目录产出**
- [ ] `ldd libmthread.so` / `otool -L` 只含系统库（约束 5）
- [ ] `nm` 确认 `udp_sendrecv` / `tcp_sendrecv` / `st_set_hook_flag` 已导出
- [ ] `nm -u` 无非系统 undefined 符号
- [ ] N1～N10 网络测试通过（keepalive 项按 L4 结论）
- [ ] N9 无 fd 泄漏
- [ ] 六处关键路径注释就位
- [ ] `eConnType` / `eERR_*` / 超时 / buffer 尺寸的数值未变（`git diff src/st_public.h stlib/st_def.h` 核验）
- [ ] L1～L7 遗留问题全部有结论或已上报

## 8. 依赖与工作量

**前置依赖**：
- **01 阶段**（根 makefile、include 规范、CI、`stlib` 绿基线）
- **02 阶段**（可用的协程切换。注意：步骤 1 的 `StIOState` 单元测试**不依赖** 02，可提前做——这是本阶段唯一能并行的部分）
- **决策点 D3**（命名 + namespace 授权）
- **决策点 D4**（`__*` 符号拆分方案）
- **新增上报 L4**（`eConnType` 枚举 bug 与 `Keeplive()`）

**被依赖**：04 阶段（三个 app 与全部网络测试都要链接 `libmthread`）。

**改动面**：

| 子系统 | 文件 | 侵入程度 |
| --- | --- | --- |
| `stlib/st_epoll.h` | 1 | 低：2 处硬错误 + guard/宏/include |
| `stlib/st_kqueue.h` | 1 | 低：1 处硬错误 + guard/宏/include |
| `src/st_thread.cc`（`StEventSchedule` 部分） | 1 | 低：初始化 + 命名 |
| `src/st_poll.h`（`StEventItem`） | 1 | 低：删 1 行 + include 路径 |
| `src/st_sys.h` + `src/st_sys.cc`（449 行） | 2 | **高**：9 个函数全面改名 + 头实现对齐 |
| `app/st_sys.h` + `app/st_sys.cc`（240 行） | 2 | **高**：17 个函数改名 |
| `src/st_connection.h` + `.cc` | 2 | 中：include + 旧名 + `Close` 语义确认 |
| `src/st_server.h` | 1 | **高**：旧名最密集（14 类），且含 `ServerStEventSuperT` 待定项 |
| `app/st_c.h` + `app/st_c.cc` | 2 | 中：旧名 + include；对外 API 名保持 |
| `src/makefile` | 1 | 中：产物落地 |
| 新增测试 | 2～4 | 中：`StIOState` 单测 + N1～N10 |
| 所有调用点（`app/`、`tests/`） | ~15 | 中：随步骤 3 改名波及 |

**风险集中点**：

1. **步骤 3（D4 改名）波及面最广**：涉及 `src/`、`app/`、`tests/` 三处约 15 个文件，且需逐个判断「该调 `st_` 层还是 `sys_` 层」。这不是机械替换，**最容易出错**。建议先写一张「调用点 → 目标层」的映射表再动手。
2. **kqueue 路径首次真机验证**：可能暴露成片问题（`Free()` 不重置 `m_size_`、空 if 体、EV_DELETE 语义差异等）。
3. **L4（`eConnType` bug）若用户批准修复**，则 keepalive 相关的连接复用路径（`StConnectionManager`、`StHashList<StNetAddrKey>`）会**首次真正被执行**——那条路径至今从未运行过，可能藏有更多问题。这会显著增加本阶段范围，需评估后决定是否拆成独立阶段。
4. **`ServerStEventSuperT`** 这个内嵌 typedef 谁定义、叫什么，目前完全不明（`src/st_server.h` 里 `ConnetionT::ServerStEventSuperT` 被用了两次）。若查不到，`StServer` 可能无法恢复 → 上报 L5。

## 9. 遗留问题（需查清或上报）

| # | 问题 | 处理 |
| --- | --- | --- |
| L1 | `GlobalEventSchedule()->Close(item)` 的真实意图（`ClearItem` / `Delete` / 需新增） | 步骤 4 确认 |
| L2 | `__connect(fd, addr, len, timeout)` 内部是否已处理 `EINPROGRESS` 等待（决定 2.6 的 `Connect()` 错误处理是否正确） | 步骤 3/4 读 `src/st_sys.cc:119` 的 `_connect` 确认 |
| L3 | `StEventItem` 是否真的不需要 `m_type_` | 步骤 2 grep 确认 |
| L4 | **`eTCP_KEEPLIVE_CONN` / `eUDP_UDPSESSION_CONN` 的 `&` 应为 `|`（值为 0 导致 keepalive 完全失效）；`Keeplive()` 硬编码 false 且非虚** | **上报用户**：修复属行为变化，且会首次激活从未运行过的连接复用路径 |
| L5 | `ConnetionT::ServerStEventSuperT` 由谁定义、正确名字是什么 | 步骤 4；查不到则**上报用户** |
| L6 | `app/st_sys.h` 的 `sys_fd` 用 4 bit 存超时（上限 15），单位与意图不明 | 记录；05 阶段文档化或上报 |
| L7 | `app/st_c.h` 在 `extern "C"` 块里用 C++ 引用 `int &bufsize`，该头无法被 C 编译器使用 | 记录；若需真正的 C 兼容需改签名（破坏性）→ 上报 |
| L8 | `ApiResize()` 是否有调用方（决定是否值得修其泄漏） | 步骤 1 grep 确认 |
| L9 | `StServer::Loop()` 的 `connfd <= 0 → continue` busy-loop 修法 | 需确认后单独处理 |
| L10 | `ST_MAX_FD`（`65535 * 2`，无括号）与 `StEventSchedule` 硬编码 `65535` 不一致 | 记录，不改 |


## 10. 实现期记录（本地 impl-plan-03-io-net，2026-09-15）

### 已落地
- `st_epoll.h`: `m_file_events_` → `m_file_`；独立 include guard；补 `<fcntl.h>`
- `st_kqueue.h`: 独立 include guard；补 `<fcntl.h>`（`memset(m_file_.data)` 已在 02 删除）
- D4: 框架超时 API → `st_*`（`src/st_sys.*`）；hook POSIX API → `sys_*`（`app/st_sys.*`）；恢复 HOOK 宏/syscall 表/`sock_flag`
- `src/st_manager.h` 转发到 `st_sys.h`
- `StEventSchedule::m_thread_schedule_` 构造初始化为 NULL；`Reset()` 供 Init 失败路径
- `libmthread.a` / `libmthread.so` **已在本机产出**（Apple Silicon）
- arm64: `ucontext_stub_arm64.c` + makefile 条件；**协程切换未实现**，仅保证可链接

### 明确未做（按用户确认）
- L4: `eTCP_KEEPLIVE_CONN` 的 `&`→`|`（单独阶段）
- TCP/UDP 回环运行时测试（arm64 stub 下 context switch 不可用）
- 完整 Linux CI 验证本分支（需 push 后看 Actions）

### 验证
- `make -C src` → `libmthread.a` + `libmthread.so`
- `otool -L libmthread.so` 仅系统库
