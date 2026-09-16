ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: main

main: main.cpp ../st_dns/dns_proto.h $(ST_ROOT)/libmthread.a
	$(CC) main.cpp $(INCLUDE) $(FLAGS) $(LIBS) -o main

clean:
	@rm -rf main *.dSYM
