ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: st_hookdemo blocking_client

# 业务文件仍是 POSIX 名字。-include 把 socket/read/write 换成 sys_*。
blocking_client_hook.o: blocking_client.c st_posix_alias.h
	$(CC) -include st_posix_alias.h -DST_BLOCKING_NO_MAIN -c blocking_client.c \
		$(FLAGS) -o blocking_client_hook.o

st_hookdemo: main.cpp blocking_client_hook.o $(ST_ROOT)/libmthread.a
	$(CC) main.cpp blocking_client_hook.o $(INCLUDE) $(FLAGS) $(LIBS) \
		-o st_hookdemo

# 同一份 .c，不 -include，不链 libmthread。证明它真是阻塞客户端。
blocking_client: blocking_client.c
	$(CC) blocking_client.c $(FLAGS) -o blocking_client

clean:
	@rm -rf st_hookdemo blocking_client blocking_client_hook.o *.dSYM
