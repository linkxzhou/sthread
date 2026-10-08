ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: st_redisclient

st_redisclient: main.cpp resp.cc resp.h $(ST_ROOT)/libmthread.a
	$(CC) main.cpp resp.cc $(INCLUDE) $(FLAGS) $(LIBS) -o st_redisclient

clean:
	@rm -rf st_redisclient *.dSYM
