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
# libmthread 产物（仓库根目录 libmthread.a / libmthread.so）
#
#   src/:       st_thread.o  st_connection.o  st_sys.o
#   stlib/:     st_log.o  st_context.o  ucontext/{ucontext,asm}.o
#               （不含 st_test.o；测试单独链 stlib/st_test.cc）
#   app/（库侧）: st_c.o  st_sys.o  st_action.o
#               C API / syscall hook；物理仍在 app/，语义属库
#
# 归档内可有两个 st_sys.o（src 与 app）；符号名空间已错开，链接正常。
# 使用方：链接 -lmthread（或 libmthread.a），零第三方运行时依赖。
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
