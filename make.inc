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
#   ST_OS / ST_ARCH  默认由 `$(CC) -dumpmachine` 推导，可命令行覆盖。
#   AR             默认 ar。Android NDK 传 llvm-ar。
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

# 归档工具。GNU make 把 AR 预置为 ar；命令行 `make AR=llvm-ar` 可以覆盖。
AR ?= ar

# 目标三元组。交叉编译时 uname 是宿主机，不能用来决定 -m64 / 系统库。
# 例：x86_64-linux-gnu、x86_64-pc-linux-gnu、aarch64-linux-android21、
#     arm64-apple-darwin24。
ST_TARGET := $(shell $(CC) -dumpmachine 2>/dev/null)
ifeq ($(strip $(ST_TARGET)),)
ST_TARGET := $(shell uname -m)-unknown
$(warning $(CC) -dumpmachine failed; falling back to uname -m for ST_ARCH)
endif

ifndef ST_OS
ifneq ($(findstring android,$(ST_TARGET)),)
ST_OS := android
else ifneq ($(findstring linux,$(ST_TARGET)),)
ST_OS := linux
else ifneq ($(findstring darwin,$(ST_TARGET)),)
ST_OS := darwin
else
ST_OS := unknown
endif
endif

ifndef ST_ARCH
ST_ARCH := $(firstword $(subst -, ,$(ST_TARGET)))
endif

# Android：pthread 在 bionic libc 里，不要 -lpthread。
# -static-libstdc++ 在 NDK clang 上静态链接 libc++（plan/10 D6）。
ifeq ($(ST_OS),android)
ST_LDLIBS := -ldl
ST_LDFLAGS += -static-libstdc++
endif

# -m32 / -m64 只对 x86 有意义，并且看目标架构而不是宿主机 uname。
# arm64（Apple Silicon、aarch64、Android arm64）上 clang 会直接
# 报 "unsupported option '-m64'"。
ifeq ($(ARCH),32)
ifneq ($(filter x86_64 amd64 i386 i686,$(ST_ARCH)),)
ST_CXXFLAGS += -march=pentium4 -m32
endif
else
ifneq ($(filter x86_64 amd64 i386 i686,$(ST_ARCH)),)
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
