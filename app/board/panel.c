/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Copyright (c) 2026 珠海笙科智能科技有限公司 (M-VAVE).
 * Connect the FM1 control API to the board scanner. */
#include "fm1.h"
#include "fm1_device.h"
#include "fm1_input.h"
#include "fm1_lcd_hw.h"
#include "lcd.inc"

static uint8_t controls_started;
uint8_t fm1_io_trs_midi_enabled(void) { return 1U; }

/* Power-on recovery chord: OCT- (button 0) and OCT+ (button 1) held together.
 * It calls fm1_device_recovery(), an undocumented stub into the ROM USB
 * updater. Its behaviour (does it return? what state is it entered from?) has
 * not been verified on hardware. Checked once, after the first scans of
 * fm1_controls_init(); it never writes flash. */
#define FM1_RECOVERY_CHORD 0x3U
static int recovery_chord_held(uint32_t buttons)
{
    return (buttons & FM1_RECOVERY_CHORD) == FM1_RECOVERY_CHORD;
}

static void check_recovery_chord(void)
{
    /* Debounced state is valid after two scans. With no scan progress, do not guess. */
    if (fm1_input_settle() && recovery_chord_held(fm1_in.buttons)) fm1_device_recovery();
}

void fm1_controls_init(void)
{
    if (!controls_started) {
        /* PB1: battery divider; PB6: master potentiometer. */
        const uint32_t analog = (1U << 1) | (1U << 6);
        FM1_PR(FM1_PB, FM1_DIR) |= analog;
        FM1_PR(FM1_PB, FM1_DIE) &= ~analog;
        FM1_PR(FM1_PB, FM1_PU) &= ~analog;
        FM1_PR(FM1_PB, FM1_PD) &= ~analog;
        fm1_input_init();
        fm1_device_start(fm1_input_tick);
        controls_started = 1U;
        check_recovery_chord();
    }
}

void fm1_controls_read(struct fm1_controls *state)
{
    if (!state) return;
    struct fm1_controls snapshot;
    uint32_t irq_state = fm1_device_irq_save();
    snapshot.keys = fm1_in.notes;
    snapshot.buttons = fm1_in.buttons;
    unsigned index = FM1_NENC;
    while (index) {
        --index;
        snapshot.encoders[index] = fm1_in.enc_steps[index];
        fm1_in.enc_steps[index] = 0;
    }
    fm1_device_irq_restore(irq_state);
    snapshot.knob = (uint16_t)fm1_device_adc(4U);
    snapshot.battery = (uint16_t)fm1_device_adc(3U);
    *state = snapshot;
}

/* on = full brightness; off = the idle level (dark until fm1_led_idle_level()). */
void fm1_led_set(unsigned key, int on)
{
    fm1_led_key(key, on);
}

/* Explicit brightness, 0 (dark) to 8 (full); larger values clamp to 8. */
void fm1_led_level(unsigned key, unsigned level)
{
    fm1_led_key_level(key, level);
}

/* Brightness of every LED that is not on or at an explicit level. Default 0. */
void fm1_led_idle_level(unsigned level)
{
    fm1_led_key_idle(level);
}
