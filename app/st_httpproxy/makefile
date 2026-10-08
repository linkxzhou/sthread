ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I.. -I../st_httpclient -I../st_wrk
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: st_httpproxy

http_parser.o: ../st_wrk/http_parser.c
	$(CC) -c $< -I../st_wrk -o $@

# http_client.cc / http_parser.c 不进 libmthread。
st_httpproxy: main.cpp ../st_httpclient/http_client.cc http_parser.o \
		$(ST_ROOT)/libmthread.a
	$(CC) main.cpp ../st_httpclient/http_client.cc http_parser.o \
		$(INCLUDE) $(FLAGS) $(LIBS) -o st_httpproxy

clean:
	@rm -rf st_httpproxy http_parser.o *.dSYM
