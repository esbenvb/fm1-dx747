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

/* Power-on chords. Both are decided once, by the first fm1_controls_init(). The engine reaches that from
 * groove_screen_init, the last hook of fm1_app_init, so it is before the LCD, before any DX747 poll and before
 * the engine starts USB MIDI and the update module (fm1_io_start, fm1_update_init). A chord has to be held
 * on consecutive scan frames; the first mismatch ends the wait, so a normal power-on costs nothing extra. The
 * wait is a busy loop in the Control task and the watchdog timeout is not known (the sealed library feeds it
 * from vPortYield, the idle task and its flash erase loops), so the waits are short; the LCD init that follows
 * already busy-waits 0.22 s.
 *
 *   SAFE BOOT  ENV + LFO (buttons 4 and 5; DX747 gives them no function), and any other buttons may be
 *              down too. DX747 then skips every optional start-up step (see dx747/boot.inc). It writes
 *              nothing, so a chord that is seen by mistake at every boot costs a "SAFE BOOT" screen
 *              and nothing else: USB MIDI and the firmware update page keep working.
 *   ROM UPDATE OCT- + OCT+ and no other button or note key, held for about 0.4 s. Calls fm1_device_recovery(),
 *              which in the sealed library is platform_enter_mask_usb_update(), the routine behind the
 *              SysEx F0 22 24 35 7D F7: mask interrupts, copy "usb_update_mode" to the SRAM update
 *              mailbox (UPDATA_BEG, 0x1c7fd80), set a P33 bit and spin until the reset. It never
 *              returns and writes no flash. What the boot loader does then is unverified. Safe boot
 *              wins when both are down. */
#define FM1_BUTTON_MASK 0x3fffU
#define FM1_SAFE_CHORD ((1U << 4) | (1U << 5))
#define FM1_RECOVERY_CHORD 0x3U
enum { FM1_SAFE_FRAMES = 64, FM1_RECOVERY_FRAMES = 364 }; /* about 70 ms and 0.4 s of scan frames, busy in the Control task */

static uint8_t safe_boot;
unsigned fm1_panel_safe_boot(void) { return safe_boot; }

static void check_boot_chords(void)
{
    /* Debounced state is valid after the settle wait. With no scan progress, do not guess. */
    if (!fm1_input_settle()) return;
    if (fm1_input_held(FM1_SAFE_CHORD, FM1_SAFE_CHORD, 0U, FM1_SAFE_FRAMES)) { safe_boot = 1U; return; }
    if (fm1_input_held(FM1_BUTTON_MASK, FM1_RECOVERY_CHORD, ~0U, FM1_RECOVERY_FRAMES)) fm1_device_recovery();
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
        check_boot_chords();
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
