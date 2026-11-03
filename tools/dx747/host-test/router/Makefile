# Native host tests for the DX747 router core. Needs clang (or set CC=gcc).
CC ?= clang
CFLAGS = -std=c11 -DFM1_HOST_TEST -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer
SRC = ../../../../app/dx747

test: test_router test_threads
	./test_router
	./test_threads

test_router: test_router.c $(SRC)/route.inc $(SRC)/boot.inc
	$(CC) $(CFLAGS) -fsanitize=address,undefined test_router.c -o $@

# The two-task design (routing task producer, control task consumer) under
# ThreadSanitizer: any data race fails the run.
test_threads: test_threads.c $(SRC)/route.inc
	$(CC) $(CFLAGS) -fsanitize=thread -pthread test_threads.c -o $@

clean:
	rm -f test_router test_threads
	rm -rf test_router.dSYM test_threads.dSYM

.PHONY: test clean
