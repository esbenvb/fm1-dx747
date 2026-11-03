/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Two-task stress for the DX747 router under ThreadSanitizer: a producer thread
 * stands in for the MIDI routing task (dx_router_event on the external router),
 * a consumer thread for the control loop (dx_router_poll on it), and the main
 * thread plays the built-in keys through a second router while publishing new
 * configurations. Every note on that reaches the engine must be balanced by a
 * note off once the keys are up and the queues are drained. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

#include "../../../../app/dx747/route.inc"

static _Atomic int net[16][128];
static _Atomic unsigned posted_total;
static int post_fn(const uint8_t *m, unsigned n)
{
    (void)n;
    unsigned type = m[0] & 0xf0U;
    if (type == 0x90) atomic_fetch_add(&net[m[0] & 15][m[1]], 1);
    else if (type == 0x80) atomic_fetch_sub(&net[m[0] & 15][m[1]], 1);
    else if (type == 0xb0 && m[1] == 123) for (unsigned i = 0; i < 128; ++i) atomic_store(&net[m[0] & 15][i], 0); /* the engine releases that track */
    atomic_fetch_add(&posted_total, 1);
    return 0;
}

static struct dx_cfg cfg_a, cfg_b;
static dx_cfgp cfgp;
static DX_A(uint32_t) epoch;
static struct dx_router ext, loc;
static _Atomic int stop;
static uint32_t rng_p = 1, rng_m = 7;
static unsigned rnd(uint32_t *s, unsigned n) { *s = *s * 1664525U + 1013904223U; return (*s >> 8) % n; }
static uint32_t ms(void) { static _Atomic uint32_t t; return atomic_fetch_add(&t, 1) / 20; }

static void *producer(void *arg)
{
    (void)arg;
    unsigned held[16][128]; memset(held, 0, sizeof held);
    for (unsigned i = 0; i < 400000; ++i) {
        unsigned ch = rnd(&rng_p, 6), note = 30 + rnd(&rng_p, 90), what = rnd(&rng_p, 10);
        uint8_t m[3];
        if (what < 5) { m[0] = (uint8_t)(0x90 | ch); m[1] = (uint8_t)note; m[2] = 90; held[ch][note] = 1; dx_router_event(&ext, DX_SRC_USB, m, 3, ms()); }
        else if (what < 9) { m[0] = (uint8_t)(0x80 | ch); m[1] = (uint8_t)note; m[2] = 0; held[ch][note] = 0; dx_router_event(&ext, DX_SRC_USB, m, 3, ms()); }
        else { m[0] = (uint8_t)(0xb0 | ch); m[1] = 1; m[2] = (uint8_t)rnd(&rng_p, 128); dx_router_event(&ext, DX_SRC_USB, m, 3, ms()); }
    }
    for (unsigned ch = 0; ch < 16; ++ch) for (unsigned n = 0; n < 128; ++n) if (held[ch][n]) {
        uint8_t m[3] = {(uint8_t)(0x80 | ch), (uint8_t)n, 0};
        dx_router_event(&ext, DX_SRC_USB, m, 3, ms());
    }
    return 0;
}

static void *consumer(void *arg)
{
    (void)arg;
    while (!atomic_load(&stop)) { dx_router_poll(&ext, ms()); usleep(50); }
    return 0;
}

int main(void)
{
    dx_cfg_default(&cfg_a); dx_cfg_default(&cfg_b);
    for (unsigned e = 0; e < 8; ++e) {
        cfg_a.e[e].rx = (uint16_t)(0x3fU >> (e % 3)); cfg_a.e[e].poly = 6;
        cfg_b.e[e].rx = (uint16_t)(0x3cU << (e % 2)); cfg_b.e[e].shift = (int8_t)((int)(e % 3) * 12 - 12); cfg_b.e[e].lo = 20; cfg_b.e[e].hi = 110;
    }
    cfg_a.pool = cfg_b.pool = 24;
    cfgp = &cfg_a; epoch = 0;
    dx_router_init(&ext, &cfgp, &epoch, post_fn);
    dx_router_init(&loc, &cfgp, &epoch, post_fn);
    ext.peer = &loc; loc.peer = &ext;
    pthread_t pt, ct;
    pthread_create(&pt, 0, producer, 0);
    pthread_create(&ct, 0, consumer, 0);
    /* control task: built-in keys, its own polling, and configuration changes */
    unsigned held[128]; memset(held, 0, sizeof held);
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned note = 36 + rnd(&rng_m, 60);
        if (rnd(&rng_m, 2)) { dx_router_key(&loc, note, 1, ms()); held[note] = 1; } else { dx_router_key(&loc, note, 0, ms()); held[note] = 0; }
        if (i % 7 == 0) dx_router_poll(&loc, ms());
        if (i % 500 == 0) cfgp = (i / 500) % 2 ? &cfg_b : &cfg_a;
    }
    for (unsigned n = 0; n < 128; ++n) if (held[n]) dx_router_key(&loc, n, 0, ms());
    pthread_join(pt, 0);
    usleep(200000); /* let the consumer thread drain the external router */
    atomic_store(&stop, 1);
    pthread_join(ct, 0);
    /* single-threaded again: finish draining both routers */
    for (int i = 0; i < 20000 && (ext.pr != ext.pw || loc.pr != loc.pw || ext.need_seq != ext.need_done || loc.need_seq != loc.need_done); ++i) {
        dx_router_poll(&ext, ms()); dx_router_poll(&loc, ms());
    }
    int bad = 0;
    for (unsigned ch = 0; ch < 16; ++ch) for (unsigned n = 0; n < 128; ++n) bad += net[ch][n] > 0; /* a spare note off is harmless, a missing one hangs a note */
    printf("threads: %u messages posted, %d hanging notes, ext fail %u, dropped %u\n",
           atomic_load(&posted_total), bad, (unsigned)ext.fail_p + (unsigned)ext.fail_c, ext.dropped);
    return bad ? 1 : 0;
}
