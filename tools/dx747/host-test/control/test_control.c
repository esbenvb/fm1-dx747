/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host tests for app/dx747/ctl.inc: the BRIGHT macro, DX7 SysEx edits and the
 * controller conversions. The engine is a fake that keeps one 156-byte voice per
 * engine and clamps edits to the DX7 parameter limits, like groove_fm_edit. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Lets a test run the consumer in the window between a producer finding a queued entry and rewriting it. */
static void (*race_hook)(void *);
#define DX_RACE_POINT(p) do { if (race_hook) race_hook((void *)(p)); } while (0)

#include "../../../../app/dx747/route.inc"
#include "../../../../app/dx747/ctl.inc"

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- the fake engine ---- */
static struct { uint8_t voice[156]; int alive, refuse_import; unsigned edits, imports, names, toggles; } eng[DX_ENGINES];
static void (*edit_hook)(unsigned);   /* runs once, just before the next edit: a program change handled in the audio block */
static unsigned limit_of(unsigned p)
{
    static const uint8_t op_max[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t global_max[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    return p < 126 ? op_max[p % 21] : p < 145 ? global_max[p - 126] : p == 155 ? 63 : 127;
}
static int f_copy(unsigned e, uint8_t v[156]) { if (!eng[e].alive) return -1; memcpy(v, eng[e].voice, 156); return 0; }
static void f_edit(unsigned e, unsigned p, int d)
{
    if (edit_hook) { void (*h)(unsigned) = edit_hook; edit_hook = NULL; h(e); }
    if (!eng[e].alive || p >= 145 || !d) return;
    int val = (int)eng[e].voice[p] + d, lim = (int)limit_of(p);
    eng[e].voice[p] = (uint8_t)(val < 0 ? 0 : val > lim ? lim : val);
    ++eng[e].edits;
}
static void f_name(unsigned e, unsigned i, char c) { if (eng[e].alive && i < 10 && c >= 32 && c <= 126) { eng[e].voice[145 + i] = (uint8_t)c; ++eng[e].names; } }
static void f_toggle(unsigned e, unsigned op) { if (eng[e].alive && op < 6) { eng[e].voice[155] ^= (uint8_t)(1U << (5 - op)); ++eng[e].toggles; } }
static int f_import(unsigned e, const uint8_t v[156])
{
    if (!eng[e].alive || eng[e].refuse_import) return -1;
    for (unsigned i = 0; i < 156; ++i) if (v[i] > limit_of(i)) return -1;   /* the engine rejects the whole voice */
    memcpy(eng[e].voice, v, 156); ++eng[e].imports; return 0;
}
static const struct dx_edit_port port = {f_copy, f_edit, f_name, f_toggle, f_import};

static struct dx_ctl ctl;
static struct dx_cfg cfg;
static struct dx_live live;
static struct dx_ctl_mail mail;

/* A voice with a known algorithm and operator output levels (k = 0 is OP6). */
static void set_voice(unsigned e, unsigned algorithm, const uint8_t levels[6])
{
    memset(eng[e].voice, 0, 156);
    for (unsigned k = 0; k < 6; ++k) eng[e].voice[DX_LEVEL(k)] = levels[k];
    eng[e].voice[134] = (uint8_t)algorithm;
    eng[e].voice[155] = 63;
    eng[e].alive = 1;
}
static void reset(void)
{
    memset(&ctl, 0, sizeof ctl); ctl.port = &port;
    memset(eng, 0, sizeof eng);
    memset(&live, 0, sizeof live);
    memset(&mail, 0, sizeof mail);
    dx_cfg_default(&cfg);
}
static unsigned lvl(unsigned e, unsigned k) { return eng[e].voice[DX_LEVEL(k)]; }

/* ---- carrier table against the engine's own algorithm table ---- */
static void test_carrier_table(void)
{
    FILE *f = fopen("../../../../web/synth/dx7core.c", "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[256];
    unsigned rows = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned x[6]; int n;
        if (sscanf(line, " { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x }, // %d", &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &n) == 7 && n >= 1 && n <= 32) {
            unsigned mask = 0;
            for (unsigned k = 0; k < 6; ++k) if ((x[k] & 3U) == 0) mask |= 1U << k;  /* output bus 0 */
            CHECK(dx_carrier[n - 1] == mask);
            ++rows;
        }
    }
    fclose(f);
    CHECK(rows == 32);
    for (unsigned a = 0; a < 32; ++a) {
        CHECK(dx_carrier[a] & 0x20U);                               /* OP1 always carries */
        CHECK((dx_carrier[a] & ~63U) == 0);
        CHECK((dx_modulators(a) & dx_carrier[a]) == 0 && (dx_modulators(a) | dx_carrier[a]) == 63U);
    }
    CHECK(dx_modulators(31) == 0);                                  /* algorithm 32: no modulators */
    CHECK(dx_modulators(0) == (1U << 0 | 1U << 1 | 1U << 2 | 1U << 4)); /* algorithm 1: OP3 and OP1 carry */
}

/* ---- BRIGHT ---- */
static void test_bright_basic(void)
{
    static const uint8_t base[6] = {50, 60, 0, 80, 70, 90};        /* algorithm 1: k3 and k5 carry, k2 is an off modulator */
    reset();
    set_voice(0, 0, base);
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(lvl(0, 0) == 70 && lvl(0, 1) == 80 && lvl(0, 2) == 0 && lvl(0, 4) == 90);
    CHECK(lvl(0, 3) == 80 && lvl(0, 5) == 90);                      /* carriers: the volume never moves */
    unsigned edits = eng[0].edits;
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(eng[0].edits == edits);                                   /* applying the same offset again writes nothing */
    dx_bright_apply(&ctl, 0, 40, 0);
    CHECK(lvl(0, 0) == 90 && lvl(0, 1) == 99 && lvl(0, 4) == 99);   /* clamped at 99 */
    dx_bright_apply(&ctl, 0, -50, 0);
    CHECK(lvl(0, 0) == 0 && lvl(0, 1) == 10 && lvl(0, 2) == 0 && lvl(0, 4) == 20); /* floors at 0 */
    dx_bright_apply(&ctl, 0, 0, 0);
    for (unsigned k = 0; k < 6; ++k) CHECK(lvl(0, k) == base[k]);   /* back to the voice exactly */
    CHECK(!ctl.br[0].tracked);
    edits = eng[0].edits;
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(eng[0].edits == edits && f_copy(0, (uint8_t[156]){0}) == 0); /* an untouched voice is not even read again */
    dx_bright_apply(&ctl, 7, 10, 0);                                /* an engine that is not a DX7 */
    dx_bright_apply(&ctl, 9, 10, 0);                                /* out of range */
    CHECK(eng[7].edits == 0);
}
static void test_bright_voice_changes(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90}, b[6] = {10, 20, 30, 40, 55, 65};
    reset();
    set_voice(0, 0, a);
    dx_bright_apply(&ctl, 0, 20, 0);
    /* a program change: the engine loads another voice; the macro notices and starts from it */
    set_voice(0, 0, b);
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(lvl(0, 0) == 30 && lvl(0, 1) == 40 && lvl(0, 2) == 50 && lvl(0, 4) == 75);
    dx_bright_apply(&ctl, 0, 0, 0);
    for (unsigned k = 0; k < 6; ++k) CHECK(lvl(0, k) == b[k]);
    /* a new voice whose levels equal the brightened ones is only seen with force */
    set_voice(0, 0, a);
    dx_bright_apply(&ctl, 0, 20, 0);                                /* levels 70 80 0 80 90 90 */
    static const uint8_t same[6] = {70, 80, 0, 80, 90, 90};
    set_voice(0, 0, same);
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(lvl(0, 0) == 70);                                         /* indistinguishable from our own work */
    dx_bright_apply(&ctl, 0, 20, 1);
    CHECK(lvl(0, 0) == 70 && lvl(0, 4) == 90);                      /* force is ignored: the voice is byte for byte the one the macro left */
    eng[0].voice[145] = 'X';                                        /* a replaced voice with the same levels differs somewhere else */
    dx_bright_apply(&ctl, 0, 20, 1);
    CHECK(lvl(0, 0) == 90 && lvl(0, 1) == 99 && lvl(0, 4) == 99);   /* force: those are the voice's own levels */
    /* an edit of a modulator from elsewhere becomes the new base */
    set_voice(0, 0, a);
    dx_bright_apply(&ctl, 0, 10, 0);                                /* 60 70 0 80 80 90 */
    eng[0].voice[DX_LEVEL(1)] = 30;
    dx_bright_apply(&ctl, 0, 10, 0);
    CHECK(lvl(0, 1) == 40 && lvl(0, 0) == 60);
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(lvl(0, 1) == 30 && lvl(0, 0) == 50);
    /* an algorithm change: the operators that stop being modulators get their own level back */
    set_voice(0, 0, a);
    dx_bright_apply(&ctl, 0, 20, 0);                                /* algorithm 1: k1 is a modulator, now 80 */
    eng[0].voice[134] = 4;                                          /* algorithm 5: k1 (OP5) carries */
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(lvl(0, 1) == 60);                                         /* back to its own level */
    CHECK(lvl(0, 0) == 70 && lvl(0, 4) == 90 && lvl(0, 2) == 0);    /* k0, k2 and k4 modulate in algorithm 5 */
    dx_bright_apply(&ctl, 0, 0, 0);
    for (unsigned k = 0; k < 6; ++k) CHECK(lvl(0, k) == a[k]);
}
static void test_bright_export_fix(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    uint8_t out[156];
    reset();
    set_voice(0, 0, a);
    dx_bright_apply(&ctl, 0, 20, 0);
    memcpy(out, eng[0].voice, 156);
    dx_bright_export_fix(&ctl, 0, out);
    for (unsigned k = 0; k < 6; ++k) CHECK(out[DX_LEVEL(k)] == a[k]);   /* an exported voice carries its own levels */
    CHECK(eng[0].voice[DX_LEVEL(0)] == 70);                              /* the live voice is untouched */
    memcpy(out, eng[0].voice, 156); out[DX_LEVEL(1)] = 33;               /* a level this macro did not write stays */
    dx_bright_export_fix(&ctl, 0, out);
    CHECK(out[DX_LEVEL(1)] == 33 && out[DX_LEVEL(0)] == 50);
    memcpy(out, eng[0].voice, 156); out[134] = 9;                        /* another algorithm: not ours to fix */
    dx_bright_export_fix(&ctl, 0, out);
    CHECK(out[DX_LEVEL(0)] == 70);
    dx_bright_apply(&ctl, 0, 0, 0);
    memcpy(out, eng[0].voice, 156);
    dx_bright_export_fix(&ctl, 0, out);
    CHECK(!memcmp(out, eng[0].voice, 156));                              /* nothing tracked: unchanged */
    dx_bright_export_fix(&ctl, 12, out);
}

/* ---- SysEx ---- */
static int pc(unsigned dev, unsigned param, unsigned value)
{
    uint8_t m[7] = {0xf0, 0x43, (uint8_t)(0x10 | dev), (uint8_t)(param >> 7), (uint8_t)(param & 127), (uint8_t)value, 0xf7};
    return dx_ctl_sysex_in(&ctl, m, sizeof m);
}
static void voice_dump(uint8_t m[163], unsigned dev, const uint8_t voice[155])
{
    unsigned sum = 0;
    static const uint8_t head[6] = {0xf0, 0x43, 0x00, 0x00, 0x01, 0x1b};
    memcpy(m, head, 6); m[2] = (uint8_t)dev;
    memcpy(m + 6, voice, 155);
    for (unsigned i = 0; i < 155; ++i) sum += voice[i];
    m[161] = (uint8_t)((0U - sum) & 127U); m[162] = 0xf7;
}
static void test_sysex_parse(void)
{
    reset();
    CHECK(pc(0, 134, 4) == 1 && ctl.sx_w == 1);
    CHECK(((ctl.sx[0] >> 4) & 255U) == 134 && ((ctl.sx[0] >> 12) & 127U) == 4 && (ctl.sx[0] & 15U) == 0);
    CHECK(pc(11, 155, 62) == 1 && ctl.sx_w == 2 && (ctl.sx[1] & 15U) == 11);
    /* not ours, or malformed */
    uint8_t m[7] = {0xf0, 0x43, 0x10, 0x00, 0x10, 0x32, 0xf7};
    CHECK(dx_ctl_sysex_in(&ctl, NULL, 7) == 0);
    m[1] = 0x41; CHECK(dx_ctl_sysex_in(&ctl, m, 7) == 0); m[1] = 0x43;      /* another maker */
    m[6] = 0xf6; CHECK(dx_ctl_sysex_in(&ctl, m, 7) == 0); m[6] = 0xf7;      /* not terminated */
    m[2] = 0x20; CHECK(dx_ctl_sysex_in(&ctl, m, 7) == 0); m[2] = 0x10;      /* not a parameter change */
    m[3] = 0x08; CHECK(dx_ctl_sysex_in(&ctl, m, 7) == 0); m[3] = 0x00;      /* a function parameter group */
    CHECK(ctl.sx_w == 2);
    m[4] = 0x90; CHECK(dx_ctl_sysex_in(&ctl, m, 7) == 1 && ctl.sx_w == 2); m[4] = 0x10; /* 8-bit data: consumed, dropped */
    CHECK(pc(0, 156, 1) == 1 && ctl.sx_w == 2);                            /* beyond parameter 155 */
    uint8_t long_msg[9] = {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x00, 0x00, 0xf7};
    CHECK(dx_ctl_sysex_in(&ctl, long_msg, 9) == 0);                        /* XG style message: left alone */
    /* the entry at the read index may be in use, so it is never rewritten: the next value of that parameter queues behind it */
    reset();
    CHECK(pc(0, 16, 10) && pc(0, 16, 30));
    CHECK(ctl.sx_w == 2 && ((ctl.sx[0] >> 12) & 127U) == 10 && ((ctl.sx[1] >> 12) & 127U) == 30);
    /* behind it, the newest value of a parameter replaces the one already queued */
    reset();
    CHECK(pc(0, 16, 10) && pc(0, 17, 20) && pc(0, 17, 40) && pc(1, 17, 50) && pc(0, 17, 60));
    CHECK(ctl.sx_w == 3 && ((ctl.sx[1] >> 12) & 127U) == 60 && ((ctl.sx[2] >> 12) & 127U) == 50 && (ctl.sx[2] & 15U) == 1);
    /* a full ring drops the newest and counts it */
    reset();
    for (unsigned i = 0; i < DX_SX_RING; ++i) CHECK(pc(i & 15U, i / 16U * 20U + 1U, 1) == 1);
    CHECK(ctl.sx_w - ctl.sx_r == DX_SX_RING);
    CHECK(pc(5, 99, 1) == 1 && ctl.sx_dropped == 1 && ctl.sx_w - ctl.sx_r == DX_SX_RING);
    /* voice dumps */
    uint8_t voice[155], dump[163];
    for (unsigned i = 0; i < 155; ++i) voice[i] = (uint8_t)(i % 50);
    reset();
    voice_dump(dump, 3, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1 && ctl.dump_w == 1 && ctl.dumps[0].dev == 3 && ctl.dumps[0].at == 0 && !memcmp(ctl.dumps[0].data, voice, 155));
    voice[0] = 7; voice_dump(dump, 4, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1 && ctl.dump_w == 2 && ctl.dumps[1].dev == 4 && ctl.dumps[1].data[0] == 7 && ctl.sx_dropped == 0); /* a pool, not one slot */
    reset();
    CHECK(pc(0, 16, 1) && pc(0, 17, 0));
    voice_dump(dump, 0, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1 && ctl.dumps[0].at == 2);  /* it remembers the parameter changes queued before it */
    reset();
    for (unsigned i = 0; i < DX_DUMPS; ++i) { voice_dump(dump, i, voice); CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1); }
    CHECK(ctl.dump_w - ctl.dump_r == DX_DUMPS && ctl.sx_dropped == 0);
    voice_dump(dump, 9, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1 && ctl.sx_dropped == 1 && ctl.dump_w - ctl.dump_r == DX_DUMPS); /* a full pool drops the newest, counted */
    reset();
    voice_dump(dump, 0, voice); dump[161] ^= 1;
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1 && ctl.dump_w == 0);   /* bad checksum: refused */
    voice_dump(dump, 0, voice); dump[50] = 0x90;
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1 && ctl.dump_w == 0);   /* 8-bit data: refused */
    voice_dump(dump, 0, voice); dump[3] = 9;
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 0);                      /* format 9 is the bank dump: not this frame */
}
/* A queued parameter is replaced by a newer value while the control task is applying it:
 * the newer value must still be applied, behind the old one. */
static void race_poll_ctl(void *p) { race_hook = NULL; dx_ctl_poll((struct dx_ctl *)p, NULL, &cfg, &live, 9000); }
static void test_sysex_coalesce_race(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    reset();
    set_voice(0, 0, a);
    CHECK(pc(0, 18, 5) && pc(0, 19, 20));
    race_hook = race_poll_ctl;
    CHECK(pc(0, 19, 99) == 1);                                      /* finds parameter 19 queued; the control task applies both first */
    CHECK(race_hook == NULL);
    CHECK(eng[0].voice[19] == 20 && eng[0].voice[18] == 5);         /* what the consumer saw before the rewrite */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 9001);
    CHECK(eng[0].voice[19] == 99);                                  /* the newer value was not lost */
    CHECK(ctl.sx_w == ctl.sx_r && ctl.sx_dropped == 0);
    /* an entry the consumer has not reached is replaced in place */
    reset();
    set_voice(0, 0, a);
    CHECK(pc(0, 18, 5) && pc(0, 19, 20) && pc(0, 19, 99));
    CHECK(ctl.sx_w - ctl.sx_r == 2);
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 9100);
    CHECK(eng[0].voice[19] == 99 && eng[0].edits == 2);
}
static void test_targets(void)
{
    reset();
    /* default: engine e listens on channel e + 1 and has no control channel */
    for (unsigned d = 0; d < 8; ++d) CHECK(dx_sysex_targets(&cfg, d) == (1U << d));
    CHECK(dx_sysex_targets(&cfg, 9) == 0 && dx_sysex_targets(&cfg, 15) == 0);
    cfg.e[1].rx |= 1U << 0;                                         /* engines 1 and 2 are layered on channel 1 */
    CHECK(dx_sysex_targets(&cfg, 0) == 0);                          /* ambiguous: ignored */
    cfg.e[0].ctl = 1;                                               /* engine 1 gets channel 1 as its control channel */
    CHECK(dx_sysex_targets(&cfg, 0) == 1U);                         /* explicit control channel wins */
    CHECK(dx_sysex_targets(&cfg, 1) == 1U << 1);                    /* engine 2 is still alone on channel 2 */
    cfg.e[2].ctl = 1;
    CHECK(dx_sysex_targets(&cfg, 0) == (1U << 0 | 1U << 2));        /* a shared control channel edits both */
    cfg.e[3].ctl = 12;                                              /* engine 4 has its own control channel: its play channel no longer edits it */
    CHECK(dx_sysex_targets(&cfg, 3) == 0 && dx_sysex_targets(&cfg, 11) == (1U << 3));
    cfg.up = 0xfb;                                                  /* engine 3 does not exist */
    CHECK(dx_sysex_targets(&cfg, 0) == 1U);
}
static void test_sysex_apply(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    reset();
    set_voice(0, 0, a); set_voice(1, 0, a);
    CHECK(pc(0, 134, 4) && pc(0, 16, 77) && pc(0, 18, 5) && pc(1, 16, 11));
    unsigned flags = dx_ctl_poll(&ctl, NULL, &cfg, &live, 1000);
    CHECK(flags == 0);
    CHECK(eng[0].voice[134] == 4 && eng[0].voice[16] == 77 && eng[0].voice[18] == 5 && eng[1].voice[16] == 11);
    CHECK(eng[1].voice[134] == 0);                                  /* only the addressed engine */
    CHECK(pc(0, 16, 120) && pc(0, 126 + 8, 40));                    /* beyond the limit: the engine clamps */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1001);
    CHECK(eng[0].voice[16] == 99 && eng[0].voice[134] == 31);       /* algorithm limit is 31 */
    CHECK(pc(0, 145, 'H') && pc(0, 146, 'I') && pc(0, 147, 5));     /* name characters, the last one is not printable */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1002);
    CHECK(eng[0].voice[145] == 'H' && eng[0].voice[146] == 'I' && eng[0].voice[147] == 0 && eng[0].names == 2);   /* the non-printable one is refused */
    CHECK(pc(0, 155, 0x3e));                                        /* OP6 off */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1003);
    CHECK(eng[0].voice[155] == 0x3e && eng[0].toggles == 1);
    CHECK(pc(0, 155, 0x01));                                        /* only OP6 on: UI operators 1..5 toggle, 6 does not */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1004);
    CHECK(eng[0].voice[155] == 0x01 && eng[0].toggles == 7);   /* 1 before, then OP1..OP5 off and OP6 on */
    CHECK(pc(0, 155, 0x01));
    unsigned toggles = eng[0].toggles;
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1005);
    CHECK(eng[0].toggles == toggles);                               /* already as asked */
    /* a device nothing listens on edits nothing */
    unsigned edits = eng[0].edits + eng[1].edits;
    CHECK(pc(12, 16, 3));
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1006);
    CHECK(eng[0].edits + eng[1].edits == edits);
    /* at most DX_SX_BATCH entries per poll */
    reset();
    set_voice(0, 0, a);
    static const uint8_t rate_params[10] = {0, 1, 2, 3, 4, 5, 6, 7, 21, 22};   /* EG rates and levels, limit 99 */
    for (unsigned i = 0; i < 10; ++i) CHECK(pc(0, rate_params[i], 3 + i));
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 2000);
    CHECK(ctl.sx_w - ctl.sx_r == 10 - DX_SX_BATCH);
    for (unsigned i = 0; i < DX_SX_BATCH; ++i) CHECK(eng[0].voice[rate_params[i]] == 3 + i);
    CHECK(eng[0].voice[rate_params[DX_SX_BATCH]] == 0);                          /* the rest waits for the next poll */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 2001);
    CHECK(ctl.sx_w == ctl.sx_r);
    for (unsigned i = 0; i < 10; ++i) CHECK(eng[0].voice[rate_params[i]] == 3 + i);
    /* a voice dump replaces the voice, with all operators on, and re-bases BRIGHT */
    reset();
    set_voice(0, 0, a);
    live.bright[0] = 20;
    dx_bright_apply(&ctl, 0, 20, 0);
    uint8_t voice[155] = {0}, dump[163];
    for (unsigned k = 0; k < 6; ++k) voice[DX_LEVEL(k)] = (uint8_t)(70 + k);
    voice[134] = 0;
    voice_dump(dump, 0, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1);
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 3000);
    CHECK(eng[0].imports == 1 && eng[0].voice[155] == 63 && ctl.dump_w == ctl.dump_r);
    CHECK(lvl(0, 0) == 90 && lvl(0, 1) == 91 && lvl(0, 2) == 92 && lvl(0, 4) == 94 && lvl(0, 3) == 73 && lvl(0, 5) == 75);
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 3001);
    CHECK(eng[0].imports == 1);                                     /* applied once */
    /* a dump for a device nothing listens on is dropped, not kept */
    voice_dump(dump, 12, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1);
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 3002);
    CHECK(eng[0].imports == 1 && ctl.dump_w == ctl.dump_r);
}

/* ---- controllers ---- */
static void test_controllers(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    CHECK(dx_cc_to_offset(64) == 0 && dx_cc_to_offset(127) == 50 && dx_cc_to_offset(0) == -50 && dx_cc_to_offset(1) == -50);
    for (unsigned v = 1; v < 128; ++v) CHECK(dx_cc_to_offset(v) >= dx_cc_to_offset(v - 1));
    CHECK(dx_cc_to_hz(0) == 20 && dx_cc_to_hz(127) >= 19000 && dx_cc_to_hz(127) <= 20000 && dx_cc_to_hz(64) > 500 && dx_cc_to_hz(64) < 800);
    for (unsigned v = 1; v < 128; ++v) CHECK(dx_cc_to_hz(v) > dx_cc_to_hz(v - 1) - 1U && dx_cc_to_hz(v) >= dx_cc_to_hz(v - 1));
    reset();
    set_voice(0, 0, a);
    dx_mail_put(&mail, DX_MAIL_BRIGHT + 0, 95);
    dx_mail_put(&mail, DX_MAIL_PAN + 1, 127);
    dx_mail_put(&mail, DX_MAIL_EXPR + 2, 33);
    unsigned flags = dx_ctl_poll(&ctl, &mail, &cfg, &live, 100);
    CHECK(flags == 0 && live.bright[0] == 24 && live.pan[1] == 50 && live.expr[2] == 33);
    CHECK(lvl(0, 0) == 74 && lvl(0, 1) == 84 && lvl(0, 4) == 94 && lvl(0, 3) == 80);  /* applied in the same poll */
    dx_mail_put(&mail, DX_MAIL_BRIGHT + 0, 64);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 101);
    CHECK(live.bright[0] == 0 && lvl(0, 0) == 50 && lvl(0, 1) == 60);
    /* master filter: an off filter is turned into a low-pass by a sweep; the type CC chooses explicitly */
    CHECK(live.filter_type == DX_FILTER_OFF);
    dx_mail_put(&mail, DX_MAIL_CUTOFF, 64);
    flags = dx_ctl_poll(&ctl, &mail, &cfg, &live, 102);
    CHECK(flags == DX_CTL_FILTER && live.filter_type == DX_FILTER_LP && live.filter_hz == dx_cc_to_hz(64));
    dx_mail_put(&mail, DX_MAIL_TYPE, 70);                            /* 70 >> 5 = 2: high-pass */
    dx_mail_put(&mail, DX_MAIL_RES, 90);
    flags = dx_ctl_poll(&ctl, &mail, &cfg, &live, 103);
    CHECK(flags == DX_CTL_FILTER && live.filter_type == DX_FILTER_HP && live.filter_res == 90);
    dx_mail_put(&mail, DX_MAIL_TYPE, 0);                             /* explicitly off, and a resonance move afterwards turns it back on */
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 104);
    CHECK(live.filter_type == DX_FILTER_OFF);
    dx_mail_put(&mail, DX_MAIL_RES, 10);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 105);
    CHECK(live.filter_type == DX_FILTER_LP);
    dx_mail_put(&mail, DX_MAIL_TYPE, 127);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 106);
    CHECK(live.filter_type == DX_FILTER_BP);
    CHECK(dx_ctl_poll(&ctl, &mail, &cfg, &live, 107) == 0);          /* nothing pending: nothing to do */
}
static void test_program_change_and_scan(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    reset();
    set_voice(0, 0, a);
    live.bright[0] = 20;
    dx_bright_apply(&ctl, 0, 20, 0);
    static const uint8_t same[6] = {70, 80, 0, 80, 90, 90};         /* a new voice that equals the brightened levels */
    set_voice(0, 0, same);
    eng[0].voice[145] = 'N';                                         /* another voice: it differs somewhere besides its levels */
    dx_mail_put(&mail, DX_MAIL_PC + 0, 0);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1000);
    CHECK(lvl(0, 0) == 70);                                          /* the engine may not have loaded the voice yet: wait */
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1000 + DX_PC_SETTLE_MS - 1);
    CHECK(lvl(0, 0) == 70);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1000 + DX_PC_SETTLE_MS);
    CHECK(lvl(0, 0) == 90 && lvl(0, 1) == 99);                       /* now it starts from the new voice */
    /* a voice that changed with nothing reporting it is found by the periodic scan */
    reset();
    set_voice(0, 0, a);
    live.bright[0] = 10;
    dx_bright_apply(&ctl, 0, 10, 0);
    static const uint8_t other[6] = {20, 25, 30, 35, 40, 45};
    set_voice(0, 0, other);
    ctl.scan_ms = 4990;
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 5000);
    CHECK(lvl(0, 0) == 20);                                          /* scan not due... */
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 4990 + DX_BRIGHT_SCAN_MS);
    CHECK(lvl(0, 0) == 30 && lvl(0, 1) == 35);                       /* ...found after the interval */
}

/* ---- regressions found by the independent review ---- */
static void queue_dump(unsigned dev, const uint8_t voice[155])
{
    uint8_t dump[163];
    voice_dump(dump, dev, voice);
    CHECK(dx_ctl_sysex_in(&ctl, dump, sizeof dump) == 1);
}
static void drain(unsigned polls, uint32_t t0)
{
    for (unsigned i = 0; i < polls; ++i) dx_ctl_poll(&ctl, NULL, &cfg, &live, t0 + i);
}
static void test_sysex_order(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    uint8_t voice[155];
    memset(voice, 0, sizeof voice);
    for (unsigned k = 0; k < 6; ++k) voice[DX_LEVEL(k)] = 70;
    /* a parameter change that follows a dump lands after it, even within one poll */
    reset(); set_voice(0, 0, a);
    queue_dump(0, voice);
    CHECK(pc(0, 16, 11));
    drain(1, 100);
    CHECK(lvl(0, 0) == 11 && lvl(0, 1) == 70);
    /* more changes than one poll takes, then a dump: the dump replaces them, not the other way round */
    reset(); set_voice(0, 0, a);
    for (unsigned i = 0; i < 10; ++i) CHECK(pc(0, i, 5));
    memset(voice, 0, sizeof voice); voice[7] = 40;
    queue_dump(0, voice);
    drain(1, 100);
    CHECK(eng[0].imports == 0);                                     /* older changes are still being applied */
    drain(2, 101);
    CHECK(eng[0].imports == 1 && eng[0].voice[7] == 40 && eng[0].voice[0] == 0 && eng[0].voice[5] == 0);
    /* a dump, then more changes than one poll takes: the changes win */
    reset(); set_voice(0, 0, a);
    memset(voice, 0, sizeof voice); for (unsigned i = 0; i < 10; ++i) voice[i] = 77;
    queue_dump(0, voice);
    for (unsigned i = 0; i < 10; ++i) CHECK(pc(0, i, 5));
    drain(4, 100);
    for (unsigned i = 0; i < 10; ++i) CHECK(eng[0].voice[i] == 5);
    CHECK(eng[0].imports == 1);
    /* a newer value of a parameter must not be merged into an entry queued before the dump */
    reset(); set_voice(0, 0, a);
    memset(voice, 0, sizeof voice); voice[3] = 60;
    CHECK(pc(0, 50, 5) && pc(0, 3, 9));
    queue_dump(0, voice);
    CHECK(pc(0, 3, 20));
    CHECK(ctl.sx_w - ctl.sx_r == 3);
    drain(3, 100);
    CHECK(eng[0].voice[3] == 20 && eng[0].imports == 1 && ctl.sx_r == ctl.sx_w);
}
static void test_dump_burst(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    uint8_t voice[155];
    /* one dump for each of the eight engines inside one control period: every engine gets its voice */
    reset();
    for (unsigned e = 0; e < DX_ENGINES; ++e) set_voice(e, 0, a);
    for (unsigned e = 0; e < DX_ENGINES; ++e) { memset(voice, 0, sizeof voice); voice[0] = (uint8_t)(e + 1); queue_dump(e, voice); }
    drain(6, 100);
    for (unsigned e = 0; e < DX_ENGINES; ++e) CHECK(eng[e].imports == 1 && eng[e].voice[0] == e + 1);
    CHECK(ctl.sx_dropped == 0 && ctl.dump_w == ctl.dump_r);
    /* what the engine would refuse (bytes over a parameter's limit) is clamped instead of refused whole */
    reset(); set_voice(0, 0, a);
    memset(voice, 0, sizeof voice); voice[16] = 120; voice[134] = 40; voice[11] = 9; voice[145] = 'Z';
    queue_dump(0, voice);
    drain(1, 100);
    CHECK(eng[0].imports == 1 && eng[0].voice[16] == 99 && eng[0].voice[134] == 31 && eng[0].voice[11] == 3 && eng[0].voice[145] == 'Z');
}
static void test_bright_force_guard(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90}, b[6] = {28, 29, 35, 40, 41, 42};
    uint8_t voice[155];
    memset(voice, 0, sizeof voice);
    /* an import the engine refused leaves the brightened voice in place: the offset must not be added twice */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 20;
    dx_bright_apply(&ctl, 0, 20, 0);
    eng[0].refuse_import = 1;
    queue_dump(0, voice);
    drain(3, 100);
    CHECK(eng[0].imports == 0 && lvl(0, 0) == 70 && lvl(0, 1) == 80 && lvl(0, 4) == 90);
    dx_bright_apply(&ctl, 0, 0, 0);
    for (unsigned k = 0; k < 6; ++k) CHECK(lvl(0, k) == a[k]);
    /* a program change, then BRIGHT activity inside the settle window: the new voice is brightened exactly once */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 24;
    dx_bright_apply(&ctl, 0, 24, 0);
    set_voice(0, 0, b);                                              /* the engine has already loaded the new voice */
    dx_mail_put(&mail, DX_MAIL_PC + 0, 0);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1000);
    dx_mail_put(&mail, DX_MAIL_BRIGHT + 0, 95);                      /* a controller moves while the program change settles */
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1006);
    CHECK(lvl(0, 0) == 28 && lvl(0, 1) == 29);                       /* held: nothing was done to the new voice yet */
    ctl.scan_ms = 1000;
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 1000 + DX_BRIGHT_SCAN_MS - 1);  /* the scan fires while waiting? no: the wait is over by now */
    CHECK(lvl(0, 0) == 52 && lvl(0, 1) == 53 && lvl(0, 2) == 59);    /* 28 29 35 plus 24, once */
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(lvl(0, 0) == 28 && lvl(0, 1) == 29 && lvl(0, 2) == 35);    /* and back to the voice's own levels */
    /* a program change the engine ignored: the voice is still the brightened one, and stays so */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 20;
    dx_bright_apply(&ctl, 0, 20, 0);
    dx_mail_put(&mail, DX_MAIL_PC + 0, 0);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 2000);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 2000 + DX_PC_SETTLE_MS);
    CHECK(lvl(0, 0) == 70 && lvl(0, 1) == 80 && lvl(0, 4) == 90);
    dx_bright_apply(&ctl, 0, 0, 0);
    for (unsigned k = 0; k < 6; ++k) CHECK(lvl(0, k) == a[k]);
}
static void test_sysex_level_under_bright(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    /* the editor shows the voice's own level: setting it to what the macro wrote means a new base, not "no change" */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 24;
    dx_bright_apply(&ctl, 0, 24, 0);                                 /* OP6 plays 74 */
    CHECK(lvl(0, 0) == 74);
    CHECK(pc(0, 16, 74));
    drain(1, 100);
    CHECK(lvl(0, 0) == 98);                                          /* the voice's own level is now 74 */
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(lvl(0, 0) == 74);
    /* under a clamping offset: an edit to 99 and an edit to 0 are not lost */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 50;
    dx_bright_apply(&ctl, 0, 50, 0);                                 /* 99 (clamped) */
    CHECK(pc(0, 16, 99));
    drain(1, 100);
    CHECK(lvl(0, 0) == 99);
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(lvl(0, 0) == 99);                                          /* the base became 99 */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 50;
    dx_bright_apply(&ctl, 0, 50, 0);
    CHECK(pc(0, 16, 0));
    drain(1, 100);
    CHECK(lvl(0, 0) == 0);                                           /* off stays off under the offset */
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(lvl(0, 0) == 0);
    /* a carrier's level is edited directly: BRIGHT never owned it */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 20;
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(pc(0, DX_LEVEL(3), 55));
    drain(1, 100);
    CHECK(lvl(0, 3) == 55 && lvl(0, 0) == 70);
}
static void test_sysex_after_program_change(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90}, b[6] = {10, 20, 30, 40, 55, 65};
    /* an edit that follows a program change lands on the new voice, not on the one that is about to be replaced */
    reset(); set_voice(0, 0, a);
    dx_mail_put(&mail, DX_MAIL_PC + 0, 0);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1000);
    CHECK(pc(0, 18, 9));
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1005);
    CHECK(eng[0].voice[18] == 0);                                    /* waiting for the voice to load */
    set_voice(0, 0, b);                                              /* the engine loads it */
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 1000 + DX_PC_SETTLE_MS);
    CHECK(eng[0].voice[18] == 9 && lvl(0, 0) == 10);
    /* other engines are not held up by it... only the ones that are loading */
    reset(); set_voice(0, 0, a); set_voice(1, 0, a);
    dx_mail_put(&mail, DX_MAIL_PC + 1, 0);
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 2000);
    CHECK(pc(0, 18, 7));
    dx_ctl_poll(&ctl, &mail, &cfg, &live, 2001);
    CHECK(eng[0].voice[18] == 7);
}
static void swap_in_b(unsigned e)
{
    static const uint8_t b[6] = {10, 20, 30, 40, 55, 65};
    set_voice(e, 0, b);
    eng[e].voice[145] = 'B';
}
static void test_bright_race_with_program_change(void)
{
    static const uint8_t a[6] = {50, 60, 0, 80, 70, 90};
    /* the engine loads another voice between the copy and the edits: the steps that landed on it are taken back */
    reset(); set_voice(0, 0, a);
    live.bright[0] = 20;
    edit_hook = swap_in_b;
    dx_bright_apply(&ctl, 0, 20, 0);
    CHECK(lvl(0, 0) == 10 && lvl(0, 1) == 20 && lvl(0, 2) == 30 && lvl(0, 4) == 55);   /* b's own levels */
    CHECK(!ctl.br[0].tracked && (ctl.dirty & 1U));
    dx_ctl_poll(&ctl, NULL, &cfg, &live, 100);                       /* starts again from the voice that is there */
    CHECK(lvl(0, 0) == 30 && lvl(0, 1) == 40 && lvl(0, 2) == 50 && lvl(0, 4) == 75);
    dx_bright_apply(&ctl, 0, 0, 0);
    CHECK(lvl(0, 0) == 10 && lvl(0, 1) == 20 && lvl(0, 2) == 30 && lvl(0, 4) == 55);
}

int main(void)
{
    test_carrier_table();
    test_bright_basic();
    test_bright_voice_changes();
    test_bright_export_fix();
    test_sysex_parse();
    test_sysex_coalesce_race();
    test_targets();
    test_sysex_apply();
    test_controllers();
    test_program_change_and_scan();
    test_sysex_order();
    test_dump_burst();
    test_bright_force_guard();
    test_sysex_level_under_bright();
    test_sysex_after_program_change();
    test_bright_race_with_program_change();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
