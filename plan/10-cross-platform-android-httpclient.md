# 10 · 跨平台（Android）+ 平台抽象层 + `st_httpclient` 样例

> **状态：✅ 已完成**（2026-10-07）。基线：`master` = `0f192b5`。落地记录见 §9。
>
> **决策：D1–D10 已按推荐默认值接受**（§5）。Windows 整段移出本期（用户决定：改动面过大）。
>
> 本文档**只描述计划，不包含任何功能代码变更**。和 [`09`](09-main-bugfix-cleanup.md) 的关系：09 管缺陷与生命周期，仍在执行中，会动 `src/st_sys.cc`。本篇管「平台覆盖面」和「新增一个客户端样例」。P0 对 `src/st_sys.cc` 的改动保持最小（D7 落在 hook 宏里，不改 09 的等待/收发骨架）。
>
> 硬约束沿用总索引：C++98；`.clang-format`（LLVM 基线 + `m_x_` 命名）+ CI 的 clang-format-18 检查；**只用 makefile**（不引入 CMake）；零第三方运行时依赖；vendored 代码保留 `COPYRIGHT`；**不发明根 `LICENSE`**；注释用中文。三件套闸门：`make lib`、`make -C tests run`、`make -C stlib/tests run`。另加 `make apps`。

---

## 0. GitHub 能不能做 Android 构建？

**能。** 托管 runner（runner-images 2026-09）上：

| 目标 | runner | 现成工具 | 说明 |
| --- | --- | --- | --- |
| Android（交叉编译） | `ubuntu-24.04` / `ubuntu-22.04` | 预装 NDK 27.x（`ANDROID_NDK_HOME`）；另有 `ANDROID_NDK_LATEST_HOME` | 用 `$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/<triple><api>-clang++` 当 `CC`，`llvm-ar` 当 `AR`。不需要 Gradle / CMake。 |
| Android（运行测试） | `ubuntu-24.04` + KVM | `reactivecircus/android-emulator-runner` | x86_64 模拟器 `adb push` 测试二进制。慢、易抖，单列为可选阶段 P2b，本期不做。 |

**能编过**和**能跑**是两回事。Android 的阻塞点是协程上下文：bionic 没有 `getcontext` / `makecontext` / `swapcontext`。本期把链接问题修掉，并交叉编译出库、apps 和测试；不在模拟器上跑测试。

---

## 1. 目标与非目标

### 1.1 目标

1. **平台抽象层（PAL）**：把散落的平台分支收拢成编译期可选的后端：**上下文切换**、**事件多路复用**、**可选 hook 层**。由 `make.inc` 按**目标**三元组（`$(CC) -dumpmachine`）选择，而不是宿主机 `uname`。Linux 和 macOS 的行为保持不变，库体积基本不涨。
2. **新样例 `app/st_httpclient`**：同步写法的 HTTP/1.1 GET/POST 命令行客户端，支持 `-c` 并发、`-n` 总数、超时、chunked 响应和退出码。对 `st_httpserver` 做冒烟测试，并接入 `make apps` 和 CI。
3. **Android**：用 NDK clang + 现有 makefile 交叉编译 `libmthread.a/.so`、apps 和测试。首期 ABI **只有 arm64-v8a 和 x86_64**。新增 `build-android.yml`（只编译）和 README 徽章。
4. **文档**：`readme.md` / `readme_en.md` 的平台表、`AGENTS.md`、本目录索引同步更新。

### 1.2 非目标（明确不做）

| # | 不做 | 理由 |
| --- | --- | --- |
| N1 | 不改协程调度模型、`STACK`（260096）、`MEM_PAGE_SIZE`（2048）、枚举数值、`st_*` / `tcp_sendrecv` 等公开签名 | 公开 API 兼容 |
| N2 | 不引入 CMake、nmake、Gradle、Bazel | 只用 makefile；Android 靠覆盖 `CC=` / `AR=` 交叉编译 |
| N3 | 不引入 libuv、boost.context 这类第三方库或 vendored 运行时 | 轻量 + 零依赖 |
| N4 | `st_httpclient` 不做 HTTPS/TLS、HTTP/2、重定向跟随、cookie | 不能链 OpenSSL；只作为样例 |
| N5 | **Windows 整段不在本期** | 见 §1.3。MinGW、Fibers、WSAPoll、IOCP、socket 句柄映射、`build-windows-*.yml`、Win64 `ulong` 指针截断、按 fd 下标索引 `SOCKET`，全部移到后续 |
| N6 | 首期 Android 不做 armeabi-v7a 和 x86（32 位） | vendored 的 ARM32 `makecontext` 用 glibc `gregs`，和 bionic 的 `sigcontext` 不兼容；`asm.S` 的 ARM32 不保存 VFP、用 `mov pc, lr`（Thumb-2 需要 `bx lr`）。i686 ELF 没有分支 |
| N7 | 不把 Linux glibc 改成走 vendored asm | 会改变现有行为；要改就单独提案并附 bench |
| N8 | P4 之前不动 `readme.md` / `readme_en.md` | 文档与实现分提交；动手时工作区应干净 |
| N9 | 不发明根 `LICENSE` | 由维护者决定 |
| N10 | 不在 CI 里跑 Android 模拟器 | P2b 可选，本期只交叉编译 |

### 1.3 后续工作（原 Windows 方案，本期不实现）

起草时核对过 Windows，结论是它至少要动三个核心部位，不适合和 Android 绑在同一期：

| 需要的能力 | POSIX 现状 | 曾考虑的 Windows 方案（未采纳、未开工） |
| --- | --- | --- |
| 协程上下文 | ucontext / asm | Fibers：`ConvertThreadToFiber` / `CreateFiberEx` / `SwitchToFiber`。Win64 是 LLP64，`ulong` 只有 32 位，现有 `ty/tx` 拼指针会被截断 |
| 事件多路复用 | epoll / kqueue | 首期曾倾向 WSAPoll（就绪模型贴近 `StIOState::Poll`）；IOCP 更远 |
| socket 与 fd | `int`、小整数下标 | `SOCKET` 是内核句柄，不能直接当 `m_file_[fd]` / `m_event_[fd]` 的下标，需要句柄到 slot 的映射 |
| hook | `dlsym` | Windows 没有 `dlsym`，`ST_HOOK=0` 直接调系统 API |
| 工具链 / CI | gcc/clang + make | MinGW-w64（MSYS2）+ `build-windows-2022.yml` / `build-windows-2025.yml` |

这些决策（事件后端、fd 类型、工具链、pthread 静态链接）**本期不拍板**。PAL 的宏留出后端名字即可，不提交 `st_wsapoll.h`、`st_context_fiber.cc` 或 Windows workflow。

可选的更远项（同样不做）：FreeBSD kqueue CI；Linux 统一改走 libtask asm 的性能提案；Android armeabi-v7a / x86。

---

## 2. 现状盘点（2026-10-07，`0f192b5`，已对照源码）

下列是实现前重新读过的结论，不是沿用草稿的未验证假设。

### 2.1 和本期直接相关的位置

| 维度 | 位置 | 现状 | 对 Android 的影响 |
| --- | --- | --- | --- |
| 事件后端 | `src/st_poll.h` | `__APPLE__ \|\| __OpenBSD__` 用 kqueue，否则 epoll | Android 走 epoll，bionic 可用，`st_epoll.h` 不用改 |
| 上下文选择 | `stlib/ucontext/ucontext.h` | `USE_UCONTEXT=1` 时包含系统 `<ucontext.h>`。只有 Apple（且 `MAC_OS_X_VERSION_10_5`）和 OpenBSD/mips 改走 libtask 私有结构 | **Linux x86_64/aarch64 用的是 glibc `get/make/swapcontext`**。`ucontext.c` / `asm.S` 在 Linux 上展开后为空（`size` 显示 `asm.o` text 为 0） |
| 上下文 asm | `stlib/ucontext/asm.S` | `NEEDAMD64CONTEXT` / `NEEDARM64CONTEXT` **只在 `__APPLE__` 下定义**，符号是 Mach-O 的 `_setmcontext` / `_getmcontext`。没有 `.note.GNU-stack` | 在 Android 上复用这两段必须加 ELF 分支：无下划线符号名。Linux 链接 `libmthread.so` 时 ld 已警告 `asm.o` 缺 GNU-stack、隐含可执行栈 |
| arm64 布局 | `stlib/ucontext/ucontext-arm64.h` + `asm.S` 的 `NEEDARM64CONTEXT` | 保存 x18–x30、d8–d15、sp、pc；`makecontext` 保持 16 字节 SP 对齐 | 与 Android AAPCS64 兼容。x18 在 Android 上是平台寄存器；默认 NDK **不**开 shadow-call-stack，保存/恢复 x18 与 Apple 路径一致，本期不改布局 |
| arm32 makecontext | `ucontext.c` 的 `NEEDARMMAKECONTEXT` | 写 `uc_mcontext.gregs[]` | bionic armv7 没有 `gregs`。推迟（N6） |
| 栈与 `ty/tx` | `src/st_thread.h` 的 `InitContext` / `ActiveThreadStartUp` | `Stack*` 拆成两个 32 位值，`ulong z` 拼回 | Android arm64/x86_64 都是 LP64，`ulong` 是 64 位，拼得回来。用编译期断言卡住 LLP64，不在本期支持 Win64 |
| hook | `app/st_sys.h`、`app/st_sys.cc`、`src/st_sys.cc` | `dlsym(RTLD_NEXT)` 再 `RTLD_DEFAULT`。`src/st_sys.cc` 里每个 `st_*` 在 `!HAS_REAL` 时 `errno=ENOSYS`。`sys_close` 已回退到 `::close` | bionic 有 `dlsym`。静态链接时 `dlsym` 返回 NULL，整套 IO 会变成 ENOSYS。D7 必须在 P0 修 |
| 构建探测 | `make.inc` | `uname -m` 决定是否加 `-m64` | **交叉编译阻塞项**：x86_64 宿主机编 aarch64 会把 `-m64` 传给 NDK clang |
| 归档工具 | `src/makefile`、`stlib/makefile` | 写死 `ar crs`；两处 `UNAME_M` 赋值后未使用 | NDK 要用 `llvm-ar`，`AR` 必须可覆盖 |
| 链接库 | `make.inc` 的 `ST_LDLIBS` | `-lpthread -ldl` | NDK 的 pthread 在 libc 里，Android 下去掉 `-lpthread`。C++ 运行时按 D6 静态链 |
| 小坑 | `app/st_c.cc` 的 `%m`；`stlib/st_netaddr.h` 的 `bzero`；`src/st_public.h` 的 `<sys/syscall.h>` | `%m` 是 glibc 扩展，bionic 的 printf 没有；`syscall.h` 全库没有 `syscall()` 调用 | P0 顺手改成可移植写法 |
| 连接超时 | `StConnection::SetTimeout` / `m_timeout_`（默认 30000） | 已有公开 setter | httpclient 只调用 setter，不改库 API |
| 样例并发 | `app/st_dns/main.cpp` | `c==1` 在 primordial 上跑，否则 `Frame::CreateThread`，主协程 `st_sleep` 等待 | httpclient 沿用，不用 `Loop(true)` |

### 2.2 Android 上下文：选定的做法

bionic 只有给信号用的 `ucontext_t`，**不实现** `getcontext` / `setcontext` / `makecontext` / `swapcontext`。所以 Android 的每个 ABI 都必须走 vendored asm，不能复用 Linux glibc 那条路。

**选定 D4-a**：把已经在 Apple Silicon / macOS x86_64 上用过的 libtask asm 推广到 ELF，而不是引入 boost.context。

- arm64-v8a：`ucontext-arm64.h` + `NEEDARM64CONTEXT`。补 ELF 符号名（`setmcontext` / `getmcontext`，无下划线）。寄存器布局不动，避免和 Apple 分叉。
- x86_64：`ucontext-amd64.h` + `NEEDAMD64CONTEXT`（SysV，和 Linux 用户态一致）。同样只补 ELF 符号名。
- `ucontext.h`：Android 上 `USE_UCONTEXT=0`，不包含系统 `<ucontext.h>`，继续用 `libthread_*` 前缀，避免和 bionic 的 `ucontext_t` 撞名。这和 Apple 的做法一致。
- 文件末尾在 `__ELF__` 下加 `.section .note.GNU-stack,"",%progbits`。Linux 上今天空的 `asm.o` 也会带上这个 note，消掉链接器的可执行栈警告，不改变切换语义。
- armeabi-v7a / x86：缺分支或和 bionic 不兼容，`#error`，不在本期修。

x18：默认 NDK 编译选项不含 `-fsanitize=shadow-call-stack`。asm 继续保存/恢复 x18，这样协程切换不会丢掉调用者放在 x18 的值，布局也和 Apple 的 `ucontext-arm64.h` 一致。打开 shadow-call-stack 的构建不在本期支持范围内。

---

## 3. 设计

### 3.1 平台抽象层

不加运行期虚接口，不加新目录。非目标平台的代码不参与编译。

```
stlib/st_platform.h     # 新增：OS 宏、ST_POLL_*、ST_HOOK、ST_OS_ANDROID
src/st_poll.h           # 改为按 ST_POLL_KQUEUE / ST_POLL_EPOLL 包含后端
stlib/ucontext/ucontext.h
                        # Android 走 ST_CTX libtask；Apple/Linux 条件与今天等价
app/st_sys.h            # ST_HOOK；dlsym 失败则直接调用（D7）
stlib/st_context.h/.cc  # context_make / context_free：POSIX 下包裹现有栈初始化
```

| 后端 | Linux glibc | macOS | Android | 本期不做 |
| --- | --- | --- | --- | --- |
| 上下文 | 系统 ucontext（不变） | libtask asm（不变） | libtask asm + ELF 符号 | Windows Fibers |
| 事件 | epoll | kqueue | epoll | WSAPoll / IOCP |
| hook | 1，dlsym 失败回退直接调用 | 同左 | 同左（静态链接也靠这条回退） | `ST_HOOK=0` 只留宏，不作为默认 |
| fd | 直接用 fd 当下标 | 同左 | 同左 | 句柄映射 |

要点：

1. `src/st_poll.h` 不再写裸的 `__APPLE__`。kqueue 的条件与今天相同：Apple 或 OpenBSD。FreeBSD 仍落在 epoll 分支（编不过），不在本期改。
2. `context_make` / `context_free` 在 POSIX 上做现在的 `calloc` + `malloc` 栈、`sigprocmask`、`ss_sp + 16`、`ss_size - 64`、`ty/tx` 拆分和 `makecontext`。不改这几个常数。`m_id_`、名字、`m_private_` 仍由 `StThread` 填写。
3. **D7**：`HOOK_SYSCALL` 在两次 `dlsym` 都失败时，把函数指针设成 `::name`。动态链接时 `dlsym` 成功，行为不变。`src/st_sys.cc` 里的 ENOSYS 分支留着当最后保险，正常路径走不到。这样 09 和本篇不会在同一函数体里交叉修改。
4. `ST_HOOK` 默认 1。静态链接不单独探测、也不把 Android 编成 `ST_HOOK=0`：D7 的回退已经覆盖「`dlsym` 返回 NULL」。`ST_HOOK=0` 的宏分支保留，给以后没有 `dlfcn.h` 的平台用，本期没有调用方。

体积：P0 前后记录 `size` 的 text。增量应在 1% 以内。把 `InitStack` 从每个翻译单元的内联挪到 `st_context.o` 可能让 `.so` 略变小，也算通过。

### 3.2 `make.inc`：按目标三元组选平台

```make
ST_TARGET := $(shell $(CC) -dumpmachine)
# 例：x86_64-linux-gnu / x86_64-pc-linux-gnu / aarch64-linux-android21 / arm64-apple-darwin24
# 先匹配 android，再 linux（Android 三元组里两者都有）
ST_OS   ?= android | linux | darwin | unknown
ST_ARCH ?= 三元组第一段（x86_64、aarch64、arm64、…）
AR ?= ar
```

- `ST_LDLIBS`：linux/darwin 仍是 `-lpthread -ldl`。android 是 `-ldl`，并加 `-static-libstdc++`（D6：NDK 的 clang 把这个标志用于静态 libc++，使用方不必再带 `libc++_shared.so`）。
- `-m64` / `-m32` 只在 `ST_ARCH` 属于 x86 系时加。`ARCH=32` 在非 x86 上不再强行加 `-m32`（原先会让编译器直接报错）。
- `$(CC) -dumpmachine` 失败时退回 `uname -m`，避免探测失败把本机构建的 `-m64` 弄丢。
- 现有 `make lib CC=clang++` 不受影响：本机三元组推导结果与今天的 `uname -m` 一致。
- 删掉 `src/makefile`、`stlib/makefile` 里未使用的 `UNAME_M`。归档改为 `$(AR) crs`。
- 不引入 `ST_EXE` / `.dll`（那是 Windows 产物命名）。

便捷目标（P2）：

```
make android ABI=arm64-v8a|x86_64 API=21
```

内部用 `$ANDROID_NDK_HOME`（或 `ANDROID_NDK`）拼出 `CC` / `AR`，然后 `make apps`、`make tests`、`make stlib-tests`（只编译，不运行）。

### 3.3 `app/st_httpclient`

**目录**：`app/st_httpclient/{main.cpp,http_client.h,http_client.cc,makefile,README.md}`。源文件头 `Copyright (C) zhoulv2000@163.com`。产物名 `st_httpclient`（不是其他样例那种 `main`）。

**CLI**（`getopt_long`，bionic / macOS 都有）：

```
st_httpclient [-X GET|POST] [-d DATA | -D FILE] [-H 'K: V']... [-c CONC] [-n TOTAL]
              [-t TIMEOUT_MS] [-k] [-o FILE] [-q] [-v] [--json] URL
  -c  并发协程数（默认 1）；-n 总请求数（默认 = c）；-t 单请求整体超时（默认 3000ms）
  -k  同一协程内复用连接（应用层 keep-alive，持有 StExecClientConnection*，
      不用 eTCP_KEEPLIVE_CONN，见 07-D1：库的 FreePtr 仍会 HashRemove）
  -o  把最后一次响应体写入文件；-q 只输出 SUMMARY；-v 把请求/响应头打到 stderr
  --json  用一行 JSON 代替 SUMMARY 文本
输出：c=1 且 n=1 且未加 -q 时，先把响应体打到 stdout；然后总是一行
  SUMMARY ok=.. fail=.. status_2xx=.. status_other=.. bytes=.. qps=.. elapsed_ms=.. p50_ms=.. p99_ms=..
退出码：0 全部 2xx；1 有失败或非 2xx；2 参数/URL 错误（含 https）；3 DNS 失败
```

**实现要点**：

1. URL 用 vendored `http_parser_parse_url`（`app/st_wrk/http_parser.h`）。`https` 或缺少 `http` scheme → 退出码 2。
2. DNS 在 `main` 里、启动协程之前做一次。IPv4 字面量走 `inet_pton`，否则 `getaddrinfo(AF_INET)`。失败退出码 3。本期不做协程内的 `st_dns` 解析，也不承诺 IPv6。
3. 传输**不用** `tcp_sendrecv`（它要预先给定接收缓冲，不适合流式 chunked）。照 `app/st_c.cc` 的 `_get_conn`：`StConnectionManager<StExecClientConnection>::AllocPtr(eTCP_CONN)` → `SetTimeout` → `Create` → `st_send` 发完整请求 → 循环 `st_recv` 喂给 `http_parser`（`HTTP_RESPONSE`）。对端关闭时再 `http_parser_execute(..., 0)` 通知 EOF。结束或不再复用时 `FreePtr`。
4. 请求行：`<METHOD> <path?query> HTTP/1.1`，`Host`（非 80 带端口）、`User-Agent: sthread-httpclient/1.0`、`Accept: */*`、`Connection: close|keep-alive`。POST 默认 `Content-Type: application/x-www-form-urlencoded` 和 `Content-Length`。同名 `-H` 覆盖默认头。
5. 状态码：2xx 计 `ok` / `status_2xx`，收到的其他状态计 `status_other` 且算 `fail`，连接/解析失败计 `fail` 且没有状态码。`ok + fail` 等于完成的请求数。
6. 超时：`SetTimeout` 管连接；发送和每次 `st_recv` 用剩余时间，和 `app/st_c.cc` 的 `_tcp_check_recv` 一样。
7. 并发：沿用 `app/st_dns/main.cpp`。单协程直接在 primordial 上跑；否则 `Frame::CreateThread`。主协程 `st_sleep(10)` 等到计数归零或超时，然后进程退出。
8. POSIX 下 `main` 开头 `signal(SIGPIPE, SIG_IGN)`。
9. makefile 单独编译 `../st_wrk/http_parser.c`，vendored 文件一个字节不改（D8）。

**冒烟**（P1 出口）：`scripts/smoke_httpclient.sh` 后台启动 `app/st_httpserver/main`，沿用 `scripts/bench_http.sh` 的 PID / trap / 端口占用检查。

- `st_httpclient -q http://127.0.0.1:$PORT/` → 退出码 0，`ok=1`
- `-q -c 10 -n 100` → `ok=100 fail=0`
- `-X POST -d 'a=1'` → 2xx（httpserver 忽略 body）
- 连接关闭的端口 → 退出码 1；`https://...` → 退出码 2
- 单请求的响应体与 `curl` 一致
- `-c 50 -n 1000` 时 `fail=0`；进程结束前用 `fcntl` 扫一遍 fd，确认没有按请求泄漏

根 makefile：`apps` 增加 `app/st_httpclient`；`smoke-httpclient`；`clean` 增加该目录。`tests/st_http_client_unittest.cpp`：fork 一个 `HttpTestConn`（Content-Length）再加一个手工 chunked 的阻塞 socket 服务端。`_build.yml` 在编译测试之后跑 `make smoke-httpclient`（阻塞）。

### 3.4 Android CI

` .github/workflows/build-android.yml`：

- `runs-on: ubuntu-24.04`；`matrix.abi: [arm64-v8a, x86_64]`；`API=21`；使用预装 `$ANDROID_NDK_HOME`。不把 NDK 29 列成阻塞维度。
- `make android ABI=$abi API=21` 编译 lib、apps、tests、stlib-tests。
- `file` / `llvm-readelf -d`：架构匹配；`NEEDED` 里不能出现 `libc++_shared`、`libstdc++`、`libpthread`。允许 `libc.so`、`libm.so`、`libdl.so`、`liblog.so`。
- `make clean` 后工作区无残留。
- 不复用 `_build.yml`：那份 workflow 会在宿主机上跑测试，交叉编译的二进制跑不了。

---

## 4. 分阶段执行

> 每个阶段的出口都包含：现有 7 个 POSIX workflow + `format.yml` 全绿；Linux 上 `make lib`、`make apps`、`make -C tests run`、`make -C stlib/tests run`。`tests/` 在 CI 里目前是非阻塞的；已知的 Linux `st_context_unittest` SIGABRT（64 KiB `makecontext` 栈，未改 `STACK`）如实记录，不藏。`ldd` 不新增第三方依赖。提交号和 `size` 写在 §9。

原草稿的 P3 是 Windows，已移到 §1.3。阶段号保留 P4，和任务里的叫法一致，避免把「文档」改叫成 P3 之后对不上。

### P0 · 平台抽象层 + 构建按目标选择（零行为变化）

1. 新增 `stlib/st_platform.h`。`src/st_poll.h`、`app/st_sys.h`、`stlib/ucontext/ucontext.h` 改为读它的宏。Apple/Linux 的后端选择与今天相同。
2. `make.inc`：`ST_TARGET` / `ST_OS` / `ST_ARCH` / 可覆盖的 `AR`；`-m64` 按目标架构。删掉未使用的 `UNAME_M`。
3. `context_make` / `context_free`。POSIX 路径与现在的栈初始化等价（`ty/tx`、`ss_sp + 16`、`ss_size - 64` 不动）。
4. D7：`HOOK_SYSCALL` 回退到直接调用。不改 `src/st_sys.cc` 的等待循环。
5. `bzero` → `memset`；`%m` → `strerror(errno)`；去掉未使用的 `<sys/syscall.h>`。
6. 记录 P0 前后 `size`。

**出口**：现有 CI 全绿；`size` 增量在 1% 以内；`src/` 与 `app/`（vendored `http_parser` 除外）不再直接判断 `__APPLE__` / `__linux__`。用一个假的 `CC`（`-dumpmachine` 打印 `aarch64-linux-android21`）确认命令行里不再出现宿主机的 `-m64`。

### P1 · `app/st_httpclient`

1. 实现 §3.3，加 README。
2. 根 makefile 的 `apps` / `clean` / `help`，以及 `smoke-httpclient`。
3. `scripts/smoke_httpclient.sh`；`tests/st_http_client_unittest.cpp`。
4. `_build.yml` 增加阻塞的 `make smoke-httpclient`。

**出口**：`make apps` 含 httpclient；Linux 上冒烟和单测通过；退出码符合 §3.3。

### P2 · Android 构建 + CI

1. `ucontext.h` / `asm.S` 的 Android ELF 分支（§2.2）；`make.inc` 的 Android 链接参数；`make android`。
2. `.github/workflows/build-android.yml`（arm64-v8a、x86_64，只编译）。
3. 徽章 markdown 留到 P4 再写进 README；P2 的 PR 描述里可以先给出链接。

**出口**：两个 ABI 都编过 `libmthread.a/.so`、apps、tests、stlib-tests；`llvm-readelf` 确认架构和依赖；Linux/macOS 现有 CI 不受影响。

### P2b（可选，本期不做）· Android 运行测试

模拟器上跑 stlib/tests 和 tests 的子集。连续多次稳定之后再考虑改成阻塞。

### P4 · 文档

1. `readme.md` / `readme_en.md`：平台表和顶部徽章增加 Android（arm64-v8a、x86_64，NDK 27，API 21，只编译）。特性里写明 Android 使用 vendored libtask asm（ELF）+ epoll。示例应用加上 `st_httpclient`。**不写** Fibers / WSAPoll（Windows 不在本期）。
2. `AGENTS.md`：约定 2、3 补 PAL 后端表；补「新平台接入」短清单；已知问题里记下 Android 的范围（无 armv7/x86，不跑模拟器）。
3. 本文件 §9 回填；`plan/README.md` 状态改为完成或执行中（以实际落地为准）。

**出口**：文档里的命令在对应平台真实执行过；徽章指向仓库里真实存在的 workflow。

### P5（可选，本期不做）

FreeBSD kqueue；Linux 改走 libtask asm 的性能提案；Android armeabi-v7a / x86；§1.3 的 Windows。

---

## 5. 决策点（已拍板）

| # | 问题 | 选项 | 决定 |
| --- | --- | --- | --- |
| D1 | PAL 的形态 | a) 宏 + 每后端一个文件，编译期选择；b) 运行期虚接口；c) libuv / boost.context | **a** |
| D2 | 平台探测 | a) `$(CC) -dumpmachine`，可命令行覆盖；b) 继续 `uname`；c) 强制手写 `ST_OS=` | **a** |
| D3 | Android 首期 ABI | a) arm64-v8a + x86_64；b) 再加 armeabi-v7a + x86 | **a**（用户明确要求；armv7/x86 推迟） |
| D4 | Android 上下文 | a) 把 vendored libtask asm 推广到 ELF；b) vendor boost.context；c) Linux glibc 也改走 asm | **a**。已核对 Apple 的 arm64/amd64 asm 与 AAPCS64/SysV 一致，缺的是 ELF 符号名和 GNU-stack note。c 会改变 Linux 行为，不做 |
| D5 | minSdk 与 NDK | a) API 21 + 预装 NDK 27；b) API 24；c) API 29（原生 ELF TLS） | **a**。`__thread` 在 API &lt; 29 走 emutls，功能正确 |
| D6 | Android C++ 运行时 | a) `-static-libstdc++`（NDK clang 下即静态 libc++）；b) `libc++_shared.so` | **a** |
| D7 | `dlsym` 失败时 | a) 回退为直接系统调用；b) 保持 ENOSYS | **a**。动态链接时 dlsym 成功，行为不变。这是 P0 唯一有意改的语义 |
| D8 | httpclient 怎么用 http_parser | a) 挪到 `app/common/`；b) makefile 直接编译 `../st_wrk/http_parser.c`；c) 自己写解析器 | **b** |
| D9 | httpclient 的 DNS | a) `main` 里解析一次再起协程；b) 协程内走 `app/st_dns` | **a** |
| D10 | CI 怎么组织 | a) 单独的 `build-android.yml`（ABI 矩阵），`_build.yml` 仍只跑 POSIX；b) 把 Android 塞进 `_build.yml` | **a** |

草稿里的 Windows 决策（事件后端、fd 用 `int` 还是 `SOCKET`、MinGW 还是 MSVC、winpthreads 静态链接）**不进入本表**，见 §1.3。

---

## 6. 风险

| # | 风险 | 影响 | 概率 | 缓解 |
| --- | --- | --- | --- | --- |
| R1 | ELF 下 libtask asm 的细节（x18、栈对齐、`ty/tx`）在 Android 上偶发崩溃 | 高 | 中 | 布局与已在 Apple 上跑过的 arm64 asm 相同；LP64 上 `ulong` 能装下指针，编译期断言；模拟器回归留给 P2b。本期 CI 只保证编过 |
| R2 | P0 抽象引入隐性行为变化 | 高 | 低 | Apple/Linux 后端条件不变；D7 仅在 dlsym 失败时生效；`size` 增量限 1% |
| R3 | NDK 或 runner 镜像升级（默认 NDK 路径、`-lpthread`） | 中 | 中 | CI 打印 NDK 版本；Android 的 `ST_LDLIBS` 不加 `-lpthread` |
| R4 | 和执行中的 09 同时改 `src/st_sys.cc` | 中 | 中 | D7 只改 `app/st_sys.h` 的宏，不改 `src/st_sys.cc` 函数体 |
| R5 | 改 README 和别人未提交的修改冲突 | 低 | 低 | README 只在 P4 改；本分支从干净的 `0f192b5` 拉出 |
| R6 | httpclient 被当成通用 HTTP 客户端 | 低 | 中 | README 写明 N4 和退出码；不提供 TLS |

---

## 7. 验收清单

### A. 抽象层（P0）

- [x] `st_platform.h` 落地；`src/`、`app/` 的业务代码不再直接判断 `__APPLE__` / `__linux__`
- [x] `make.inc` 按 `$(CC) -dumpmachine` 选平台；交叉编译不再把宿主机 `-m64` 传给 aarch64
- [x] `dlsym` 失败时回退直接调用（D7），并在 §9 说明
- [x] `size` 增量在 1% 以内（text 下降）。POSIX workflow 以 PR CI 为准，见 §9

### B. httpclient（P1）

- [x] `make apps` 包含 `st_httpclient`；README 齐全
- [x] `make smoke-httpclient` 在 Linux 上通过；退出码符合约定
- [x] `tests/st_http_client_unittest` 覆盖 Content-Length 和 chunked
- [x] CI 中有阻塞的 smoke 步骤（`_build.yml`）

### C. Android（P2）

- [x] 本机 NDK r27d 编过 arm64-v8a 和 x86_64；`build-android.yml` 以 PR CI 为准
- [x] `libmthread.so` 架构正确，`NEEDED` 只有 `libc.so` / `libm.so` / `libdl.so`
- [ ] （可选 P2b，本期不做）模拟器上 stlib/tests 通过

### D. 文档（P4）

- [x] `readme.md` / `readme_en.md` 平台表 + Android 徽章
- [x] `AGENTS.md` 后端表；`plan/README.md` 状态更新；§9 有提交号
- [x] 不新增根 `LICENSE`；`COPYRIGHT` 不变

---

## 8. 建议目录落点

```
stlib/st_platform.h                      # P0
stlib/st_context.h / st_context.cc       # P0：context_make / context_free
stlib/ucontext/ucontext.h, asm.S         # P2：Android ELF（vendored，改动处加注释）
app/st_httpclient/                       # P1
scripts/smoke_httpclient.sh              # P1
tests/st_http_client_unittest.cpp        # P1
.github/workflows/build-android.yml      # P2
plan/10-cross-platform-android-httpclient.md  # 本文
```

不新增：`st_context_fiber.cc`、`st_wsapoll.h`、`_build-windows.yml`、`build-windows-*.yml`。

---

## 9. 落地记录（实现时填写）

> 每完成一个阶段，在此追加：日期、提交号、平台/编译器、三件套和 `make apps` / smoke 结果、`size` 数据、偏差说明。

### 2026-10-07 · Linux x86_64（g++ 13.3.0）+ NDK r27d（clang 18.0.4）

| 阶段 | 提交 | 结果 |
| --- | --- | --- |
| 计划 | `2daad14` | `plan/10-cross-platform-android-httpclient.md`；Windows 移到 §1.3 |
| P0 | `9a7293b` | `st_platform.h`、`make.inc` 按 `$(CC) -dumpmachine`、`context_make`、D7 回退 |
| P1 | `c30688b` | `app/st_httpclient`、`scripts/smoke_httpclient.sh`、单测、`_build.yml` 阻塞 smoke |
| P2 | `a12da7d` | ELF `setmcontext`/`getmcontext`、`.note.GNU-stack`、`make android`、`build-android.yml` |
| P4 | 本提交 | `readme.md` / `readme_en.md` / `AGENTS.md` / 本段 |

**Linux（g++ 13.3.0，`TRACE` 默认，smoke 为 `TRACE=0`）：**

| 命令 | 结果 |
| --- | --- |
| `make lib` | 绿。链接 `libmthread.so` 不再出现 GNU-stack / 可执行栈警告。`asm.o` text = 0（Linux 仍走 glibc ucontext） |
| `size` | `.so` text **170635**（P0 前 `0f192b5` 为 172600，约 −1.1%）。`.a` 成员 text 合计 **143066**（前 145953，约 −2.0%） |
| `ldd libmthread.so` | `libstdc++`、`libgcc_s`、`libc`、`libm`。无第三方 |
| `make apps` | 绿，含 `st_httpclient`。`ldd` 同样只有系统库 |
| `make -C stlib/tests run` | `ALL STLIB TESTS PASSED` |
| `make smoke-httpclient` | `ok get/conc/post/refused/https/body/stress/keepalive` |
| `make format-check CLANG_FORMAT=clang-format-18` | 绿 |
| `make -C tests run` | **未全绿**（见偏差 8）。单独续跑：`st_http_client_unittest` 通过（Content-Length + chunked） |

**Android（本机 NDK r27d，`make android API=21`，只编译）：**

| ABI | 结果 |
| --- | --- |
| arm64-v8a | `libmthread.so` 为 ARM aarch64。`asm.o` 导出 `setmcontext` / `getmcontext`（无 `_` 前缀），带 `.note.GNU-stack`。`NEEDED`：`libdl.so`、`libm.so`、`libc.so` |
| x86_64 | 同上，架构 x86-64。NDK clang 接受 `-m64`。`ucontext-amd64.h` 的 `#pragma message` 会刷屏，不失败 |

`stlib/libst.so`、apps、`tests/*_unittest`、`stlib/tests/*_test` 的 `NEEDED` 同样只有上述三个系统库。`ABI=armeabi-v7a` 在进编译前拒绝。未设置 `ANDROID_NDK` / `ANDROID_NDK_HOME` 时拒绝。

**偏差（相对本文原文）：**

1. **没有 P3。** Windows（MinGW、Fibers、WSAPoll、IOCP、句柄映射、Win64 `ulong`）整段在 §1.3，不实现。阶段号保留 P4。
2. **D7 只改 `app/st_sys.h` 的 `HOOK_SYSCALL`。** `src/st_sys.cc` 的 ENOSYS 分支未动，避免和执行中的 plan/09 交叉。静态链接不探测、也不把 `ST_HOOK` 改成 0，靠「dlsym 失败则 `::name`」。
3. **`malloc` 失败时 `context_make` 返回 −1**，不再在空指针上继续。成功路径的 `ss_sp + 16`、`ss_size - 64`、`ty`/`tx` 不变。
4. **`.note.GNU-stack` 加在所有 ELF 的 `asm.S` 末尾**，包括 Linux 上 text 为空的 `asm.o`。只消掉链接警告，不改变切换。
5. **`make android` 同时构建 `stlib`**（计划正文写了 apps / tests / stlib-tests）。Android 的 `libmthread.so` 链接行多了 `$(ST_LDLIBS)`（`-ldl`），Linux 的 `.so` 链接行不变。
6. **非 x86 上 `ARCH=32` 不再强加 `-m32`。** 不引入 `ST_EXE`。
7. **httpclient 的日志级别是 `LLOG_CRIT`。** `LLOG_ERR` 会打到 stdout，和 `curl` 的响应体对不上。fd 泄漏检查是进程结束前 `fcntl` 扫描 0..255，打开数 > 32 则失败。不做 IPv6。样例不使用 `eTCP_KEEPLIVE_CONN`。
8. **`make -C tests run` 的三处失败在 `0f192b5` 上已存在，未改 `STACK`：** `st_loopback_unittest` 的 `UdpSequentialLoopback`（`rc == 0`，同进程连续 UDP）；`st_context_unittest` SIGABRT（`stack smashing detected`，64 KiB `makecontext` 栈）；`st_sys_api_unittest` 的 `SysApiAccept`（`cfd >= 0`）。官方目标 `set -e`，停在 loopback。其余单测（含新的 http client）通过。
9. **P2b 模拟器、armeabi-v7a / x86、FreeBSD CI、Linux 改走 libtask asm，都没做。** 未发明根 `LICENSE`。`COPYRIGHT` 未改。

---

## 10. 与需求的对照

| 需求 | 落点 |
| --- | --- |
| GitHub 是否支持 Android 构建 | §0：ubuntu runner 预装 NDK，makefile 交叉编译 |
| 支持 Android，先修链接失败 | §2.2、§3.4、P2。做法是 ELF 化 vendored libtask asm（D4-a） |
| 首期只要 arm64-v8a 和 x86_64 | D3、N6 |
| Windows 先不做 | §1.2 N5、§1.3 |
| 轻量、不引入第三方运行时 | D1、D4-a、D6、N3 |
| 增加 http client 样例 | §3.3、P1 |
| 先写 plan，再按阶段实现 | 本文；实现结果回填 §9 |
