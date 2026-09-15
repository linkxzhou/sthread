# sthread 公共编译开关
#
# 被根 makefile 与 stlib/、src/、stlib/tests/ 下的 makefile 共同 include。
# include 之前必须先设置 ST_ROOT 为「从当前 makefile 所在目录到仓库根」的相对路径。
#
# 可用开关（全部通过命令行覆盖，例如 `make ASAN=1 TRACE=0`）：
#
#   TRACE=0|1      默认 1。开启时编译期定义 -DTRACE，LOG_TRACE 会输出大量日志。
#                  历史上各 makefile 一律默认开启 -DTRACE，这里保持不变。
#   DEBUG=0|1      默认 1。开启时带 -g2 调试信息。
#   ASAN=0|1       默认 0。AddressSanitizer 与协程自定义栈切换配合较差
#                  （需要 __sanitizer_start_switch_fiber 之类的标注），故默认关闭。
#   TCMALLOC=0|1   默认 0。链接 -ltcmalloc，需自行安装 gperftools。
#   PROFILER=0|1   默认 0。链接 -lprofiler，需自行安装 gperftools。
#   COVERAGE=0|1   默认 0。LLVM -fprofile-instr-generate/-fcoverage-mapping。
#   ARCH=32|64     默认 64。
#
# 硬约束：C++98。libmthread 不得链接任何第三方库，因此 TCMALLOC / PROFILER
# 只能作为开发期可选开关存在。

ST_ROOT ?= .

CC = g++

TRACE    ?= 1
DEBUG    ?= 1
ASAN     ?= 0
TCMALLOC ?= 0
PROFILER ?= 0
COVERAGE ?= 0
ARCH     ?= 64

# include 根只有仓库根目录一个。跨目录引用一律写从仓库根出发的路径
# （如 #include "stlib/st_def.h"），同目录引用用裸文件名。
ST_INC = -I$(ST_ROOT)

ST_CXXFLAGS = -std=c++98 -fPIC \
	-Wno-backslash-newline-escape \
	-Wno-format \
	-Wno-deprecated-declarations \
	-Wno-null-dereference

ST_LDFLAGS =
ST_LDLIBS  = -lpthread -ldl

ST_UNAME_M := $(shell uname -m)

# -m32 / -m64 只对 x86 有意义。arm64（Apple Silicon、aarch64）上 clang 会直接
# 报 "unsupported option '-m64'"，所以那里不加。历史上各 makefile 无条件写
# -m64，在 x86 上行为不变。
ifeq ($(ARCH),32)
ST_CXXFLAGS += -march=pentium4 -m32
else
ifneq ($(filter x86_64 amd64 i386 i686,$(ST_UNAME_M)),)
ST_CXXFLAGS += -m64
endif
endif

ST_CXXFLAGS += -pthread

ifneq ($(TRACE),0)
ST_CXXFLAGS += -DTRACE
endif

ifneq ($(DEBUG),0)
ST_CXXFLAGS += -g2
endif

ifneq ($(ASAN),0)
ST_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
ST_LDFLAGS  += -fsanitize=address
endif

ifneq ($(TCMALLOC),0)
ST_LDLIBS += -ltcmalloc
endif

ifneq ($(PROFILER),0)
ST_LDLIBS += -lprofiler
endif

# LLVM source-based coverage（clang / Apple clang / Homebrew llvm）
ifneq ($(COVERAGE),0)
ST_CXXFLAGS += -fprofile-instr-generate -fcoverage-mapping -O0
ST_LDFLAGS  += -fprofile-instr-generate
endif
