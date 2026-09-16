ST_ROOT = ../..
include $(ST_ROOT)/make.inc

STLIB_SRC = $(ST_ROOT)/stlib

# 测试直接编 st_log.cc + st_test.cc（D6：st_test 不进 libst）。
# 不依赖 stlib/libst.a，避免和 ucontext 目标重复链接。
COMMON_SRC = $(STLIB_SRC)/st_log.cc $(STLIB_SRC)/st_test.cc

INCLUDE = $(ST_INC)
FLAGS   = $(ST_CXXFLAGS) $(ST_LDFLAGS) $(ST_LDLIBS)

TEST_BINS = st_buffer_test st_hash_list_test st_heap_test st_heap_timer_test \
	st_netaddr_test st_buffer_pool_test st_demo_usage_test

.PHONY: all run clean \
	stlib_buffer stlib_hash_list stlib_heap stlib_heap_timer

all: $(TEST_BINS)

# 保留历史 target 名，避免破坏既有用法与文档
stlib_buffer: st_buffer_test
stlib_hash_list: st_hash_list_test
stlib_heap: st_heap_test
stlib_heap_timer: st_heap_timer_test

%_test: %_test.cc $(COMMON_SRC)
	$(CC) $< $(COMMON_SRC) $(INCLUDE) $(FLAGS) -o $@

# stlib/st_test.h 的 StTester 析构在断言失败时打印 "[FAILED]" 后调用 exit(0)，
# 也就是说退出码恒为 0，无法单独作为通过/失败依据。这里三条一起判：
#   1) 退出码为 0
#   2) 输出里没有 [FAILED]
#   3) 输出里有 "==== PASSED n tests" 汇总行
# （exit(0) 这个笔误留给 plan/02 处理，改它会动到测试框架语义。）
run: $(TEST_BINS)
	@fail=0; \
	for t in $(TEST_BINS); do \
		printf '==== running %s\n' "$$t"; \
		out=`./$$t 2>&1`; rc=$$?; \
		printf '%s\n' "$$out" | tail -n 1; \
		if [ $$rc -ne 0 ]; then \
			printf '[FAIL] %s 退出码 %s\n' "$$t" "$$rc"; fail=1; fi; \
		if printf '%s\n' "$$out" | grep -q '\[FAILED\]'; then \
			printf '[FAIL] %s 存在断言失败\n' "$$t"; fail=1; fi; \
		if ! printf '%s\n' "$$out" | grep -q '^==== PASSED'; then \
			printf '[FAIL] %s 未输出 PASSED 汇总\n' "$$t"; fail=1; fi; \
	done; \
	if [ $$fail -ne 0 ]; then echo 'STLIB TESTS FAILED'; exit 1; fi; \
	echo 'ALL STLIB TESTS PASSED'

clean:
	@rm -f $(TEST_BINS) *.o
	@rm -rf *.dSYM
