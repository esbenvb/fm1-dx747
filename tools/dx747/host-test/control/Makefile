# Native host tests for the DX747 control logic (app/dx747/ctl.inc). Needs clang (or set CC=gcc).
CC ?= clang
CFLAGS = -std=c11 -DFM1_HOST_TEST -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer
SRC = ../../../../app/dx747

test: test_control test_threads
	./test_control
	./test_threads

test_control: test_control.c $(SRC)/ctl.inc $(SRC)/route.inc ../../../../web/synth/dx7core.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined test_control.c -o $@

# The SysEx hook (producer) against the control task (consumer) under ThreadSanitizer.
test_threads: test_threads.c $(SRC)/ctl.inc $(SRC)/route.inc
	$(CC) $(CFLAGS) -fsanitize=thread -pthread test_threads.c -o $@

clean:
	rm -f test_control test_threads
	rm -rf test_control.dSYM test_threads.dSYM

.PHONY: test clean
