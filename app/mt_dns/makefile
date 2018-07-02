INCLUDE = -I../../include -I.
SRC = .
FLAGS = -Wno-backslash-newline-escape -Wno-format -g -DTRACE
LIBS = ../../libmthread.a

all:
	g++ $(SRC)/dns.cpp $(SRC)/main.cpp $(INCLUDE) \
		$(FLAGS) $(LIBS) -o main

clean:
	@rm -rf main *.dSYM
