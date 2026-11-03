# Host tests for the DX747 audio out stage (master filter, USB tap, volume).
# Needs clang (or set CC=gcc). Builds app/audio_output.c natively against mocks.
CC ?= clang
APP = ../../../../app
SDK = ../../../../sdk/include
CFLAGS = -std=gnu11 -DFM1_HOST_TEST -I$(SDK) -I$(APP) -Wall -Wextra -Werror -Wdouble-promotion -g -O1 -fno-omit-frame-pointer
SAN = -fsanitize=address,undefined -fno-sanitize-recover=all

test: test_filter
	./test_filter

test_filter: test_filter.c $(APP)/audio_output.c $(APP)/audio_output.h $(APP)/dx747/master_fx.inc $(APP)/dx747/master_fx.h
	$(CC) $(CFLAGS) $(SAN) test_filter.c -o $@ -lm

# The code under test alone, as the firmware build compiles it (-O2, -Werror).
compile-check:
	$(CC) $(CFLAGS) -O2 -c $(APP)/audio_output.c -o /dev/null

# The audio ISR path may not call runtime helpers (64-bit multiply, shifts, divide,
# memcpy) because they live in flash. Compile for a 32-bit core (Thumb-2, which has no
# 64-bit ALU either) and list every call made by the ISR functions; only the SDK audio
# entry points and the stage's own loops may appear (FM1_AUDIO_CODE is empty on the host,
# so the compiler may or may not inline them).
ISR_FUNCS = fm1_output_mix|mfx_run|mfx_lp|mfx_bp|mfx_hp
space := $(subst ,, )
ISR_ALLOWED = fm1_bluetooth_audio_read fm1_engine_output_level fm1_usb_audio_read fm1_usb_audio_write mfx_bp mfx_hp mfx_lp mfx_run
isr-check:
	@$(CC) --target=thumbv7m-none-eabi -ffreestanding -nostdlibinc -std=gnu11 -DFM1_HOST_TEST -I$(SDK) -I$(APP) -O2 -S $(APP)/audio_output.c -o isr.s
	@awk '/^($(ISR_FUNCS)):/{f=1} f&&/\.fnend/{f=0} f' isr.s | grep -E '[[:space:]]bl[x]?[[:space:]]' | sed 's/.*[[:space:]]//' | sort -u > isr.calls; \
	bad=`grep -vxE '$(subst $(space),|,$(ISR_ALLOWED))' isr.calls`; rm -f isr.s; \
	if [ -n "$$bad" ]; then echo "isr-check: unexpected calls in the audio ISR path:"; echo "$$bad"; rm -f isr.calls; exit 1; fi; \
	echo "isr-check: calls are $$(tr '\n' ' ' < isr.calls)"; rm -f isr.calls

clean:
	rm -f test_filter isr.s isr.calls
	rm -rf test_filter.dSYM

.PHONY: test compile-check isr-check clean
