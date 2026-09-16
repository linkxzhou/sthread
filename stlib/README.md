# stlib

sthread 的基础库：日志、网络地址、缓冲池、哈希表、最小堆 / 定时器、epoll·kqueue
抽象、协程 ucontext 底座，以及少量工具类。

绝大多数接口是 **header-only**（C++98）。生产库目标文件只有：

| 单元 | 作用 |
| --- | --- |
| `st_log.cc` | 日志实现 |
| `st_context.cc` | 协程上下文封装 |
| `ucontext/ucontext.c` + `asm.S` | get/set/swap/makecontext（含 arm64） |

`st_test.cc`（轻量 `TEST` / `ASSERT_*`）**不进** `libst` / `libmthread`（plan/06 D6），
只在 `stlib/tests` 与 `tests/` 构建时单独编译。

构建：

```bash
make -C stlib          # libst.a / libst.so
make -C stlib/tests run
```

完整网络框架请链接仓库根目录的 `libmthread`（见根 [`README.md`](../README.md) /
[`AGENTS.md`](../AGENTS.md)）。

---

## 组件清单

| 头文件 | 职责 | 线程模型（约定） |
| --- | --- | --- |
| `st_def.h` | 错误码、事件掩码、`ST_*` 宏、`time64_t` | 无状态 |
| `st_log.h` | 分级日志 | `__thread` 缓冲；多 OS 线程各自一份 |
| `st_util.h` | `Util`、`UtilPtrPool`、`referenceable`、`any_cast` | 工具无锁；池按调用方线程使用 |
| `st_singleton.h` | `Instance<T>()` 线程局部单例 | **每 OS 线程一份**，非进程全局 |
| `st_tailq.h` | 侵入式双向队列 | 调用方保证单线程访问 |
| `st_netaddr.h` | `StNetAddr` / IP 字符串 | 静态缓冲为 `__thread` |
| `st_hash_list.h` | 开链哈希；`HashRemove` 返回节点、调用方释放 | 单线程 |
| `st_buffer.h` | `StBuffer` / `StBufferPool` | 随连接在单 OS 线程内使用 |
| `st_heap.h` / `st_heap_timer.h` | 最小堆与基于堆的定时器 | 单线程（调度器线程） |
| `st_epoll.h` / `st_kqueue.h` | `StIOState` 同名接口 | 仅服务本线程 `StEventSchedule` |
| `st_closure.h` | `NewStClosure` 回调 | 堆对象，由线程 Reset 等路径释放 |
| `st_context.h` + `ucontext/` | 协程上下文 | 与调度器同线程 |
| `st_test.h` | 测试框架（仅测试构建） | — |

### 线程模型一句话

stlib **不做跨 OS 线程的协程迁移**。同一 OS 线程内：一个调度器、一套
epoll/kqueue、一份线程局部单例与日志缓冲。多线程时每线程各自 `Instance` /
事件循环，互不共享堆与 fd 表。

---

## 最小可运行示例（本目录）

```bash
make -C stlib/tests st_demo_usage_test
./stlib/tests/st_demo_usage_test
```

源码：[`tests/st_demo_usage_test.cc`](tests/st_demo_usage_test.cc) —— 演示最小堆、
缓冲池与 `Util::TimeMs()`。

若要「协程 + TCP 监听」的完整服务端，请用框架库示例（需 `libmthread`）：

```bash
make -C app/st_httpserver
./app/st_httpserver/main 8765
curl -v http://127.0.0.1:8765/
```

---

## 裸宏清单（本期不改名）

生产侧常见宏（改名会牵动全仓库调用点，plan/06 Phase 4 只文档化）：

| 前缀 / 名 | 头文件 | 含义 |
| --- | --- | --- |
| `ST_OK` / `ST_ERROR` / … | `st_def.h` | 返回码 |
| `ST_NONE` / `ST_READABLE` / `ST_WRITEABLE` / `ST_EVERR` | `st_def.h` | 事件掩码 |
| `ST_MAX_FD` / `ST_LISTEN_LEN` / `ST_RECV_BUFFSIZE` / `ST_SEND_BUFFSIZE` | `st_def.h` | 容量常量 |
| `ST_ALGIN` | `st_def.h` | 历史拼写（非 ALIGN）；按 8 字节填充式对齐 |
| `ST_MIN` / `ST_MAX` / `ST_NELEMS` / `ST_MAXINT` | `st_def.h` | 工具宏 |
| `st_safe_delete` / `st_safe_free` / … | `st_def.h` | 空指针安全释放 |
| `likely` / `unlikely` | `st_def.h` | 分支提示 |
| `LOG_*` / `LLOG_*` | `st_log.h` | 日志级别与输出宏 |
| `CPP_TAILQ_*` | `st_tailq.h` | 侵入式队列 |
| `TEST` / `ASSERT_*` | `st_test.h` | **仅测试** |

---

## 与 libmthread 的关系

`make lib` 把 stlib 的生产目标文件编进根目录 `libmthread.a/.so`，再加上
`src/` 与 `app/st_c|st_sys|st_action`。使用方一般只链 `libmthread`，不必单独链
`libst`。
