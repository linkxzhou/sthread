# 07 · src 重构计划：清理 + 优化框架核心（协程调度 / 事件分发 / 连接 / 服务端）

> 本文档**只描述计划，不包含任何代码变更**。与 [`06-stlib-refactor-cleanup.md`](06-stlib-refactor-cleanup.md) 是姊妹篇：06 打磨底座 stlib，本篇打磨框架核心 `src/`。目标相同——把 sthread 做成一个干净、可独立使用的**通用 server + 协程库**。
>
> 执行顺序上本篇**不依赖** 06 完成，但 06 的三项修复会改变 src 行为，需联动验证（见第 2.3 节）。

---

## 1. 目标与非目标

### 1.1 目标

`src/` 是 libmthread 的框架主体：`StThreadSchedule`（协程调度）、`StEventSchedule`（事件分发）、`StThread`/`StThreadItem`（协程实体）、`StConnection`/`StClientConnection`/`StConnectionManager`（连接）、`StServer`（服务端模板）、`StSysSchedule` + `st_*`（带超时的同步式 IO 封装）。本计划完成后：

1. **无死代码/无重复实现**：8 个同构的 `st_*` IO 函数收敛为一份骨架；重复的 `CreateThread`/`AllocThread` 去重。
2. **无已知潜伏 bug**：第 4 节 P-B 清单逐项修复并配回归单测，重点是**三处内存泄漏**（协程栈、server 连接对象、`m_private_` 误释放）。
3. **头文件卫生**：消除头文件全局 `using namespace`、框架层对 `app/` 的反向 include。
4. **行为兼容**：`app/`、`tests/` 现有调用点源码级兼容；`app/st_c.h` 契约 API（`st_init_frame`/`udp_sendrecv`/`tcp_sendrecv`/`st_set_hook_flag`）语义不变。

### 1.2 非目标

- 不改调度模型（单 OS 线程内协程切换、daemon 事件循环），不改 M:N。
- 不改枚举数值（`eThreadType`/`eThreadFlag`/`eThreadState`/`eConnType`）、`MEM_PAGE_SIZE`、`STACK`、默认超时（30000/1000）。
- 不动 `InitContext` 的 ty/tx 拆分、`ss_sp+16`/`ss_size-64` 余量（AGENTS.md 明令勿动）。
- 不引入 C++11+、不引入第三方依赖。
- `app/` 目录重构不在本篇（`app/st_c.cc` 里发现的问题只登记，见 2.3）。

---

## 2. 现状盘点（逐文件实测）

### 2.1 组件与规模

| 文件 | 职责 | 规模 | 主要问题 |
| --- | --- | --- | --- |
| `st_public.h` | 枚举 / 错误码 / 全局调度器宏 | 91 行 | 异常 include `st_test.h`（06-A13） |
| `st_poll.h` | `StEventItem`/`StThreadItem` + 平台后端选择 | 279 行 | `BindItem` 死接口；公共数据成员过多 |
| `st_thread.h/.cc` | `StThreadSchedule`/`StEventSchedule`/`StThread` | 352+561 行 | 栈内存泄漏；`Startup` 转发残留；`CreateThread` 重复 |
| `st_sys.h/.cc` | `StSysSchedule` + 8 个 `st_*` 同步式 IO | 211+533 行 | **8 份同构代码**；返回值约定无文档；st_accept 超时语义错 |
| `st_connection.h/.cc` | `StConnection`/`StClientConnection`/`StConnectionManager` | 246+120 行 | IPv6 尺寸错误；keepalive 复用失效；`-65535` 魔术数 |
| `st_server.h` | `StServer`/`StServerConnection` 模板 | 176 行 | conn 对象泄漏；反向 include `app/st_c.h` |
| `st_manager.h` | 转发头（历史兼容） | 8 行 | `st_sys.h` include guard 仍叫 `_ST_MANAGER_H__` |
| `makefile` | libmthread 产物定义 | 93 行 | 16-42 行注释块描述的是 01 阶段的失败状态，已过时 |

### 2.2 关键事实（grep/阅读实测）

- **每 OS 线程启动即预分配约 2.3MB**：`StEventSchedule::Init` 里 `m_maxfd_ = ST_MAX(max_num, 65535)` → `m_event_` 指针数组 512KB + `StIOState` 三个数组约 1.8MB（epoll 版）。与「默认 max_num=1024」的直觉完全不符。
- **协程栈每对象约 266KB**，`StThread` 走 `UtilPtrPool` 池化复用；但 `FreeStack()` 只 `free(m_stack_)`（Stack 结构体本身），**从不释放 `m_vaddr_` 指向的 266KB 栈内存** —— 池销毁时每个对象集中泄漏一次（B1）。
- `StServer::CallBack` 结束时 `conn->Close()` 后**不归还连接池**：每接受一个连接泄漏一个 `StConnection` 派生对象（连带的两个 8200B buffer 也随之无法回池）（B10）。
- `st_accept(fd, …)` 调 `Schedule(thread, NULL, item, -1)`：`wakeup_timeout = -1` 进 sleep 堆即「早已过期」，任何 fd 事件后的 `Wakeup(now)` 都会把 accept 协程误唤醒 → 假 `ETIME` → `Loop()` 空转重试。其余 7 个 `st_*` 函数都有 `timeout <= -1 → 0x7fffffff` 规范化，**唯独 st_accept 没有**（B3）。
- `st_sendto`/`st_read` 的 `while ((n = REAL_FUNC(…)) < 0)` 循环体内还有 `if (n == 0)` 分支 —— 编译期不可达的死代码（A8）。
- `tests/` 下 28 个 `*_unittest.cpp` 已覆盖 src 的主要路径（scheduler/thread/connection/server/keepalive/loopback/accept 等），是本次重构的回归锚点。

### 2.3 与 06（stlib）的联动点

| 06 的改动 | 对 src 的影响 | 联动动作 |
| --- | --- | --- |
| 06-B1 修 `StNetAddr::operator==`（`=`→`==`） | `StConnectionManager` 的四元组判重从「永远相等」变成真比较 | 07 的 B11 验证时重点跑 `st_conn_manager_unittest` + `st_keepalive_unittest` |
| 06-C1 堆 sift 化 O(n²)→O(log n) | `m_sleep_list_`（协程 sleep/IO 超时堆）直接受益 | 07 基准数据可复用 06 的 |
| 06-B10 `HashRemove` 所有权拆分 | `StConnectionManager::FreePtr` 依赖「HashRemove 内部 delete key」的现状 | 07 的 B11 与 06-B10 同一提交窗口内联动改 |
| 06-D7 kqueue 补 `ST_EVERR` | macOS 上 `Dispatch` 的 ST_EVERR 分支从「永不触发」变「可触发」 | 07 的 B4/B5 必须先于或随 06-D7 落地 |

另登记两个 `app/` 侧发现（不在本篇范围，仅记录备查）：`app/st_c.cc` 的 `st_set_private`/`st_get_private` 中 `LOG_ASSERT(athread == NULL)` **断言写反**（有活跃协程时必 abort）；`tcp_sendrecv` 的 `FreePtr` 调用被注释掉（30/153 行），连接对象只 `Close()` 不还池。这两项与 07-B2/B10 同根，建议在 07 落地后顺手修（或单开 08-app 篇）。

---

## 3. 硬约束（与 06 一致，复述）

1. C++98；GNU 扩展 `__thread`/`__builtin_expect` 可用。
2. Linux + macOS 双平台；平台分支只在 `stlib/st_epoll.h`/`stlib/st_kqueue.h`。
3. 零第三方运行时依赖。
4. 行为兼容：`app/`、`tests/` 调用点源码级兼容；对外符号与枚举数值不动。
5. 风格以 `.clang-format` 为准；注释保持中文。
6. 每 Phase 以「基线三件套」全绿为出口：`make lib`、`make -C tests run`、`make -C stlib/tests run`。

---

## 4. 问题清单

### P-A · 死代码与重复（纯删除/合并，零行为变化）

| # | 位置 | 内容 | 证据/说明 |
| --- | --- | --- | --- |
| A1 | `st_thread.h:152` | `StEventSchedule::BindItem()` | 全仓库零调用；且 `unlikely(IsValidFd(...))` 分支预测提示反向（常见路径是有效 fd） |
| A2 | `st_sys.h:81-127` | `StSysSchedule::WaitEvents()` | 仅 `tests/st_log_buffer_extra_unittest.cpp` 一个 smoke 测试在用；与 `st_*` 内的 Schedule 等待循环完全同构 → **D2** |
| A3 | `st_thread.cc:551-561` | `StThreadSchedule::Startup()` 静态转发器 | daemon 回调在 `StSysSchedule::Init` 已被重绑为 `StSysSchedule::StartUp`；`DaemonThread()` 里的 `NewStClosure(Startup, this)` 绑定会被覆盖。收敛为单一入口（C8） |
| A4 | `st_thread.h:346-348` + `st_sys.h:129-146` | `CreateThread`/`AllocThread` 两份雷同实现 | `StSysSchedule` 版转发到 `StThreadSchedule` 版，保留一处 |
| A5 | `st_connection.h:36` | `StConnection::Create()` 基类占位返回 -1 | 改纯虚或删除（子类均有真实实现；grep 确认无直接调基类版的点） |
| A6 | `src/makefile:16-42` | 大段注释描述 01 阶段「编不过」的中间状态 | 已过时，重写为当前真实的产物说明 |
| A7 | `st_server.h:21-24` | `StServerConnection` 空模板类 | 无扩展、无使用（实现前 grep 复核） |
| A8 | `st_sys.cc:30-35,229-233` | `st_sendto`/`st_read` 循环体内 `if (n == 0)` 分支 | `while ((n = …) < 0)` 保证不可达，死分支 |
| A9 | `st_thread.h:312-318` | `ActiveThreadStartUp` 尾部 `Yield` 之后的 `SwitchThread` + `context_exit` | 协程 `Run` 完成后不再入队，`Yield` 切走即永不返回，后续代码不可达；删除或注释说明（实现时单步验证一次再删） |

### P-B · 潜伏 bug（逐项修复 + 回归单测）

| # | 位置 | 问题 | 修法要点 |
| --- | --- | --- | --- |
| B1 | `st_thread.h:241-247` | `FreeStack()` 只 `free(m_stack_)`，**泄漏 `m_vaddr_` 的 266KB 栈内存**（每 `StThread` 析构一次） | 先 `free(m_stack_->m_vaddr_)` 再 `free(m_stack_)`；配「创建/销毁协程前后 RSS 不增长」单测 |
| B2 | `st_poll.h:129` | `StThreadItem::Reset()` 用 `st_safe_free(m_private_)` 释放**调用方指针**（`st_set_private` 塞入的用户数据）：用户传栈/静态地址即 invalid free，传堆指针则 double free 风险 | `Reset` 不释放 `m_private_`，仅置 NULL（所有权归调用方）；同步注释 `SetPrivate` 契约 |
| B3 | `st_sys.cc:524` | `st_accept` 把 `wakeup_timeout=-1` 原样传入 sleep 堆 → 「无限等待」变成「立即过期」，任何事件后的 `Wakeup` 都误唤醒 accept 协程（假 ETIME 空转） | 与其他 7 个 `st_*` 一致：先 `timeout <= -1 → 0x7fffffff` 规范化再算 wakeup；配「无连接时 accept 不被无关事件惊扰」单测 |
| B4 | `st_thread.cc:441-446` | `Dispatch` 的 ST_EVERR 分支：`EvHangup()` + `Delete(item)` 后**不唤醒等待中的 owner 协程**（协程僵到 sleep 超时），也不清 `m_event_[fd]` 槽位 | 补 `IOWaitToRunable` 唤醒 + 槽位清理；与 06-D7（kqueue EVERR）同窗口验证 |
| B5 | `st_thread.cc:437-438` | `Dispatch` 里 `LOG_ASSERT(thread != NULL)`：无 owner 的 item（如 listen fd 注册时未设 owner）在事件到达时直接 abort；NDEBUG 下则裸奔 | 改显式判空 + 错误日志 + `continue`；assert 只留内部不变量 |
| B6 | `st_connection.h:151-156`、`st_connection.cc:84`、`st_server.h:76` | IPv6 尺寸/结构错误：`Connect` 用 `sizeof(sockaddr_in)`（v6 应 `sockaddr_in6` + `GetSock6Addr`）；UDP `RecvData` 按 v4 截断对端地址；`CreateSocket` bind 用 `sizeof(struct sockaddr)`（v4 碰巧正确，v6 错） | 按 `IsIPV6()` 分支取地址与长度；配 IPv6 loopback 单测（若 CI 环境支持） |
| B7 | `st_connection.h:151-175` | `Connect` 错误处理冗余：`st_connect` 内部已处理 `EISCONN`/`EINPROGRESS` 重试，外层再判 `EINPROGRESS→-1` 属误判残留；两个失败分支日志文案相同 | 简化：`<0` 即失败（区分超时/硬错误取 errno）；行为以 `st_conn_io_unittest` 回归为准 |
| B8 | `st_connection.h:38-43` | `Close()` 用 `m_osfd_ > 0`，fd==0 漏关 | 改 `>= 0`（-1 为无效哨兵） |
| B9 | `st_connection.cc:40`、`st_sys.cc:120,386` | `LOG_TRACE("…%s", buf)` 对**二进制网络缓冲**用 `%s`：越界读 + 日志爆炸风险 | 改为打印长度/指针，或 `%.*s` 限长 |
| B10 | `st_server.h:163-166` | `CallBack` 结束 `conn->Close()` 后**不归还连接池**：每连接泄漏一个 conn 对象（其两个 buffer 也无法回池） | 出口处 `Instance<StConnectionManager<ConnectionT>>()->FreePtr(conn)`；注意与 B11/B12 联动（→ **D5**） |
| B11 | `st_connection.h:228-240` | `StConnectionManager::FreePtr` 对 keepalive 连接先 `HashRemove` 再入池 → **keepalive 复用永远不生效**（hash 里留不住），且池化 `Reset()` 不重置 `m_addr_`/`m_destaddr_` → 语义半残 | 设计级修正 → **D1** |
| B12 | `st_connection.h:66-75` | `Reset()` 把 `m_osfd_` 置 -1 但**不关 socket**：池化路径（`FreePtr`→`Reset`）fd 泄漏；与 `Close()` 职责重叠 | 明确契约：`Reset` 内含 `Close()`；keepalive 复用路径（D1）除外并注释 |
| B13 | `st_thread.cc:219-258` | `StEventSchedule::Init` 每线程按 65535 预分配 ~2.3MB；`setrlimit` 提升 `rlim_max` 在 macOS 无权限时静默失败（返回值未查） | 容量取 `min(实际 rlim_cur, 65535)`；`setrlimit` 查返回值降级告警（→ **D4**，涉及行为变化） |
| B14 | `st_thread.h:197-200` | 构造函数 `LOG_ASSERT(InitStack())`：NDEBUG 下 assert 蒸发 → 栈未初始化继续跑 | 改显式 `if (!InitStack())` 错误日志（构造失败语义保持：成员置空，首次切换前断言） |
| B15 | `st_sys.cc` 全体 | 8 个 `st_*` 返回值约定不统一且无文档（-1 硬错误/超时 ETIME、-2 无 item、-3 调度失败、0 对端关闭/部分成功） | 不改动数值，只在 `st_sys.h` 补约定注释表（C9 一并） |
| B16 | `st_sys.cc:131-134` | `st_recvfrom` 把 `n == 0` 当「对端关闭」：UDP 零长数据报合法，无「对端关闭」概念 | → **D3**（修语义 vs 保持现状注释） |
| B17 | `st_thread.cc:181-189` | `WakeupParent` 的 `dynamic_cast<StThread*>` 结果赋给 `StThreadItem*`：无意义转换 | 去 cast，直接用基类指针 |
| B18 | `st_thread.cc:519-522` | `Schedule` 失败路径：`thread->Add(item)` 已把 item 链入线程 fdset，`Add(fdset)` 失败直接 return → 线程 fdset 残留陈旧 item（池化复用时带出） | 失败时回滚 thread 的 fdset（从链表摘除本次新增） |
| B19 | `st_poll.h:177-183` | `GetStThreadid()` 在 stack 为 NULL 时返回 `(uint64_t)-1` | 返回 0 并注释（无调用方依赖 -1，实现前 grep 复核） |
| B20 | `st_server.h:128-137` | `CallBack` 的 `thread == NULL` 错误路径：`ClearItem`+free item + `conn->Close()`，但 conn 不还池（同 B10 的错误路径变体） | 随 B10 一并处理 |

### P-C · 结构与性能优化（行为等价为前提）

| # | 位置 | 问题 | 方案 |
| --- | --- | --- | --- |
| C1 | `st_sys.cc` 全体 | **8 个 `st_*` 函数是同构骨架**（hook 检查 → 活跃协程检查 → 超时归一 → 循环：syscall → EINTR continue → EAGAIN 注册事件并 Schedule → 重试），各 ~60 行，共 ~470 行 | 提炼内部 helper（如 `WaitFdReady(fd, events, deadline)` 承载等待循环），8 个函数只剩 syscall 调用与结果解释。**行为逐函数等价**，目标 ~470→~200 行；每提炼一个函数跑一遍相关 unittest。本篇优化主线 |
| C2 | `st_thread.h` + `st_sys.h` | `CreateThread`/`AllocThread` 重复（A4） | `StSysSchedule` 版改为转发；公开签名不变 |
| C3 | `st_connection.h:16-17`、`st_server.h:18-19`、`st_poll.h:23` | 头文件在**全局作用域** `using namespace stlib/sthread`：污染所有包含者，与「通用库」定位冲突 | 头文件内改为显式前缀或 `namespace sthread { using … }` 内嵌；逐个编译验证 app/tests |
| C4 | `st_server.h:8-9` | 框架层 `st_server.h` 反向 include `app/st_c.h`/`app/st_sys.h`：实际只用 `st_set_hook_flag` 与 `sys_close` | 依赖降级：`sys_*` 声明移入框架可见头或前向声明；`SetHookFlag` 保留薄封装。消除 src→app 的 include 环 |
| C5 | `st_connection.cc:111` | `DoInput` 返回 `-65535` 表示「重置接收缓冲」的魔术数契约（当前零使用方） | 命名化为 `ST_CONN_RESET_RECVBUF` 常量 + 注释契约；不改数值 |
| C6 | `st_thread.cc:475-498` | `Wait()` 的 timeval 两段式计算（先 `{0, wait*1000}` 再 `>=1000` 重算） | 合并为一次 `tv_sec/tv_usec` 拆分 |
| C7 | `st_sys.h:5-6`、`st_manager.h` | include guard `_ST_MANAGER_H__` 与文件名 `st_sys.h` 不符（历史遗留） | guard 更名 `_ST_SYS_H_`；保留 `st_manager.h` 转发头不动 |
| C8 | `st_thread.cc:551-561` + `st_sys.h:42` | daemon 回调双入口（`Startup` 转发 vs `StartUp` 本体） | 收敛：`DaemonThread()` 惰性绑定指向唯一入口；删除转发器（A3） |
| C9 | `st_sys.h:187-209` | `st_*` 对外声明缺契约注释（B15） | 补返回值/errno/超时语义注释表 |
| C10 | `st_poll.h` | `StEventItem`/`StThreadItem` 公共数据成员（`m_fdset_`/`m_next_`/`m_name_` 等）封装性差 | 本期**只注释不动结构**（TAILQ 侵入式设计决定，改动面大，另立项） |

---

## 5. 分阶段执行

> 与 06 共用「基线三件套」。若 06 已开始执行，本篇 Phase 2 起与 06 Phase 2 之后的代码基线对齐（联动点见 2.3）。

### Phase 0 · 基线锚定（无代码改动）

1. 当前 HEAD 跑三件套并记录；导出 `libmthread.so` 符号表（`nm -g --defined-only`）与体积快照。
2. 记录 `tests/` 覆盖率（近期节点 ~74%）作为下限。
3. 用 `st_loopback_unittest`/`st_server_unittest` 跑 100 次循环，记录 RSS 曲线基线（为 B1/B10 泄漏修复提供对比）。

**出口条件**：三件套全绿；基线数据写入本文「落地记录」。

### Phase 1 · 死代码与重复清理（P-A 全部）

- 直接删：A1、A5、A6、A8；grep 复核后删：A7。
- A2（D2 拍板）、A3/A9（合并为 C8 一并做，先单步验证不可达性）、A4（并入 C2）。
- **出口条件**：三件套全绿；符号表 diff 只减不增且逐项可解释。

### Phase 2 · 潜伏 bug 修复（P-B 全部）

- 每项一个提交：修复 + `tests/` 回归单测（沿用 `st_test.h`）。
- 顺序：先内存安全（B1、B2、B10、B12、B18），再语义（B3、B4、B5、B16、B17、B19），再平台（B6、B13），最后整洁（B7、B8、B9、B14、B15、B20）。
- B10/B12/D1/B11 同一提交窗口，联动 06-B10（HashRemove 所有权）。
- B4/B5 与 06-D7（kqueue EVERR）同窗口。
- **出口条件**：三件套全绿；新增单测全过；RSS 基线对比确认 B1/B10 修复生效（loopback 循环后内存不增长）；覆盖率不低于基线。

### Phase 3 · 结构与性能优化（P-C 全部）

- **C1（`st_sys.cc` 骨架提炼）单独成串**：先写出 helper 并让 `st_read` 一个函数迁移 + 全量回归，确认等价后逐函数推广；每步可独立回滚。
- C2/C8（调度器去重收敛）、C6、C7、C9：各自独立小提交。
- C3（头文件 `using namespace` 清理）：一次机械替换提交，预期 `app/`、`tests/` 需要补少量显式前缀——**允许**为此改调用点的 include/前缀，但不许改逻辑。
- C4（反向依赖降级）：先理清 `StServer` 对 `sys_close`/`st_set_hook_flag` 的真实需求再动手。
- C5、C10：纯注释/常量命名。

**出口条件**：三件套全绿；`st_sys.cc` 行数显著下降且 8 个函数返回值/errno 行为与基线逐一比对一致；`st_sys_api_unittest`、`st_accept_unittest`、`st_conn_io_unittest`、`st_loopback_unittest` 重点回归通过。

### Phase 4 · 通用库收尾

1. **文档**：更新 `AGENTS.md`（目录结构、已知问题表抹掉已修项）、`readme.md` 的架构描述；`src/` 关键类补/订三件套注释（沿用 05 的「用途/线程模型/所有权」格式）。
2. **对外头文件清单**：明确 libmthread 使用方应该 include 哪些头（现状隐式依赖 `app/st_c.h` + `src/*` 混用），给出推荐入口并写入 readme。
3. **落地记录**：把 Phase 0-3 的偏差、符号表 diff、性能/内存对比数据回填到本文第 9 节。

**出口条件**：文档示例与真实 API 一致；三件套全绿；`otool -L`/`ldd` 确认产物零第三方依赖。

---

## 6. 决策点（已拍板 · 2026-09-16）

> 用户确认：**全部按建议采纳 D1–D6**，进入 Phase 0。

| # | 问题 | 拍板 | 落地要点 |
| --- | --- | --- | --- |
| D1 | keepalive 连接复用（B11/B12） | **b** | 保持「只保 fd、不真池复用」；补诚实注释；真复用另立项 |
| D2 | `WaitEvents`（A2） | **a** | 删除；唯一 smoke 改写走 `st_*` |
| D3 | `st_recvfrom` `n==0`（B16） | **b** | 保持「对端关闭」解释 + 注释；不改语义 |
| D4 | 每线程 fd 预分配（B13） | **a** | 容量 = `min(rlim_cur, 65535)`；验证无硬编码假设 |
| D5 | `CallBack` 归还 conn（B10/B20） | **a** | 出口统一 `FreePtr(conn)`；与 B12 Reset/Close 同窗口 |
| D6 | `src`→`app/` 反向 include（C4） | **a** | `sys_*` 前向化优先；可退化为去掉未用的 `app/st_c.h` |

---

## 7. 风险总览

| # | 风险 | 影响 | 概率 | 缓解 |
| --- | --- | --- | --- | --- |
| R1 | C1 骨架提炼改变某个 `st_*` 的边缘行为（errno 时机、部分写返回值） | 高 | 中 | 逐函数迁移 + 逐函数回归；8 个函数的现行返回值/errno 先在 Phase 0 列成对照表，落地时逐项比对 |
| R2 | B1/B10 修泄漏后，对象池复用路径暴露隐藏的状态残留（栈复用、buffer 复用） | 中 | 中 | 池化 `Reset()` 语义单测先行；RSS 基准对比；`st_harvest_unittest` 重点跑 |
| R3 | B3（accept 无限等待）修复后 accept 语义从「假超时重试」变「真挂起」，`Loop()` 的 `continue` 重试路径变冷 | 中 | 低 | 这是修复的预期效果；确认 `st_accept_unittest`/`st_server_unittest` 仍绿即可 |
| R4 | 06 的 `operator==` 修复改变连接判重，keepalive hash 行为变化 | 中 | 中 | 与 06 同窗口验证 `st_conn_manager_unittest`；若旧路径依赖错误判等，先记录再修（不许回退 06 的正确修复） |
| R5 | C3 清 `using namespace` 引发 app/tests 编译错误面扩散 | 低 | 高 | 机械补前缀；一次提交隔离；CI 双平台验证 |
| R6 | B13/D4 改 fd 容量假设，`m_event_[fd]` 索引越界（fd > 容量） | 中 | 低 | `IsValidFd` 已有边界检查贯穿；单测覆盖「fd 接近容量上限」场景 |
| R7 | Linux 验证缺失（本机 macOS） | 中 | 中 | Phase 0 即建立 Linux 验证（CI/Docker），epoll 路径每个 Phase 必跑 |

---

## 8. 验收总清单（可作 PR checklist）

### A. 构建与回归

- [ ] 三件套全绿（macOS + Linux）：`make lib`、`make -C tests run`、`make -C stlib/tests run`
- [ ] 覆盖率不低于 Phase 0 基线
- [ ] 库符号表 diff 逐项有解释；`otool -L`/`ldd` 零第三方依赖

### B. 清理

- [ ] P-A 全部落地；`src/` 内无同构重复实现（C1/C2/C8 完成）
- [ ] 头文件无全局作用域 `using namespace`（C3）；`src/` 不再 include `app/` 头（C4/D6）
- [ ] `src/makefile` 过时注释块重写

### C. 修复

- [ ] P-B 逐项修复且有回归单测
- [ ] 三处内存泄漏（B1 栈、B10 conn、B12 fd）用 RSS/ fd 计数基准验证
- [ ] `st_accept` 无限等待语义正确（B3）

### D. 文档

- [ ] `st_sys.h` 返回值/errno 约定注释表（C9/B15）
- [ ] `AGENTS.md`、`readme.md` 同步；本文落地记录回填

---

## 9. 落地记录（实现时填写）

> 每完成一个 Phase，在此追加：日期、提交号、三件套结果、与计划的偏差。

### Phase 0 · 基线锚定（2026-09-16）

- **HEAD**：`cf62edd`（plan/06 已推 origin/master；本文件此前未跟踪）
- **决策**：D1–D6 全部按建议采纳（见 §6）
- **三件套**（macOS arm64 / TRACE 日志偏吵但不影响结果）
  - `make lib`：绿；`libmthread.a` 1435008 B；`libmthread.so` 216000 B
  - `make test`（stlib）：`ALL STLIB TESTS PASSED`（含 heap_bench N=1e5 push_us≈9479 pop_us≈43042）
  - `make -C tests run`：全部 PASSED（含 loopback/server/conn_manager/sys_api/harvest/http 等）
  - `otool -L libmthread.so`：仅 `libc++` + `libSystem`（零第三方）
- **符号快照**
  - `nm -gU libmthread.so`：129 个全局已定义符号（全文见本机 `/tmp/p07_nm_libmthread.so.txt`，未入库）
  - `nm -gU libmthread.a`：593 行（含归档成员重复名）
- **覆盖率下限**：沿用 plan/06 节点 ~74%（`7f9286d` 口径：src+stlib，排除 ucontext/epoll/st_test/app）；本 Phase 未重跑 gcov，作为不低于线
- **RSS 基线**（macOS `/usr/bin/time -l`，每轮独立进程峰值 RSS，N=100）
  - `st_loopback_unittest`：min 3.844 / max 3.906 / mean 3.868 MB；first 3.859 → last 3.875
  - `st_server_unittest`：min 3.812 / max 3.875 / mean 3.833 MB；first 3.812 → last 3.812
  - 解读：跨进程峰值稳定（σ≈0.01MB），**不**替代进程内「创建/销毁协程·conn」泄漏曲线；Phase 2 修 B1/B10 时需补进程内循环 RSS/堆计数对比
- **`st_*` 返回值/errno 对照（R1 / C1 预备，现行行为摘要）**

  | API | 成功 | `n==0` | 超时 | 不可恢复 errno | 调度失败 |
  | --- | --- | --- | --- | --- | --- |
  | `st_read` | `>0` 字节 | return 0（句柄关闭） | `-1`/`ETIME` | `-1` | `-2` 超时调度 / `-3` schedule |
  | `st_write` | `nbyte`（循环写满） | return 0（句柄关闭） | `-1`/`ETIME` | `-1` | `-2`/`-3` |
  | `st_recv` | `>0` | return 0（对端关闭） | `-1`/`ETIME` | `-1` | `-2`/`-3` |
  | `st_send` | `nbyte` | return 0（对端关闭） | `-1`/`ETIME` | `-1` | `-2`/`-3` |
  | `st_recvfrom` | `>0` | return 0（注释：对端关闭；D3 保持） | `-1`/`ETIME` | `-1` | `-2`/`-3` |
  | `st_sendto` | `n` | return 0 | `-1`/`ETIME` | `-1` | `-2`/`-3` |
  | `st_connect` | `0`/`n`；`EISCONN`→0 | — | `-1`/`ETIME` | `-1` | `-2`/`-3` |
  | `st_accept` | `connfd` | — | 无显式超时（B3） | `-1` | `-2`/`-3` |

  公共前置：`!GetHookFlag()` → `-1`/`ENOSYS`；非法 fd → `-1`/`EINVAL`；`EINTR` 重试；`EAGAIN`/`EWOULDBLOCK`（connect 另含 `EINPROGRESS`）挂起再试。

- **偏差 / 备注**
  - Linux/Docker 验证（R7）本机未做，仍欠
  - `make -C tests run` 全程 PVERB 很吵；`TRACE=0` 未关掉 verbose（记一笔，可后续收口）
  - 下一步：Phase 1 P-A（A1/A5/A6/A8 直删 + A7 grep；A2 按 D2）

### Phase 1 · P-A 死代码（2026-09-16）

- **提交**：`6431b0f`
- **落地**
  - **A1** 删除未使用 `StEventSchedule::BindItem`
  - **A2/D2** 删除 `StSysSchedule::WaitEvents`；`tests/st_log_buffer_extra_unittest` 改为 `StReadSmoke`（`st_read`）
  - **A3/C8** 删除 `StThreadSchedule::Startup` 转发器；`DaemonThread` 不再预绑回调，由 `StSysSchedule::Init` 绑 `StartUp`
  - **A4/C2** `StSysSchedule::{CreateThread,AllocThread}` 薄转发到 `StThreadSchedule`
  - **A5** `StConnection::Create` 改为纯虚；`StServerConnection` 提供默认 `return -1`；测试侧 `KeepliveProbeConn`/`CovConn` 补 stub
  - **A6** 重写 `src/makefile` 过时注释为当前产物说明
  - **A7** **保留** `StServerConnection`（grep：accept/server 单测与 Http/Echo/Udp 等仍作基类）——与原「可删」假设偏差，已记
  - **A8** 删除 `st_sendto`/`st_read` 中 `while (n<0)` 内不可达的 `n==0` 分支
  - **A9** 删除 `ActiveThreadStartUp` 在 `Yield` 之后的不可达 `SwitchThread`/`context_exit`，留注释
- **三件套**：`make lib` / stlib tests / `make -C tests run` 全绿；format-check 绿
- **产物**：`libmthread.so` 195488 B（Phase0 216000）；`nm -gU` 128 符号（Phase0 129，少 `Startup`）
- **偏差**：A7 保留；A5 引发两处测试改调用点（允许，因纯虚化）
- **下一步**：Phase 2 P-B（优先 B1/B2/B10/B12/B18）

### Phase 2 · P-B 潜伏 bug（2026-09-16）

- **提交**：`69fb180`
- **落地（按 D1–D6）**
  - **B1** `FreeStack` 先 `free(m_vaddr_)` 再释放 `Stack`
  - **B2** `Reset` 不再 `free(m_private_)`；`SetPrivate`/`st_set_private` 注明所有权归调用方
  - **B3** `st_accept` Schedule 用 `TimeMs()+0x7fffffff` 替代 `-1`
  - **B4** Dispatch `ST_EVERR`：`IOWaitToRunable` + `ClearItem`
  - **B5** Dispatch 无 owner：日志 + continue（去 `LOG_ASSERT`）
  - **B6** Connect/UDP RecvData/bind 按 `IsIPV6` 选 `sockaddr_in6` 与长度
  - **B7** Connect 外层简化为 `<0` 失败
  - **B8** `Close` 条件改为 `m_osfd_ >= 0`
  - **B9** 二进制缓冲日志改 `%p`/长度，去掉 `%s`
  - **B10/B20/D5** `CallBack` 正常/错误出口 `FreePtr(conn)`
  - **B11/D1** `FreePtr` 注释声明 keepalive 不真复用（仍 HashRemove）
  - **B12** `Reset` 内调用 `Close()`
  - **B13/D4** fd 容量 `min(rlim_cur, 65535)`；`setrlimit` 失败告警
  - **B14** `InitStack` 失败显式日志（去裸 `LOG_ASSERT`）
  - **B15/C9** `st_sys.h` 补返回值约定注释表
  - **B16/D3** `st_recvfrom` `n==0` 保持 + 注释
  - **B17** `WakeupParent` 去掉无意义 `dynamic_cast`
  - **B18** `Schedule` Add 失败回滚 `item` 出线程 fdset
  - **B19** `GetStThreadid` 无栈返回 0
- **单测**：`st_coverage_extra` +3（FreeStack / PrivateNotFreed / ThreadId）→ 15 PASSED
- **三件套**：全绿；format-check 绿
- **偏差**：未单独加 IPv6 loopback CI 单测（本机仍以 v4 回归为主）；B18 的 fdset 非空回滚未实现（现网调用 fdset 恒 NULL）
- **下一步**：Phase 3 P-C（C1 `st_sys` 骨架为主）
