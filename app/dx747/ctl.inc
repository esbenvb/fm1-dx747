/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 control logic: the BRIGHT macro, DX7 SysEx edits and the controller
 * mailbox. Portable: the engine is reached through struct dx_edit_port, so this
 * file runs in app/project.c and in tools/dx747/host-test/control alike.
 *
 * Threading. dx_ctl_sysex_in is the producer of the SysEx ring and the dump
 * pool (the MIDI hook) and touches nothing else. Everything else is the control
 * task, which is the only consumer.
 *
 * Order. Parameter changes and voice dumps are applied in the order they
 * arrived: a dump remembers how many parameter changes were queued before it,
 * and the consumer imports it exactly when it has applied those. An engine whose
 * program change is still loading is left alone until it has settled, so an
 * edit that follows a program change lands on the new voice.
 *
 * Cost. Engine edits pause the audio gate each time, so at most DX_SX_BATCH
 * units of SysEx work are done per poll (a dump counts DX_DUMP_COST) and a
 * controller that keeps moving costs one BRIGHT update per poll, not one per
 * message. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "route.h"
#include "scene.h"

struct dx_edit_port {
    int (*copy)(unsigned engine, uint8_t voice[156]);
    void (*edit)(unsigned engine, unsigned parameter, int delta);   /* VCED parameter 0..144, clamped by the engine */
    void (*name)(unsigned engine, unsigned index, char c);
    void (*toggle_op)(unsigned engine, unsigned ui_operator);       /* UI OP1..OP6 as 0..5 */
    int (*import)(unsigned engine, const uint8_t voice[156]);       /* 0 when the voice was loaded */
    /* After a voice is loaded or the engine was panicked, its controller attenuation stays closed until some controller
     * message arrives: a voice with amplitude-modulation sensitivity stays silent. The engine's own default (breath
     * open, CC2 = 127) is applied by sending exactly that. */
    int (*refresh)(unsigned engine);                                 /* 0 when posted; non-zero is retried at the next poll */
};

enum { DX_SX_RING = 64, DX_SX_BATCH = 6, DX_DUMPS = 8, DX_DUMP_COST = 3, DX_BRIGHT_SCAN_MS = 100, DX_PC_SETTLE_MS = 20 };
enum { DX_CTL_FILTER = 1 };   /* dx_ctl_poll result: the live master-filter settings changed */

/* What the macro did to one engine's voice. Levels are VCED output levels 0..99. */
struct dx_bright {
    uint8_t tracked;          /* modulator levels currently differ from the voice's own */
    uint8_t algo;             /* algorithm 0..31 when the levels were written */
    uint8_t base[6], applied[6]; /* per operator index k (0 = OP6): the voice's level, and what was written */
    uint32_t sig;             /* signature of the whole voice as the macro left it */
};
struct dx_dump { uint8_t data[155], dev; unsigned at; };   /* at: parameter changes queued before it arrived */
struct dx_ctl {
    const struct dx_edit_port *port;
    struct dx_bright br[DX_ENGINES];
    uint8_t dirty;            /* engines whose BRIGHT must be (re)applied */
    uint8_t force;            /* engines whose voice was replaced by an import: start from the voice as it is */
    uint8_t refresh;          /* engines that need their controller state applied again (see struct dx_edit_port) */
    uint8_t pc_wait;          /* program change seen, waiting for the engine to load the voice */
    uint32_t pc_at[DX_ENGINES], scan_ms;
    DX_A(uint32_t) sx[DX_SX_RING]; /* DX7 parameter changes: dev | param << 4 | value << 12 */
    DX_A(unsigned) sx_w, sx_r;
    struct dx_dump dumps[DX_DUMPS]; /* single-voice dumps, oldest first */
    DX_A(unsigned) dump_w, dump_r;
    DX_A(uint32_t) sx_dropped;
};

/* Operator k (0 = OP6 ... 5 = OP1) is a carrier in algorithm a when bit k is set. Derived from the
 * engine's own table (web/synth/dx7core.c: a carrier writes to bus 0); the host test re-derives it. */
static const uint8_t dx_carrier[32] = {
    0x28, 0x28, 0x24, 0x24, 0x2a, 0x2a, 0x28, 0x28, 0x28, 0x24, 0x24, 0x28, 0x28, 0x28, 0x28, 0x20,
    0x20, 0x20, 0x26, 0x34, 0x36, 0x2e, 0x36, 0x3e, 0x3e, 0x34, 0x34, 0x29, 0x3a, 0x39, 0x3e, 0x3f
};
#define DX_LEVEL(k) (21U * (k) + 16U)   /* VCED index of operator k's output level */

DX_FN int dx_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
DX_FN unsigned dx_modulators(unsigned algorithm) { return ~(unsigned)dx_carrier[algorithm & 31U] & 63U; }
DX_FN int dx_is_level(unsigned i) { return i >= 16U && i < DX_LEVEL(6) && (i - 16U) % 21U == 0; }

/* The engine rejects a voice with any byte above its parameter limit; a dump may carry a few (the
 * workbench clamps them the same way). */
DX_FN unsigned dx_fm_limit(unsigned p)
{
    static const uint8_t op_max[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t global_max[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    return p < 126 ? op_max[p % 21] : p < 145 ? global_max[p - 126] : p == 155 ? 63U : 127U;
}
DX_FN void dx_clamp_voice(uint8_t v[156])
{
    for (unsigned i = 0; i < 156; ++i) if (v[i] > dx_fm_limit(i)) v[i] = (uint8_t)dx_fm_limit(i);
}
DX_FN uint32_t dx_sig(const uint8_t v[156])
{
    uint32_t h = 2166136261U;
    for (unsigned i = 0; i < 156; ++i) h = (h ^ v[i]) * 16777619U;
    return h;
}

/* ---- BRIGHT -------------------------------------------------------------- */

/* Make engine e's modulator output levels the voice's own level plus offset
 * (carriers are never touched, so the volume does not move). A modulator at
 * level 0 stays off.
 *
 * Without force, a level that is not what this macro wrote is taken as the
 * voice's own: that is how program changes and edits from elsewhere are noticed.
 * force says the caller replaced the whole voice (a dump, a scene import, a
 * workbench upload) and every level is the voice's own, even one that happens
 * to equal what this macro wrote. It is ignored when the voice is byte for byte
 * the one the macro left, because then nothing was replaced (an import the
 * engine refused). */
DX_FN void dx_bright_apply(struct dx_ctl *c, unsigned e, int offset, int force)
{
    if (e >= DX_ENGINES) return;
    struct dx_bright *b = &c->br[e];
    if (!b->tracked && offset == 0) return;
    uint8_t v[156], x[156], y[156];
    if (c->port->copy(e, v)) return;
    if (b->tracked && force && dx_sig(v) == b->sig) force = 0;
    unsigned algo = v[134] & 31U, old_mods = dx_modulators(b->algo), new_mods = dx_modulators(algo), edits = 0;
    uint8_t orig[6], want[6];
    for (unsigned k = 0; k < 6; ++k) {
        unsigned cur = v[DX_LEVEL(k)];
        orig[k] = (uint8_t)((b->tracked && !force && ((old_mods >> k) & 1U) && cur == b->applied[k]) ? b->base[k] : cur);
    }
    memcpy(x, v, sizeof x);
    for (unsigned k = 0; k < 6; ++k) {
        want[k] = orig[k];
        if (((new_mods >> k) & 1U) && orig[k] > 0) want[k] = (uint8_t)dx_clampi((int)orig[k] + offset, 0, 99);
        x[DX_LEVEL(k)] = want[k];
        if (want[k] != v[DX_LEVEL(k)]) { c->port->edit(e, DX_LEVEL(k), (int)want[k] - (int)v[DX_LEVEL(k)]); ++edits; }
        b->base[k] = orig[k]; b->applied[k] = want[k];
    }
    b->algo = (uint8_t)algo;
    b->tracked = offset != 0;
    b->sig = dx_sig(x);
    /* The edits are relative to the copy above. If the engine loaded another voice in between (a program
     * change is handled in the audio block, not in this task), those steps landed on the new voice. */
    if (edits && c->port->copy(e, y) == 0 && dx_sig(y) != b->sig) {
        int replaced = 0;
        for (unsigned i = 0; i < 156; ++i) if (!dx_is_level(i) && y[i] != v[i]) replaced = 1;
        if (replaced) {
            for (unsigned k = 0; k < 6; ++k)
                if (want[k] != v[DX_LEVEL(k)]) c->port->edit(e, DX_LEVEL(k), (int)v[DX_LEVEL(k)] - (int)want[k]);
            b->tracked = 0;
            c->dirty |= (uint8_t)(1U << e);     /* start again from the voice that is there now */
        } else {                                 /* the engine clamped or refused a step: believe what is there */
            for (unsigned k = 0; k < 6; ++k) b->applied[k] = y[DX_LEVEL(k)];
            b->sig = dx_sig(y);
        }
    }
}
/* An exported voice (scene capture, workbench download) must carry the voice's own levels, or recalling or
 * uploading it would add the offset a second time. */
DX_FN void dx_bright_export_fix(const struct dx_ctl *c, unsigned e, uint8_t voice[156])
{
    if (e >= DX_ENGINES || !c->br[e].tracked) return;
    const struct dx_bright *b = &c->br[e];
    if ((voice[134] & 31U) != b->algo) return;
    unsigned mods = dx_modulators(b->algo);
    for (unsigned k = 0; k < 6; ++k)
        if (((mods >> k) & 1U) && voice[DX_LEVEL(k)] == b->applied[k]) voice[DX_LEVEL(k)] = b->base[k];
}
DX_FN void dx_bright_changed_ctl(struct dx_ctl *c, unsigned e) { if (e < DX_ENGINES) c->dirty |= (uint8_t)(1U << e); }
/* The voice was replaced (a program change that has settled): start BRIGHT from the voice as it is. The refresh of a
 * program change is sent by the router, in order, right behind the message. */
DX_FN void dx_bright_force(struct dx_ctl *c, unsigned e)
{
    if (e < DX_ENGINES) c->force |= (uint8_t)(1U << e), c->dirty |= (uint8_t)(1U << e);
}
/* The voice was replaced by something DX747 did itself (a dump, a scene, an upload, the patch encoder): re-base BRIGHT,
 * and send the refresh. */
DX_FN void dx_voice_loaded(struct dx_ctl *c, unsigned e)
{
    if (e < DX_ENGINES) dx_bright_force(c, e), c->refresh |= (uint8_t)(1U << e);
}

/* ---- DX7 SysEx ------------------------------------------------------------ */

/* Called from the MIDI hook with a complete frame (F0 .. F7). Returns 1 when
 * the frame is a DX7 message this code owns, whether or not it could be kept:
 *   F0 43 1n gg pp dd F7          parameter change, gg 0/1 = VCED parameters 0..127 / 128..155
 *   F0 43 0n 00 01 1B <155> ck F7 single voice dump
 * Other Yamaha messages and the larger bank dump (never delivered: the transport
 * drops frames over 1024 bytes) are left alone. What cannot be kept (a full ring
 * or dump pool) is counted in sx_dropped. */
DX_FN int dx_ctl_sysex_in(struct dx_ctl *c, const uint8_t *m, size_t n)
{
    if (!m || n < 7 || m[0] != 0xf0 || m[1] != 0x43 || m[n - 1] != 0xf7) return 0;
    if (n == 7 && (m[2] & 0xf0) == 0x10 && m[3] <= 1) {
        if (m[4] >= 128 || m[5] >= 128) return 1;
        unsigned param = ((unsigned)m[3] << 7) | m[4];
        if (param > 155) return 1;
        unsigned word = (m[2] & 15U) | param << 4 | (unsigned)m[5] << 12, w = c->sx_w, r = c->sx_r;
        /* The newest value of a parameter replaces an older one that is still queued, but never one queued
         * before a dump that is still waiting (it has to land after the dump) and never the entry at r,
         * which may be in use. */
        unsigned from = r + 1, dw = c->dump_w;
        if (dw != c->dump_r) {
            unsigned at = c->dumps[(dw - 1) % DX_DUMPS].at;
            if ((int)(at - from) > 0) from = at;
        }
        if (w != r)
            for (unsigned i = from; i != w; ++i) {
                uint32_t q = c->sx[i % DX_SX_RING];
                if ((q & 0xfffU) != (word & 0xfffU)) continue;
                DX_RACE_POINT(c);
                c->sx[i % DX_SX_RING] = word;
                DX_BARRIER();
                if ((int)(i - c->sx_r) > 0) return 1; /* not reached by the consumer yet: it will see the new value */
                break;                                /* it was already past: queue behind it instead */
            }
        r = c->sx_r;
        if (w - r >= DX_SX_RING) { ++c->sx_dropped; return 1; }
        c->sx[w % DX_SX_RING] = word;
        DX_BARRIER();
        c->sx_w = w + 1;
        return 1;
    }
    if (n == 163 && (m[2] & 0xf0) == 0 && m[3] == 0 && m[4] == 1 && m[5] == 27) {
        unsigned sum = 0;
        for (unsigned i = 6; i < 162; ++i) { if (m[i] >= 128) return 1; sum += m[i]; }
        if (sum & 127U) return 1;
        unsigned dw = c->dump_w;
        if (dw - c->dump_r >= DX_DUMPS) { ++c->sx_dropped; return 1; }
        struct dx_dump *d = &c->dumps[dw % DX_DUMPS];
        memcpy(d->data, m + 6, 155);
        d->dev = m[2] & 15U;
        d->at = c->sx_w;
        DX_BARRIER();
        c->dump_w = dw + 1;
        return 1;
    }
    return 0;
}

/* Engines a DX7 message for device number dev (channel dev + 1) edits: those with that control
 * channel; with none, the one engine that listens on the channel, if it has no control channel
 * of its own and nothing else listens there. A layered channel is therefore never overwritten
 * by accident: give the engine a control channel to edit it. */
DX_FN unsigned dx_sysex_targets(const struct dx_cfg *cfg, unsigned dev)
{
    unsigned ctl = 0, rx = 0, nrx = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        if (!((cfg->up >> e) & 1U)) continue;
        if (cfg->e[e].ctl == dev + 1U) ctl |= 1U << e;
        if ((cfg->e[e].rx >> dev) & 1U) { rx |= 1U << e; ++nrx; }   /* every listener counts, with or without a control channel */
    }
    if (ctl) return ctl;
    if (nrx != 1) return 0U;
    for (unsigned e = 0; e < DX_ENGINES; ++e) if ((rx >> e) & 1U) return cfg->e[e].ctl ? 0U : rx;
    return 0U;
}

DX_FN void dx_sx_apply(struct dx_ctl *c, unsigned e, unsigned param, unsigned value)
{
    const struct dx_edit_port *p = c->port;
    struct dx_bright *b = &c->br[e];
    uint8_t v[156];
    if (param < 145) {
        /* While BRIGHT is on, the level an editor shows is the voice's own: set that, and let the macro
         * write base + offset. Comparing with the live level would swallow a value that happens to equal it. */
        if (b->tracked)
            for (unsigned k = 0; k < 6; ++k)
                if (param == DX_LEVEL(k) && ((dx_modulators(b->algo) >> k) & 1U)) {
                    b->base[k] = (uint8_t)(value > 99U ? 99U : value);
                    c->dirty |= (uint8_t)(1U << e);
                    return;
                }
        if (p->copy(e, v)) return;
        int delta = (int)value - (int)v[param];
        if (delta) p->edit(e, param, delta);
    } else if (param < 155) {
        p->name(e, param - 145U, (char)value);
    } else {
        if (value > 63U) value = 63U;
        if (p->copy(e, v)) return;
        for (unsigned i = 0; i < 6; ++i) { /* UI operator i is bit 5 - i of the VCED mask */
            unsigned want = (value >> (5U - i)) & 1U, have = (v[155] >> (5U - i)) & 1U;
            if (want != have) p->toggle_op(e, i);
        }
    }
    c->dirty |= (uint8_t)(1U << e);
}

/* ---- controllers ---------------------------------------------------------- */

/* CC74 to 20 Hz .. 20 kHz, exponential: 1000^(1/127) per step in Q16. */
DX_FN unsigned dx_cc_to_hz(unsigned v)
{
    uint32_t hz = 20U << 16;
    for (unsigned i = 0; i < v && i < 127U; ++i) hz = (uint32_t)(((uint64_t)hz * 69200U) >> 16);
    unsigned out = hz >> 16;
    return out < 20U ? 20U : out > 20000U ? 20000U : out;
}
DX_FN int dx_cc_to_offset(unsigned v) { return dx_clampi(((int)v - 64) * 50 / 63, -50, 50); } /* 64 is centre */

/* One control-task step. Returns DX_CTL_* flags. A controller of one kind arrives as its latest value only,
 * so the order in which different master-filter controllers were sent is not kept: type is applied last. */
DX_FN unsigned dx_ctl_poll(struct dx_ctl *c, struct dx_ctl_mail *mail, const struct dx_cfg *cfg, struct dx_live *live, uint32_t now)
{
    unsigned flags = 0, v;
    if (mail) {
        for (unsigned e = 0; e < DX_ENGINES; ++e) {
            if (dx_mail_take(mail, DX_MAIL_BRIGHT + e, &v)) { live->bright[e] = (int8_t)dx_cc_to_offset(v); c->dirty |= (uint8_t)(1U << e); }
            if (dx_mail_take(mail, DX_MAIL_PAN + e, &v)) live->pan[e] = (int8_t)dx_cc_to_offset(v);
            if (dx_mail_take(mail, DX_MAIL_EXPR + e, &v)) live->expr[e] = (uint8_t)v;
            if (dx_mail_take(mail, DX_MAIL_PC + e, &v)) { c->pc_wait |= (uint8_t)(1U << e); c->pc_at[e] = now; }
        }
        if (dx_mail_take(mail, DX_MAIL_PANIC, &v)) c->refresh |= cfg->up;   /* every engine was stopped */
        /* A cutoff or resonance move is meant to be heard: an off filter becomes a low-pass. */
        if (dx_mail_take(mail, DX_MAIL_CUTOFF, &v)) {
            live->filter_hz = (uint16_t)dx_cc_to_hz(v);
            if (live->filter_type == DX_FILTER_OFF) live->filter_type = DX_FILTER_LP;
            flags |= DX_CTL_FILTER;
        }
        if (dx_mail_take(mail, DX_MAIL_RES, &v)) {
            live->filter_res = (uint8_t)v;
            if (live->filter_type == DX_FILTER_OFF) live->filter_type = DX_FILTER_LP;
            flags |= DX_CTL_FILTER;
        }
        if (dx_mail_take(mail, DX_MAIL_TYPE, &v)) { live->filter_type = (uint8_t)(v >> 5); flags |= DX_CTL_FILTER; }
    }
    /* An engine whose program change has had time to load is no longer waiting; its voice is rechecked. */
    for (unsigned e = 0; e < DX_ENGINES; ++e)
        if (((c->pc_wait >> e) & 1U) && now - c->pc_at[e] >= DX_PC_SETTLE_MS) {
            c->pc_wait &= (uint8_t)~(1U << e);
            dx_bright_force(c, e);
        }
    /* SysEx in arrival order: parameter changes and dumps share one sequence (a dump records how many
     * parameter changes preceded it). Work for an engine that is still loading a program change waits. */
    unsigned budget = DX_SX_BATCH;
    while (budget) {
        unsigned dr = c->dump_r;
        if (dr != c->dump_w && c->dumps[dr % DX_DUMPS].at == c->sx_r) {
            const struct dx_dump *d = &c->dumps[dr % DX_DUMPS];
            unsigned targets = dx_sysex_targets(cfg, d->dev);
            if (targets & c->pc_wait) break;
            DX_BARRIER();
            uint8_t voice[156];
            memcpy(voice, d->data, 155);
            voice[155] = 63; /* all operators on, as the stock import does */
            dx_clamp_voice(voice);
            for (unsigned e = 0; e < DX_ENGINES; ++e)
                if ((targets & (1U << e)) && c->port->import(e, voice) == 0) dx_voice_loaded(c, e);
            c->dump_r = dr + 1;
            budget = budget > DX_DUMP_COST ? budget - DX_DUMP_COST : 0;
            continue;
        }
        if (c->sx_r == c->sx_w) break;
        uint32_t q = c->sx[c->sx_r % DX_SX_RING];
        unsigned targets = dx_sysex_targets(cfg, q & 15U);
        if (targets & c->pc_wait) break;
        for (unsigned e = 0; e < DX_ENGINES; ++e)
            if (targets & (1U << e)) dx_sx_apply(c, e, (q >> 4) & 255U, (q >> 12) & 127U);
        c->sx_r = c->sx_r + 1;
        --budget;
    }
    if (now - c->scan_ms >= DX_BRIGHT_SCAN_MS) { /* a voice can change in ways nothing reports */
        c->scan_ms = now;
        for (unsigned e = 0; e < DX_ENGINES; ++e) if (c->br[e].tracked) c->dirty |= (uint8_t)(1U << e);
    }
    /* BRIGHT waits for engines that are still loading a program change: any step taken now would land on
     * a voice that is about to be replaced, or on the new one before it can be told apart. */
    unsigned hold = c->pc_wait, dirty = c->dirty, force = c->force;
    for (unsigned e = 0; e < DX_ENGINES; ++e)
        if ((((c->refresh & ~hold) >> e) & 1U) && c->port->refresh(e) == 0) c->refresh &= (uint8_t)~(1U << e);
    c->dirty = (uint8_t)(dirty & hold); c->force = (uint8_t)(force & hold);
    for (unsigned e = 0; e < DX_ENGINES; ++e)
        if (((dirty & ~hold) >> e) & 1U) dx_bright_apply(c, e, live->bright[e], (force >> e) & 1U);
    return flags;
}
