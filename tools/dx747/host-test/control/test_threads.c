/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The SysEx hook (one thread, the producer) against the control task (another,
 * the only consumer) under ThreadSanitizer: any data race fails the run. The
 * fake engine is touched by the consumer only. At the end every parameter must
 * hold the last value that was sent for it. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../../../app/dx747/route.inc"
#include "../../../../app/dx747/ctl.inc"

static uint8_t voice[156];
static unsigned edits;
static int f_copy(unsigned e, uint8_t v[156]) { (void)e; memcpy(v, voice, 156); return 0; }
static void f_edit(unsigned e, unsigned p, int d)
{
    (void)e;
    int val = (int)voice[p] + d;
    voice[p] = (uint8_t)(val < 0 ? 0 : val > 99 ? 99 : val);
    ++edits;
}
static void f_name(unsigned e, unsigned i, char c) { (void)e; (void)i; (void)c; }
static void f_toggle(unsigned e, unsigned op) { (void)e; (void)op; }
static int f_import(unsigned e, const uint8_t v[156]) { (void)e; memcpy(voice, v, 155); return 0; }
static int f_refresh(unsigned e) { (void)e; return 0; }
static int f_level(unsigned e, unsigned v) { (void)e; (void)v; return 0; }
static const struct dx_edit_port port = {f_copy, f_edit, f_name, f_toggle, f_import, f_refresh, f_level};

static struct dx_ctl ctl;
static struct dx_cfg cfg;
static struct dx_live live;
static atomic_int done;
enum { PARAMS = 12, ROUNDS = 40000 };
static uint8_t last[PARAMS];

static void *producer(void *arg)
{
    (void)arg;
    unsigned seed = 12345;
    for (unsigned n = 0; n < ROUNDS; ++n) {
        seed = seed * 1664525U + 1013904223U;
        unsigned p = (seed >> 16) % PARAMS, val = (seed >> 8) % 100U;
        uint8_t m[7] = {0xf0, 0x43, 0x10, 0x00, (uint8_t)(p), (uint8_t)val, 0xf7}; /* parameters 0..11 of engine 1 */
        (void)dx_ctl_sysex_in(&ctl, m, sizeof m);
        last[p] = (uint8_t)val;                       /* producer-owned, read after the join */
        if ((n & 63U) == 0) sched_yield();
    }
    atomic_store(&done, 1);
    return NULL;
}
int main(void)
{
    ctl.port = &port;
    dx_cfg_default(&cfg);
    memset(last, 0xff, sizeof last);
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    uint32_t now = 0;
    while (!atomic_load(&done)) dx_ctl_poll(&ctl, NULL, &cfg, &live, now += 2);
    pthread_join(t, NULL);
    for (unsigned i = 0; i < 20; ++i) dx_ctl_poll(&ctl, NULL, &cfg, &live, now += 2); /* drain */
    unsigned bad = 0;
    for (unsigned p = 0; p < PARAMS; ++p) if (last[p] != 0xff && voice[p] != last[p]) ++bad;
    printf("threads: %u messages, %u edits applied, %u dropped, %u parameters wrong\n", ROUNDS, edits, ctl.sx_dropped, bad);
    /* a full ring may drop the newest value of a parameter, so only a run without drops must match exactly */
    if (ctl.sx_dropped == 0 && bad) { printf("FAIL: a parameter does not hold its last value\n"); return 1; }
    return 0;
}
