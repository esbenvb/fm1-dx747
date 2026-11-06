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

# The SMK-37 flavour (DX_SMK37): the same sources against smk37_open's SDK headers, with DX747's SMK-37 controls layer
# (targets/smk37/app/smk37_controls.c) fed by events. Driven by recipes in recipes-smk37/ (commands skey, spad, sfader, ...).
SMKFLAGS = -std=c11 -O1 -g -w -fwrapv -ffp-contract=off -DFM1_HOST_TEST -DFM1_OPEN_WEB -DSMK37_MODEL_PRO=1 -fno-omit-frame-pointer \
         -I$(ROOT)/web/engine -I$(ROOT)/web/synth -I$(ROOT)/third_party/synth -I$(ROOT)/targets/smk37/sdk/include -I$(ROOT)/targets/smk37/board \
         -I$(ROOT)/app/synth -I$(ROOT)/app
SMKSRC = $(SRC) $(ROOT)/targets/smk37/app/smk37_controls.c

dx747-sim-smk37: harness.c $(SMKSRC) $(DEPS)
	$(CC) $(SMKFLAGS) harness.c $(SMKSRC) -o $@ -lz -lm

dx747-sim: harness.c $(SRC) $(DEPS)
	$(CC) $(CFLAGS) harness.c $(SRC) -o $@ -lz -lm

clean:
	rm -f dx747-sim dx747-sim-smk37
	rm -rf dx747-sim.dSYM

.PHONY: clean
