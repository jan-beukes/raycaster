CC = gcc
CFLAGS = -Wall -Wextra
LFLAGS = -lSDL3 -lm

SRCS = src/*.c
SRCS += src/ext/*.c

all: game

game: $(SRCS)
	$(CC) $(CFLAGS) $^ $(LFLAGS)

