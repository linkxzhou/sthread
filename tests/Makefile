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
	sysapi connio logbuf harvest httpserver covextra

# Primary suite (used by make -C tests run and coverage)
all: singleton base thread manager connection session keepalive main \
	util buffer heap hash scheduler connmgr loopback context server accept \
	sysapi connio logbuf harvest httpserver covextra

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
	st_coverage_extra_unittest

run: all
	@set -e; \
	for t in $(RUN_TESTS); do \
	  echo "=== run $$t ==="; \
	  ./$$t; \
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
	  LLVM_PROFILE_FILE="$(COV_DIR)/$${t}.%p%c.profraw" ./$$t \
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
	@rm -rf st_*_unittest uthread_unittest server_unittest *.dSYM *.o
