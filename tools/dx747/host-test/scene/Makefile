# Native host tests for the DX747 scene store. Needs clang (or set CC=gcc).
CC ?= clang
CFLAGS = -std=c11 -DFM1_HOST_TEST -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
         -fsanitize=address,undefined -I../../../../sdk/include
SRC = ../../../../app/dx747

test: test_scene
	./test_scene

test_scene: test_scene.c $(SRC)/scene.inc $(SRC)/scene.h $(SRC)/route.h
	$(CC) $(CFLAGS) test_scene.c -o $@

clean:
	rm -f test_scene
	rm -rf test_scene.dSYM

.PHONY: test clean
