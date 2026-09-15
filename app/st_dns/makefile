ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS)
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: main

main: dns.cpp main.cpp
	$(CC) dns.cpp main.cpp $(INCLUDE) $(FLAGS) $(LIBS) -o main

clean:
	@rm -rf main *.dSYM
