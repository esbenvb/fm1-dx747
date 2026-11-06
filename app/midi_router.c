/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 MIDI routing. All external MIDI (DIN, USB, BLE) and the built-in keys go
 * through the router core in dx747/route.inc, which rewrites every message onto
 * the private channel of the engine it is meant for. This file is one of the
 * -fno-lto units on purpose: the weak default of fm1_app_midi_received would
 * silently win in an LTO unit. It also keeps the groove_midi_* symbols the
 * sealed engine links against, and thin stand-ins for the stock MIDI-out,
 * Thru and clock settings that the stock files still call. */
#include "instrument.h"
#include "fm1_engine_control.h"
#include <string.h>
#include "dx747/route.inc"
#include "dx747/boot.inc"
#include "dx747/dx747.h"

/* Router state. cfg_store is double buffered: the control task fills the idle
 * copy and then swings the pointer, so the routing task always reads a whole
 * configuration. */
static struct dx_cfg cfg_store[2];
static dx_cfgp cfg_active;
static unsigned cfg_flip;
static DX_A(uint32_t) panic_epoch;
static struct dx_router ext;  /* producer: MIDI routing task, consumer: control loop */
static struct dx_router loc;  /* built-in keys: control task is both */
static struct dx_ctl_mail mailbox; /* CC74/71/10/11 and program changes for the control task */
static struct groove_midi_config stand_in; /* stock MIDI-out/Thru settings, stored but unused */

static int post(const uint8_t *message, unsigned bytes) { return fm1_midi_post(message, bytes); }

void dx_route_init(void)
{
    dx_cfg_default(&cfg_store[0]);
    cfg_flip = 0; cfg_active = &cfg_store[0]; panic_epoch = 0;
    dx_router_init(&ext, &cfg_active, &panic_epoch, post);
    dx_router_init(&loc, &cfg_active, &panic_epoch, post);
    ext.peer = &loc; loc.peer = &ext;
    memset(&mailbox, 0, sizeof mailbox);
    ext.mail = &mailbox;
}
struct dx_ctl_mail *dx_route_mailbox(void) { return &mailbox; }
const struct dx_cfg *dx_route_cfg(void) { return cfg_active; }
void dx_route_publish(const struct dx_cfg *c)
{
    unsigned next = cfg_flip ^ 1U;
    cfg_store[next] = *c;
    DX_BARRIER();
    cfg_flip = next; cfg_active = &cfg_store[next];
}
void dx_route_key(unsigned note, unsigned on) { dx_router_key(&loc, note, on, fm1_millis()); }
#ifdef DX_SMK37
void dx_route_key_v(unsigned note, unsigned velocity, unsigned on) { dx_router_key_v(&loc, note, velocity, on, fm1_millis()); }
void dx_route_ctl(unsigned status, unsigned d1, unsigned d2) { dx_router_ctl(&loc, status, d1, d2, fm1_millis()); }
#endif
void dx_route_panic(void)
{
    ++panic_epoch;
    const uint8_t reset = 0xff;
    (void)fm1_midi_post(&reset, 1);
    /* the control task posts the refresh once the reset is in the engine's ring, so it arrives behind it */
    dx_mail_put(&mailbox, DX_MAIL_PANIC, 0);
}
int dx_route_active(unsigned channel)
{
    uint32_t now = fm1_millis();
    return dx_router_active(&ext, channel, now, 1200) || dx_router_active(&loc, channel, now, 1200);
}
unsigned dx_route_source(unsigned channel)
{
    if (channel >= 16) return 0;
    uint32_t now = fm1_millis();
    int keys = dx_router_active(&loc, channel, now, 1200);
    if (keys && (!dx_router_active(&ext, channel, now, 1200) || (int32_t)(loc.act_ms[channel] - ext.act_ms[channel]) >= 0)) return DX_SRC_KEYS;
    return ext.act_src[channel];
}
unsigned dx_route_log(struct dx_log *out, unsigned max)
{
    unsigned count = 0, a = ext.log_n, b = loc.log_n;
    unsigned ra = a < DX_LOG_MAX ? a : DX_LOG_MAX, rb = b < DX_LOG_MAX ? b : DX_LOG_MAX;
    while (count < max && (ra || rb)) {
        const struct dx_log *x = ra ? &ext.log[(a - 1) % DX_LOG_MAX] : 0;
        const struct dx_log *y = rb ? &loc.log[(b - 1) % DX_LOG_MAX] : 0;
        if (x && (!y || (int32_t)(x->ms - y->ms) >= 0)) { out[count++] = *x; --a; --ra; }
        else { out[count++] = *y; --b; --rb; }
    }
    return count;
}
unsigned dx_route_voices(void) { return ext.nheld + loc.nheld; }
uint32_t dx_route_dropped(void) { return ext.dropped + loc.dropped + ext.fail_p + ext.fail_c + loc.fail_p + loc.fail_c; }

/* ---- engine hooks ------------------------------------------------------- */

/* Routing task: every short message from DIN, USB or BLE. System realtime
 * traffic (clock, transport, active sensing) is not used by DX747 and is not
 * forwarded; System Reset cuts everything. */
void fm1_app_midi_received(unsigned source, const uint8_t *m, size_t n)
{
    if (dx_safe_boot() || source >= 3 || !m || !n || n > 3) return;
    if (m[0] == 0xff) { dx_route_panic(); return; }
    if (m[0] == 0xf3 && n == 2) { dx_router_song_select(&ext, source, m[1], fm1_millis()); return; } /* only if the engine passes it on */
    if (m[0] >= 0xf0) return;
    dx_router_event(&ext, source, m, (unsigned)n, fm1_millis());
}

void groove_midi_init(void) { dx_route_init(); }
void groove_midi_poll(void)
{
    uint32_t now = fm1_millis();
    dx_router_poll(&ext, now);
    dx_router_poll(&loc, now);
}
void groove_midi_panic(void) { dx_route_panic(); }
void groove_midi_clock_reset(void) {}

/* The engine calls these from audio code (sequencer MIDI out and clock out).
 * DX747 has no sequencer and no MIDI out, so they stay silent, SRAM-resident,
 * non-blocking and flash-free. */
FM1_AUDIO_CODE unsigned groove_midi_mode(unsigned track) { (void)track; return 0; }
FM1_AUDIO_CODE void groove_midi_note(unsigned track, unsigned note, unsigned velocity) { (void)track; (void)note; (void)velocity; }
FM1_AUDIO_CODE void groove_midi_track_reset(unsigned track) { (void)track; }
#ifdef DX_SMK37
/* The SMK-37 library calls the arpeggiator's note hook too (the FM-1 library does not): DX747 has no arpeggiator. */
FM1_AUDIO_CODE void groove_midi_arp_note(unsigned track, unsigned note, unsigned velocity) { (void)track; (void)note; (void)velocity; }
#endif
FM1_AUDIO_CODE void groove_midi_transport(unsigned owner, uint8_t status) { (void)owner; (void)status; }
FM1_AUDIO_CODE void groove_midi_clock(unsigned owner, unsigned tempo) { (void)owner; (void)tempo; }

/* ---- stand-ins for the stock MIDI settings API (replaced with the stock UI) */

void groove_midi_defaults(struct groove_midi_config *c) { memset(c, 0, sizeof *c); }
int groove_midi_config_valid(const struct groove_midi_config *c) { (void)c; return 1; }
void groove_midi_config_read(struct groove_midi_config *c) { *c = stand_in; }
void groove_midi_config_apply(const struct groove_midi_config *c) { stand_in = *c; }
unsigned groove_midi_setting(unsigned track, unsigned parameter)
{ return parameter == 1 && track < DX_ENGINES ? cfg_active ? cfg_active->priv[track] : 0U : 0U; }
void groove_midi_fader(unsigned track, unsigned value) { (void)track; (void)value; }
int groove_midi_clock_accept(unsigned source, uint8_t status) { (void)source; (void)status; return 0; }
uint32_t groove_midi_dropped(void) { return dx_route_dropped(); }

/* The stock panel posts a raw channel message per key. Until the DX747 screen
 * calls dx_route_key directly, accept those and route them like any keyboard. */
int groove_midi_local(const uint8_t *m, size_t n)
{
    if (dx_safe_boot()) return 0;
    if (!m || !n || n > 3 || m[0] < 0x80 || (n > 1 && m[1] >= 128) || (n > 2 && m[2] >= 128)) return -1;
    if (m[0] >= 0xf0) { if (m[0] == 0xff) dx_route_panic(); return 0; }
    dx_router_event(&loc, DX_SRC_KEYS, m, (unsigned)n, fm1_millis());
    return 0;
}
