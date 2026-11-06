/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 user interface for the 240x240 LCD, the panel buttons, encoders and
 * keys. It replaces the stock sequencer UI. The screens work on parts (one MIDI
 * channel with one, possibly layered, sound; docs/design/PARTS-MODEL.md): Parts
 * as the home overview, the Part page for its layers, the layer pages Sound,
 * Range, Velocity and Sends, MIDI in, Mix, FX, Master, Scenes and System.
 * Drawing lives in dx747/ui_*.inc; this file holds the two entry points the
 * engine calls on its control task and the hooks the browser simulator links
 * against. */
#include "instrument.h"
#include "fm_patch.h"
#include "fm1_engine_control.h"
#include <string.h>
#include "dx747/dx747.h"
#include "dx747/parts.inc"
#include "version.h"
#include "dx747/ui_core.inc"
#include "dx747/ui_data.inc"
#include "dx747/ui_screens.inc"
#include "dx747/ui_input.inc"

static uint32_t last_frame, last_signature;
static unsigned frame_forced = 1;
void dx_ui_release_keys(void) { release_keys(); }
#ifdef DX_SMK37
void dx_ui_release_part(void) { release_part(); }
#endif

static uint32_t hash_bytes(const void *data, unsigned bytes, uint32_t h)
{
    const uint8_t *p = data;
    while (bytes--) h = (h ^ *p++) * 16777619U;
    return h;
}
static void render_frame(void)
{
    for (unsigned b = 0; b < BAND_N; ++b) {
        band_begin(b);
        draw_screen();
        band_end(b);
    }
}

void groove_screen_init(void)
{
    memset(&ui, 0, sizeof ui);
    memset(&fr, 0, sizeof fr);
    ui.screen = S_BOOT;
    ui.octave = 0;
    fm1_controls_init();
#ifdef DX_SMK37
    smk_reset();
#endif
    if (fm1_panel_safe_boot()) { dx_safe_boot_enter(); ui.screen = S_SAFE; }
    fm1_screen_init();
    ui.boot_until = fm1_millis() + BOOT_MS;       /* the animation starts when the screen is ready, not before its 0.2 s start-up delay */
    for (unsigned i = 0; i < 41; ++i) fm1_led_set(i, 0);
    if (ui.screen == S_SAFE) { fm1_led_set(BTN_ENV, 1); fm1_led_set(BTN_LFO, 1); } /* a cue that does not need the LCD */
    memset(led_code, 0, sizeof led_code);
    band_invalidate();
    frame_forced = 1;
}

void groove_screen_control(void)
{
    uint32_t now = fm1_millis();
    fm1_controls_read(&ctl);
    (void)groove_master_update(ctl.knob);
    if (ui.screen == S_SAFE) { /* the panel is ignored; the screen is redrawn now and then in case the LCD missed a band */
        if (frame_forced || now - last_frame >= 250U) { last_frame = now; frame_forced = 0; render_frame(); }
        return;
    }
    if (!(ctl.buttons & BTN_BIT(BTN_HOME))) ui.home_from = 0xff;   /* only a HOME that is still held can be undone by the chord */
    unsigned pressed = ctl.buttons & ~prev_ctl.buttons;
    if (pressed) on_button(pressed);
    uint32_t down = ctl.keys & ~prev_ctl.keys, up = prev_ctl.keys & ~ctl.keys;
    for (unsigned k = 0; k < 27; ++k) {
        if ((down >> k) & 1U) on_key(k, 1);
        if ((up >> k) & 1U) on_key(k, 0);
    }
    on_encoders();
    prev_ctl = ctl;
    if (ui.screen == S_BOOT && (int32_t)(now - ui.boot_until) >= 0) go(S_PARTS);
    if (ui.screen == S_BOOT) { uint32_t left = ui.boot_until - now; ui.boot_t = (uint16_t)(BOOT_MS - (left > BOOT_MS ? BOOT_MS : left)); }
    if (now - last_frame < 33U && !frame_forced) return;
    last_frame = now;
    frame_collect(now & ~255U);
    fr.ov_on = ui.ov_kind != OV_NONE && (int32_t)(ui.ov_until - now) > 0;   /* the real clock: the snapshot's own is 256 ms coarse */
    if (!fr.ov_on) ui.ov_kind = OV_NONE;                                    /* an ended overlay must not come back when the clock wraps */
    { /* DX7 SysEx that arrived with no room left is not applied: say so instead of losing it silently */
        static uint32_t sx_seen;
        uint32_t lost = dx_sysex_dropped();
        if (lost != sx_seen) { sx_seen = lost; notice("DX7 SYSEX LOST"); }
    }
    learn_poll();
    update_leds();
    uint32_t signature = hash_bytes(&ui, sizeof ui, hash_bytes(&fr, sizeof fr, 2166136261U));
    if (signature == last_signature && !frame_forced) return;
    last_signature = signature;
    frame_forced = 0;
    render_frame();
}

/* MMC from the workbench has no meaning without a sequencer. */
void groove_screen_mmc(unsigned command) { (void)command; }

#ifdef FM1_HOST_TEST
/* The browser simulator drives and inspects the screen through these. DX747's
 * own screens are exercised through the panel (buttons, keys, encoders), so the
 * stock page-specific hooks are inert. */
void groove_screen_snapshot(uint8_t state[8]) { memset(state, 0, 8); state[0] = fr.live.parts.selected; state[1] = ui.screen; state[3] = fr.live.parts.part[fr.live.parts.selected].sel; }
int groove_screen_open(unsigned target) { (void)target; if (ui.screen == S_BOOT) go(S_PARTS); return 0; }
int groove_screen_preset(unsigned slot) { (void)slot; return 0; }
unsigned groove_screen_preset_position(void) { return 0; }
void groove_screen_tap(uint32_t now) { (void)now; }
const char *groove_screen_web_knobs(void) { return ""; }
unsigned groove_screen_web_first(void) { return 0; }
int groove_screen_web_step(unsigned step, unsigned toggle) { (void)step; (void)toggle; return 0; }
const void *groove_screen_web_targets(void) { return NULL; }
unsigned groove_screen_web_target_count(void) { return 0; }
unsigned groove_screen_web_context(void) { return 0; }
unsigned groove_screen_web_mask(void) { return 0; }
int groove_screen_web_adjust(unsigned context, unsigned action, unsigned index, unsigned owner, int delta)
{ (void)context; (void)action; (void)index; (void)owner; (void)delta; return 0; }
void groove_screen_web_fine(unsigned fine) { (void)fine; }
#endif
