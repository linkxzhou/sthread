ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I.. -I../st_wrk
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: st_httpclient

http_parser.o: ../st_wrk/http_parser.c
	$(CC) -c $< $(ST_INC) -I../st_wrk -g -o $@

st_httpclient: main.cpp http_client.cc http_client.h http_parser.o \
		$(ST_ROOT)/libmthread.a
	$(CC) main.cpp http_client.cc http_parser.o $(INCLUDE) $(FLAGS) $(LIBS) \
		-o st_httpclient

clean:
	@rm -rf st_httpclient http_parser.o *.dSYM
