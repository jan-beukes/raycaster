CC = gcc
CFLAGS = -Wall -Wextra
LFLAGS = -lSDL3

SRCS = src/*.c

all: game

game: $(SRCS)
	$(CC) $(CFLAGS) $^ $(LFLAGS)

