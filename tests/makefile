CLANG = g++
INCLUDE = -I../include -I. -I../include/ucontext
SRC = ..
TEST_LIB = ./libucontext.a # ./gtest/googletest/libgtest.a
DEBUG = -g -DTRACE
FLAGS = -lpthread -ltcmalloc -lprofiler -ldl \
	-Wno-backslash-newline-escape -Wno-format \
	-Wno-deprecated-declarations $(DEBUG) \
	-fsanitize=address -fno-omit-frame-pointer
COMM_SRC = $(SRC)/st_test.cpp $(SRC)/st_log.cpp

hash_list:
	$(CLANG) st_hash_list_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_manager.cpp \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_hash_list_unittest

heap:
	$(CLANG) st_heap_unittest.cpp $(COMM_SRC) \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_heap_unittest

heap_timer:
	$(CLANG) st_heap_timer_unittest.cpp $(COMM_SRC) \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_heap_timer_unittest

buffer:
	$(CLANG) st_buffer_unittest.cpp $(COMM_SRC) \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_buffer_unittest

item:
	$(CLANG) st_item_unittest.cpp $(COMM_SRC) \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_item_unittest

connection:
	$(CLANG) st_connection_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_connection.cpp  $(SRC)/st_thread.cpp \
		$(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_connection_unittest

thread:
	$(CLANG) st_thread_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_thread.cpp \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_thread_unittest

c:
	$(CLANG) st_c_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_connection.cpp $(SRC)/st_thread.cpp $(SRC)/st_sys.cpp \
		$(SRC)/st_manager.cpp $(SRC)/st_c.cpp \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_c_unittest

session:
	$(CLANG) st_session_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_thread.cpp  $(SRC)/st_manager.cpp \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_session_unittest

manager:
	$(CLANG) st_manager_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_thread.cpp $(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
		$(INCLUDE) $(TEST_LIB) \ 
		$(FLAGS) -o st_manager_unittest

server:
	$(CLANG) st_server_unittest.cpp $(COMM_SRC) \
		$(SRC)/st_connection.cpp  $(SRC)/st_thread.cpp \
		$(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
		$(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_server_unittest

ucontext:
	$(CLANG) -c $(SRC)/st_asm.S $(FLAGS) -o ./st_asm.o
	$(CLANG) -c $(SRC)/st_ucontext.cpp $(INCLUDE) $(FLAGS) -o ./st_ucontext.o
	$(CLANG) -c $(SRC)/st_log.cpp $(INCLUDE) $(FLAGS) -o ./st_log.o
	ar cr libucontext.a ./st_asm.o ./st_ucontext.o ./st_log.o
	$(CLANG) st_ucontext_unittest.cpp \
		$(SRC)/st_test.cpp $(INCLUDE) $(TEST_LIB) \
		$(FLAGS) -o st_ucontext_unittest
	rm -rf *.o

queue:
	$(CLANG) queue_unittest.cpp $(FLAGS) -o queue_unittest

singleton:
	$(CLANG) st_singleton_unittest.cpp $(TEST_LIB) \
		$(FLAGS) -o st_singleton_unittest

clean:
	rm -rf *_unittest
	rm -rf *.dSYM
	rm -rf *.o