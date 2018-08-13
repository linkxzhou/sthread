INCLUDE = -I../../include -I.
SRC = .
FLAGS = -Wno-backslash-newline-escape -Wno-unused-value -Wno-format -Wno-c++11-compat-deprecated-writable-strings -g
LIBS = ../../libmthread.a

all:
	g++ $(SRC)/http_parser.c $(SRC)/wrk.cpp $(INCLUDE) \
		$(FLAGS) $(LIBS) -o wrk

clean:
	@rm -rf main *.dSYM
