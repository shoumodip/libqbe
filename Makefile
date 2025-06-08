CC = cc
CFLAGS = `cat compile_flags.txt` -O3

SRC = $(wildcard src/*.c) $(wildcard src/amd64/*.c) $(wildcard src/arm64/*.c) $(wildcard src/rv64/*.c)
OBJ = $(SRC:.c=.o)

lib/libqbe.a: $(OBJ)
	mkdir -p lib
	ar rcs $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
