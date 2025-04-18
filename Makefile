CC=gcc
CFLAGS=-Wall -Wextra -pthread
LIBS=-lm

all: game

game: game.c
	$(CC) $(CFLAGS) -o ninja_fruit game.c $(LIBS)

clean:
	rm -f ninja_fruit highscore.txt

.PHONY: all clean 