# Native host tests for the DX747 parts model (app/dx747/parts.inc). Needs clang (or set CC=gcc).
CC ?= clang
CFLAGS = -std=c11 -DFM1_HOST_TEST -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
         -fsanitize=address,undefined
SRC = ../../../../app/dx747

test: test_parts
	./test_parts

test_parts: test_parts.c $(SRC)/parts.inc $(SRC)/parts.h $(SRC)/route.inc $(SRC)/route.h
	$(CC) $(CFLAGS) test_parts.c -o $@

clean:
	rm -f test_parts
	rm -rf test_parts.dSYM

.PHONY: test clean
