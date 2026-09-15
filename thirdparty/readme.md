# thirdparty

**本仓库不 vendor 任何第三方运行时依赖。**

硬约束是「零第三方运行时依赖」：`libmthread.a` / `libmthread.so` 不得链接
任何第三方库。验证方式：

```bash
ldd libmthread.so          # Linux：只应有 libc / libstdc++ / libpthread / libm / libdl
otool -L libmthread.so     # macOS：只应有系统库
```

## gperftools（tcmalloc / profiler）

历史上 `thirdparty/gperftools` 是一个 gitlink（submodule 指针，
`160000 fe62a0baab87ba3abca12f4a621532bf67c9a7d2`），但仓库里从来没有
`.gitmodules`。后果是：

- `git submodule status` 直接报错
  （`fatal: no submodule mapping found in .gitmodules for path 'thirdparty/gperftools'`）
- 新 clone 只能得到一个空目录

已按决策 D2 **移除该 gitlink**。gperftools 现在是**可选的开发期工具**，
需要时请自行安装：

```bash
# Debian / Ubuntu
sudo apt-get install libgoogle-perftools-dev google-perftools
# macOS
brew install gperftools
```

然后用开关打开（默认全关，见 `make.inc`）：

```bash
make TCMALLOC=1            # 追加 -ltcmalloc
make PROFILER=1            # 追加 -lprofiler
make TCMALLOC=1 PROFILER=1
```

### 用 tcmalloc 做内存分析

```bash
make clean && make TCMALLOC=1
HEAPPROFILE=/tmp/st.hprof ./你的程序
pprof --text ./你的程序 /tmp/st.hprof.0001.heap
```

### 用 profiler 做 CPU 分析

```bash
make clean && make PROFILER=1
CPUPROFILE=/tmp/st.prof ./你的程序
pprof --text ./你的程序 /tmp/st.prof
```

## 其它第三方来源的代码

这些是以源码形式内嵌（vendored）的参考实现，不是运行时依赖：

| 位置 | 来源 | 许可 |
| --- | --- | --- |
| `stlib/ucontext/`（`ucontext.c`、`asm.S`、`ucontext-*.h`、`uthread.*`） | Russ Cox, libtask | MIT，见根目录 [`COPYRIGHT`](../COPYRIGHT) |
| `stlib/tests/ucontext/`（`task.c`、`channel.c`、`primes.c` …） | Russ Cox, libtask 上游示例，**不参与构建** | 同上 |
| `app/st_wrk/http_parser.{c,h}` | nodejs http-parser | MIT |
