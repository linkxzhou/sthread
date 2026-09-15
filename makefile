ST_ROOT = .
include $(ST_ROOT)/make.inc

# 开关一律透传给子目录的 make
export TRACE DEBUG ASAN TCMALLOC PROFILER ARCH

CLANG_FORMAT ?= clang-format

# 参与格式检查的范围：本仓库自己写的代码。
# 排除 stlib/ucontext/（Russ Cox libtask，见 COPYRIGHT）、stlib/tests/ucontext/、
# stlib/tiny/、app/st_wrk/http_parser.*（nodejs http-parser）等 vendor 代码，
# 以及 app/、tests/ 下尚未整理的历史代码（属 plan/04、plan/05 范围）。
FORMAT_SRC = $(wildcard stlib/*.h stlib/*.cc src/*.h src/*.cc stlib/tests/*.cc)

.PHONY: all help stlib lib apps tests stlib-tests test format format-check clean

# plan/01 的出口条件是「stlib 成为可编译、可运行、C++98 干净的绿色基线」，
# 所以当前默认目标是 stlib。src/ 还编不过（旧名未定义、st_manager.h 缺失等，
# lib / apps / tests 由 plan/01～04 打通。
all: stlib
	@echo ""
	@echo "已构建 stlib（stlib/libst.a、stlib/libst.so）。"
	@echo "运行 'make test' 跑 stlib 的四个测试，'make help' 看全部目标与开关。"

help:
	@echo "目标："
	@echo "  make               = make stlib（当前阶段的绿色基线）"
	@echo "  make stlib         构建 stlib/libst.a 与 stlib/libst.so"
	@echo "  make stlib-tests   构建 stlib/tests 的四个测试"
	@echo "  make test          构建并运行 stlib/tests 的四个测试"
	@echo "  make lib           构建 libmthread.a / libmthread.so（仓库根目录）"
	@echo "                     [阻塞于 plan/02、plan/03]"
	@echo "  make apps          构建 app/st_dns、app/st_memcacheclient、app/st_wrk"
	@echo "                     [阻塞于 plan/04]"
	@echo "  make tests         构建 tests/ 下的 unittest [阻塞于 plan/04]"
	@echo "  make format        对本仓库自己的代码跑 clang-format -i"
	@echo "  make format-check  只检查不改写（--dry-run --Werror）"
	@echo "  make clean         清理所有构建产物"
	@echo ""
	@echo "开关（默认值见 make.inc）："
	@echo "  TRACE=0|1     [1] -DTRACE，关掉可消除 LOG_TRACE 的大量输出"
	@echo "  DEBUG=0|1     [1] -g2"
	@echo "  ASAN=0|1      [0] -fsanitize=address"
	@echo "  TCMALLOC=0|1  [0] -ltcmalloc（需自行安装 gperftools）"
	@echo "  PROFILER=0|1  [0] -lprofiler（需自行安装 gperftools）"
	@echo "  ARCH=32|64    [64]"

stlib:
	@$(MAKE) -C stlib

# libmthread 的组成见 src/makefile 顶部注释与 AGENTS.md。
# 注意 libmthread 自带 stlib 的目标文件，不依赖 stlib/libst.a。
lib:
	@$(MAKE) -C src

apps: lib
	@$(MAKE) -C app/st_dns
	@$(MAKE) -C app/st_memcacheclient
	@$(MAKE) -C app/st_wrk

tests: lib
	@$(MAKE) -C tests

stlib-tests:
	@$(MAKE) -C stlib/tests

test:
	@$(MAKE) -C stlib/tests run

format:
	$(CLANG_FORMAT) -i $(FORMAT_SRC)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SRC)

clean:
	@$(MAKE) -C stlib clean
	@$(MAKE) -C stlib/tests clean
	@$(MAKE) -C src clean
	@$(MAKE) -C tests clean
	@$(MAKE) -C app/st_dns clean
	@$(MAKE) -C app/st_memcacheclient clean
	@$(MAKE) -C app/st_wrk clean
	@rm -f libmthread.a libmthread.so
	@rm -f app/st_wrk/wrk
	@rm -rf *.dSYM
