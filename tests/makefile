INCLUDE = -I../include -I. -I../include/ucontext
SRC = ..
GTEST_LIB = ./gtest/googletest/libgtest.a -lpthread -ldl
DEBUG = #-g -DTRACE
FLAGS = -Wno-backslash-newline-escape -Wno-format -Wno-deprecated-declarations -O3 $(DEBUG)
COMM_SRC = $(SRC)/st_util.cpp $(SRC)/st_log.cpp $(SRC)/st_item.cpp

hash_list:
	g++ st_hash_list_unittest.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_hash_list_unittest

heap:
	g++ st_heap_unittest.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_heap_unittest

heap_timer:
	g++ st_heap_timer_unittest.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_heap_timer_unittest

buffer:
	g++ st_buffer_unittest.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_buffer_unittest

item:
	g++ st_item_unittest.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o st_item_unittest

event:
	g++ mt_event_unittest.cpp $(SRC)/mt_core.cpp \
	$(SRC)/mt_ext.cpp $(SRC)/mt_thread.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_sys_hook.cpp $(COMM_SRC) $(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_event_unittest

action:
	g++ mt_action_unittest.cpp $(SRC)/mt_action.cpp $(SRC)/mt_sys_hook.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_core.cpp \
	$(SRC)/mt_ext.cpp $(SRC)/mt_c.cpp $(SRC)/mt_thread.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_action_unittest

connection:
	g++ mt_connection_unittest.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_core.cpp $(SRC)/mt_sys_hook.cpp $(SRC)/mt_ext.cpp $(SRC)/mt_thread.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_connection_unittest

thread:
	g++ mt_thread_unittest.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_core.cpp $(SRC)/mt_ext.cpp $(SRC)/mt_sys_hook.cpp $(SRC)/mt_connection.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_thread_unittest

c:
	g++ mt_c_unittest.cpp $(SRC)/mt_c.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_core.cpp $(SRC)/mt_ext.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_c_unittest

session:
	g++ mt_ext_unittest.cpp $(SRC)/mt_ext.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_thread.cpp $(SRC)/mt_c.cpp \
	$(SRC)/mt_core.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_ext_unittest

frame:
	g++ mt_frame_unittest.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_core.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_ext.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_frame_unittest

ucontext:
	gcc -c $(SRC)/mt_asm.S $(FLAGS) -o ./mt_asm.o
	gcc -c $(SRC)/mt_ucontext.cpp $(INCLUDE) $(FLAGS) -o ./mt_ucontext.o
	gcc -c $(SRC)/mt_utils.cpp $(INCLUDE) $(FLAGS) -o ./mt_utils.o
	ar cr libucontext.a ./mt_asm.o ./mt_ucontext.o ./mt_utils.o
	g++ mt_ucontext_unittest.cpp $(INCLUDE) $(GTEST_LIB) ./libucontext.a \
	$(FLAGS) -o mt_ucontext_unittest
	rm -rf *.o

mt_server:
	g++ mt_server_unittest.cpp $(SRC)/mt_action.cpp $(SRC)/mt_sys_hook.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_core.cpp \
	$(SRC)/mt_ext.cpp $(SRC)/mt_c.cpp $(SRC)/mt_thread.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_server_unittest

server:
	g++ server_unittest.cpp $(FLAGS) -o server_unittest

queue:
	g++ queue_unittest.cpp $(FLAGS) -o queue_unittest

singleton:
	g++ st_singleton_unittest.cpp $(GTEST_LIB) $(FLAGS) -o st_singleton_unittest

clean:
	rm -rf *_unittest
	rm -rf *.dSYM

all:
	make clean
	make hash_list heap heap_timer buffer action
	make connection thread c session frame ucontext
	make server