# 04 · 应用与测试：st_dns / st_memcacheclient / st_wrk / tests / 兼容回归

> 阶段目标一句话：**让三个示例 app 与全部 unittest 真正编译、真正跑起来，并用一张回归清单证明前三阶段没有改坏任何东西。**

## 硬约束（本阶段同样适用）

1. 不改变现有功能行为；对外 API 语义兼容。基准为「代码表达的语义意图」，详见 [`README.md`](README.md#对不改变现有功能行为这条约束的必要澄清)。
2. C++98。禁止 C++11+ 语言特性；`__thread` / `__builtin_expect` 等 GNU 扩展可用。
3. Linux + macOS 双平台可编译。
4. 本仓库代码风格（LLVM 基线 + `m_x_`，见决策点 D1）。
5. 零第三方运行时依赖。三个 app 只应链接 `libmthread.a` + 系统库。
6. 协程调度 + epoll/kqueue；业务同步写法、框架内部异步；非阻塞 TCP/UDP 客户端；链接 `libmthread.a`/`.so` 即可使用。

**本阶段的特殊定位**：前三阶段是「修」，本阶段是「证」。所有验收都必须是**可运行的证据**，不接受「读代码认为没问题」。

---

## 1. 目标

1. `app/st_dns`、`app/st_memcacheclient`、`app/st_wrk` 三个 app 编译通过并可运行。
2. `tests/` 下全部 unittest 编译通过并运行通过。
3. `tests/Makefile` 重写——它当前引用的源文件几乎全不存在。
4. 建立**兼容回归清单**，覆盖 01～03 触碰过的每一个语义点。
5. 验证 readme 里承诺的两个示例（DNS client、HTTP server）真的能跑（为 05 阶段重写 readme 提供依据）。
6. 高并发冒烟：验证「只要内存和句柄足够就能开大量协程」这一核心卖点。

## 2. 现状（点名真实文件）

### 2.1 三个 app 的构成

| app | 源文件 | 说明 |
| --- | --- | --- |
| `app/st_dns` | `dns.cpp`、`dns.h`、`main.cpp`、`makefile` | DNS 客户端。readme 的第一个示例就是它 |
| `app/st_memcacheclient` | `memcache.cpp/.h`、`mt_array.cpp/.h`、`main.cpp`、`makefile` | memcache 客户端。注意 `mt_array` 仍是 `mt_` 旧前缀 |
| `app/st_wrk` | `wrk.cpp`、`http_parser.c/.h`、`process.h`、`process_test.cpp`、`stats.h`、`utils.h`、`makefile` | HTTP 压测工具（wrk 风格） |

另外 `app/` 根下还有**不属于任何 app 的框架代码**：

| 文件 | 性质 |
| --- | --- |
| `app/st_c.cc` / `app/st_c.h` | **对外 C 风格 API**（`udp_sendrecv` / `tcp_sendrecv` / `st_set_hook_flag` / `st_set_private` / `st_get_private`） |
| `app/st_sys.cc` / `app/st_sys.h` | **syscall hook 层**（17 个 `__*` 函数，用 `dlsym`） |
| `app/thread.h` | `StThread` 的重复定义（02 阶段步骤 5 处理） |

这三组文件放在 `app/` 下是**目录语义错误**——它们是库的一部分，不是示例应用。这正是 01 阶段的遗留决策（是否移入 `src/`），也是 03 阶段 `libmthread` 产物定义的依据。

### 2.2 三个 app 的 makefile 都指向不存在的路径

三者结构几乎相同：

```makefile
INCLUDE = -I../../include -I.      # ← include/ 目录不存在（01 阶段 2.2）
SRC     = .
LIBS    = ../../libmthread.a       # ← 期望产物在仓库根（03 阶段步骤 5 已对齐）
all:
	g++ $(SRC)/dns.cpp $(SRC)/main.cpp $(INCLUDE) $(FLAGS) $(LIBS) -o main
clean:
	@rm -rf main *.dSYM
```

各自的 `FLAGS` 差异（**注意 `-DTRACE` 的不一致**）：

| app | FLAGS 要点 |
| --- | --- |
| `st_dns` | `-g -DTRACE`（TRACE **开**） |
| `st_memcacheclient` | `-g -DTRACE` + `-Wno-unused-value`（TRACE **开**） |
| `st_wrk` | `-g` + `-Wno-unused-value -Wno-c++11-compat-deprecated-writable-strings`（TRACE **关**，被注释掉） |

`st_wrk` 的 `-Wno-c++11-compat-deprecated-writable-strings` 是 **clang 专属**告警选项，g++ 不认识（会报 unknown warning option，虽然通常只是告警）。说明这个 makefile 是在 macOS/clang 上写的 → 佐证 R4（双平台验证缺失）。

三者都缺 `.PHONY`，且 `all` 的目标名与产物名不一致（`st_dns` 产出 `main`，但 `clean` 也删 `main`——一致；`st_wrk` 产出 `wrk`，但 `clean` 删的是 `main` → **`st_wrk` 的 clean 是坏的**，删不掉 `wrk`。这也解释了为什么 `app/st_wrk/wrk` 这个二进制被提交进了仓库）。

### 2.3 `tests/Makefile` 严重失配（实测）

`tests/Makefile` 有 12 个 target。逐个核对它引用的源文件是否存在：

| target | 引用的 `$(SRC)/` 文件（`SRC = ../src`） | 存在？ |
| --- | --- | --- |
| `stlib_hash_list` | （只用 `st_hash_list_test.cc`） | 该测试文件在 `stlib/tests/`，不在 `tests/` → **本目录无此文件** |
| `heap` | `st_heap_test.cpp` | **不存在**（真实文件是 `stlib/tests/st_heap_test.cc`） |
| `heap_timer` | `st_heap_timer_unittest.cpp` | **不存在** |
| `buffer` | `st_buffer_test.cpp` | **不存在** |
| `item` | `st_item_unittest.cpp` | **不存在** |
| `connection` | `src/st_connection.cpp`、`st_thread.cpp`、`st_sys.cpp`、`st_manager.cpp` | **全部不存在**（真实是 `.cc`，且无 `st_manager`） |
| `thread` | `src/st_thread.cpp` | **不存在**（真实 `.cc`） |
| `c` | `src/st_connection.cpp`、`st_thread.cpp`、`st_sys.cpp`、`st_manager.cpp`、`st_c.cpp` | **全部不存在** |
| `session` | `src/st_thread.cpp`、`st_manager.cpp` | **全部不存在** |
| `manager` | `src/st_thread.cpp`、`st_sys.cpp`、`st_manager.cpp` | **全部不存在**，且该 target 有语法 bug（见下） |
| `server` | `src/st_connection.cpp`、`st_thread.cpp`、`st_sys.cpp`、`st_manager.cpp` | **全部不存在** |
| `ucontext` | `src/st_asm.S`、`st_ucontext.cpp`、`st_log.cpp`、`st_test.cpp` | **全部不存在**（真实在 `stlib/ucontext/asm.S`、`stlib/ucontext/ucontext.c`、`stlib/st_log.cc`、`stlib/st_test.cc`） |
| `queue` | `queue_unittest.cpp` | **不存在** |
| `singleton` | `st_singleton_unittest.cpp` | 存在 |

即：**12 个 target 里只有 `singleton` 的源文件是齐的。**

`manager` target 还有一处 makefile 语法 bug：

```makefile
		$(INCLUDE) $(COMM_LIB) \ 
		$(FLAGS) -o st_manager_unittest
```

第一行续行符 `\` 后面有一个**尾随空格**，导致 `\` 不被识别为续行符 → 这一行在 shell 里断开，makefile 行为错乱。

另外 `COMM_LIB = $(COMM_DIR)/stlib/libst.a` 指向 `stlib` 产物（01 阶段会产出），但 `tests` 同时也需要 `src/` 的实现 —— 现在的写法是把 `src/*.cpp` 直接加入编译命令（而不是链接 `libmthread.a`）。**这与「只需链接一个 libmthread」的设计承诺不符**，应改为链接 `libmthread.a`。

还有 `COMM_SRC` 这个变量在多个 target 里被引用，但**从未定义**（展开为空）。

`FLAGS` 含 `-lpthread -ltcmalloc -lprofiler -ldl` + `-fsanitize=address`：
- `-ltcmalloc -lprofiler` 依赖 gperftools（D2），违反约束 5，应改为可选开关
- ASan 与 tcmalloc **互不兼容**（都替换 malloc），同时开启会失败
- ASan 与协程自定义栈切换配合差（01 阶段 P5）

### 2.4 `tests/` 下的真实文件

```
tests/Makefile
tests/server_unittest.cpp          tests/st_base_unittest.cpp
tests/st_connection_unittest.cpp   tests/st_c_unittest.cpp
tests/st_main_unittest.cpp         tests/st_manager_unittest.cpp
tests/st_server_unittest.cpp       tests/st_session_unittest.cpp
tests/st_singleton_unittest.cpp    tests/st_thread_unittest.cpp
tests/st_ucontext_unittest.cpp     tests/uthread_unittest.cpp
tests/uthread.cpp                  tests/uthread.h
tests/scripts/keepalive_unittest.py
tests/scripts/udpsvr_unittest.py
tests/st_server_unittest           ← 已提交的二进制（01 阶段步骤 10 清理）
```

注意 `tests/st_manager_unittest.cpp` 存在，印证 02 阶段 L5：`Manager` / `st_manager` 曾是真实存在的模块，现已被 `StSysSchedule` / `st_sys` 取代。**这个测试文件的内容是恢复 `Manager` 语义的重要线索。**

`tests/` 里的源文件 include 的是 `../include/st_*.h` 一整组（01 阶段 2.5）：

```
../include/st_base.h        ../include/st_c.h         ../include/st_connection.h
../include/st_manager.h     ../include/st_netaddr.h   ../include/st_server.h
../include/st_singleton.h   ../include/st_sys.h       ../include/st_thread.h
../include/st_util.h        ../include/ucontext/st_ucontext.h
```

其中 `st_base.h` 在全仓库**完全不存在**（其它都能对应到 `src/` 或 `stlib/` 下的真实文件）。这是又一个待查项。

`tests/uthread.h` / `uthread.cpp` / `uthread_unittest.cpp` 与 `stlib/ucontext/uthread.h` / `uthread.c` 同名——需确认是同一份代码的两个拷贝还是不同东西（关系到 02 阶段 L1 的 `context_switch` 查找）。

### 2.5 测试框架：`stlib/st_test.h`

仓库自带轻量测试框架（`stlib/st_test.h` 179 行 + `st_test.cc` 82 行），不依赖第三方（符合约束 5）。

**01 阶段步骤 4 已标注需先确认的事**：它的通过/失败判定方式与进程退出码。这直接决定 CI 能否自动判定测试结果。若它不设置非零退出码，则需要在 04 阶段解决（否则「测试通过」只能靠人眼看输出）。

### 2.6 Python 脚本测试

- `tests/scripts/keepalive_unittest.py` —— keepalive 相关。**注意 03 阶段 L4**：keepalive 功能当前完全失效（`eTCP_KEEPLIVE_CONN` 值为 0 + `Keeplive()` 硬编码 false），所以这个脚本现在不可能通过
- `tests/scripts/udpsvr_unittest.py` —— UDP server 侧

两者的 Python 版本（py2 vs py3）需确认——仓库年代较早，可能是 py2 语法。

### 2.7 `app/st_wrk` 的特殊情况

`app/st_wrk/http_parser.c` / `.h` 是 vendored 的 nodejs http-parser。这不违反约束 5（源码内联，非运行时依赖），但：

- 它是 C 文件，需用 C 编译器或 `extern "C"` 处理
- 许可证义务真实存在（http-parser 是 MIT）→ 关联决策点 D6
- `app/st_wrk/process_test.cpp` 与 `process.h` 看起来是独立的小测试，未被 makefile 引用

`app/st_wrk/wrk` 二进制已被提交（01 阶段步骤 10 清理）。

### 2.8 `stlib/tiny/` 未被使用

`stlib/tiny/tiny_http_parse.h` 与 `tiny_util.h` **没有被任何 makefile 或源文件引用**（实测 include 清单里有 `tiny_util.h`，需定位是谁引用的——可能是 `tiny_http_parse.h` 内部引用）。关联决策点 D6。

## 3. 问题与风险

| # | 问题 | 影响 | 缓解 |
| --- | --- | --- | --- |
| P1 | `tests/Makefile` 12 个 target 里 11 个源文件缺失 | 测试完全无法运行 | 按真实文件重写；改为链接 `libmthread.a` |
| P2 | `tests/` 全部 include `../include/*`，该目录不存在 | 编译失败 | 按 01 阶段 include 规范统一重写 |
| P3 | `../include/st_base.h` 全仓库不存在 | 不知道该指向什么 | **需查清**（git 历史 / 上报） |
| P4 | keepalive 测试注定失败（03 阶段 L4） | 回归清单有一项永红 | 按 L4 决策：修则测，不修则标记为 known-failure 并说明 |
| P5 | ASan + tcmalloc 互斥，且 ASan 与协程栈冲突 | 测试环境不可用 | 两者都改为默认关闭的开关 |
| P6 | `st_wrk` 的 clean 删错文件名 | 产物残留被提交 | 修 makefile |
| P7 | `-DTRACE` 三个 app 不一致 | 输出量差异大 | **保持各自现状**（不改行为），仅补开关 |
| P8 | `-Wno-c++11-compat-deprecated-writable-strings` 是 clang 专属 | g++ 上告警 | 用条件判断按编译器加选项 |
| P9 | `st_test.h` 的退出码行为未知 | CI 无法自动判定 | 步骤 1 先确认 |
| P10 | Python 脚本可能是 py2 | 现代环境跑不了 | 确认后决定是否移植（移植属改动，需说明） |
| P11 | DNS 测试依赖外网 | CI 不稳定 | 提供本地 DNS mock 或标记为需网络的可选测试 |
| P12 | `tests/uthread.*` 与 `stlib/ucontext/uthread.*` 重复 | 混淆 | 确认关系后取一 |

## 4. 做 / 不做

### 做

- 重写 `tests/Makefile`（按真实文件 + 链接 `libmthread.a`）
- 按 01 规范统一 `tests/` 与 `app/` 的 include 路径
- 修三个 app 的 makefile（`INCLUDE`、`.PHONY`、`st_wrk` 的 clean）
- 让三个 app 编译通过并各跑一次
- 让 `tests/` 全部 unittest 编译通过并运行
- 确认 `st_test.h` 的退出码语义，必要时让 CI 能自动判定
- 建立并执行兼容回归清单（第 5 节步骤 6）
- 高并发冒烟测试
- 确认 `../include/st_base.h`、`tests/uthread.*`、`stlib/tiny/` 的去向
- ASan / tcmalloc / profiler 改为默认关闭开关（与 01 阶段的开关体系统一）

### 不做

- **不**改任何 app 的业务逻辑（DNS 报文构造、memcache 协议、wrk 统计算法）
- **不**改 `app/st_wrk/http_parser.c`（vendored 上游代码，改了就无法跟上游同步）
- **不**改 `mt_array.cpp/.h` 的 `mt_` 前缀（属命名统一的延伸，但它是 app 内部私有类型，不在 D3 范围；改名无收益）
- **不**改 `-DTRACE` 的各 app 默认值（P7）
- **不**给 app 增加新功能 / 新命令行参数
- **不**引入第三方测试框架（gtest / catch2）——违反约束 5，且 `st_test.h` 已够用
- **不**改 Python 脚本的测试逻辑（仅在必要时做 py2→py3 语法移植，且需说明）
- **不**删 `stlib/tiny/`、`app/st_wrk/process_test.cpp`（待 D6）
- **不**修 03 阶段 L4 的 keepalive bug（除非用户批准）

## 5. 分步步骤

### 步骤 1：前置确认（先查清，再动手）

四个必须先有答案的问题：

| # | 问题 | 查法 |
| --- | --- | --- |
| Q1 | `st_test.h` 的失败如何体现？退出码是否非零？ | 读 `stlib/st_test.h` + `st_test.cc`；写一个必然失败的断言实测 `echo $?` |
| Q2 | `../include/st_base.h` 原本是什么？ | `git log --all -- '*st_base.h'`、`git log --all -S 'st_base.h'` |
| Q3 | `tests/uthread.*` 与 `stlib/ucontext/uthread.*` 是否同一份？ | `diff tests/uthread.h stlib/ucontext/uthread.h` |
| Q4 | Python 脚本是 py2 还是 py3？ | 读文件头 / `print` 语法 |

Q1 最关键：它决定后续所有「测试通过」的验收项能否自动化。若答案是「不设退出码」，则需要在本阶段补上（属对测试框架的改动，需在 PR 说明）。

**出口**：Q1～Q4 有答案。

### 步骤 2：重写 `tests/Makefile`

原则：

1. **链接 `libmthread.a`**，不再把 `src/*.cpp` 塞进编译命令（符合「只需一个库」的设计承诺）
2. include 只用 `-I..`（仓库根，按 01 规范）
3. 删除对不存在文件的引用
4. `stlib` 相关的测试（`heap`/`heap_timer`/`buffer`/`stlib_hash_list`）本就在 `stlib/tests/` 下，**从 `tests/Makefile` 移除**，避免两处重复
5. 每个 target 对应一个真实存在的 `tests/*_unittest.cpp`
6. 补 `.PHONY`，补一个 `all` 聚合 target，补 `run` target 依次执行
7. `-ltcmalloc -lprofiler -fsanitize=address` 改为默认关闭开关

目标 target 集合（按真实文件）：

| target | 源文件 | 备注 |
| --- | --- | --- |
| `base` | `st_base_unittest.cpp` | 依赖 Q2 的答案 |
| `singleton` | `st_singleton_unittest.cpp` | 当前唯一齐备的 |
| `thread` | `st_thread_unittest.cpp` | 02 阶段 T1～T8 的载体 |
| `ucontext` | `st_ucontext_unittest.cpp` | 02 阶段 L1 相关 |
| `uthread` | `uthread_unittest.cpp` + `uthread.cpp` | 依赖 Q3 |
| `connection` | `st_connection_unittest.cpp` | 03 阶段 N1～N7 的载体 |
| `server` | `st_server_unittest.cpp` | 03 阶段 N8 |
| `server2` | `server_unittest.cpp` | 与上一个的关系待确认 |
| `session` | `st_session_unittest.cpp` | 与 keepalive/UDP session 相关（L4） |
| `manager` | `st_manager_unittest.cpp` | 02 阶段 L5 的线索来源 |
| `c` | `st_c_unittest.cpp` | 对外 C API（`udp_sendrecv` 等）的测试 |
| `main` | `st_main_unittest.cpp` | 待确认内容 |

**出口**：`make -C tests all` 能走完全部编译（不要求全部通过运行）。

### 步骤 3：统一 `tests/` 与 `app/` 的 include 路径

用 01 阶段步骤 2 的 MISSING 脚本，这次覆盖**全仓库**：

```bash
grep -rhno '#include "[^"]*"' src app stlib tests \
     --include=*.h --include=*.cc --include=*.cpp --include=*.c \
  | sed 's/.*#include "//;s/"//' | sort -u \
  | while read p; do
      [ -n "$(find . -path ./thirdparty -prune -o -path "*$p" -print | head -1)" ] \
        || echo "MISSING: $p"
    done
```

映射表（`tests/` 部分）：

| 原 include | 改为 |
| --- | --- |
| `../include/st_thread.h` | `src/st_thread.h` |
| `../include/st_connection.h` | `src/st_connection.h` |
| `../include/st_server.h` | `src/st_server.h` |
| `../include/st_sys.h` | `src/st_sys.h` |
| `../include/st_manager.h` | `src/st_sys.h`（待 02 阶段 L5 确认） |
| `../include/st_netaddr.h` | `stlib/st_netaddr.h` |
| `../include/st_singleton.h` | `stlib/st_singleton.h` |
| `../include/st_util.h` | `stlib/st_util.h` |
| `../include/st_c.h` | `app/st_c.h`（或移位后的新路径） |
| `../include/ucontext/st_ucontext.h` | `stlib/ucontext/ucontext.h` |
| `../include/st_base.h` | **待 Q2 确认** |

`app/` 部分：`app/st_c.h` 的 `st_connection.h` / `st_manager.h` / `st_thread.h` / `st_util.h` → 加正确前缀（03 阶段步骤 4 已部分处理）。

**出口**：MISSING 脚本全仓库输出为空。

### 步骤 4：修三个 app 的 makefile 并编译

统一改动：

1. `INCLUDE = -I../.. -I.`（仓库根 + 自身目录）
2. `LIBS = ../../libmthread.a`（保持不变，03 阶段已确认产物在根目录）
3. 补 `.PHONY: all clean`
4. `st_wrk` 的 `clean` 从 `rm -rf main *.dSYM` 改为 `rm -rf wrk *.dSYM`（P6）
5. `st_wrk` 的 clang 专属告警选项按编译器条件添加（P8）：
   ```makefile
   ifeq ($(shell $(CC) --version 2>&1 | grep -c clang),1)
       FLAGS += -Wno-c++11-compat-deprecated-writable-strings
   endif
   ```
6. 加 `-std=c++98`（约束 2）。**注意 `http_parser.c` 是 C 文件**，不能用 `-std=c++98` 编；需分开编译或用 `-x c`
7. `-DTRACE` 保持各 app 现状（P7），仅确保根 makefile 的 `TRACE=0` 开关能覆盖

逐个编译：

```bash
make -C app/st_dns && make -C app/st_memcacheclient && make -C app/st_wrk
```

**出口**：三个 app 编译通过（双平台）。

### 步骤 5：运行三个 app

| app | 运行方式 | 成功判据 |
| --- | --- | --- |
| `st_dns` | `./app/st_dns/main` | 完成至少一次 DNS 查询并输出解析出的 IP。**注意**：readme 示例里查的是 `www.2000.com` ～ `www.2004.com` 这类不存在的域名，实际会失败——需换成真实域名或本地 mock（P11） |
| `st_memcacheclient` | `./app/st_memcacheclient/main` | 需要一个 memcached 实例。若环境没有，起一个本地 memcached 或用 `memcache.pcap` 做协议对照（该 pcap 就是为此保留的） |
| `st_wrk` | `./app/st_wrk/wrk <url>` | 对本地 HTTP server 压测，输出 QPS/延迟统计。可用 `st_server_unittest` 或 Python 起的简易 server 做被压测端 |

`st_dns` 的 P11（依赖外网）处理建议：
- CI 里标记为「需要网络」的可选测试
- 或用 `tests/scripts/udpsvr_unittest.py` 的思路起一个本地假 DNS server

**出口**：三个 app 各有一次成功运行记录（含输出）。

### 步骤 6：兼容回归清单（**本阶段的核心产出**）

这是「证明 01～03 没改坏东西」的唯一凭据。逐项列出前三阶段触碰的语义点，以及验证方式：

#### 6.1 数值与常量未变（静态核验）

| 项 | 验证方式 |
| --- | --- |
| `eThreadType` / `eThreadState` / `eThreadFlag` 数值 | `git diff <base> -- src/st_public.h` 确认枚举块无改动 |
| `eConnType` 数值 | 同上。**若 L4 被批准修复则此项预期变化**，需单独 release note |
| `eERR_*` 错误码（0 ～ -13） | 同上 |
| `MEM_PAGE_SIZE`(2048)、`THREAD_DAEMON_NAME`、`THREAD_PRIMO_NAME` | 同上 |
| `ST_OK/ERROR/UNKOWN/BUSY/DONE/DECLINED/ABORT` | `git diff <base> -- stlib/st_def.h` |
| `ST_NONE/READABLE/WRITEABLE/EVERR` (0/1/2/4) | 同上 |
| `ST_RECV_BUFFSIZE` / `ST_SEND_BUFFSIZE`（8192） | 同上 |
| `ST_MAX_FD` / `ST_LISTEN_LEN` / `ST_MAXINT` / `ST_MAXTIME` | 同上 |
| 超时默认值：`StEventSchedule` 30000、`StConnection` 30000、`StSysSchedule` 1000 | grep 确认 |
| `STACK`（默认栈大小） | **02 阶段 L2**：必须是找回的原值，不能是新取的值 |

#### 6.2 对外 API 签名未变（静态核验）

| API | 来源 |
| --- | --- |
| `udp_sendrecv(struct sockaddr_in*, void*, int, void*, int&, int)` | `app/st_c.h` |
| `tcp_sendrecv(..., CheckLengthCallback, bool keeplive = false)` | `app/st_c.h` |
| `st_set_private(void*)` / `st_get_private()` / `st_set_hook_flag()` | `app/st_c.h` |
| `StConnection` 的虚函数：`DoOutput` / `DoInput` / `DoProcess` / `DoError` / `Create` / `Reset` | `src/st_connection.h` |
| `StEventItem` 的虚函数：`EvInput` / `EvOutput` / `EvHangup` / `Reset` | `src/st_poll.h` |
| `StThreadItem` 的虚函数：`Run` / `RestoreContext` / `Add` / `Reset` | `src/st_poll.h` |
| `StIOState` 的 8 个方法（两个后端必须完全一致） | `stlib/st_epoll.h` / `st_kqueue.h` |
| `StHeapTimer::Startup` / `Stop` / `CheckExpired` | `stlib/st_heap_timer.h` |
| `Instance<T>()` 的线程局部语义 | `stlib/st_singleton.h` |

核验手段：对库产物做符号对照

```bash
nm -g --defined-only libmthread.a | sort > /tmp/syms_new.txt
# 与基线对比（若有基线）；否则至少确认上表符号全部存在且 demangle 后签名一致
```

**注意**：若 D3 批准把 `StConnection` / `StServer` 移入 `namespace sthread`，则它们的 mangled name **会变**——这是已授权的 ABI 变化，需在此处明确标注为「预期变化」。

#### 6.3 行为回归（动态验证）

| # | 语义点 | 来源阶段 | 验证 |
| --- | --- | --- | --- |
| B1 | 协程创建 / Yield / Sleep / Wakeup / 父子唤醒 | 02 T1～T8 | 跑 02 阶段测试 |
| B2 | sleep 堆按唤醒时刻排序 | 02 T3 | 多协程不同 sleep 时长，验证唤醒顺序 |
| B3 | 定时器到期与取消 | 02 | `stlib/tests/st_heap_timer_test` |
| B4 | `Instance<T>()` 每 OS 线程一份 | 02 | 两个 pthread 各取 `Instance<X>()`，断言指针不同 |
| B5 | epoll mask 合并 / 去重 / MOD / DEL | 03 步骤 1 | `StIOState` 单测 |
| B6 | kqueue 与 epoll 行为一致 | 03 N10 | 同一套用例双平台 |
| B7 | UDP 收发与超时 | 03 N1/N2 | `st_c_unittest` |
| B8 | TCP 短连接闭环 | 03 N3 | `st_connection_unittest` |
| B9 | TCP connect 超时 / 对端关闭 | 03 N4/N5 | 同上 |
| B10 | **大包分片（> 8192）触发内部 yield** | 03 N6 | **核心价值主张的验证** |
| B11 | 并发客户端无串包 | 03 N7 | 多协程并发请求 |
| B12 | `StServer` accept 闭环 | 03 N8 | `st_server_unittest` |
| B13 | fd 无泄漏 | 03 N9 | 反复 1000 次连接后计数稳定 |
| B14 | keepalive 连接复用 | 03 L4 | **当前必然失败**；按 L4 结论标记 known-failure 或验证修复 |
| B15 | DNS 客户端端到端 | 04 步骤 5 | `app/st_dns` 真实查询 |
| B16 | HTTP server 端到端 | 04 步骤 5 | `st_wrk` 压 `st_server_unittest` |
| B17 | memcache 客户端 | 04 步骤 5 | 对本地 memcached |
| B18 | 高并发协程 | 04 步骤 7 | 见下 |
| B19 | 零第三方运行时依赖 | 03 步骤 5 | `ldd` / `otool -L` |

#### 6.4 构建回归

| 项 | 验证 |
| --- | --- |
| 根 `make` 产出 `libmthread.a` + `.so` | 01/03 |
| `make clean` 后 `git status --porcelain` 为空 | 01 步骤 10 |
| `-std=c++98` 零 error（全仓库） | 约束 2 |
| Linux + macOS 双平台 | 约束 3 |
| `clang-format --dry-run --Werror` 通过 | 01 步骤 9 |

**出口**：清单逐项有结论（通过 / known-failure + 原因 / 预期变化 + 说明）。

### 步骤 7：高并发冒烟

验证 readme 的核心卖点：「理论上只要系统内存足够大，句柄没有限制，可以无限创建无限个协程」。

测试设计：

| 项 | 内容 |
| --- | --- |
| 规模 | 从 1000 起，倍增到 10000、50000（视内存） |
| 每协程内存 | 由 `STACK` 与 `MEM_PAGE_SIZE` 决定：`MEM_PAGE_SIZE * 2 + (STACK / MEM_PAGE_SIZE + 1) * MEM_PAGE_SIZE`。**需实测单协程实际占用**并记录——这是「能开多少协程」的量化依据 |
| fd 限制 | `StEventSchedule::Init` 会 `setrlimit` 到 65535。验证它是否真的生效（非 root 下 `rlim_max` 可能无法提升） |
| 判据 | 不崩溃；每个协程的回调都执行到；无 fd 泄漏；内存增长符合上面公式的预期 |

同时记录两个数字供 05 阶段 readme 使用：
- 单协程内存占用（字节）
- 单进程可创建协程数上限（在给定内存/fd 限制下）

**出口**：有实测数据；readme 的卖点从「理论上」变成「实测 N 个协程占用 M 内存」。

### 步骤 8：处理遗留文件

按步骤 1 的 Q2～Q4 与决策点 D6 的结论：

| 对象 | 处理 |
| --- | --- |
| `tests/uthread.*` vs `stlib/ucontext/uthread.*` | 若相同则删一份（保留 `stlib/` 的）；若不同则加注释说明区别 |
| `stlib/tiny/` | 按 D6：若是废弃残留则删，若在开发中则加 README 说明 |
| `app/st_wrk/process_test.cpp` / `process.h` | 未被 makefile 引用；确认是否要补 target 或删除 |
| `app/st_memcacheclient/memcache.pcap` | **保留**（协议对照素材），在 `app/st_memcacheclient/` 下加 README 说明用途 |
| `stlib/tests/ucontext/` | 按 D6：标注为「上游参考样例，不参与构建」 |
| `app/st_dns/test.log` | 01 阶段已 `git rm --cached` |

**出口**：无来历不明的文件。

## 6. 兼容策略

| 对象 | 策略 |
| --- | --- |
| app 业务逻辑 | **一律不动**。app 是「使用方」，它们的代码是对外 API 兼容性的活证据——如果 app 要改才能编译，说明 API 被改坏了 |
| app 的命令行接口 | 不变（`./main`、`./wrk <url>`） |
| `http_parser.c` | 不动（vendored 上游） |
| `tests/*_unittest.cpp` 的测试逻辑 | **原则上不动**，只改 include 路径与旧名。若某个测试因 API 改名而无法编译，优先反思是不是 API 改错了 |
| `tests/Makefile` 的 target 名 | 尽量保留原名（`thread`、`connection`、`server`、`c`、`session`、`manager`、`ucontext`、`singleton`），便于老用户沿用习惯。readme 里写的 `make event` 无对应 target → 05 阶段修 readme |
| `-DTRACE` 各 app 默认值 | 保持现状 |
| Python 脚本 | 若必须 py2→py3 移植，只改语法不改逻辑，并在 PR 说明 |
| `st_test.h` | 若 Q1 发现需补退出码，属对测试框架的改动 → 在 PR 明确说明（它影响所有测试的判定方式） |

**一条重要原则**：本阶段若发现「必须修改 app 或 test 的业务代码才能编译通过」，这是 **01～03 改坏了 API 的信号**，应回到上游阶段修正，而不是在这里改 app。唯一例外是 include 路径与 D3/D4 已授权的改名。

## 7. 验收

- [ ] Q1～Q4 四个前置问题有答案
- [ ] MISSING 脚本在**全仓库**范围输出为空
- [ ] `tests/Makefile` 的每个 target 引用的源文件都真实存在
- [ ] `tests/Makefile` 的 `manager` target 续行符 bug 已修
- [ ] `tests/` 改为链接 `libmthread.a`，不再直接编译 `src/*.cc`
- [ ] `make -C tests` 全部 target 编译通过（双平台）
- [ ] `tests/` 全部 unittest 运行通过（keepalive 项按 L4 结论）
- [ ] 三个 app 编译通过（双平台）
- [ ] `app/st_dns` 完成一次真实 DNS 查询并输出正确 IP
- [ ] `app/st_memcacheclient` 对本地 memcached 完成一次 set/get
- [ ] `app/st_wrk` 成功压测本地 HTTP server 并输出统计
- [ ] `st_wrk` 的 `clean` 能真正删除 `wrk`
- [ ] `tests/scripts/*.py` 可执行（或已确认 py 版本问题并处理）
- [ ] 回归清单 6.1（数值常量）全部无变化，或变化已被明确授权并记录
- [ ] 回归清单 6.2（对外 API 签名）全部一致，namespace 变更已授权并标注
- [ ] 回归清单 6.3（B1～B19）逐项有结论
- [ ] 回归清单 6.4（构建）全绿
- [ ] 高并发冒烟：≥ 10000 协程不崩溃，且单协程内存占用与上限已实测记录
- [ ] ASan / tcmalloc / profiler 默认关闭
- [ ] 步骤 8 的遗留文件全部有处理结论

## 8. 依赖与工作量

**前置依赖**：
- **01 阶段**（include 规范、根 makefile、开关体系、CI）
- **02 阶段**（协程可用，否则所有测试都跑不起来）
- **03 阶段**（`libmthread.a` 产出，否则 app 与 test 无法链接）—— **这是最硬的依赖**
- **决策点 D2**（gperftools → 决定 `-ltcmalloc` 怎么处理）、**D6**（`stlib/tiny/`、`stlib/tests/ucontext/` 去向）
- **03 阶段 L4**（keepalive）—— 决定 B14 与 `keepalive_unittest.py` 是测还是标红

**被依赖**：05 阶段。readme 的每个示例都必须来自本阶段真实跑通过的代码，高并发数据也来自本阶段步骤 7。

**改动面**：

| 子系统 | 文件 | 侵入程度 |
| --- | --- | --- |
| `tests/Makefile` | 1 | **高**：基本重写（12 个 target 全部失配） |
| `tests/*_unittest.cpp` | 12 | 中：仅 include 路径 + D3/D4 改名波及；**不动测试逻辑** |
| `tests/uthread.*` | 2 | 低：视 Q3 结论删除或加注释 |
| `tests/scripts/*.py` | 2 | 低～中：视 Q4 结论 |
| `app/st_dns/makefile` | 1 | 低 |
| `app/st_memcacheclient/makefile` | 1 | 低 |
| `app/st_wrk/makefile` | 1 | 中：clean bug + 编译器条件选项 + C/C++ 混编 |
| `app/st_c.h` / `app/st_c.cc` | 2 | 低：03 阶段已处理大部分 |
| `app/*/` 业务源码 | 0 | **不动** |
| 新增：高并发冒烟测试 | 1 | 中 |
| 新增：回归清单文档 | 1 | 低（但内容量大） |

**风险集中点**：

1. **本阶段是「真相时刻」**。前三阶段都是静态修复（编译通过），本阶段第一次真正运行。大概率会暴露前三阶段未发现的运行时问题（尤其是 02 阶段 R5 的上下文切换、03 阶段的事件派发时序）。**应预留返工到 02/03 的可能**。
2. **`st_wrk` 的 C/C++ 混编 + `-std=c++98`**：`http_parser.c` 必须用 C 编译。当前 makefile 把 `.c` 和 `.cpp` 一起丢给 `g++`，加了 `-std=c++98` 后 `.c` 文件会被当 C++ 编译，可能报错。需拆分编译步骤。
3. **外部依赖导致的不稳定**：DNS 需外网、memcache 需 memcached 实例、wrk 需被压测端。三者都需要在 CI 里提供或跳过。
4. **B14 (keepalive)** 的结论会显著影响本阶段范围：若 L4 批准修复，则 `StConnectionManager` + `StHashList<StNetAddrKey>` 这条从未运行过的路径会首次被激活，可能藏有大量问题。

## 9. 遗留问题

| # | 问题 | 处理 |
| --- | --- | --- |
| L1 | `../include/st_base.h` 原本对应什么 | 步骤 1 Q2；查不到则**上报用户** |
| L2 | `tests/uthread.*` 与 `stlib/ucontext/uthread.*` 的关系 | 步骤 1 Q3 |
| L3 | `st_test.h` 是否需要补退出码 | 步骤 1 Q1；若需补则在 PR 明确说明 |
| L4 | Python 脚本的版本与是否移植 | 步骤 1 Q4 |
| L5 | `tests/server_unittest.cpp` 与 `tests/st_server_unittest.cpp` 的区别 | 步骤 2 |
| L6 | `tests/st_main_unittest.cpp` 的用途 | 步骤 2 |
| L7 | `app/st_wrk/process_test.cpp` 是否要纳入构建 | 步骤 8 |
| L8 | `st_dns` 示例用的不存在域名（`www.2000.com` 等）该换成什么 | 步骤 5；影响 05 阶段 readme 示例 |
| L9 | 非 root 下 `setrlimit` 提升 fd 上限能否成功 | 步骤 7；影响「无限协程」卖点的表述 |
