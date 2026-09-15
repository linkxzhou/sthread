# 05 · 文档与规范：readme.md / 代码注释 / AGENTS.md

> 阶段目标一句话：**让一个从未见过这个仓库的人（或 AI）读完文档就能正确编译、正确使用、正确贡献——而且文档里每一行示例都真实跑过。**

## 硬约束（本阶段同样适用）

1. 不改变现有功能行为；对外 API 语义兼容。本阶段**只改文档与注释，不改任何可执行代码**。
2. C++98。文档中的示例代码必须是 C++98 合法的。
3. Linux + macOS 双平台可编译。文档必须分别说明两个平台的构建方式。
4. Google C++ Style（见决策点 D1）。**本阶段负责让文档措辞与 `.clang-format` 的实际内容一致**。
5. 零第三方运行时依赖。文档需明确说明哪些依赖是可选开发期依赖。
6. 协程调度 + epoll/kqueue；业务同步写法、框架内部异步；非阻塞 TCP/UDP 客户端；链接 `libmthread.a`/`.so` 即可使用。**这六条是 readme 的卖点，也是 `AGENTS.md` 的核心约定。**

---

## 1. 目标

1. 重写 `readme.md`——当前版本通篇是**已不存在的旧 API**，照着做必然失败。
2. 新增根目录 `AGENTS.md`，面向 AI 助手与新贡献者。
3. 给关键类补「用途 / 线程模型 / 所有权」三件套注释。
4. 把 01～04 阶段查清的所有事实（`libmthread` 产物内容、实测协程内存占用、双平台差异、已知问题）沉淀成文档。
5. 统一文档措辞与 `.clang-format` 实际内容（D1）。
6. 补许可证文件（D6）。

## 2. 现状（点名真实内容）

### 2.1 `readme.md` 的 API 全部过时（逐条核对）

`readme.md`（211 行）的两个示例里出现的标识符，与当前代码的对应关系：

| readme 里写的 | 当前代码 | 状态 |
| --- | --- | --- |
| `mt_init_frame()` | — | **不存在** |
| `mt_set_hook_flag()` | `st_set_hook_flag()`（`app/st_c.h`） | 已改名 |
| `mt_set_timeout(200)` | — | **不存在** |
| `Frame` / `Instance<Frame>()` | `StThreadSchedule` / `StSysSchedule` | 已改名 |
| `Frame::CreateThread(func, s)` | `StThreadSchedule::CreateThread(StClosure*, bool)` | **签名已变**（改为 `StClosure*`） |
| `Frame::Loop(true)` | — | **不存在**（`StServer::Loop()` 是服务端的，语义不同） |
| `Util::system_ms()` | `Util::TimeMs()`（`stlib/st_util.h`） | 已改名 |
| `IMtAction` | `StConnection` | 已改名 |
| `IMtActionServer` | `StServer<ConnectionT, ServerT>` | 已改名 + 改为模板 |
| `IMessage` | — | **不存在** |
| `HandleEncode` / `HandleInput` / `HandleProcess` / `HandleError` | `DoOutput` / `DoInput` / `DoProcess` / `DoError` | 已改名（03 阶段步骤 4） |
| `eTCP_SHORT_CONN` | `eTCP_CONN`（`src/st_public.h`） | 已改名 |
| `eTCP_ACCEPT_CONN` | — | **不存在** |
| `safe_delete` | `st_safe_delete`（`stlib/st_def.h`） | 已改名 |
| `action->SetConnType(...)` | `StConnection::SetConnType(eConnType)` | 仍存在 |
| `Instance<DNS>()->dns_lookup(...)` | `dns_lookup(const char*, std::vector<int32_t>&, ...)`（`app/st_dns/dns.h:82`） | **仍存在**，签名待细核 |
| `udp_sendrecv(&addr, buf, len, recvbuf, recv_len, timeout)` | `app/st_c.h` 同名 | **仍存在且签名一致** |

即：**readme 的两个完整示例，除 `udp_sendrecv` 一行外，几乎没有一行是当前可用的。**

### 2.2 `readme.md` 的构建说明也是错的

```
## 编译
编译.a或者.so，到当前目录下执行：
    make
编译测试代码，到tests目录下执行：
    make
或者只运行某个单元测试：
    make event
```

三处问题：

1. **根目录没有 makefile**（01 阶段 2.2 / 步骤 7）→「到当前目录下执行 make」直接失败
2. **`tests/Makefile` 没有 `all` 默认目标**，且 12 个 target 里 11 个源文件缺失（04 阶段 2.3）→「到 tests 目录执行 make」失败
3. **没有 `event` 这个 target**（04 阶段 2.3 的 target 清单里无此项）→ 示例命令本身是错的

### 2.3 `readme.md` 的特性清单（这部分是准确的，应保留）

```
1. 不用依赖任何第三方库
2. 基于支持多个平台的协程调度
3. 支持epoll，kequeue                    ← "kequeue" 拼写错误
3. 不用写异步调度代码，全部代码同步，但是框架内部是异步处理   ← 编号重复（两个 3）
4. 提供非阻塞TCP客户端
5. 提供非阻塞UDP客户端
6. 优秀的跨平台特性和高性能（理论上只要系统内存足够大，句柄没有限制，可以无限创建无限个协程）
7. 使用简单，只需要引入一个libmthread.a或者libmthread.so
```

这 7 条**正是任务给出的六条硬约束的来源**，方向正确，需要保留。但有三个小问题：
- 编号重复（两个 `3.`）
- `kequeue` 应为 `kqueue`
- 第 6 条的「理论上…无限创建无限个协程」需要用 04 阶段步骤 7 的**实测数据**替换掉「理论上」

末尾还有一段 ASCII 架构图：

```
TCPServer   UDPServer          TCPClient  UDPClient
         |                              |
Recv    Error    Send           Recv    Error    Send
```

方向对（对应 `DoInput` / `DoError` / `DoOutput`），但过于简略，且没有体现协程调度与事件循环——这是本框架最核心的部分。

### 2.4 代码注释现状

注释**存在但偏少且偏低层**。现有注释的特点：

- 大量行尾中文短注释，说明字段含义。例：`src/st_public.h` 的 `eNORMAL = 0x1, // 通用的`、`src/st_thread.h` 的 `CPP_TAILQ_INIT(&m_run_list_); // 运行队列`
- 有 `// TODO:` 标记未完成处。例：`src/st_thread.h:28` 的 `// TODO: 回收sthread`、`src/st_connection.h` 里两处 `GlobalEventScheduler()->Add(m_item_); // TODO:`
- **缺失的是「为什么」与「怎么用」**：
  - 没有任何类级别的用途说明（`StThreadSchedule` / `StEventSchedule` / `StConnection` 各自职责是什么？）
  - 没有线程模型说明（`Instance<T>()` 是线程局部的这件事，只有读 `st_singleton.h` 才能发现，但它决定了「协程不可跨 OS 线程」这一关键约束）
  - 没有所有权说明（`StThread` 谁 new 谁 delete？`m_callback_` 的 `StClosure*` 由谁释放？`StBuffer` 从 `StBufferPool` 取了要不要还？）
  - `InitContext()` 的 `ty`/`tx` 指针拆分毫无注释（02 阶段步骤 6 已列入）
  - 双后端 `StIOState` 为何要保持接口完全一致（03 阶段步骤 7 已列入）

`stlib/ucontext/ucontext.h` 头部有 `Copyright (c) 2005-2006 Russ Cox, MIT; see COPYRIGHT`，但**仓库里没有 `COPYRIGHT` 文件**（D6）。

### 2.5 `AGENTS.md` 不存在

根目录当前只有 `readme.md`，没有 `AGENTS.md`、`CONTRIBUTING.md`、`LICENSE`、`CHANGELOG.md`。

### 2.6 其它文档文件

| 文件 | 内容 |
| --- | --- |
| `thirdparty/readme.md` | 51 字节，只有两个空标题（`# gperftools安装`、`# 使用gperftools内存分析`），**无正文** |
| `stlib/tests/ucontext/README` | libtask 上游的 README |

`tests/` 下**没有** readme（任务描述提到「tests/readme.md（小写）含 DNS/HTTP 示例」，实测该内容其实在**根目录的 `readme.md`** 里，`tests/` 下无 readme 文件）。

## 3. 问题与风险

| # | 问题 | 影响 | 缓解 |
| --- | --- | --- | --- |
| P1 | readme 示例全部失效，新用户第一步就受挫 | **高**：这是仓库的门面 | 重写，且每个示例必须真实编译运行过 |
| P2 | 若照抄 01～04 的「计划」写文档，会写出与实际代码不符的内容 | 高 | **纪律**：文档只写 04 阶段验收通过的事实，不写计划中的意图 |
| P3 | 「Google Style」措辞与 `.clang-format` 实际不符（D1） | 中 | 按 D1 结论统一措辞 |
| P4 | 「无限创建无限个协程」属夸大表述 | 中 | 用 04 阶段实测数据替换 |
| P5 | 若 03 阶段 L4（keepalive）未修，文档需诚实说明该功能不可用 | 中 | 建「已知限制」章节 |
| P6 | 缺 LICENSE，而代码含 MIT 第三方成分 | 中 | D6；**license 必须由维护者决定** |
| P7 | 中英文文档二选一或双语，工作量差异大 | 低 | 需确认（见第 9 节） |
| P8 | AGENTS.md 写太长则 AI 不读，太短则约束不住 | 中 | 控制在一屏可览，细节链接到 plan |
| P9 | 注释用中文还是英文 | 低 | 现有代码是中文注释 → 保持一致 |

## 4. 做 / 不做

### 做

- 重写 `readme.md`（特性、构建、快速开始、架构、API 参考、已知限制）
- 新增 `AGENTS.md`
- 给关键类补「用途 / 线程模型 / 所有权」注释
- 落实 02 阶段步骤 6 与 03 阶段步骤 7 列出的那批关键注释（本阶段负责审校，确保它们已就位且准确）
- 补 `COPYRIGHT` / `LICENSE`（D6）
- 补 `thirdparty/readme.md` 的来源说明
- 沉淀 04 阶段的实测数据（协程内存占用、协程数上限、`libmthread` 依赖清单）
- 建立「已知限制 / 已知问题」清单（汇总 01～04 的全部 L 项）
- 统一「Google Style」措辞（D1）
- 修 readme 的编号重复与 `kequeue` 拼写

### 不做

- **不改任何可执行代码**。本阶段是纯文档阶段（例外：代码注释，但注释不改变语义）
- **不**写「计划中」的功能。文档只描述已验收通过的现状
- **不**写 API 文档生成（Doxygen 等）—— 属新增工具链，需独立决策
- **不**改 `stlib/tests/ucontext/README`（上游文件）
- **不**删除现有的中文行尾注释（它们是有用的）
- **不**清理 `// TODO:` 标记（它们标示真实的未完成项，应保留并汇总到「已知问题」）
- **不**写 CHANGELOG（需确认版本策略，见第 9 节）

## 5. 分步步骤

### 步骤 1：`readme.md` 重写

**前置纪律（P2）**：开写之前，先确认 04 阶段的验收清单已全绿。readme 里的每一个命令、每一段代码，都必须是 04 阶段真实执行过的。

建议结构：

```
sthread
---
# 简介           ← 保留现有措辞（准确）
# 特性           ← 保留 7 条，修编号/拼写/夸大表述
# 环境要求        ← 新增：编译器、平台、C++98、可选依赖
# 快速开始
  ## 编译        ← 按真实 make 目标重写
  ## 一个最小示例  ← 新增：最短的可运行协程示例
# 核心概念        ← 新增：协程调度 / 事件循环 / 同步写法内部异步
# 示例
  ## DNS 客户端   ← 重写（基于 app/st_dns 真实代码）
  ## HTTP 服务端  ← 重写（基于 tests/st_server_unittest 真实代码）
  ## TCP/UDP 客户端 ← 新增（基于 app/st_c.h 的 udp_sendrecv/tcp_sendrecv）
# 架构           ← 扩充 ASCII 图
# API 参考        ← 新增：对外 API 一览
# 性能           ← 新增：04 阶段实测数据
# 已知限制        ← 新增
# 构建选项        ← 新增：DEBUG/TRACE/ASAN/TCMALLOC 开关
# 贡献           ← 指向 AGENTS.md
# 许可证         ← 待 D6
```

各节要点：

**「环境要求」**（新增，当前完全缺失）：

| 项 | 内容 |
| --- | --- |
| 语言标准 | C++98 |
| Linux | g++（实测 13.3 可用），epoll 后端 |
| macOS | clang++，kqueue 后端。**架构支持需按 02 阶段 L4 结论填写**（`asm.S` 无 arm64 分支） |
| 运行时依赖 | 无（仅系统库：libc / libstdc++ / libpthread / libm / libdl） |
| 可选开发期依赖 | gperftools（tcmalloc / profiler），默认关闭（D2） |

**「编译」**（按 01 阶段步骤 7 的根 makefile 重写）：

```bash
make                    # 产出 libmthread.a 与 libmthread.so
make apps               # 编译 app/ 下的示例
make tests              # 编译测试
make clean

# 可选开关
make DEBUG=1            # -g
make TRACE=0            # 关闭 LOG_TRACE 输出
make ASAN=1             # AddressSanitizer（注意与协程栈切换配合不佳）
make TCMALLOC=1         # 需自行安装 gperftools
```

**「核心概念」**（新增，这是 readme 当前最缺的部分）。需解释清楚三件事：

1. **协程模型**：1:N（每个 OS 线程一套调度器）。`Instance<T>()` 是线程局部的 → **协程不能跨 OS 线程迁移**。三种协程角色：primordial（主）/ daemon（调度）/ 普通协程
2. **「同步写法、内部异步」到底怎么实现**：业务调 `RecvData()` 看似阻塞，实际是「数据没到 → 注册 fd 事件 → `Yield` 让出 → daemon 协程 `epoll_wait` → 就绪后 `IOWaitToRunable` 唤醒 → 从 `RecvData()` 里继续」。**这是本框架的核心价值，必须讲透**
3. **双后端**：Linux epoll / macOS kqueue，编译期二选一（`src/st_poll.h`），业务代码完全无感

**「示例」**：三个示例都必须来自 04 阶段跑通的真实代码。注意 04 阶段 L8：`st_dns` 的示例用了不存在的域名（`www.2000.com` 等），readme 里要换成真实可查的域名。

**「架构」**：扩充 ASCII 图，至少体现协程调度与事件循环：

```
        业务协程 (同步写法)
              |  RecvData / SendData
              v
      StConnection / StServer
              |  数据未就绪 → 注册事件 + Yield
              v
      StEventSchedule ── StEventItem(fd) ──┐
              ^                             |
              |  IOWaitToRunable (唤醒)      |  AddEvent
              |                             v
      StThreadSchedule <── daemon协程 ── StIOState (epoll / kqueue)
        run/io/pend/sleep 四队列              ^
              |                              |  Poll
              └── m_sleep_list_ (最小堆) ─ StHeapTimer
```

**「性能」**（新增，用 04 阶段步骤 7 的实测数据）：

| 指标 | 来源 |
| --- | --- |
| 单协程内存占用 | 04 步骤 7 实测。公式：`MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE` |
| 实测可创建协程数 | 04 步骤 7（含 fd 上限说明，见 04 阶段 L9：非 root 下 `setrlimit` 可能受限） |
| wrk 压测 QPS | 04 步骤 5 |

**「已知限制」**（新增，汇总 01～04 的 L 项）。候选内容：

- 协程对象当前不回收（`src/st_thread.h` 的 `// TODO: 回收sthread`，02 阶段 L9）
- keepalive 连接复用状态（按 03 阶段 L4 结论：已修 / 仍不可用）
- Apple Silicon 支持状态（02 阶段 L4）
- `app/st_c.h` 在 `extern "C"` 块里用 C++ 引用，该头不能被 C 编译器使用（03 阶段 L7）
- `sys_fd` 的超时字段只有 4 bit（03 阶段 L6）
- 单进程内协程不可跨 OS 线程

**出口**：readme 的每个命令与示例都被实际执行验证过。

### 步骤 2：`AGENTS.md`

面向两类读者：AI 助手、新贡献者。**控制篇幅在一屏可览**（P8），细节链接到 `plan/`。

必须覆盖的内容（任务明确列出）：

| # | 约定 |
| --- | --- |
| 1 | **不用第三方库**：`libmthread` 零第三方运行时依赖；gperftools / ASan 仅可选开发期，默认关 |
| 2 | **多平台协程**：ucontext + `asm.S` 多架构；协程实现细节勿擅动 |
| 3 | **epoll + kqueue**：编译期二选一；两个后端接口必须完全一致 |
| 4 | **同步 API / 内部异步**：业务同步写法是核心卖点；不要引入回调式 API |
| 5 | **非阻塞 TCP/UDP 客户端**：`udp_sendrecv` / `tcp_sendrecv` / `StClientConnection` |
| 6 | **高性能**：内存与句柄足够则可大量协程；改动勿增加单协程内存占用 |
| 7 | **只用 `libmthread.a` 或 `.so`**：使用方只链接一个库；不要让使用方直接编译 `src/*.cc` |
| 8 | **目录结构**：各目录职责 |
| 9 | **构建**：根 `make`；开关说明 |
| 10 | **C++98**：禁用清单 |
| 11 | **代码风格**：按 D1 结论；`.clang-format` 用法 |
| 12 | **贡献注意**：API 兼容、枚举数值不可改、不提交二进制、注释用中文 |

具体内容见根目录 [`AGENTS.md`](../AGENTS.md)（本阶段已随本计划一并交付初版，后续需按 01～04 的实际结论更新）。

**出口**：`AGENTS.md` 存在且内容与实际代码一致。

### 步骤 3：关键类注释

给下表每个类补三件套：**用途（一句话）/ 线程模型 / 所有权**。

| 类 | 文件 | 注释要点 |
| --- | --- | --- |
| `StThreadSchedule` | `src/st_thread.h` | 协程调度器；**线程局部单例**（每 OS 线程一份）；四条 TAILQ + sleep 堆的各自用途；daemon/primo 由它惰性创建并持有 |
| `StEventSchedule` | `src/st_thread.h` | 事件调度器；线程局部单例；`m_event_` 是 fd→`StEventItem*` 的直接索引数组（容量 `m_maxfd_`=65535）；持有 `StIOState` |
| `StThreadItem` | `src/st_poll.h` | 协程的数据部分（抽象）；`m_state_` 是枚举值、`m_flag_` 是位掩码，**两者语义不同勿混用**；`m_callback_` 的 `StClosure*` 在 `Reset()` 里被 delete |
| `StThread` | `src/st_thread.h` | 协程的实现部分；栈由 `malloc` 分配、`FreeStack` 释放；**`InitContext` 的 `ty`/`tx` 拆分原理**（02 步骤 6） |
| `StEventItem` | `src/st_poll.h` | 一个 fd 上的事件；虚回调由使用方派生覆盖；由 `UtilPtrPool` 池化管理 |
| `StIOState` | `st_epoll.h` / `st_kqueue.h` | 多路复用后端；**两个后端的公开接口必须完全一致**（`src/st_poll.h` 靠此做条件编译）；`m_file_[fd].mask` 是本地 mask 缓存，用于去重与合并 |
| `StConnection` | `src/st_connection.h` | 连接基类；**`SendData`/`RecvData` 对业务是同步语义，内部会 Yield**（03 步骤 7）；buffer 从 `StBufferPool` 借用，`Reset()` 归还 |
| `StClientConnection<T>` | `src/st_connection.h` | 客户端连接；兼 `StTimer`（可入定时器堆）；`Create()` 会建 socket + 注册事件 + （TCP 时）connect |
| `StConnectionManager<T>` | `src/st_connection.h` | 连接池；仅对 `IS_KEEPLIVE` 类型走 `StHashList` 复用；**注意 03 阶段 L4** |
| `StServer<T, ServerT>` | `src/st_server.h` | 服务端；`Loop()` 每连接创建一个协程 |
| `StSysSchedule` | `src/st_sys.h` | 系统级调度；持有 `StHeapTimer`；`StartUp` 是 daemon 事件循环（待 02 阶段 L6 确认） |
| `Singleton<T>` / `Instance<T>()` | `stlib/st_singleton.h` | **线程局部单例**；用 `pthread_key` 做析构；这是「协程不可跨 OS 线程」的根源 |
| `StHeap` / `StHeapList<T>` | `stlib/st_heap.h` | 可入堆元素 / 堆容器；`HeapValue()` 决定排序键 |
| `StTimer` / `StHeapTimer` | `stlib/st_heap_timer.h` | 定时器；注意 02 阶段 L8（`m_time_expired_` 的 `uint64_t` 初值 `-1` 与 `HeapValue()` 返回 `int64_t` 语义相反） |
| `StBuffer` / `StBufferPool<>` | `stlib/st_buffer.h` | 收发缓冲与池；尺寸 `ST_RECV_BUFFSIZE`/`ST_SEND_BUFFSIZE`（8192） |
| `StClosure` 家族 | `stlib/st_closure.h` | C++98 下的可调用对象封装（无 lambda 可用）；`NewStClosure(...)` 是工厂 |
| `UtilPtrPool<T>` | `stlib/st_util.h` | 对象池；`AllocPtr` / `UtilPtrPoolFree` 配对使用 |

注释语言：**中文**（与现有代码一致，P9）。

**同时审校** 02 阶段步骤 6 与 03 阶段步骤 7 列出的那批注释是否已就位且准确（本阶段是它们的最终验收方）。

**出口**：上表每个类都有三件套注释。

### 步骤 4：许可证与第三方来源（D6）

| 动作 | 内容 |
| --- | --- |
| 补 `COPYRIGHT` | `stlib/ucontext/ucontext.h` 引用了它（Russ Cox, MIT） |
| 补根 `LICENSE` | **需维护者决定本仓库自身的 license**（当前源码头部是 `Copyright (C) zhoulv2000@163.com`，无 license 声明）→ 见第 9 节 |
| 补 `thirdparty/readme.md` | 说明 gperftools 的来源与可选性（D2 结论） |
| 标注 `stlib/tests/ucontext/` | 加说明：libtask 上游参考样例，不参与构建 |
| 标注 `app/st_wrk/http_parser.c` | 说明是 vendored nodejs http-parser（MIT），勿改，需同步上游时替换整个文件 |
| 标注 `app/st_memcacheclient/memcache.pcap` | 说明是协议对照抓包素材（04 阶段步骤 8） |

**出口**：所有第三方成分有来源与许可说明；本仓库 license 明确。

### 步骤 5：统一措辞（D1）

全库搜索「Google」并按 D1 结论统一。若 D1 选 (a)（保留 LLVM 基线）：

- `readme.md` / `AGENTS.md` / `plan/*.md` 中的「Google C++ Style」改为「本仓库代码风格（LLVM 基线 + `m_x_` 成员命名）」
- 说明 `.clang-format` 是唯一权威：`clang-format -i` 即合规
- 明确记录与 Google 的差异点（`AccessModifierOffset: -2`、`PointerAlignment: Right`、成员命名 `m_x_`）

若选 (b)（切 Google）：措辞保留「Google C++ Style」，但需说明成员命名是否也切（D1 的 b/c 之分）。

**出口**：文档措辞与 `.clang-format` 实际内容零矛盾。

### 步骤 6：已知问题汇总

把 01～04 阶段的全部 L 项（遗留问题）汇总成一份清单，分三类：

| 类别 | 处理 |
| --- | --- |
| 已解决 | 记录结论（供后人查阅「为什么是这样」） |
| 已知限制 | 写入 readme 的「已知限制」章节（面向用户） |
| 待办 | 写入 `AGENTS.md` 或独立 issue（面向贡献者） |

来源清单：01 阶段遗留决策（`app/st_sys.cc`/`st_c.cc` 归属）、02 阶段 L1～L9、03 阶段 L1～L10、04 阶段 L1～L9。

**出口**：无遗漏，每项都有归类与结论。

## 6. 兼容策略

| 对象 | 策略 |
| --- | --- |
| 可执行代码 | **一律不动**。本阶段只改 `.md` 与注释 |
| 代码注释 | 只增不删。现有中文行尾注释保留 |
| `// TODO:` 标记 | **保留**（它们标示真实未完成项），并汇总到已知问题 |
| `readme.md` | 全面重写。但「简介」与「特性」的措辞尽量沿用原文（准确且有作者风格） |
| readme 的旧 API 示例 | 替换。**建议在「迁移说明」里保留一张旧名→新名对照表**（即 2.1 的表格），帮助老用户迁移 |
| `stlib/tests/ucontext/README` | 不改（上游文件） |
| 文件名大小写 | `readme.md` 保持小写（现状）；`AGENTS.md` 用大写（社区惯例） |
| 中英文 | 保持中文（现状）；是否加英文版见第 9 节 |

## 7. 验收

- [ ] `readme.md` 里**每一条命令**都被实际执行验证过
- [ ] `readme.md` 里**每一段示例代码**都真实编译过（建议：示例代码直接取自 `app/` 或 `tests/` 下真实文件，避免重新手写引入偏差）
- [ ] readme 不再出现 `mt_init_frame` / `Frame` / `IMtAction` / `IMtActionServer` / `IMessage` / `eTCP_SHORT_CONN` / `eTCP_ACCEPT_CONN` / `Util::system_ms` / `safe_delete` / `mt_set_timeout` 等不存在的标识符
- [ ] readme 的构建说明与真实 make 目标一致（不再有 `make event`）
- [ ] readme 的特性清单编号正确、`kqueue` 拼写正确
- [ ] readme 的「无限协程」表述已替换为 04 阶段实测数据
- [ ] readme 有「环境要求」「核心概念」「已知限制」「构建选项」四个新增章节
- [ ] readme 的架构图体现了协程调度与事件循环
- [ ] readme 含旧名→新名迁移对照表
- [ ] `AGENTS.md` 存在于根目录，覆盖 12 项约定，且一屏可览
- [ ] 步骤 3 表格里**每个类**都有「用途 / 线程模型 / 所有权」三件套注释
- [ ] 02 阶段步骤 6 的四处注释与 03 阶段步骤 7 的六处注释已就位且准确
- [ ] `COPYRIGHT` 存在（Russ Cox / MIT）
- [ ] 根 `LICENSE` 存在（内容由维护者确定）
- [ ] `thirdparty/readme.md` 说明了 gperftools 的来源与可选性
- [ ] `stlib/tests/ucontext/`、`app/st_wrk/http_parser.c`、`app/st_memcacheclient/memcache.pcap` 均有来源/用途说明
- [ ] 全库「Google Style」措辞与 `.clang-format` 实际内容零矛盾
- [ ] 已知问题清单完整（01～04 的全部 L 项）
- [ ] **本阶段的 `git diff` 只含 `.md` 文件与注释行**（可用 `git diff --stat` + 人工复核证明「不改代码」）

## 8. 依赖与工作量

**前置依赖**：

- **04 阶段必须全部验收通过**。这是硬依赖：readme 的每个示例、每个命令、每个性能数字都来自 04 阶段的实际运行结果。**在 04 阶段完成前写 readme，必然写出与代码不符的内容（P2）**
- **决策点 D1**（风格措辞）、**D6**（许可证）
- **02 阶段 L4**（Apple Silicon 支持状态）→ 决定「环境要求」怎么写
- **03 阶段 L4**（keepalive）→ 决定「已知限制」怎么写
- **04 阶段步骤 7 的实测数据** → 决定「性能」章节内容

**被依赖**：无（本阶段是收尾）。但 `AGENTS.md` 会持续约束后续所有贡献。

**改动面**：

| 对象 | 文件 | 侵入程度 |
| --- | --- | --- |
| `readme.md` | 1 | **高**：基本重写（211 行 → 预计更长） |
| `AGENTS.md` | 1 新增 | 中 |
| 关键类注释 | ~12 个文件 | 中：只加注释，不改逻辑 |
| `COPYRIGHT` / `LICENSE` | 2 新增 | 低（但 license 内容需维护者定） |
| `thirdparty/readme.md` | 1 | 低 |
| 各目录的来源说明 | 2～3 | 低 |
| 措辞统一 | ~8 个 `.md` | 低 |

**风险集中点**：

1. **P2（文档与实现脱节）是本阶段最大风险**。缓解办法有两个，建议都用：
   - readme 的示例代码**直接从真实文件摘取**（如用注释标注 `// 完整代码见 app/st_dns/main.cpp`），而不是重新手写
   - 在 CI 里加一个「文档示例编译检查」——把 readme 的代码块抽出来编译。这需要新增工具，属可选增强
2. **LICENSE 无法由实施者决定**（P6）。这是唯一一个「不做决策就无法完成」的验收项。
3. **若 01～04 阶段的结论与本计划的预期不符**（很可能，因为 02 阶段 L1/L2 与 03 阶段 L5 都有「查不到就要上报」的分支），本阶段的文档内容需相应调整。**本阶段的计划本身必须保持灵活**。

## 9. 遗留问题（需确认）

| # | 问题 | 说明 |
| --- | --- | --- |
| L1 | **本仓库自身的 LICENSE 是什么？** | 根目录无 LICENSE，源码头部仅 `Copyright (C) zhoulv2000@163.com`。而代码含 MIT 的第三方成分（libtask ucontext、http-parser）。**必须由维护者决定**，实施者不能代选 |
| L2 | 文档要中文、英文还是双语？ | 现状全中文。若面向国际用户需英文版，工作量翻倍 |
| L3 | 是否需要 `CONTRIBUTING.md`？ | `AGENTS.md` 已覆盖贡献注意事项，可能不必再开一份 |
| L4 | 是否需要 `CHANGELOG.md` 与版本号策略？ | 当前无版本概念。若要发布 `libmthread` 则需要 |
| L5 | 是否要在 CI 里做「readme 示例编译检查」？ | 能根治 P2，但需新增工具链 |
| L6 | `Instance<DNS>()->dns_lookup(...)` 的完整签名与 `DNS` 类是否仍可用 | `app/st_dns/dns.h:82` 确有 `dns_lookup(const char*, std::vector<int32_t>&, ...)`。步骤 1 需核对完整参数与 `Instance<DNS>()` 可用性，决定 DNS 示例怎么写 |
