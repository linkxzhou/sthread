# 09 · 主分支缺陷修复与精简计划

> 状态：执行中（macOS arm64 修复与回归已启动；Linux 和压力验收待验证）。基线：2026-10-06，`origin/master` / 本地 `master` = `0141a94`。远端不存在 `main`，默认分支为 `master`；`git pull --ff-only origin master` 已执行，结果 Already up to date。

## 目标与边界

按“复现和锁定缺陷 → 修复资源与事件生命周期 → 安全精简 → 双平台验证”推进。保持 C++98、现有 API/枚举数值/超时/栈尺寸、协程调度模型和 epoll/kqueue 双平台行为；不引入运行时依赖。既有 `plan/01`～`08` 已完成，避免重复开展泛化重构；keepalive 真连接复用属于功能设计，单列决策，不混入 bugfix。仅证实无调用且无 ABI/测试/文档契约时删除代码。

## 现状与证据（2026-10-06 macOS arm64）

- `make lib TRACE=0` 成功；`make test TRACE=0` 的 stlib 全部通过。
- `make -C tests run TRACE=0` **失败**：`st_coverage_extra_unittest.cpp:41` 的 `ConnNullBuffers` 断言 `conn.RecvData() == -1` 不成立。`StConnection::Reset()`（`src/st_connection.h:74-85`）会归还并重新分配 buffer，注释“frees buffers -> NULL”与现状相反；这首先是测试前提失效，不能据此宣称 RecvData 空缓冲逻辑错误。`RecvData()` 本身在空缓冲时返回 -1（`src/st_connection.cc:63-67`），实际错误 fd 调用返回 -2（同文件 :103-109）。先修测试构造/预期，再独立测试真正的空缓冲分支。测试套件 `set -e`，失败后 `st_dns_proto_unittest` 未运行。
- 已有历史 Linux 记录：同进程同协程连续 `udp_sendrecv` 第二次不成功、`st_sys_api_unittest` UDP 断言失败，以及 `st_context_unittest` 64 KiB `makecontext` 栈 SIGABRT（`plan/08-apps-bench-dnsserver.md:292-296`）。本次没有 Linux 环境实测；这些是**待复现**，不视为已验证根因。
- 可疑资源路径：`app/st_c.cc:24-31` 的 `_get_conn` 在 `Create` 失败时直接返回 NULL，归还连接的代码被注释；`_release_conn`（:39-60）关 fd/清事件但没有 `StConnectionManager::FreePtr`，需结合池的使用契约判定是否为泄漏/非预期复用。`src/st_connection.h:140-159` 创建事件项后 `Add` 返回值未检查；失败处理含两个 TODO。以上均须先做故障注入和所有权梳理，避免重复释放。
- 可疑调度路径：`src/st_thread.h:32-37` 析构仅清指针，尚未回收 daemon/primo/协程；`src/st_thread.cc:207-216` 创建普通协程后未显式归还。`src/st_thread.cc:537-546` 的 `Schedule` 在 `fdset != NULL` 时 Add 失败的回滚尚未实现；当前调用主要传 NULL。`src/st_thread.cc:340-346` 出现旧事件项被新项替换的日志，需验证 fd 复用时是否有过期 owner/队列指针。
- 文档和实现有过时描述：根 `makefile:18-20` 仍说 src 编不过、默认目标停在 stlib；`AGENTS.md:5` 指向大写 `README.md`，根目录实际是 `readme.md`；`plan/README.md` 的早期失败快照是历史资料，不要将其当作当前故障。文档修订不应顺手改变默认构建目标。

## 阶段 A · 建立可重复的正确性基线（P0）

1. 固定工作区、平台、编译器、HEAD；保留每个失败命令、测试名和错误输出。先修 `ConnNullBuffers` 的错误测试前提：分别覆盖 `Reset` 后缓冲可用、真实空缓冲分支、无效 fd 返回约定。保证不为迎合断言改变线上 `Reset` 语义。
2. 补 `udp_sendrecv` 单协程连续 2～N 次以及不同 fd 连续复用的 loopback 回归；记录第几次失败、errno、`GetEventItem` 映射、内核兴趣、线程 fdset；再对比 Linux epoll 与 macOS kqueue，定位 send/recv/释放哪一段遗留状态。检查 `st_sys_api_unittest` UDP 失败是否同根因，避免重复修复。
3. 在 Linux 单独复现 `st_context_unittest`，确认 abort 来源（测试只给 64 KiB 栈还是上下文实现）；优先修测试前提或上下文边界检查，不修改 `STACK`/`MEM_PAGE_SIZE` 契约。把平台差异写清楚。
4. 出口：macOS `make lib TRACE=0 && make -C tests run TRACE=0 && make test TRACE=0` 全绿；Linux 同样执行且有记录；新回归用例在修复前可稳定失败、修复后通过。

## 阶段 B · 生命周期与错误路径（P0/P1）

1. 画清 `_get_conn`/`_release_conn` → `Create`/`ClearItem`/`Close` → `StConnectionManager::FreePtr` 的所有权表（连接、事件项、socket、buffer）；分别注入 socket 失败、Add 失败、connect 失败、收发超时和 fd 复用，检测泄漏/悬挂/重复释放。确认契约后只补必要清理与 Add 返回值处理，并移除已注释的伪实现/TODO。
2. 核对 `WaitFdReady`/`Schedule`/`Delete`/`Dispatch` 的映射和队列不变量；确保失败回滚、超时、正常就绪和 hangup 各路径均摘掉原 item 且不会误删复用后的 fd；若非空 fdset 分支无调用，不擅自删公开能力，先做故障注入再补回滚。
3. 统计连续创建/退出协程的 RSS 和池对象数量；区分池缓存与真泄漏，再决定归还时机和 scheduler 析构回收方式，保留父子唤醒及 TLS 析构安全。keepalive 是否改为真复用需另行确认 API 语义，不在本阶段隐式改行为。
4. 出口：每条修复有失败先行的测试；循环请求/建连及超时压测无 fd/RSS 持续增长；ASan（可用平台）无新的 UAF/双重释放；原有回归全绿。

## 阶段 C · 低风险清理与功能优化（P1/P2）

1. 基于 `rg`/调用图与编译引用验证，逐项删除无效注释、过期 TODO、不可达分支和未使用局部变量；每项说明“为何安全删除”，不做全局重命名/模块迁移。优先修 `app/st_c.cc` 的注释掉的 FreePtr 分支与 `src/st_connection.h` 的 TODO（以前一阶段所有权结论为准）。
2. 同步修正 `makefile` / `AGENTS.md` / `plan/README.md` 中已过期的当前状态描述和大小写错误；只改文档注释，不调整默认目标或兼容 API。构建配置如需变更，须单独说明收益与验证结果。
3. 性能优化以已冻结 `reports/baseline-*.md` 为参照，同平台同编译开关重复 HTTP/DNS smoke；先测 CPU、RSS、QPS、P95，再仅对可归因热点做局部优化，保留结果与回退条件（吞吐不退化、正确性测试全绿）。不以短连接基线推断 keepalive 收益。

## 本次执行记录（2026-10-06，macOS arm64）

- 阶段 A：修正 `ConnNullBuffers` 的错误前提，分别断言 Reset 后缓冲存在、模拟空缓冲返回 -1 和无效 fd 返回 -2；增加同进程同协程 4 次 `udp_sendrecv` 的 loopback 回归。该场景在 macOS 上修复前也可通过，**不能据此宣称 Linux 历史故障已经修复**；Linux 容器命令已安装但 Podman socket 未启动，Linux `st_context_unittest`/UDP 均未复现。
- 阶段 B：客户端事件项由连接对象在 Reset/析构时负责注销、归还；`Create` 检查事件注册失败并清理，`_get_conn` 创建失败归还连接，`_release_conn` 归还非 keepalive 请求连接，keepalive 请求也关闭并归还而不声称真复用。侵入式队列 `Add(fdset)` 改为逐项转移以维护 parent/size，非空 fdset 的 Schedule 注册失败恢复原队列；新增定向测试。**协程创建后的自动回收暂未实现**：当前切栈/外部持有指针契约未澄清，直接在 Yield 中归还会导致悬挂引用；需后续独立方案与长期 RSS 验证。
- 阶段 C：清理注释掉的归还代码和过时注释，更新文档链接与索引；保留默认 `make` 目标。macOS `make apps TRACE=0`、`make -C tests run TRACE=0`（含新增测试）、`make test TRACE=0` 均通过；`make format-check CLANG_FORMAT=/opt/homebrew/opt/llvm/bin/clang-format` 通过，`otool -L libmthread.so` 仅见 libc++/libSystem。HTTP smoke 10/10、error=0、2126.75 req/s；DNS smoke 10/10、fail=0、909.09 qps；样本小、平台不同，不与 Linux 冻结基线直接比较。原生 `make format-check` 缺少 PATH 内的 clang-format，指定绝对路径可用。ASan、Linux、fd/RSS 长时压力和跨平台性能回归**未完成**，本计划仍处于执行中。

## 最终验收与交付

- macOS arm64 + Linux：`make lib TRACE=0`、`make apps TRACE=0`、`make -C tests run TRACE=0`、`make test TRACE=0`；平台可用时加 `ASAN=1` 独立干净构建，避免混用旧目标文件。
- 覆盖 UDP 连续请求、TCP/UDP 超时、fd 复用/注册失败、协程创建回收；对 `otool -L`/`ldd` 确认零新增第三方运行时依赖。
- 每阶段记录“复现测试 → 最小修复 → 验证结果”；是否提交由维护者决定。如果 Linux 环境暂不可用，标记该出口未验证，不能声称全平台完成。
