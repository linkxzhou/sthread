ST_ROOT = ../..
include $(ST_ROOT)/make.inc

INCLUDE = $(ST_INC) -I. -I..
FLAGS = $(ST_CXXFLAGS) -Wno-unused-value
LIBS = $(ST_ROOT)/libmthread.a $(ST_LDFLAGS) $(ST_LDLIBS)

.PHONY: all clean

all: st_echoserver st_echoclient

st_echoserver: server.cpp $(ST_ROOT)/libmthread.a
	$(CC) server.cpp $(INCLUDE) $(FLAGS) $(LIBS) -o st_echoserver

st_echoclient: client.cpp $(ST_ROOT)/libmthread.a
	$(CC) client.cpp $(INCLUDE) $(FLAGS) $(LIBS) -o st_echoclient

clean:
	@rm -rf st_echoserver st_echoclient *.dSYM
