/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 on the SMK-37 Pro and Elite: the controls layer.
 *
 * M-VAVE's open board layer (app/board/scan.c, panel.c: the key, pad, fader and encoder scanner, the RGB lights, the display) reports
 * what happens on the panel as events, smk37_control_event(kind, id, value), and reads the panel back as a struct fm1_controls
 * (smk37_controls_snapshot): the same virtual panel of 14 buttons and 7 encoders the FM-1 has, which is what DX747's screens work on.
 * This file is DX747's side of that interface (M-VAVE's own, in smk37_open, belongs to its stock firmware). What it does:
 *   - the 18 buttons and 8 knobs are the panel DX747's screens work on (14 buttons and 7 encoders of the FM-1, and two more buttons,
 *     back and forward): button_map and encoder_map below say which printed button or knob is which, and button_label gives the names
 *     printed on the unit, so that the screens can name what to press (smk37_button_label). Every button that does something has
 *     a button of its own: there is no SHIFT. Knob 8 is the master level;
 *   - the 37 keys play the selected part with the velocity they are struck with, the 16 pads are scenes, layers and parts, or mute and solo (dx_ui_pad),
 *     the four faders are part levels, the master filter or the effects by screen (FADER BANK: parts 1 to 4 or 5 to 8; a fader sets its thing as soon as it moves),
 *     K1 to K4 are the pan of four parts on Mix (KNOB BANK: parts 1 to 4 or 5 to 8),
 *     the wheels bend and modulate the selected part, the pedal sustains it;
 *   - safe boot is the FM-1's chord, add layer and remove layer (SCALE and CHORD, left and right as ENV and LFO) held while switching on.
 * Everything in this file about the real panel (button and encoder numbers, value ranges, the safe-boot read) is taken from smk37_open
 * and has not been run on an SMK-37 by this project: docs/ON-DEVICE-CHECKLIST-SMK37.md. */
#include <string.h>
#include "fm1.h"
#include "fm1_device.h"
#include "smk37_controls.h"
#include "dx747/dx747.h"

void smk37_scan_read(void);
void smk37_led(unsigned id, unsigned state);

enum { KNOB_BANK = 0, FADER_BANK = 1, SCALE_BUTTON = 8, CHORD_BUTTON = 9 };
/* What is printed on the 18 buttons (the drawing of the panel in the SMK-37 manual), by the board's button ids: the left group is 0 to 5
 * and the right group 6 to 17, each row by row. */
static const char *const button_label[18] = { "KNOB BANK", "FADER BANK", "<", ">", "OCTAVE-", "OCTAVE+", "ARP", "NOTE REPEAT", "SCALE", "CHORD",
                                              "GLOBE", "BT", "PATCH", "PARA", "FX", "SEQ", "SEQ PLAY", "SEQ REC" };
/* Printed button -> DX747's button (BTN_* in app/dx747/ui_data.inc, 255: none here): 0 OCTAVE-, 1 OCTAVE+, 2 FX, 3 Mix, 4 add layer,
 * 5 remove layer (SCALE adds and CHORD removes: left adds and right removes, as ENV and LFO on the FM-1), 6 edit pages, 7 System, 8 Parts, 9 save a scene, 10 LEARN, 11 Part, 12 Scenes, 13 MIDI in, 14 back, 15 forward.
 * KNOB BANK has no function yet and FADER BANK is handled here, below. */
static const uint8_t button_map[18] = { 255, 255, 14, 15, 0, 1, 10, 11, 4, 5, 7, 13, 8, 6, 2, 3, 12, 9 };
/* Printed knob (K1 to K8, left to right) -> DX747's encoder (SELECT 0, ALGO 1, K1 to K4 2 to 5, PRESETS 6): the part, the layer, four
 * knobs that change with the screen, the voice. Knob 8 is the master level. */
static const uint8_t encoder_map[7] = { 2, 3, 4, 5, 0, 1, 6 };
static const char *const encoder_label[8] = { "K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8" };

/* What the screens call the controls: the label printed on the button that DX747 reads as its button v (NULL if none), and the label of
 * the knob that is its encoder v (7 is the master level). Not the FM-1's names: the tip lines say what is on this unit. */
const char *smk37_button_label(unsigned v)
{
    for (unsigned i = 0; i < 18; ++i) if (button_map[i] == v) return button_label[i];
    return NULL;
}
const char *smk37_encoder_label(unsigned v)
{
    if (v == 7) return encoder_label[7];
    for (unsigned i = 0; i < 7; ++i) if (encoder_map[i] == v) return encoder_label[i];
    return NULL;
}

static struct fm1_controls panel;
static uint32_t physical;                      /* the 18 buttons held */
static uint8_t bank, kbank, last[4], seen;     /* fader bank, knob bank, the faders' last readings and which of them have been read */
static uint8_t master = 100;                   /* the master level, 0..127: encoder 8 */

static void set_master(int level)
{
    master = (uint8_t)(level < 0 ? 0 : level > 127 ? 127 : level);
    panel.knob = (uint16_t)((master * 1023U + 63U) / 127U);
}

void smk37_controls_reset(void)
{
    memset(&panel, 0, sizeof panel);
    physical = 0; bank = kbank = 0; seen = 0; master = 100;
    set_master(master);
}

static void rebuild_buttons(void)
{
    panel.buttons = 0;
    for (unsigned i = 0; i < 18; ++i) {
        if (!((physical >> i) & 1U) || button_map[i] >= 16) continue;
        panel.buttons |= 1U << button_map[i];
    }
}

/* What a fader controls depends on the screen (dx_ui_fader_role: a part's level, the master filter, an effect). A fader sets its thing as soon as it moves,
 * where it stands: no pickup, so that a fader left in another place after a screen or a bank change is never dead. The first reading of a fader, at power-on,
 * only says where it stands (the unit does not take its levels from the faders), and a fader that has not moved changes nothing. */
static void fader(unsigned id, unsigned value)
{
    if (value > 127U) value = 127U;
    unsigned previous = last[id], known = (seen >> id) & 1U;
    last[id] = (uint8_t)value; seen |= (uint8_t)(1U << id);
    if (!known || value == previous) return;
    unsigned role = dx_ui_fader_role(id, bank);
    if (role < 15U) dx_ui_role_set(role, value);        /* nothing to move (an empty slot, the boot animation, safe boot) is role 255 */
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
            if (value) physical |= bit; else physical &= ~bit;
            if (value && id == FADER_BANK) { bank ^= 1U; smk37_led(FADER_BANK, bank); }
            if (value && id == KNOB_BANK) { kbank ^= 1U; smk37_led(KNOB_BANK, kbank); }
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
 * the part they play changes. The buttons and encoders are forgotten too: a lost release must not leave a button held. */
void smk37_controls_panic(void)
{
    dx_ui_release_keys();
    for (unsigned i = 0; i < 16; ++i) smk37_control_event(SMK37_PAD, i, 0);
    physical = 0;
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
unsigned smk37_knob_bank(void) { return kbank; }
const char *smk37_printed_label(unsigned id) { return id < 18 ? button_label[id] : NULL; }
unsigned smk37_pad_steps(void) { return 0; }
unsigned smk37_pad_focus(void) { return 0; }
unsigned smk37_screen_pad_key(unsigned pad) { (void)pad; return 255U; }       /* no pad stands for a virtual key: DX747 takes pads as pads */
uint32_t smk37_pad_color(unsigned pad, unsigned lit) { (void)lit; return dx_ui_pad_color(pad); }

/* The three functions DX747's screens expect of the panel (app/dx747/dx747.h). */
/* The lamps of DX747's buttons are on the printed buttons that carry them (button_map), lit through the board's own smk37_led(); the board's
 * fm1_led_set would light the lamp of the stock firmware's button for the same number. The lamps of the keys (14 and up) stay with it. */
void fm1_led_level(unsigned key, unsigned level)
{
    if (key >= 14U) { fm1_led_set(key, level > 0U); return; }
    for (unsigned i = 0; i < 18; ++i) if (button_map[i] == key) smk37_led(i, level > 0U);
}
void fm1_led_idle_level(unsigned level) { (void)level; }                      /* no button backlight level on the SMK-37 yet */
unsigned fm1_panel_safe_boot(void)
{
    unsigned held = 0;
    smk37_controls_reset();                                /* the panel starts from nothing: the master level, the faders' readings */
    for (unsigned i = 0; i < 40U; ++i) {                   /* about 400 ms of scanning: the scanner needs a few passes to see the chord */
        smk37_scan_read();
        if ((physical & (1U << SCALE_BUTTON)) && (physical & (1U << CHORD_BUTTON))) held = 1;
        fm1_device_delay(10);
    }
    smk37_controls_release();                              /* the chord is not a button press for the screens */
    return held;
}
