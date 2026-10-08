ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: st_portscan

st_portscan: main.cpp $(ST_ROOT)/libmthread.a
	$(CC) main.cpp $(INCLUDE) $(FLAGS) $(LIBS) -o st_portscan

clean:
	@rm -rf st_portscan *.dSYM
