BINARY = libst.so libst.a

DEBUG = -g2 -DTRACE
C_ARGS = -fPIC \
	-Wno-backslash-newline-escape \
	-Wno-format \
	-Wno-deprecated-declarations \
	-Wno-null-dereference \
	$(DEBUG)

ifeq ($(ARCH),32)
	C_ARGS += -march=pentium4 -m32 -pthread
else
	C_ARGS += -m64 -pthread
endif

CC = g++
INC = -I. -I./ucontext

all: $(BINARY)
	@rm -rf *.o

%.o: %.c
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC) $(CRESET)  

%.o: %.cc
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC) $(CRESET)

%.o: %.S
	@echo -e Compiling $(GREEN)$<$(RESET) ...$(RED)
	@$(CC) $(C_ARGS) -c -o $@ $< $(INC) $(CRESET) 

clean:
	@rm -f $(BINARY) *.o

LIBO = st_ucontext.o st_log.o st_test.o st_asm.o

libst.a: $(LIBO)
	@echo -e Linking $(CYAN)$@$(RESET) ...$(RED)
	@rm -f $@
	@ar crs $@ $^ $(CRESET)
	@chmod +x $@
	@mv libst.a ./lib/

libst.so: $(LIBO)
	@echo -e Linking $(CYAN)$@$(RESET) ...$(RED)
	@$(CC) -o $@ $^ -shared $(C_ARGS) -Wl $(CRESET)
	@mv libst.so ./lib/