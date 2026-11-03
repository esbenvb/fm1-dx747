# Native host test for the DX747 LED brightness and recovery chord (M6).
# Builds the real app/board/panel.c and fm1_input.h against a mocked GPIO,
# scan DMA and button matrix. Needs clang (or set CC=gcc).
CC ?= clang
ROOT = ../../../..
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
         -fno-sanitize-recover=all -fno-omit-frame-pointer -DFM1_HOST_TEST=1 \
         -I. -Istubs -I$(ROOT)/app/board -I$(ROOT)/sdk/include

test: test_backlight
	./test_backlight

# panel.c is copied unchanged so that its "fm1_lcd_hw.h" and "lcd.inc" includes
# resolve to stubs/ instead of the sibling hardware files.
build/panel_real.c: $(ROOT)/app/board/panel.c
	mkdir -p build
	cp $< $@

test_backlight: test_backlight.c host_hw.h build/panel_real.c \
                $(ROOT)/app/board/fm1_input.h $(ROOT)/app/board/fm1_gpio.h \
                stubs/fm1_lcd_hw.h stubs/lcd.inc
	$(CC) $(CFLAGS) test_backlight.c -o $@

clean:
	rm -rf test_backlight test_backlight.dSYM build

.PHONY: test clean
