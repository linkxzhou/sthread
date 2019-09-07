DEBUG = -g

BINARY = libmthread.so libmthread.a

# Comment the following line if you are not using the gnu c compiler
DEBUG = -g -DTRACE
C_ARGS = -fPIC -O1 -Wno-backslash-newline-escape -Wno-format -Wno-deprecated-declarations $(DEBUG)
#.SUFFIXES: .o .cpp

ifeq ($(ARCH),32)
	C_ARGS += -march=pentium4 -m32 -pthread
else
	C_ARGS += -m64 -pthread
endif

# You might have to change this if your c compiler is not cc
CC = g++
INC = -I./include

#.c.o:
#	$(CC) -c $*.c

all: $(BINARY) 
	@rm -rf *.o

# RED = \\e[1m\\e[31m
# DARKRED = \\e[31m
# GREEN = \\e[1m\\e[32m
# DARKGREEN = \\e[32m
# BLUE = \\e[1m\\e[34m
# DARKBLUE = \\e[34m
# YELLOW = \\e[1m\\e[33m
# DARKYELLOW = \\e[33m
# MAGENTA = \\e[1m\\e[35m
# DARKMAGENTA = \\e[35m
# CYAN = \\e[1m\\e[36m
# DARKCYAN = \\e[36m
# RESET = \\e[m
# CRESET = ;echo -ne \\e[m; test -s $@

%.o: %.cpp
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC) $(CRESET)

%.o: %.c
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC) $(CRESET)  

%.o: %.S
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC) $(CRESET) 

clean:
	@rm -f $(BINARY) *.o

LIB_O = mt_action.o mt_asm.o mt_c.o \
	mt_connection.o mt_core.o mt_ext.o \
	mt_sys_hook.o mt_thread.o mt_ucontext.o mt_utils.o

libmthread.a: $(LIB_O)
	@echo -e Linking $(CYAN)$@$(RESET) ...$(RED)
	@-rm -f $@ 
	@ar crs $@ $^ $(CRESET)
	@chmod +x $@

libmthread.so: $(LIB_O)
	@echo -e  Linking $(CYAN)$@$(RESET) ...$(RED)
	@$(CC) -o $@ $^ -shared $(C_ARGS) -Wl $(CRESET)