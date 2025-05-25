CC = cc
CFLAGS = -Wall -Wextra -O3 -std=c99 -g

SRC = $(wildcard src/*.c) $(wildcard src/amd64/*.c) $(wildcard src/arm64/*.c) $(wildcard src/rv64/*.c)
OBJ = $(SRC:.c=.o)

lib/libqbe.a: $(OBJ)
	mkdir -p lib
	ar rcs $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
