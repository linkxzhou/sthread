INCLUDE = -I../include -I. -I../include/ucontext
SRC = ..
GTEST_LIB = -lpthread -ldl # ./gtest/googletest/libgtest.a
DEBUG = -g -DTRACE
FLAGS = -Wno-backslash-newline-escape -Wno-format -Wno-deprecated-declarations -O1 $(DEBUG)
COMM_SRC = $(SRC)/st_test.cpp $(SRC)/st_log.cpp $(SRC)/st_item.cpp

hash_list:
	g++ st_hash_list_unittest.cpp $(COMM_SRC) \
	$(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_hash_list_unittest

heap:
	g++ st_heap_unittest.cpp $(COMM_SRC) \
	$(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_heap_unittest

heap_timer:
	g++ st_heap_timer_unittest.cpp $(COMM_SRC) \
	$(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_heap_timer_unittest

buffer:
	g++ st_buffer_unittest.cpp $(COMM_SRC) \
	$(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_buffer_unittest

item:
	g++ st_item_unittest.cpp $(COMM_SRC) \
	$(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_item_unittest

event:
	g++ mt_event_unittest.cpp $(SRC)/mt_core.cpp \
	$(SRC)/mt_ext.cpp $(SRC)/mt_thread.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_sys_hook.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) \
	./libucontext.a $(FLAGS) -o mt_event_unittest

connection:
	g++ mt_connection_unittest.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_core.cpp $(SRC)/mt_sys_hook.cpp $(SRC)/mt_ext.cpp $(SRC)/mt_thread.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_connection_unittest

thread:
	g++ st_thread_unittest.cpp $(COMM_SRC) $(SRC)/st_thread.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o st_thread_unittest

c:
	g++ mt_c_unittest.cpp $(SRC)/mt_c.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_core.cpp $(SRC)/mt_ext.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_c_unittest

session:
	g++ st_session_unittest.cpp $(COMM_SRC) \
	$(SRC)/st_thread.cpp $(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o st_session_unittest

manager:
	g++ st_manager_unittest.cpp $(COMM_SRC) \
	$(SRC)/st_thread.cpp $(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o st_manager_unittest

server:
	g++ st_server_unittest.cpp $(COMM_SRC) \
	$(SRC)/st_thread.cpp $(SRC)/st_sys.cpp $(SRC)/st_manager.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o st_server_unittest

ucontext:
	gcc -c $(SRC)/st_asm.S $(FLAGS) -o ./st_asm.o
	gcc -c $(SRC)/st_ucontext.cpp $(INCLUDE) $(FLAGS) -o ./st_ucontext.o
	gcc -c $(SRC)/st_util.cpp $(INCLUDE) $(FLAGS) -o ./st_util.o
	gcc -c $(SRC)/st_log.cpp $(INCLUDE) $(FLAGS) -o ./st_log.o
	ar cr libucontext.a ./st_asm.o ./st_ucontext.o ./st_util.o ./st_log.o
	g++ st_ucontext_unittest.cpp $(INCLUDE) $(GTEST_LIB) ./libucontext.a \
	$(FLAGS) -o st_ucontext_unittest
	rm -rf *.o

queue:
	g++ queue_unittest.cpp $(FLAGS) -o queue_unittest

singleton:
	g++ st_singleton_unittest.cpp $(GTEST_LIB) \
	$(FLAGS) -o st_singleton_unittest

clean:
	rm -rf *_unittest
	rm -rf *.dSYM

all:
	make clean
	make hash_list heap heap_timer buffer action
	make connection thread c session frame ucontext
	make server