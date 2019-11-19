CLANG = g++

COMM_DIR = ../..
COMM_LIB = $(COMM_DIR)/stlib/lib/libst.a

INCLUDE = -I$(COMM_DIR)/ -I. -I$(COMM_DIR)/stlib
SRC = ../../src

DEBUG = -g2 -DTRACE
FLAGS = -lpthread -ltcmalloc -lprofiler -ldl \
	-Wno-backslash-newline-escape -Wno-format \
	-Wno-deprecated-declarations $(DEBUG) \
	-fsanitize=address -fno-omit-frame-pointer

hash_list:
	$(CLANG) st_hash_list_test.cpp \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_hash_list_test

heap:
	$(CLANG) st_heap_test.cpp \
		$(COMM_SRC) \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_heap_test

heap_timer:
	$(CLANG) st_heap_timer_unittest.cpp $(COMM_SRC) \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_heap_timer_unittest

buffer:
	$(CLANG) st_buffer_test.cpp $(COMM_SRC) \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_buffer_test

item:
	$(CLANG) st_item_unittest.cpp $(COMM_SRC) \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_item_unittest

connection:
	$(CLANG) st_connection_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_connection.cpp  $(SRC)/st_thread.cpp \
		$(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_connection_unittest

thread:
	$(CLANG) st_thread_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_thread.cpp \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_thread_unittest

c:
	$(CLANG) st_c_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_connection.cpp $(SRC)/st_thread.cpp $(SRC)/st_sys.cpp \
		$(SRC)/st_manager.cpp $(SRC)/st_c.cpp \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_c_unittest

session:
	$(CLANG) st_session_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_thread.cpp  $(SRC)/st_manager.cpp \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_session_unittest

manager:
	$(CLANG) st_manager_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_thread.cpp $(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
		$(INCLUDE) $(COMM_LIB) \ 
		$(FLAGS) -o st_manager_unittest

server:
	$(CLANG) st_server_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_connection.cpp  $(SRC)/st_thread.cpp \
		$(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
		$(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_server_unittest

ucontext:
	$(CLANG) -c $(SRC)/st_asm.S $(FLAGS) -o ./st_asm.o
	$(CLANG) -c $(SRC)/st_ucontext.cpp $(INCLUDE) $(FLAGS) -o ./st_ucontext.o
	$(CLANG) -c $(SRC)/st_log.cpp $(INCLUDE) $(FLAGS) -o ./st_log.o
	ar cr libucontext.a ./st_asm.o ./st_ucontext.o ./st_log.o
	$(CLANG) st_ucontext_unittest.cpp \
		$(SRC)/st_test.cpp $(INCLUDE) $(COMM_LIB) \
		$(FLAGS) -o st_ucontext_unittest
	rm -rf *.o

queue:
	$(CLANG) queue_unittest.cpp $(FLAGS) -o queue_unittest

singleton:
	$(CLANG) st_singleton_unittest.cpp $(COMM_LIB) \
		$(FLAGS) -o st_singleton_unittest

clean:
	rm -rf *_unittest
	rm -rf *.dSYM
	rm -rf *.o