ST_ROOT = ..
include $(ST_ROOT)/make.inc

LIB = $(ST_ROOT)/libmthread.a
INCLUDE = $(ST_INC)
FLAGS = $(ST_CXXFLAGS) $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean run singleton base thread manager connection session keepalive c main server ucontext

all: singleton base thread manager connection session keepalive main

# Network-ish / heavier targets kept separate
extra: c server

singleton: st_singleton_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_singleton_unittest

base: st_base_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_base_unittest

thread: st_thread_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_thread_unittest

manager: st_manager_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_manager_unittest

connection: st_connection_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_connection_unittest

session: st_session_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_session_unittest

keepalive: st_keepalive_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_keepalive_unittest

c: st_c_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_c_unittest

main: st_main_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_main_unittest

server: st_server_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_server_unittest

ucontext: st_ucontext_unittest.cpp
	$(CC) $< $(INCLUDE) $(FLAGS) $(LIB) -o st_ucontext_unittest

run: all
	@set -e; \
	for t in st_singleton_unittest st_base_unittest st_thread_unittest \
	         st_manager_unittest st_connection_unittest st_session_unittest \
	         st_keepalive_unittest st_main_unittest; do \
	  echo "=== run $$t ==="; \
	  ./$$t; \
	done

clean:
	@rm -rf st_*_unittest uthread_unittest server_unittest *.dSYM *.o
