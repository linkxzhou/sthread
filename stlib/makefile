ST_ROOT = ..
include $(ST_ROOT)/make.inc

BINARY = libst.a libst.so

INC    = $(ST_INC)
C_ARGS = $(ST_CXXFLAGS)

# stlib 生产库编译单元（D6：st_test.cc 不进 libst，仅测试构建时编译）：
#   st_log.cc            日志实现
#   st_context.cc        协程上下文封装
#   ucontext/ucontext.c  getmcontext/setmcontext/swapcontext/makecontext 的 C 部分
#                        （Linux 走系统 ucontext，本文件在该平台展开为空）
#   ucontext/asm.S       上述函数的汇编实现（同上）
# 其余 stlib/*.h 都是模板或 inline，不产出目标文件。
# 测试框架：st_test.cc — 见 stlib/tests/makefile 与 tests/makefile。
UNAME_M := $(shell uname -m)
# arm64 uses asm.S NEEDARM64CONTEXT (same as other arches).
UCONTEXT_ASM_O = ucontext/asm.o
LIBO = st_log.o st_context.o ucontext/ucontext.o $(UCONTEXT_ASM_O)

.PHONY: all clean

all: $(BINARY)
	@rm -f $(LIBO)

%.o: %.c
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

%.o: %.cc
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

%.o: %.S
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

libst.a: $(LIBO)
	@echo "Linking $@ ..."
	@rm -f $@
	@ar crs $@ $^

libst.so: $(LIBO)
	@echo "Linking $@ ..."
	@$(CC) -o $@ $^ -shared $(C_ARGS) $(ST_LDFLAGS)

clean:
	@rm -f $(BINARY) $(LIBO) *.o ucontext/*.o
