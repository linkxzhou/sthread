# plan/04 兼容回归清单（执行记录）

> 分支：`impl-l4-keepalive`（含 arm64 ucontext + L4）  
> 机器：macOS arm64（Apple Silicon）  
> 日期：以提交时间为准

## 构建

| 项 | 结果 |
| --- | --- |
| `make lib` → `libmthread.a` / `.so` | 通过 |
| `make -C app/st_dns` | 通过 |
| `make -C app/st_memcacheclient` | 通过 |
| `make -C app/st_wrk` | 通过 |
| `make -C tests all` | 通过（primary targets） |
| `make -C tests server` / `c` | 编译通过 |
| `otool -L libmthread.so` | 仅系统库（plan/03 已验） |

## 单测运行（arm64）

| 测试 | 结果 | 备注 |
| --- | --- | --- |
| `st_singleton_unittest` | PASSED | |
| `st_base_unittest` | PASSED | `StEventItem`（原 `st_base`/`StEventSuper`） |
| `st_thread_unittest` | PASSED | daemon/primo 可取；`Wait(10)` 在真实 arm64 ucontext 上通过 |
| `st_manager_unittest` | PASSED | `st_init_frame` + `CreateThread` API；无真实切换 |
| `st_connection_unittest` | PASSED | Create/Close；`st_connect` 不再递归进 hook |
| `st_session_unittest` | PASSED | TAILQ / 栈内存公式 |
| `st_keepalive_unittest` | PASSED | 枚举 0x11/0x21 + `Keeplive()` |
| `st_main_unittest` | exit 0 | pthread + `Instance` |
| `st_server_unittest` | 编译通过 | 只验 `CreateSocket`+`Listen`，不进 `Loop` |
| `st_c_unittest` | 编译通过 | 含外网请求，未作 CI 强制运行 |

## App 运行（2026-09-15，Apple Silicon，`impl-l4-keepalive`）

| App | 结果 | 备注 |
| --- | --- | --- |
| `st_dns` | **部分通过** | 150 协程创建并进 IO wait；对合成域名 `www.2000–2149.com` UDP 查询超时失败（预期无 A 记录），无崩溃。`Frame::Loop(true)` 不退出，冒烟用超时杀掉。 |
| `st_memcacheclient` | **通过** | 本机 `memcached :11211`；`SendRecv` ret=0，可见 `STORED` / `VALUE k1`；有 `item conflict` 告警（同 fd 多 action），不影响本次冒烟结论。 |
| `st_wrk` | **通过** | `./wrk -n 3 -c 3 -d 2s http://127.0.0.1:8765/` → 3 requests，~832 req/s；协程 connect/读写路径可用。 |

说明：arm64 真实 ucontext + L4 之后，示例 app 的协程 IO 路径已可在本机验证。

## 本阶段相对 01～03 的关键改动

1. **`app/st_action.{h,cc}`**：精简 `IMessage` / `IMtAction` / `IMtActionClient`，`SendRecv` 走 `tcp_sendrecv`/`udp_sendrecv`（历史 `mt_action` 已删除，未整包恢复）。
2. **`app/st_frame.h` + `st_init_frame()`**：示例 app 的 `Frame` / `mt_init_frame` 兼容。
3. **`src/st_server.h`**：`Manager` → `StSysSchedule`；`StEventSuper` → `StEventItem`；`Handle*` → `Do*`；`Close` → `ClearItem`。
4. **`stlib/st_test.h`**：断言失败由 `exit(0)` 改为 **`exit(1)`**（CI 可判定）。
5. **三个 app makefile**：`-I../..`，接入 `make.inc`；`st_wrk` clean 修为删 `wrk`。
6. **`tests/Makefile`**：链接 `libmthread.a`；按真实文件重写 target。

## 明确不做 / known-failure

| 项 | 状态 |
| --- | --- |
| L4 keepalive (`eTCP_KEEPLIVE_CONN`) | **已修**（`|` + `Keeplive()`） |
| arm64 真实 `ucontext` / `asm` | **已落地**（`NEEDARM64CONTEXT` + `libthread_makecontext`） |
| 完整恢复历史 `mt_action` 多路 poll 客户端 | 用精简 `st_action` 替代 |
| 高并发 1万+ 协程实测 | 待测（底层切换已通） |
| `tests/uthread.*` vs `stlib/ucontext/uthread.*` | 仍并存；未删（内容不同） |

## 建议后续

1. Linux CI：跑 `make apps` + DNS/wrk 冒烟 + `make -C tests run`。
2. ~~arm64 真实 ucontext~~ / ~~app 冒烟~~ 已完成（见上表）。
3. 万级协程 / QPS 专项压测仍待做。
4. plan/05 示例已对齐 `st_init_frame` / `StSysSchedule` / `st_action`。
