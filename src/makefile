ST_ROOT = ..
include $(ST_ROOT)/make.inc

# 产物放在仓库根目录：app/*/makefile 里写的是 LIBS = ../../libmthread.a，
# readme 也承诺「到当前目录下执行 make」，两者都指向仓库根。
BINARY = $(ST_ROOT)/libmthread.a $(ST_ROOT)/libmthread.so

INC    = $(ST_INC)
# -O1 fights coverage (-O0 from make.inc); keep objs when COVERAGE=1 for llvm-cov.
ifeq ($(COVERAGE),0)
C_ARGS = $(ST_CXXFLAGS) -O1
else
C_ARGS = $(ST_CXXFLAGS)
endif

# ---------------------------------------------------------------------------
# libmthread 产物组成（书面定义，另见 AGENTS.md「libmthread 产物定义」一节）
#
#   框架实现主体（src/）
#     st_thread.o      协程与调度
#     st_connection.o  连接对象
#     st_sys.o         带超时的内部 IO 封装
#   基础设施（stlib/ + ucontext；D6：不含 st_test.o）
#     st_log.o  st_context.o  ucontext/ucontext.o  ucontext/asm.o
#   对外 C 风格 API 与 syscall hook（当前物理位置仍在 app/ 下）
#     st_c.o           udp_sendrecv / tcp_sendrecv / st_set_private ...
#     st_sys.o         socket/close/read/write... 的 hook
#
# 说明：
# 1. app/st_c.cc 与 app/st_sys.cc 在语义上属于「库」而不是「示例 app」，
#    readme 承诺使用方只需链接一个 libmthread，所以它们必须进这个库。
#    本阶段只在 makefile 层面把它们编进来，不做目录搬迁（改动更小）。
#    是否物理移入 src/ 留给 03/04 决定。
# 2. 因此归档里会出现两个基名相同的成员（src/st_sys.o 与 app/st_sys.o）。
#    ar / ld 允许重名成员，但这两个文件同时用相同名字声明了签名不同的
#    extern "C" 符号（plan/README.md 的 R3 / D4），链接期必然冲突。
#    该冲突的拆分方案属于 03 阶段。
# 3. 本阶段 src/*.cc 与 app/st_c.cc、app/st_sys.cc 都还编不过
#    （旧名未定义、st_manager.h 缺失、epoll 实现 bug 等，属 02/03/04）。
#    这里先把源文件清单和产物路径修正到位，`make lib` 会在编译期停下，
#    而不是像以前那样连一个存在的源文件都找不到。
# ---------------------------------------------------------------------------

SRC_O = st_connection.o st_sys.o st_thread.o

# On Apple Silicon, asm.S is PowerPC-only; use stub instead.
UNAME_M := $(shell uname -m)
UCONTEXT_ASM_O = $(ST_ROOT)/stlib/ucontext/asm.o

STLIB_O = $(ST_ROOT)/stlib/st_log.o \
	$(ST_ROOT)/stlib/st_context.o \
	$(ST_ROOT)/stlib/ucontext/ucontext.o \
	$(UCONTEXT_ASM_O)

APP_O = $(ST_ROOT)/app/st_c.o $(ST_ROOT)/app/st_sys.o $(ST_ROOT)/app/st_action.o

LIB_O = $(SRC_O) $(STLIB_O) $(APP_O)

.PHONY: all clean

all: $(BINARY)
ifeq ($(COVERAGE),0)
	@rm -f $(LIB_O)
endif

%.o: %.cpp
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

%.o: %.c
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

%.o: %.cc
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

%.o: %.S
	@echo "Compiling $< ..."
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC)

$(ST_ROOT)/libmthread.a: $(LIB_O)
	@echo "Linking $@ ..."
	@rm -f $@
	@ar crs $@ $^

$(ST_ROOT)/libmthread.so: $(LIB_O)
	@echo "Linking $@ ..."
	@$(CC) -o $@ $^ -shared $(C_ARGS) $(ST_LDFLAGS)

clean:
	@rm -f $(BINARY) $(LIB_O) *.o
