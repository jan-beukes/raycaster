CC = gcc
CFLAGS = -Wall -Wextra
LFLAGS = -lSDL3 -lm

SRCS = *.c
SRCS += ext/*.c

all: game

game: $(SRCS)
	$(CC) $(CFLAGS) $^ $(LFLAGS)

