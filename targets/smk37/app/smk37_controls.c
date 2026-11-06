/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 on the SMK-37 Pro and Elite: the controls layer.
 *
 * M-VAVE's open board layer (app/board/scan.c, panel.c: the key, pad, fader and encoder scanner, the RGB lights, the display) reports
 * what happens on the panel as events, smk37_control_event(kind, id, value), and reads the panel back as a struct fm1_controls
 * (smk37_controls_snapshot): the same virtual panel of 14 buttons and 7 encoders the FM-1 has, which is what DX747's screens work on.
 * This file is DX747's side of that interface (M-VAVE's own, in smk37_open, belongs to its stock firmware). What it does:
 *   - the 18 buttons and 8 encoders are the FM-1's 14 buttons and 7 encoders, with the same physical-to-virtual maps as M-VAVE's
 *     stock firmware (button_map, encoder_map below); SHIFT + ENV is LFO, because the two share one lamp; encoder 8 is the master level;
 *   - the 37 keys play the selected part with the velocity they are struck with, the pads recall scenes and mute and solo (dx_ui_pad),
 *     the four faders are part levels (FADER BANK: parts 1 to 4 or 5 to 8, with pickup so that a fader takes over where the part is),
 *     the wheels bend and modulate the selected part, the pedal sustains it;
 *   - safe boot is SHIFT and ENV held while switching on (the FM-1's ENV and LFO chord cannot be pressed together here).
 * Everything in this file about the real panel (button and encoder numbers, value ranges, the safe-boot read) is taken from smk37_open
 * and has not been run on an SMK-37 by this project: docs/ON-DEVICE-CHECKLIST-SMK37.md. */
#include <string.h>
#include "fm1.h"
#include "fm1_device.h"
#include "smk37_controls.h"
#include "dx747/dx747.h"

void smk37_scan_read(void);

enum { SHIFT = 0, FADER_BANK = 1, ENV_BUTTON = 9 };
static const uint8_t button_map[18] = { 255, 255, 8, 9, 0, 1, 10, 255, 3, 4, 7, 255, 8, 6, 2, 11, 12, 13 };
static const uint8_t encoder_map[7] = { 2, 3, 4, 5, 0, 1, 6 };

static struct fm1_controls panel;
static uint32_t physical;                      /* the 18 buttons held */
static uint32_t shifted;                       /* buttons that went down with SHIFT held */
static uint8_t bank, pickup, last[4], seen;    /* fader bank, faders that have not yet taken over, their last readings */
static uint8_t owned[8];                       /* the level this layer last gave each part: when it differs, the level changed elsewhere (a scene, MIDI, the panel) */
static uint8_t master = 100;                   /* the master level, 0..127: encoder 8 */

static void set_master(int level)
{
    master = (uint8_t)(level < 0 ? 0 : level > 127 ? 127 : level);
    panel.knob = (uint16_t)((master * 1023U + 63U) / 127U);
}

void smk37_controls_reset(void)
{
    memset(&panel, 0, sizeof panel);
    physical = shifted = 0; bank = 0; pickup = 15; seen = 0; master = 100;
    memset(owned, 255, sizeof owned);
    set_master(master);
}

static void rebuild_buttons(void)
{
    panel.buttons = 0;
    for (unsigned i = 0; i < 18; ++i) {
        if (!((physical >> i) & 1U) || button_map[i] >= 14) continue;
        unsigned button = button_map[i];
        if (i == ENV_BUTTON && ((shifted >> i) & 1U)) button = 5;      /* SHIFT + ENV: LFO */
        panel.buttons |= 1U << button;
    }
}

static void fader(unsigned id, unsigned value)
{
    unsigned part = bank * 4U + id, bit = 1U << id;
    if (value > 127U) value = 127U;
    unsigned previous = last[id], known = (seen >> id) & 1U;
    last[id] = (uint8_t)value; seen |= (uint8_t)bit;
    if (!dx_ui_fader_live(part)) return;                    /* nothing to move (an empty slot, the boot animation, safe boot): keep waiting to take over */
    int target = (int)dx_ui_part_level(part), v = (int)value;
    if (!(pickup & bit) && owned[part] != 255U && owned[part] != target) pickup |= (uint8_t)bit;   /* the level moved elsewhere: pick it up again */
    if (pickup & bit) {                                     /* after a bank change, at power-on or a level changed elsewhere: take over where the part is */
        if (!(known && ((previous <= (unsigned)target && value >= (unsigned)target) || (previous >= (unsigned)target && value <= (unsigned)target)))
            && (v - target > 2 || target - v > 2)) return;
        pickup &= (uint8_t)~bit;
        if (known && value == previous) { owned[part] = (uint8_t)target; return; }
    } else if (known && value == previous) return;
    dx_ui_fader(part, value);
    owned[part] = (uint8_t)dx_ui_part_level(part);
}

void smk37_control_event(unsigned kind, unsigned id, int value)
{
    switch (kind) {
    case SMK37_KEY:
        if (id < 37) {
            int note = 48 + (int)id + 12 * dx_ui_octave();
            if (note >= 0 && note < 128) dx_ui_note((unsigned)note, value > 0 ? (unsigned)value : 0U, value > 0);
        }
        break;
    case SMK37_PAD:
        if (id < 16) dx_ui_pad(id, value > 0);
        break;
    case SMK37_ENCODER:
        if (id < 7) {
            int sum = panel.encoders[encoder_map[id]] + value;
            panel.encoders[encoder_map[id]] = (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
        } else if (id == 7) set_master((int)master + value);
        break;
    case SMK37_FADER:
        if (id < 4) fader(id, (unsigned)value);
        else if (id < 6) dx_ui_wheel(id - 4U, (unsigned)value);
        break;
    case SMK37_PEDAL:
        dx_ui_pedal(value >= 64);
        break;
    case SMK37_BUTTON:
        if (id < 18) {
            unsigned bit = 1U << id, was = physical & bit;
            if (!!was == (value != 0)) break;
            if (value) physical |= bit; else { physical &= ~bit; shifted &= ~bit; }
            if (value) {
                if (physical & (1U << SHIFT)) shifted |= bit & (1U << ENV_BUTTON);
                if (id == FADER_BANK) { bank ^= 1U; pickup = 15; }
            }
            rebuild_buttons();
        }
        break;
    default:
        break;                                             /* pad pressure and the combination messages: not used */
    }
}

void smk37_controls_snapshot(struct fm1_controls *out)
{
    *out = panel;
    memset(panel.encoders, 0, sizeof panel.encoders);
}

/* Everything held is let go: the board calls this when it loses events (an overflow); DX747 lets go of keys, pedal and wheels itself when
 * the part they play changes. The buttons and encoders are forgotten too: a lost release must not leave SHIFT or ENV held. */
void smk37_controls_panic(void)
{
    dx_ui_release_keys();
    for (unsigned i = 0; i < 16; ++i) smk37_control_event(SMK37_PAD, i, 0);
    physical = shifted = 0;
    panel.buttons = 0;
    memset(panel.encoders, 0, sizeof panel.encoders);
}
void smk37_controls_release(void)
{
    for (unsigned i = 0; i < 37; ++i) smk37_control_event(SMK37_KEY, i, 0);
    for (unsigned i = 0; i < 18; ++i) smk37_control_event(SMK37_BUTTON, i, 0);
    smk37_control_event(SMK37_PEDAL, 0, 0);
    memset(panel.encoders, 0, sizeof panel.encoders);
}

/* What the board's panel.c asks of the application for the lights. */
unsigned smk37_fader_bank(void) { return bank; }
unsigned smk37_pad_steps(void) { return 0; }
unsigned smk37_pad_focus(void) { return 0; }
unsigned smk37_screen_pad_key(unsigned pad) { (void)pad; return 255U; }       /* no pad stands for a virtual key: DX747 takes pads as pads */
uint32_t smk37_pad_color(unsigned pad, unsigned lit) { (void)lit; return dx_ui_pad_color(pad); }

/* The three functions DX747's screens expect of the panel (app/dx747/dx747.h). */
void fm1_led_level(unsigned key, unsigned level) { fm1_led_set(key, level > 0U); }
void fm1_led_idle_level(unsigned level) { (void)level; }                      /* no button backlight level on the SMK-37 yet */
unsigned fm1_panel_safe_boot(void)
{
    unsigned held = 0;
    smk37_controls_reset();                                /* the panel starts from nothing: the master level, the fader pickup */
    for (unsigned i = 0; i < 40U; ++i) {                   /* about 400 ms of scanning: the scanner needs a few passes to see the chord */
        smk37_scan_read();
        if ((physical & (1U << SHIFT)) && (physical & (1U << ENV_BUTTON))) held = 1;
        fm1_device_delay(10);
    }
    smk37_controls_release();                              /* the chord is not a button press for the screens */
    return held;
}
