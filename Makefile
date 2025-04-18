CC=gcc
CFLAGS=-Wall -Wextra -pthread
LIBS=-lm -lSDL2 -lSDL2_mixer

# Source files and objects
SRCS=game.c
OBJS=$(SRCS:.c=.o)
DEPS=game.h

# Target executable
TARGET=ninja_fruit

all: $(TARGET)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET) *.o highscore.txt

.PHONY: all clean 