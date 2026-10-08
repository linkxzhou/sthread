ST_ROOT = ..
include $(ST_ROOT)/make.inc

LIB = $(ST_ROOT)/libmthread.a
# D6：测试框架不进 libmthread，unittest 单独编进 st_test.cc
ST_TEST_SRC = $(ST_ROOT)/stlib/st_test.cc
INCLUDE = $(ST_INC)
FLAGS = $(ST_CXXFLAGS) $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean run extra coverage coverage-report \
	singleton base thread manager connection session keepalive c main server ucontext \
	util buffer heap hash scheduler connmgr loopback context accept \
	sysapi connio logbuf harvest httpserver covextra dnsproto httpclient \
	ioedge fdreuse schededge hook serveredge

# Primary suite (used by make -C tests run and coverage)
all: singleton base thread manager connection session keepalive main \
	util buffer heap hash scheduler connmgr loopback context server accept \
	sysapi connio logbuf harvest httpserver covextra dnsproto httpclient \
	ioedge fdreuse schededge hook serveredge

extra: c

singleton: st_singleton_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_singleton_unittest

base: st_base_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_base_unittest

thread: st_thread_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_thread_unittest

manager: st_manager_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_manager_unittest

connection: st_connection_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_connection_unittest

session: st_session_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_session_unittest

keepalive: st_keepalive_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_keepalive_unittest

c: st_c_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_c_unittest

main: st_main_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_main_unittest

server: st_server_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_server_unittest

ucontext: st_ucontext_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_ucontext_unittest

util: st_util_netaddr_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_util_netaddr_unittest

buffer: st_buffer_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_buffer_unittest

heap: st_heap_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_heap_unittest

hash: st_hash_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_hash_unittest

scheduler: st_scheduler_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_scheduler_unittest

connmgr: st_conn_manager_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_conn_manager_unittest

loopback: st_loopback_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_loopback_unittest

context: st_context_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_context_unittest

accept: st_accept_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_accept_unittest

sysapi: st_sys_api_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_sys_api_unittest

connio: st_conn_io_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_conn_io_unittest

logbuf: st_log_buffer_extra_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_log_buffer_extra_unittest

harvest: st_harvest_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_harvest_unittest

httpserver: st_http_server_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_http_server_unittest

covextra: st_coverage_extra_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_coverage_extra_unittest

dnsproto: st_dns_proto_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_dns_proto_unittest

httpclient: st_http_client_unittest.cpp
	$(CC) -c $(ST_ROOT)/app/st_wrk/http_parser.c -I$(ST_ROOT)/app/st_wrk \
		-o http_parser_test.o
	$(CC) $< $(ST_ROOT)/app/st_httpclient/http_client.cc http_parser_test.o \
		$(ST_TEST_SRC) $(INCLUDE) -I$(ST_ROOT)/app/st_httpclient \
		-I$(ST_ROOT)/app/st_wrk $(FLAGS) $(LIB) -o st_http_client_unittest

ioedge: st_io_edge_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_io_edge_unittest

fdreuse: st_fd_reuse_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_fd_reuse_unittest

schededge: st_sched_edge_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_sched_edge_unittest

serveredge: st_server_edge_unittest.cpp
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(LIB) -o st_server_edge_unittest

# dlsym 回退只在动态链接的宿主机上测。Android 交叉编译不编这个 .so。
ifeq ($(ST_OS),android)
HOOK_LIB =
HOOK_PRELOAD =
HOOK_LDFLAGS =
else ifeq ($(ST_OS),darwin)
HOOK_LIB = libst_dlsym_null.dylib
HOOK_PRELOAD = DYLD_INSERT_LIBRARIES=$(CURDIR)/libst_dlsym_null.dylib
# 计数器只在插入的 dylib 里。允许链接期未定义，运行时没有库则为空。
HOOK_LDFLAGS = -Wl,-U,_st_dlsym_null_hits
else
HOOK_LIB = libst_dlsym_null.so
HOOK_PRELOAD = LD_PRELOAD=$(CURDIR)/libst_dlsym_null.so
HOOK_LDFLAGS =
endif

libst_dlsym_null.so: dlsym_null_preload.c
	$(CC) -shared -fPIC $< -ldl -o $@

libst_dlsym_null.dylib: dlsym_null_preload.c
	$(CC) -dynamiclib -fPIC $< -o $@

hook: st_hook_unittest.cpp $(HOOK_LIB)
	$(CC) $< $(ST_TEST_SRC) $(INCLUDE) $(FLAGS) $(HOOK_LDFLAGS) $(LIB) -o st_hook_unittest

RUN_TESTS = \
	st_singleton_unittest st_base_unittest st_thread_unittest \
	st_manager_unittest st_connection_unittest st_session_unittest \
	st_keepalive_unittest st_main_unittest st_server_unittest \
	st_util_netaddr_unittest st_buffer_unittest st_heap_unittest \
	st_hash_unittest st_scheduler_unittest st_conn_manager_unittest \
	st_loopback_unittest st_context_unittest st_accept_unittest \
	st_sys_api_unittest st_conn_io_unittest st_log_buffer_extra_unittest \
	st_harvest_unittest \
	st_http_server_unittest \
	st_coverage_extra_unittest \
	st_dns_proto_unittest \
	st_http_client_unittest \
	st_io_edge_unittest \
	st_fd_reuse_unittest \
	st_sched_edge_unittest \
	st_hook_unittest \
	st_server_edge_unittest

run: all
	@set -e; \
	for t in $(RUN_TESTS); do \
	  echo "=== run $$t ==="; \
	  if [ "$$t" = "st_hook_unittest" ] && [ -n "$(HOOK_PRELOAD)" ]; then \
	    $(HOOK_PRELOAD) ./$$t; \
	  else \
	    ./$$t; \
	  fi; \
	done

# LLVM source coverage. Requires COVERAGE=1 rebuild of lib + tests.
# Default gate: line cover of src/ + stlib/ (exclude vendor ucontext + app demos).
COV_DIR ?= /tmp/sthread_cov
PROFDATA ?= $(COV_DIR)/merged.profdata
LLVM_PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov
COV_IGNORE ?= (stlib/ucontext/|stlib/st_epoll\.h|stlib/st_test\.|/usr/|/opt/|tests/|app/)
COV_THRESHOLD ?= 70  # interim; long-term target remains ~80

coverage: all
	@mkdir -p $(COV_DIR)
	@rm -f $(COV_DIR)/*.profraw $(PROFDATA)
	@failed=0; \
	for t in $(RUN_TESTS); do \
	  echo "=== cov $$t ==="; \
	  if [ "$$t" = "st_hook_unittest" ] && [ -n "$(HOOK_PRELOAD)" ]; then \
	    preload="$(HOOK_PRELOAD)"; \
	  else \
	    preload=""; \
	  fi; \
	  LLVM_PROFILE_FILE="$(COV_DIR)/$${t}.%p%c.profraw" $$preload ./$$t \
	    >$(COV_DIR)/$${t}.out 2>$(COV_DIR)/$${t}.err || { echo "FAIL $$t exit $$?"; failed=1; }; \
	done; \
	if [ $$failed -ne 0 ]; then exit 1; fi
	@$(LLVM_PROFDATA) merge -sparse $(COV_DIR)/*.profraw -o $(PROFDATA)
	@$(MAKE) coverage-report

# Report via per-binary lcov export union (avoids multi-object mismatched under-count).
# Lib .o are kept when COVERAGE=1 (see src/makefile) for optional deep dives.
coverage-report:
	@python3 $(ST_ROOT)/tests/merge_cov.py \
	  --llvm-cov=$(LLVM_COV) \
	  --profdata=$(PROFDATA) \
	  --ignore='$(COV_IGNORE)' \
	  --tests="$(RUN_TESTS)" \
	  --out=$(COV_DIR)/report.txt \
	  --threshold=$(COV_THRESHOLD)

clean:
	@rm -rf st_*_unittest uthread_unittest server_unittest *.dSYM *.o \
		libst_dlsym_null.so libst_dlsym_null.dylib *.profraw *.profdata
