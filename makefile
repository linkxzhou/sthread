ST_ROOT = .
include $(ST_ROOT)/make.inc

# 开关一律透传给子目录的 make
export TRACE DEBUG ASAN TCMALLOC PROFILER ARCH COVERAGE

CLANG_FORMAT ?= clang-format

# 参与格式检查的范围：本仓库自己写的代码。
# 排除 stlib/ucontext/（Russ Cox libtask，见 COPYRIGHT）、stlib/tests/ucontext/、
# app/st_wrk/http_parser.*（nodejs http-parser）等 vendor 代码，
# 以及 app/、tests/ 下尚未整理的历史代码（属 plan/04、plan/05 范围）。
FORMAT_SRC = $(wildcard stlib/*.h stlib/*.cc src/*.h src/*.cc stlib/tests/*.cc) \
	app/st_httpclient/main.cpp app/st_httpclient/http_client.h \
	app/st_httpclient/http_client.cc tests/st_http_client_unittest.cpp

.PHONY: all help stlib lib apps tests stlib-tests test format format-check clean \
	bench-http bench-dns bench bench-curve smoke-httpclient android

# 默认目标保持 stlib；lib / apps / tests 已由 plan/01～08 打通，
# 框架回归与后续改进见 plan/09-main-bugfix-cleanup.md。
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
	@echo "  make apps          构建 app/st_dns、st_memcacheclient、st_wrk、st_httpserver、st_dnsserver、st_httpclient"
	@echo "  make tests         构建 tests/ 下的 unittest"
	@echo "  make bench-http    HTTP 压测闭环（默认 BENCH_PROFILE=smoke，TRACE=0）"
	@echo "  make bench-dns     DNS 压测闭环（本地 st_dnsserver，默认 smoke，TRACE=0）"
	@echo "  make bench         bench-http + bench-dns"
	@echo "  make bench-curve  并发曲线（TRACE=0，st_httpclient，重复取中位数）"
	@echo "  make smoke-httpclient  起 st_httpserver，跑 st_httpclient 冒烟"
	@echo "  make android      NDK 交叉编译 lib/apps/tests（ABI=arm64-v8a|x86_64，API=21，只编译）"
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
	@echo "  ST_OS=...     由 \$$(CC) -dumpmachine 推导（linux/darwin/android），可覆盖"
	@echo "  AR=...        [ar] 归档工具，Android 用 llvm-ar"
	@echo "  ABI=...      [arm64-v8a] 仅 make android：arm64-v8a 或 x86_64"
	@echo "  API=...      [21] 仅 make android：NDK min API"

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
	@$(MAKE) -C app/st_httpserver
	@$(MAKE) -C app/st_dnsserver
	@$(MAKE) -C app/st_httpclient

BENCH_PROFILE ?= smoke

# 压测编 TRACE=0：默认 TRACE=1 会把 LOG_TRACE 打进 stdout，SUMMARY 难 grep 且 QPS 无意义。
bench-http:
	@$(MAKE) apps TRACE=0
	@mkdir -p reports
	TRACE=0 BENCH_PROFILE=$(BENCH_PROFILE) ./scripts/bench_http.sh

bench-dns:
	@$(MAKE) apps TRACE=0
	@mkdir -p reports
	TRACE=0 BENCH_PROFILE=$(BENCH_PROFILE) ./scripts/bench_dns.sh

bench: bench-http bench-dns

# 并发曲线。时间戳结果在 reports/curve-*（gitignore）。冻结图见 docs/perf/ 与 reports/baseline-curve.md。
bench-curve:
	@$(MAKE) apps TRACE=0
	@mkdir -p reports
	TRACE=0 ./scripts/bench_curve.sh

smoke-httpclient:
	@$(MAKE) apps TRACE=0
	TRACE=0 ./scripts/smoke_httpclient.sh

# Android NDK 交叉编译。只编译 lib、apps、tests、stlib-tests，不在宿主机上运行。
# 需要 ANDROID_NDK 或 ANDROID_NDK_HOME。不在解析期 $(error)，普通 make lib 不受影响。
ANDROID_NDK ?= $(ANDROID_NDK_HOME)
API ?= 21
ABI ?= arm64-v8a

android:
	@test -n "$(ANDROID_NDK)" || { echo "请设置 ANDROID_NDK 或 ANDROID_NDK_HOME"; exit 1; }
	@ndk="$(ANDROID_NDK)"; \
	bin=$$(ls -d "$$ndk"/toolchains/llvm/prebuilt/*/bin 2>/dev/null | head -1); \
	test -n "$$bin" || { echo "找不到 NDK llvm 工具链: $$ndk"; exit 1; }; \
	case "$(ABI)" in \
	  arm64-v8a) triple=aarch64-linux-android ;; \
	  x86_64) triple=x86_64-linux-android ;; \
	  *) echo "ABI 只支持 arm64-v8a 和 x86_64（当前: $(ABI)）"; exit 1 ;; \
	esac; \
	cxx="$$bin/$${triple}$(API)-clang++"; \
	ar="$$bin/llvm-ar"; \
	test -x "$$cxx" || { echo "找不到 $$cxx"; exit 1; }; \
	echo "NDK $$ndk"; \
	echo "CC=$$cxx"; \
	echo "AR=$$ar"; \
	"$$cxx" --version | head -1; \
	$(MAKE) clean; \
	$(MAKE) stlib apps tests stlib-tests CC="$$cxx" AR="$$ar"

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
	@$(MAKE) -C app/st_httpserver clean
	@$(MAKE) -C app/st_dnsserver clean
	@$(MAKE) -C app/st_httpclient clean
	@rm -f libmthread.a libmthread.so
	@rm -f app/st_wrk/wrk
	@rm -rf *.dSYM
