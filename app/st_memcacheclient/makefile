INCLUDE = -I../../include -I.
SRC = .
FLAGS = -Wno-backslash-newline-escape -Wno-unused-value -Wno-format -g -DTRACE
LIBS = ../../libmthread.a

all:
	g++ $(SRC)/mt_array.cpp $(SRC)/memcache.cpp $(SRC)/main.cpp $(INCLUDE) \
		$(FLAGS) $(LIBS) -o main

clean:
	@rm -rf main *.dSYM
