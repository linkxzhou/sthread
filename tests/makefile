INCLUDE = -I../include -I.
SRC = ..
GTEST_LIB = ./gtest/googletest/libgtest.a -lpthread
FLAGS = -Wno-backslash-newline-escape -Wno-format -g

hash_list:
	g++ mt_hash_list_unittest.cpp $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o mt_hash_list_unittest

heap:
	g++ mt_heap_unittest.cpp $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o mt_heap_unittest

heap_timer:
	g++ mt_heap_timer_unittest.cpp $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o mt_heap_timer_unittest

buffer:
	g++ mt_buffer_unittest.cpp $(SRC)/mt_buffer.cpp $(INCLUDE) $(GTEST_LIB) $(FLAGS) -o mt_buffer_unittest

event:
	g++ mt_event_unittest.cpp $(SRC)/mt_event_proxyer.cpp \
	$(SRC)/mt_buffer.cpp $(SRC)/mt_session.cpp $(SRC)/mt_thread.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_sys_hook.cpp $(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_event_unittest

action:
	g++ mt_action_unittest.cpp $(SRC)/mt_action.cpp $(SRC)/mt_sys_hook.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_buffer.cpp $(SRC)/mt_event_proxyer.cpp \
	$(SRC)/mt_session.cpp $(SRC)/mt_c.cpp $(SRC)/mt_thread.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_action_unittest

connection:
	g++ mt_connection_unittest.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_buffer.cpp $(SRC)/mt_event_proxyer.cpp \
	$(SRC)/mt_sys_hook.cpp $(SRC)/mt_session.cpp \
	$(INCLUDE) $(GTEST_LIB) $(FLAGS) -o mt_connection_unittest

thread:
	g++ mt_thread_unittest.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_event_proxyer.cpp $(SRC)/mt_buffer.cpp \
	$(SRC)/mt_session.cpp $(SRC)/mt_sys_hook.cpp $(SRC)/mt_connection.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_thread_unittest

c:
	g++ mt_c_unittest.cpp $(SRC)/mt_c.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_event_proxyer.cpp \
	$(SRC)/mt_buffer.cpp $(SRC)/mt_session.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_c_unittest

session:
	g++ mt_session_unittest.cpp $(SRC)/mt_session.cpp \
	$(SRC)/mt_connection.cpp $(SRC)/mt_thread.cpp $(SRC)/mt_c.cpp \
	$(SRC)/mt_buffer.cpp $(SRC)/mt_event_proxyer.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_session_unittest

frame:
	g++ mt_frame_unittest.cpp $(SRC)/mt_connection.cpp \
	$(SRC)/mt_buffer.cpp $(SRC)/mt_event_proxyer.cpp $(SRC)/mt_thread.cpp \
	$(SRC)/mt_session.cpp $(SRC)/mt_sys_hook.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_frame_unittest

ucontext:
	gcc -c $(SRC)/mt_asm.S $(FLAGS) -o ./mt_asm.o
	gcc -c $(SRC)/mt_ucontext.cpp $(INCLUDE) $(FLAGS) -o ./mt_ucontext.o
	ar cr libucontext.a ./mt_asm.o ./mt_ucontext.o
	g++ mt_ucontext_unittest.cpp \
	$(INCLUDE) $(GTEST_LIB) ./libucontext.a $(FLAGS) -o mt_ucontext_unittest
	rm -rf *.o

server:
	g++ server_unittest.cpp $(FLAGS) -o server_unittest

queue:
	g++ queue_unittest.cpp $(FLAGS) -o queue_unittest

clean:
	rm -rf *_unittest
	rm -rf *.dSYM

all:
	make clean
	make hash_list heap heap_timer buffer action
	make connection thread c session frame ucontext
	make server