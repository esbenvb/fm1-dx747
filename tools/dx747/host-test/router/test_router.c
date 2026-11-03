/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host tests for the DX747 router core and boot recipe (app/dx747/route.inc,
 * boot.inc). The engine is modelled as a recording sink with an optional
 * 63-slot input ring that is drained once per 2.9 ms block and "resets" on
 * overflow, like the sealed engine's ring. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Minimal stand-ins for instrument.h, used by boot.inc. */
enum { GROOVE_TRACKS = 8, GROOVE_DX7 = 0, GROOVE_TRACK_NORMAL = 0, GROOVE_TRACK_PLAY = 1, GROOVE_TRACK_OFF = 2 };
static int mock_role_fail[GROOVE_TRACKS], mock_src_fail[GROOVE_TRACKS];
static int mock_calls[64], mock_ncalls;
int groove_track_role_set(unsigned t, unsigned role)
{ mock_calls[mock_ncalls++] = (int)(t * 10 + 1); return role == GROOVE_TRACK_PLAY ? mock_role_fail[t] : -1; }
int groove_source_set(unsigned t, unsigned type)
{ mock_calls[mock_ncalls++] = (int)(t * 10 + 2); return type == GROOVE_DX7 ? mock_src_fail[t] : -1; }

#include "../../../../app/dx747/route.inc"
#include "../../../../app/dx747/boot.inc"

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct rec { uint8_t m[3]; uint8_t n; };
static struct rec posted[8192];
static unsigned nposted;
static int ring_model, ring_q, ring_overflow, ring_peak;
static int post_fn(const uint8_t *m, unsigned n)
{
    if (nposted < 8192) { memcpy(posted[nposted].m, m, n); posted[nposted].n = (uint8_t)n; ++nposted; }
    if (ring_model) {
        if (ring_q >= 63) { ++ring_overflow; ring_q = 0; return -1; }
        ++ring_q; if (ring_q > ring_peak) ring_peak = ring_q;
    }
    return 0;
}
static struct dx_cfg cfg;
static dx_cfgp cfgp;
static DX_A(uint32_t) epoch;
static struct dx_router ext, loc;

static void reset(void)
{
    dx_cfg_default(&cfg); cfgp = &cfg; epoch = 0; nposted = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) cfg.e[e].rx = 0; /* tests grant channels explicitly */
    ring_model = ring_q = ring_overflow = ring_peak = 0;
    dx_router_init(&ext, &cfgp, &epoch, post_fn);
    dx_router_init(&loc, &cfgp, &epoch, post_fn);
    ext.peer = &loc; loc.peer = &ext;
}
static void ev3(struct dx_router *r, unsigned src, unsigned st, unsigned a, unsigned b, uint32_t now)
{
    uint8_t m[3] = {(uint8_t)st, (uint8_t)a, (uint8_t)b};
    dx_router_event(r, src, m, (st & 0xe0U) == 0xc0U ? 2U : 3U, now);
}
static void non(unsigned src, unsigned ch, unsigned note, uint32_t now) { ev3(&ext, src, 0x90U | ch, note, 100, now); }
static void noff(unsigned src, unsigned ch, unsigned note, uint32_t now) { ev3(&ext, src, 0x80U | ch, note, 0, now); }
static void fresh(void) { nposted = 0; ext.tokens = DX_TOKENS; ext.started = 1; }
static void drain(uint32_t now)
{
    for (int i = 0; i < 4000; ++i) {
        dx_router_poll(&ext, now + (uint32_t)i); dx_router_poll(&loc, now + (uint32_t)i);
        if (ext.pr == ext.pw && loc.pr == loc.pw && ext.need_seq == ext.need_done && loc.need_seq == loc.need_done) break;
    }
}

static unsigned count_on(unsigned priv, int note)
{
    unsigned c = 0;
    for (unsigned i = 0; i < nposted; ++i)
        if (posted[i].m[0] == (0x90U | priv) && (note < 0 || posted[i].m[1] == note)) ++c;
    return c;
}
static unsigned count_off(unsigned priv, int note)
{
    unsigned c = 0;
    for (unsigned i = 0; i < nposted; ++i)
        if (posted[i].m[0] == (0x80U | priv) && (note < 0 || posted[i].m[1] == note)) ++c;
    return c;
}

static void test_basic_and_mask(void)
{
    reset();
    cfg.e[0].rx = (1U << 0) | (1U << 2); /* engine 1 listens on ch 1 and ch 3 */
    cfg.e[1].rx = 1U << 1;               /* engine 2 listens on ch 2 */
    non(DX_SRC_USB, 0, 60, 0);
    CHECK(count_on(cfg.priv[0], 60) == 1);
    non(DX_SRC_USB, 2, 61, 0);
    CHECK(count_on(cfg.priv[0], 61) == 1);
    non(DX_SRC_USB, 1, 62, 0); /* ch 2 belongs to engine 2 only */
    CHECK(count_on(cfg.priv[0], 62) == 0);
    CHECK(count_on(cfg.priv[1], 62) == 1);
    noff(DX_SRC_USB, 0, 60, 1);
    CHECK(count_off(cfg.priv[0], 60) == 1);
    ev3(&ext, DX_SRC_USB, 0x90, 64, 0, 2); /* note on velocity 0 is a note off */
    CHECK(ext.nheld == 2);
    CHECK(ext.act_seen & 1U);
}

static void test_layering_and_split(void)
{
    reset();
    for (unsigned e = 0; e < 3; ++e) cfg.e[e].rx = 1U; /* engines 1-3 all on ch 1 */
    cfg.e[0].lo = 0;  cfg.e[0].hi = 71;  cfg.e[0].shift = 24;  /* C-2..B3 -> +2 oct */
    cfg.e[1].lo = 72; cfg.e[1].hi = 107; cfg.e[1].shift = -12; /* C4..B6 -> -1 oct */
    non(DX_SRC_DIN, 0, 48, 0);   /* in engine 1 and 3 (pad, full) */
    CHECK(count_on(cfg.priv[0], 72) == 1);
    CHECK(count_on(cfg.priv[1], -1) == 0);
    CHECK(count_on(cfg.priv[2], 48) == 1);
    non(DX_SRC_DIN, 0, 84, 0);   /* in engine 2 (-12 = 72) and 3 */
    CHECK(count_on(cfg.priv[1], 72) == 1);
    CHECK(count_on(cfg.priv[2], 84) == 1);
    CHECK(ext.nheld == 4);
    noff(DX_SRC_DIN, 0, 48, 1);  noff(DX_SRC_DIN, 0, 84, 1);
    CHECK(ext.nheld == 0);
    CHECK(count_off(cfg.priv[0], 72) == 1 && count_off(cfg.priv[1], 72) == 1);
    CHECK(count_off(cfg.priv[2], 48) == 1 && count_off(cfg.priv[2], 84) == 1);
}

static void test_note_off_survives_config_change(void)
{
    reset();
    cfg.e[0].rx = 1U; cfg.e[0].shift = 12;
    non(DX_SRC_USB, 0, 60, 0);
    CHECK(count_on(cfg.priv[0], 72) == 1);
    struct dx_cfg next = cfg; next.e[0].shift = -12; next.e[0].rx = 0;
    cfgp = &next;
    noff(DX_SRC_USB, 0, 60, 1);
    CHECK(count_off(cfg.priv[0], 72) == 1); /* released where it started */
    CHECK(ext.nheld == 0);
    cfgp = &cfg;
}

static void test_fold(void)
{
    struct dx_engine_cfg e = {1, 0, 127, 24, DX_FOLD_DROP, 0, 4, 0};
    unsigned out = 99;
    CHECK(dx_map_note(&e, 60, &out) && out == 84);
    CHECK(!dx_map_note(&e, 110, &out));
    e.fold = DX_FOLD_CLIP; CHECK(dx_map_note(&e, 110, &out) && out == 127);
    e.fold = DX_FOLD_WRAP; CHECK(dx_map_note(&e, 110, &out) && out == 122); /* 134 - 12 */
    e.shift = -36; e.fold = DX_FOLD_WRAP; CHECK(dx_map_note(&e, 12, &out) && out == 0);
    CHECK(dx_map_note(&e, 5, &out) && out == 5 - 36 + 36);
    e.fold = DX_FOLD_CLIP; CHECK(dx_map_note(&e, 5, &out) && out == 0);
    e.fold = DX_FOLD_DROP; CHECK(!dx_map_note(&e, 5, &out));
    e.lo = 40; e.hi = 50; e.shift = 0; CHECK(!dx_map_note(&e, 39, &out) && dx_map_note(&e, 40, &out) && dx_map_note(&e, 50, &out) && !dx_map_note(&e, 51, &out));
    e.lo = 60; e.hi = 50; CHECK(!dx_map_note(&e, 55, &out)); /* inverted window plays nothing */
}

static void test_sources_are_independent(void)
{
    reset();
    cfg.e[0].rx = 1U;
    non(DX_SRC_USB, 0, 60, 0);
    non(DX_SRC_DIN, 0, 60, 0); /* same channel and note from another keyboard */
    CHECK(ext.nheld == 2);
    noff(DX_SRC_USB, 0, 60, 1);
    CHECK(ext.nheld == 1 && count_off(cfg.priv[0], 60) == 1);
    noff(DX_SRC_DIN, 0, 60, 2);
    CHECK(ext.nheld == 0 && count_off(cfg.priv[0], 60) == 2);
}

static void test_retrigger(void)
{
    reset();
    cfg.e[0].rx = 1U;
    non(DX_SRC_USB, 0, 60, 0);
    non(DX_SRC_USB, 0, 60, 1);
    CHECK(count_on(cfg.priv[0], 60) == 2 && count_off(cfg.priv[0], 60) == 1 && ext.nheld == 1);
}

static void test_sustain_pedal(void)
{
    reset();
    cfg.e[0].rx = 1U;
    ev3(&ext, DX_SRC_USB, 0xb0, 64, 127, 0);
    non(DX_SRC_USB, 0, 60, 0);
    noff(DX_SRC_USB, 0, 60, 1);
    CHECK(count_off(cfg.priv[0], 60) == 1); /* forwarded at once, engine holds it */
    CHECK(ext.nheld == 1 && ext.held[0].sust == 1); /* still counted in the budget */
    ev3(&ext, DX_SRC_USB, 0xb0, 64, 0, 2);
    CHECK(ext.nheld == 0);
    unsigned pedal_msgs = 0;
    for (unsigned i = 0; i < nposted; ++i) pedal_msgs += posted[i].m[0] == (0xb0U | cfg.priv[0]) && posted[i].m[1] == 64;
    CHECK(pedal_msgs == 2);
}

static void test_controllers_and_ctl_channel(void)
{
    reset();
    cfg.e[0].rx = 1U;            /* engine 1 plays ch 1 */
    cfg.e[1].rx = 1U;            /* engine 2 layered on ch 1 */
    cfg.e[2].rx = 0; cfg.e[2].ctl = 5; /* engine 3 is edited on ch 5 */
    cfg.e[0].ctl = 1;            /* engine 1 control channel equals its play channel */
    ev3(&ext, DX_SRC_USB, 0xb0, 1, 90, 0);      /* mod wheel on ch 1: both engines, once each */
    CHECK(nposted == 2);
    CHECK(posted[0].m[0] == (0xb0U | cfg.priv[0]) && posted[0].m[1] == 1 && posted[0].m[2] == 90);
    fresh();
    ev3(&ext, DX_SRC_USB, 0xb4, 74, 64, 0);     /* ch 5 control channel: engine 3 only */
    CHECK(nposted == 1 && posted[0].m[0] == (0xb0U | cfg.priv[2]));
    fresh();
    ev3(&ext, DX_SRC_USB, 0x94, 60, 100, 0);    /* notes on a control channel are ignored */
    CHECK(nposted == 0);
    ev3(&ext, DX_SRC_USB, 0xc0, 12, 0, 0);      /* program change reaches both ch 1 engines */
    CHECK(nposted == 2 && posted[0].n == 2 && posted[0].m[0] == (0xc0U | cfg.priv[0]) && posted[0].m[1] == 12);
    fresh();
    ev3(&ext, DX_SRC_USB, 0xe0, 0, 80, 0);      /* pitch bend */
    CHECK(nposted == 2 && (posted[0].m[0] & 0xf0U) == 0xe0 && posted[0].m[2] == 80);
    fresh();
    ev3(&ext, DX_SRC_USB, 0xd0, 33, 0, 0);      /* channel pressure is two bytes */
    CHECK(nposted == 2 && posted[0].n == 2);
}

static void test_all_notes_off(void)
{
    reset();
    cfg.e[0].rx = 1U;
    non(DX_SRC_USB, 0, 60, 0); non(DX_SRC_USB, 0, 64, 0);
    non(DX_SRC_DIN, 0, 67, 0);
    ev3(&ext, DX_SRC_USB, 0xb0, 123, 0, 1);
    CHECK(ext.nheld == 1); /* only the DIN note remains */
    CHECK(count_off(cfg.priv[0], 60) == 1 && count_off(cfg.priv[0], 64) == 1 && count_off(cfg.priv[0], 67) == 0);
    unsigned cc123 = 0;
    for (unsigned i = 0; i < nposted; ++i) cc123 += posted[i].m[0] == (0xb0U | cfg.priv[0]) && posted[i].m[1] == 123;
    CHECK(cc123 == 1);
}

static void test_muted_and_missing_engines(void)
{
    reset();
    cfg.e[0].rx = 1U; cfg.e[1].rx = 1U; cfg.e[1].mute = 1; cfg.e[2].rx = 1U; cfg.up &= (uint8_t)~(1U << 2);
    non(DX_SRC_USB, 0, 60, 0);
    CHECK(count_on(cfg.priv[0], 60) == 1 && count_on(cfg.priv[1], 60) == 0 && count_on(cfg.priv[2], 60) == 0);
    CHECK(ext.nheld == 1);
}

static void test_poly_limit_and_pool(void)
{
    reset();
    cfg.e[0].rx = 1U; cfg.e[0].poly = 2;
    non(DX_SRC_USB, 0, 60, 0); non(DX_SRC_USB, 0, 62, 0);
    non(DX_SRC_USB, 0, 64, 0); /* third note: oldest (60) is released first */
    CHECK(count_off(cfg.priv[0], 60) == 1 && ext.nheld == 2 && ext.steals == 1);
    noff(DX_SRC_USB, 0, 60, 1); /* the stolen key coming up must not double release */
    CHECK(count_off(cfg.priv[0], 60) == 1);

    reset();
    cfg.pool = 3;
    for (unsigned e = 0; e < 4; ++e) { cfg.e[e].rx = 1U; cfg.e[e].poly = 8; }
    non(DX_SRC_USB, 0, 60, 0); /* four layers but a pool of three */
    CHECK(ext.nheld == 3 && ext.steals == 1);
    /* the pool counts the other router too */
    reset();
    cfg.pool = 2; cfg.e[0].rx = 1U; cfg.e[0].poly = 8;
    ev3(&loc, DX_SRC_KEYS, 0x90, 60, 100, 0); ev3(&loc, DX_SRC_KEYS, 0x90, 62, 100, 0);
    non(DX_SRC_USB, 0, 64, 0);
    CHECK(ext.nheld + loc.nheld <= 3 && ext.nheld == 1 && ext.steals == 0); /* nothing of its own to steal */
}

static void test_pacing_never_overflows_ring(void)
{
    reset();
    ring_model = 1;
    for (unsigned e = 0; e < 8; ++e) { cfg.e[e].rx = 1U; cfg.e[e].poly = 8; }
    cfg.pool = 200;
    uint32_t now = 0; unsigned blocks_seen = 0; int window_max = 0, window = 0;
    for (int tick = 0; tick < 400; ++tick) {
        now = (uint32_t)tick;
        if (tick == 5) for (unsigned n = 0; n < 6; ++n) non(DX_SRC_USB, 0, 48 + n * 4, now);   /* dense layered chord: 48 note ons */
        if (tick == 7) for (unsigned n = 0; n < 6; ++n) ev3(&loc, DX_SRC_KEYS, 0x90, 50 + n * 4, 100, now); /* and the built-in keys */
        if (tick == 200) for (unsigned n = 0; n < 6; ++n) { noff(DX_SRC_USB, 0, 48 + n * 4, now); ev3(&loc, DX_SRC_KEYS, 0x80, 50 + n * 4, 0, now); }
        dx_router_poll(&ext, now); dx_router_poll(&loc, now);
        window = ring_q > window ? ring_q : window; window_max = window > window_max ? window : window_max;
        unsigned block = (unsigned)((double)tick / 2.9);
        if (block != blocks_seen) { blocks_seen = block; ring_q = 0; window = 0; } /* the engine drained the ring */
    }
    CHECK(ring_overflow == 0);
    CHECK(window_max <= 63);
    CHECK(ext.nheld == 0 && loc.nheld == 0); /* all released */
    CHECK(count_on(cfg.priv[0], -1) == 12 && count_off(cfg.priv[0], -1) == 12);
    CHECK(ext.pr == ext.pw && loc.pr == loc.pw);
}

static void test_coalescing_and_order(void)
{
    reset();
    cfg.e[0].rx = 1U;
    ext.tokens = 0; ext.started = 1; ext.last_ms = 0; /* nothing may be posted yet */
    for (unsigned v = 0; v < 50; ++v) ev3(&ext, DX_SRC_USB, 0xb0, 1, v, 0);   /* a mod wheel sweep */
    non(DX_SRC_USB, 0, 60, 0);
    for (unsigned v = 0; v < 50; ++v) ev3(&ext, DX_SRC_USB, 0xe0, 0, v, 0);   /* then bend */
    noff(DX_SRC_USB, 0, 60, 0);
    /* The entry at the read index is never rewritten (the consumer may be
     * delivering it), so the sweep leaves its first value plus the latest one. */
    CHECK(ext.pw - ext.pr == 5); /* CC first, CC latest, note on, bend, note off */
    drain(10);
    CHECK(nposted == 5);
    CHECK(posted[0].m[1] == 1 && posted[0].m[2] == 0 && posted[1].m[1] == 1 && posted[1].m[2] == 49);
    CHECK((posted[2].m[0] & 0xf0U) == 0x90 && (posted[3].m[0] & 0xf0U) == 0xe0 && posted[3].m[2] == 49 && (posted[4].m[0] & 0xf0U) == 0x80);
}

static void test_queue_full_policy(void)
{
    /* Past the soft limit a note on is refused outright (no entry, nothing to
     * hang), while releases are still accepted. */
    reset();
    cfg.e[0].rx = 0xffff; cfg.e[0].poly = 8; cfg.pool = 200;
    ext.tokens = 0; ext.started = 1; ext.last_ms = 0;
    for (unsigned n = 0; n < 100; ++n) non(DX_SRC_USB, (n % 16), 20 + n, 0);
    CHECK(ext.dropped > 0 && ext.need_seq == ext.need_done);
    CHECK(ext.pw - ext.pr <= DX_PEND_SOFT + 16);
    for (unsigned n = 0; n < 100; ++n) noff(DX_SRC_USB, (n % 16), 20 + n, 0);
    CHECK(ext.nheld == 0);
    drain(10);
    int net[16][128]; memset(net, 0, sizeof net);
    for (unsigned i = 0; i < nposted; ++i) {
        unsigned ch = posted[i].m[0] & 15U, type = posted[i].m[0] & 0xf0U;
        if (type == 0x90) ++net[ch][posted[i].m[1]]; else if (type == 0x80) --net[ch][posted[i].m[1]];
    }
    int bad = 0; for (unsigned ch = 0; ch < 16; ++ch) for (unsigned n = 0; n < 128; ++n) bad += net[ch][n] != 0;
    CHECK(bad == 0);
}

static void test_ring_overflow_recovers(void)
{
    /* Releases are never refused until the ring itself is full; then the
     * consumer sends all-notes-off on every private channel, in budget-sized
     * steps, and the producer forgets the notes that predate it. */
    reset();
    cfg.e[0].rx = 0xffff; cfg.e[0].poly = 8; cfg.pool = 200;
    ext.tokens = 0; ext.started = 1; ext.last_ms = 0;
    non(DX_SRC_USB, 0, 60, 0); non(DX_SRC_USB, 0, 62, 0);
    for (unsigned n = 0; n < DX_PEND_MAX + 8; ++n) ev3(&ext, DX_SRC_USB, 0xb0, 123, 0, 0);
    CHECK(ext.need_seq != ext.need_done);
    drain(100);
    CHECK(ext.need_seq == ext.need_done && ext.pr == ext.pw);
    non(DX_SRC_USB, 0, 70, 200); /* a note that starts after recovery survives it */
    CHECK(ext.nheld == 1 && ext.held[0].note == 70);
    unsigned cc123 = 0;
    for (unsigned i = 0; i < nposted; ++i) cc123 += posted[i].m[1] == 123 && (posted[i].m[0] & 0xf0U) == 0xb0;
    CHECK(cc123 == DX_PEND_MAX - 4 + DX_ENGINES); /* 2 note ons and 2 note offs shared the ring, plus 8 recovery messages */
}

static void test_post_failure_clears_state(void)
{
    reset();
    ring_model = 1; ring_q = 63;
    cfg.e[0].rx = 1U;
    non(DX_SRC_USB, 0, 60, 0);
    CHECK(ext.fail_p == 1 && ext.nheld == 0 && ring_overflow == 1);
}

static void test_panic_epoch(void)
{
    reset();
    cfg.e[0].rx = 1U;
    non(DX_SRC_USB, 0, 60, 0);
    ev3(&ext, DX_SRC_USB, 0xb0, 64, 127, 0);
    CHECK(ext.nheld == 1 && ext.pedal[DX_SRC_USB]);
    ++epoch;
    dx_router_poll(&ext, 1);
    dx_producer_sync(&ext);
    CHECK(ext.nheld == 0 && ext.pedal[DX_SRC_USB] == 0);
    dx_router_release_all(&ext); /* harmless when empty */
    cfg.e[0].rx = 1U;
    non(DX_SRC_USB, 0, 61, 5); non(DX_SRC_USB, 0, 62, 5);
    unsigned before = count_off(cfg.priv[0], -1);
    dx_router_release_all(&ext);
    CHECK(count_off(cfg.priv[0], -1) == before + 2 && ext.nheld == 0);
}

static void test_activity_and_log(void)
{
    reset();
    cfg.e[0].rx = 1U;
    non(DX_SRC_BLE, 6, 60, 1000);                 /* an unrouted channel still counts as activity */
    CHECK(dx_router_active(&ext, 6, 1500, 2000) && !dx_router_active(&ext, 6, 3500, 2000) && !dx_router_active(&ext, 5, 1500, 2000));
    CHECK(ext.act_src[6] == DX_SRC_BLE);
    for (unsigned v = 0; v < 20; ++v) ev3(&ext, DX_SRC_USB, 0xb0, 1, v, 1100 + v); /* repeated CC1 updates one log line */
    CHECK(ext.log_n == 2);
    for (unsigned v = 0; v < 20; ++v) ev3(&ext, DX_SRC_USB, 0xe0, 0, v, 1200);   /* bend is not logged */
    CHECK(ext.log_n == 2);
    ev3(&loc, DX_SRC_KEYS, 0x90, 60, 100, 1300);
    CHECK(loc.act_src[0] == DX_SRC_KEYS && dx_router_active(&loc, 0, 1300, 10));
}

static void test_keys_use_keyboard_channel(void)
{
    reset();
    cfg.kbd_ch = 4; cfg.e[3].rx = 1U << 4; cfg.e[0].rx = 1U;
    dx_router_key(&loc, 60, 1, 0);
    CHECK(count_on(cfg.priv[3], 60) == 1 && count_on(cfg.priv[0], 60) == 0);
    CHECK(posted[0].m[2] == 100); /* fixed velocity like stock */
    dx_router_key(&loc, 60, 0, 1);
    CHECK(count_off(cfg.priv[3], 60) == 1 && loc.nheld == 0);
}

static void test_bad_input_ignored(void)
{
    reset();
    cfg.e[0].rx = 1U;
    uint8_t bad1[3] = {0x90, 200, 100}, bad2[3] = {0x40, 1, 1}, clock[1] = {0xf8}, sysex[3] = {0xf0, 1, 2};
    dx_router_event(&ext, DX_SRC_USB, bad1, 3, 0);
    dx_router_event(&ext, DX_SRC_USB, bad2, 3, 0);
    dx_router_event(&ext, DX_SRC_USB, clock, 1, 0);
    dx_router_event(&ext, DX_SRC_USB, sysex, 3, 0);
    dx_router_event(&ext, 9, bad1, 3, 0);
    dx_router_event(&ext, DX_SRC_USB, (const uint8_t *)0, 3, 0);
    CHECK(nposted == 0 && ext.nheld == 0);
    cfgp = 0; ev3(&ext, DX_SRC_USB, 0x90, 60, 100, 0); CHECK(nposted == 0); cfgp = &cfg; /* no config published yet */
}

static void test_boot_recipe(void)
{
    struct dx_boot b;
    memset(mock_role_fail, 0, sizeof mock_role_fail); memset(mock_src_fail, 0, sizeof mock_src_fail); mock_ncalls = 0;
    CHECK(dx_boot_engines(&b) == 8 && b.up == 0xff);
    CHECK(mock_ncalls == 16);
    for (int t = 0; t < 8; ++t) CHECK(mock_calls[t * 2] == t * 10 + 1 && mock_calls[t * 2 + 1] == t * 10 + 2); /* role first, then source, per track */
    mock_src_fail[5] = -8; mock_role_fail[2] = -8; mock_ncalls = 0;
    CHECK(dx_boot_engines(&b) == 6 && b.up == (0xff & ~(1 << 5) & ~(1 << 2)));
    CHECK(b.err[5] == -8 && b.err[2] == -8 && b.err[0] == 0);
    CHECK(mock_ncalls == 15); /* a failing role set skips that track's source set */
}

/* Random streams with random configuration changes: once every key and pedal
 * is released and the queue drained, every note on posted to an engine must
 * have been balanced by a note off or an all-notes-off. */
static uint32_t rng_state = 12345;
static unsigned rnd(unsigned n) { rng_state = rng_state * 1664525U + 1013904223U; return (rng_state >> 8) % n; }

static void test_fuzz_no_stuck_notes(void)
{
    for (unsigned round = 0; round < 40; ++round) {
        reset();
        static struct dx_cfg alt;
        alt = cfg;
        unsigned held_key[DX_SRC_COUNT][4][128];
        memset(held_key, 0, sizeof held_key);
        uint32_t now = 0;
        for (unsigned step = 0; step < 3000; ++step) {
            now += rnd(3);
            if (rnd(40) == 0) { /* publish a new random configuration */
                for (unsigned e = 0; e < 8; ++e) {
                    alt.e[e].rx = (uint16_t)(rnd(16) | (rnd(4) ? 0 : 1U << 5));
                    alt.e[e].lo = (uint8_t)rnd(60); alt.e[e].hi = (uint8_t)(60 + rnd(68));
                    alt.e[e].shift = (int8_t)((int)rnd(7) * 12 - 36); alt.e[e].fold = (uint8_t)rnd(3);
                    alt.e[e].poly = (uint8_t)(1 + rnd(8)); alt.e[e].mute = (uint8_t)(rnd(10) == 0);
                    alt.e[e].ctl = (uint8_t)rnd(7);
                }
                alt.pool = (uint8_t)(2 + rnd(30)); alt.up = (uint8_t)(0xff & ~(rnd(3) ? 0 : 1U << rnd(8)));
                static struct dx_cfg pub; pub = alt; cfgp = &pub;
            }
            unsigned src = rnd(DX_SRC_COUNT), ch = rnd(4), note = 20 + rnd(100), what = rnd(10);
            struct dx_router *r = src == DX_SRC_KEYS ? &loc : &ext;
            if (what < 4) { ev3(r, src, 0x90U | ch, note, 1 + rnd(127), now); held_key[src][ch][note] = 1; }
            else if (what < 8) { ev3(r, src, 0x80U | ch, note, 0, now); held_key[src][ch][note] = 0; }
            else if (what == 8) ev3(r, src, 0xb0U | ch, 64, rnd(2) ? 127 : 0, now);
            else if (rnd(3) == 0) ev3(r, src, 0xb0U | ch, 123, 0, now);
            else ev3(r, src, 0xe0U | ch, 0, rnd(128), now);
            if (rnd(5) == 0) { dx_router_poll(&ext, now); dx_router_poll(&loc, now); }
        }
        for (unsigned src = 0; src < DX_SRC_COUNT; ++src) for (unsigned ch = 0; ch < 4; ++ch) {
            struct dx_router *r = src == DX_SRC_KEYS ? &loc : &ext;
            for (unsigned n = 0; n < 128; ++n) if (held_key[src][ch][n]) ev3(r, src, 0x80U | ch, n, 0, now);
            ev3(r, src, 0xb0U | ch, 64, 0, now);
        }
        drain(now + 100);
        CHECK(ext.nheld == 0 && loc.nheld == 0);
        int net[16][128]; memset(net, 0, sizeof net);
        for (unsigned i = 0; i < nposted; ++i) {
            unsigned ch = posted[i].m[0] & 15U, type = posted[i].m[0] & 0xf0U;
            if (type == 0x90) ++net[ch][posted[i].m[1]];
            else if (type == 0x80) --net[ch][posted[i].m[1]];
            else if (type == 0xb0 && posted[i].m[1] == 123) memset(net[ch], 0, sizeof net[ch]);
        }
        int bad = 0;
        for (unsigned ch = 0; ch < 16; ++ch) for (unsigned n = 0; n < 128; ++n) bad += net[ch][n] > 0;
        if (bad) { printf("fuzz round %u: %d unbalanced; ext dropped %u fail_p %u fail_c %u need %u/%u pend %u\n", round, bad, ext.dropped, (unsigned)ext.fail_p, (unsigned)ext.fail_c, (unsigned)ext.need_seq, ext.need_done, (unsigned)(ext.pw - ext.pr)); for (unsigned ch = 0; ch < 16; ++ch) for (unsigned n = 0; n < 128; ++n) if (net[ch][n] > 0) printf("  priv ch %u note %u net %d\n", ch, n, net[ch][n]); }
        CHECK(bad == 0);
    }
}

int main(void)
{
    test_basic_and_mask();
    test_layering_and_split();
    test_note_off_survives_config_change();
    test_fold();
    test_sources_are_independent();
    test_retrigger();
    test_sustain_pedal();
    test_controllers_and_ctl_channel();
    test_all_notes_off();
    test_muted_and_missing_engines();
    test_poly_limit_and_pool();
    test_pacing_never_overflows_ring();
    test_coalescing_and_order();
    test_queue_full_policy();
    test_ring_overflow_recovers();
    test_post_failure_clears_state();
    test_panic_epoch();
    test_activity_and_log();
    test_keys_use_keyboard_channel();
    test_bad_input_ignored();
    test_boot_recipe();
    test_fuzz_no_stuck_notes();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
