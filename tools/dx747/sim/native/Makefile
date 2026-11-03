# Native build of the browser simulator, for fast UI work and scripted screenshots
# without the WASI SDK. It compiles the same sources the WASM build uses
# (tools/Build-Web.ps1): app/ minus app/board, web/, third_party/synth.
# Usage: make -C tools/dx747/sim/native && ./dx747-sim OUTDIR script.txt
CC ?= clang
ROOT = ../../../..
CFLAGS = -std=c11 -O1 -g -w -fwrapv -ffp-contract=off -DFM1_HOST_TEST -DFM1_OPEN_WEB -fno-omit-frame-pointer \
         -I$(ROOT)/web/engine -I$(ROOT)/web/synth -I$(ROOT)/third_party/synth -I$(ROOT)/sdk/include -I$(ROOT)/app/synth -I$(ROOT)/app
SRC = $(shell find $(ROOT)/app $(ROOT)/web $(ROOT)/third_party/synth -name '*.c' -not -path '$(ROOT)/app/board/*')
DEPS = $(shell find $(ROOT)/app $(ROOT)/web $(ROOT)/third_party/synth \( -name '*.h' -o -name '*.inc' \) -not -path '$(ROOT)/app/board/*')

dx747-sim: harness.c $(SRC) $(DEPS)
	$(CC) $(CFLAGS) harness.c $(SRC) -o $@ -lz -lm

clean:
	rm -f dx747-sim
	rm -rf dx747-sim.dSYM

.PHONY: clean
