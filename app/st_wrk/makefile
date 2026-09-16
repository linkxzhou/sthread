ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

# clang-only warning suppression (g++ ignores unknown -Wno-* with a note)
CLANG := $(shell $(CC) --version 2>&1 | grep -c clang)
ifeq ($(CLANG),1)
FLAGS += -Wno-c++11-compat-deprecated-writable-strings
endif

.PHONY: all clean

all: wrk

wrk: http_parser.c wrk.cpp $(ST_ROOT)/libmthread.a
	$(CC) -c http_parser.c $(ST_INC) -I. -g -o http_parser.o
	$(CC) wrk.cpp http_parser.o $(INCLUDE) $(FLAGS) $(LIBS) -o wrk

clean:
	@rm -rf wrk http_parser.o *.dSYM
