/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 router core implementation (see route.h for the types and the threading
 * contract). Included once by app/midi_router.c on the device and by the host
 * tests in tools/dx747/host-test/router. */
#include "route.h"

DX_FN void dx_cfg_default(struct dx_cfg *c)
{
    memset(c, 0, sizeof *c);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        c->e[e].rx = (uint16_t)(1U << e); c->e[e].lo = 0; c->e[e].hi = 127;
        c->e[e].fold = DX_FOLD_DROP; c->e[e].poly = 4;
        c->priv[e] = (uint8_t)(e == 7 ? 9 : e);
    }
    c->up = 0xff; c->kbd_ch = 0; c->pool = 16;
}

/* Where a played note sounds on one engine. Returns 0 when it is not played. */
DX_FN int dx_map_note(const struct dx_engine_cfg *e, unsigned note, unsigned *out)
{
    if (note < e->lo || note > e->hi) return 0;
    int n = (int)note + e->shift;
    if (n >= 0 && n <= 127) { *out = (unsigned)n; return 1; }
    switch (e->fold) {
    case DX_FOLD_CLIP: *out = n < 0 ? 0U : 127U; return 1;
    case DX_FOLD_WRAP:
        while (n < 0) n += 12;
        while (n > 127) n -= 12;
        *out = (unsigned)n; return 1;
    default: return 0;
    }
}

DX_FN void dx_router_init(struct dx_router *r, const dx_cfgp *cfg, const DX_A(uint32_t) *panic_epoch, dx_post_fn post)
{
    memset(r, 0, sizeof *r);
    r->cfg = cfg; r->panic_epoch = panic_epoch; r->post = post;
    r->panic_seen_p = r->panic_seen_c = panic_epoch ? *panic_epoch : 0;
}

/* ---- producer side ------------------------------------------------------ */

DX_FN void dx_refill(struct dx_router *r, uint32_t now)
{
    if (!r->started) { r->started = 1; r->tokens = DX_TOKENS; r->last_ms = now; return; }
    uint32_t dt = now - r->last_ms;
    if (!dt) return;
    r->last_ms = now;
    unsigned add = dt > 8 ? DX_TOKENS : (unsigned)dt * DX_RATE;
    r->tokens = r->tokens + add > DX_TOKENS ? DX_TOKENS : r->tokens + add;
}

DX_FN void dx_clear_held(struct dx_router *r)
{
    r->nheld = 0;
    memset(r->pedal, 0, sizeof r->pedal);
    memset(r->pedal_eng, 0, sizeof r->pedal_eng);
}

DX_FN void dx_drop_at(struct dx_router *r, unsigned i)
{
    unsigned n = r->nheld;
    r->held[i] = r->held[n - 1];
    r->nheld = n - 1;
}

/* Forget notes the engine can no longer be holding: those up to a sequence mark. */
DX_FN void dx_drop_upto(struct dx_router *r, uint32_t mark)
{
    for (unsigned i = 0; i < r->nheld;) {
        if ((int32_t)(r->held[i].seq - mark) <= 0) dx_drop_at(r, i); else ++i;
    }
}

/* React to things another task did to the shared engine. */
DX_FN void dx_producer_sync(struct dx_router *r)
{
    if (r->panic_epoch && *r->panic_epoch != r->panic_seen_p) {
        r->panic_seen_p = *r->panic_epoch;
        dx_clear_held(r);
    }
    uint32_t drop = r->drop_seq;
    if (drop != r->drop_seen) {
        r->drop_seen = drop;
        dx_drop_upto(r, r->drop_mark); /* notes started after the mark are still real */
    }
}

DX_FN void dx_deliver_p(struct dx_router *r, const uint8_t *m, unsigned n)
{
    if (r->post(m, n)) { ++r->fail_p; dx_clear_held(r); } /* the engine reset itself: nothing is held any more */
}

DX_FN int dx_coalescible(const uint8_t *m)
{
    unsigned type = m[0] & 0xf0U;
    if (type == 0xe0 || type == 0xd0) return 1;
    return type == 0xb0 && m[1] != 64 && m[1] != 120 && m[1] != 121 && m[1] != 123;
}

/* Releases are the only messages that must not be lost. */
DX_FN int dx_critical(const uint8_t *m)
{
    unsigned type = m[0] & 0xf0U;
    return type == 0x80 || (type == 0xb0 && (m[1] == 64 || m[1] == 120 || m[1] == 121 || m[1] == 123));
}

/* Whether a message that is not a release would be accepted right now. */
DX_FN int dx_can_accept(const struct dx_router *r)
{
    unsigned pr = r->pr, pw = r->pw;
    return (pr == pw && r->tokens) || pw - pr < DX_PEND_SOFT;
}

DX_FN void dx_send(struct dx_router *r, const uint8_t *m, unsigned n)
{
    unsigned pr = r->pr, pw = r->pw;
    if (pr == pw && r->tokens) { --r->tokens; dx_deliver_p(r, m, n); return; }
    if (pw != pr && dx_coalescible(m)) {
        /* Never start at the entry at pr: the consumer may be delivering it. */
        for (unsigned i = pr + 1; i != pw; ++i) {
            uint32_t q = r->pend[i % DX_PEND_MAX];
            unsigned type = m[0] & 0xf0U;
            if ((q & 0xff) != m[0] || (q >> 24) != n) continue;
            if (type == 0xb0 && ((q >> 8) & 0xff) != m[1]) continue;
            DX_RACE_POINT(r);
            r->pend[i % DX_PEND_MAX] = dx_pack(m, n);
            DX_BARRIER();
            /* The consumer may have passed the entry since pr was read and would never see
             * the new value. Only an entry it has not reached yet can stand in for this
             * message; otherwise the message is queued behind it, which is also in order. */
            if ((int)(i - r->pr) > 0) return;
            break;
        }
        pr = r->pr;
    }
    if (pw - pr >= DX_PEND_SOFT && !dx_critical(m)) { ++r->dropped; return; }
    if (pw - pr >= DX_PEND_MAX) { ++r->dropped; ++r->need_seq; return; } /* even a release no longer fits */
    r->pend[pw % DX_PEND_MAX] = dx_pack(m, n);
    DX_BARRIER();
    r->pw = pw + 1;
}

DX_FN void dx_note_msg(struct dx_router *r, const struct dx_cfg *c, unsigned eng, unsigned on, unsigned note)
{
    uint8_t m[3] = {(uint8_t)((on ? 0x90U : 0x80U) | c->priv[eng]), (uint8_t)note, (uint8_t)(on ? 100 : 0)};
    dx_send(r, m, 3);
}

DX_FN void dx_steal(struct dx_router *r, const struct dx_cfg *c, unsigned i)
{
    dx_note_msg(r, c, r->held[i].eng, 0, r->held[i].out);
    ++r->steals;
    dx_drop_at(r, i);
}

DX_FN int dx_oldest(const struct dx_router *r, int eng)
{
    int best = -1;
    unsigned n = r->nheld;
    for (unsigned i = 0; i < n; ++i) {
        if (eng >= 0 && r->held[i].eng != (unsigned)eng) continue;
        if (best < 0 || (int32_t)(r->held[i].seq - r->held[(unsigned)best].seq) < 0) best = (int)i;
    }
    return best;
}

DX_FN void dx_note_on(struct dx_router *r, const struct dx_cfg *c, unsigned src, unsigned ch, unsigned note)
{
    unsigned key = src << 4 | ch;
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        const struct dx_engine_cfg *ec = &c->e[e];
        unsigned out;
        if (!(c->up & (1U << e)) || ec->mute || !((ec->rx >> ch) & 1U) || !dx_map_note(ec, note, &out)) continue;
        if (!dx_can_accept(r)) { ++r->dropped; continue; } /* never start a note that cannot be queued */
        for (unsigned i = 0; i < r->nheld; ++i)
            if (r->held[i].key == key && r->held[i].note == note && r->held[i].eng == e) {
                dx_note_msg(r, c, e, 0, r->held[i].out); /* the same key struck again: retrigger */
                dx_drop_at(r, i);
                break;
            }
        unsigned poly = ec->poly ? ec->poly : 1U, count = 0;
        for (unsigned i = 0; i < r->nheld; ++i) count += r->held[i].eng == e;
        if (count >= poly) { int o = dx_oldest(r, (int)e); if (o >= 0) dx_steal(r, c, (unsigned)o); }
        unsigned total = r->nheld + (r->peer ? r->peer->nheld : 0U);
        if (total >= c->pool) { int o = dx_oldest(r, -1); if (o >= 0) dx_steal(r, c, (unsigned)o); }
        if (r->nheld >= DX_HELD_MAX) { int o = dx_oldest(r, -1); if (o >= 0) dx_steal(r, c, (unsigned)o); }
        if (r->nheld >= DX_HELD_MAX) { ++r->dropped; continue; }
        unsigned slot = r->nheld;
        struct dx_note *n = &r->held[slot];
        n->key = (uint8_t)key; n->note = (uint8_t)note; n->eng = (uint8_t)e; n->out = (uint8_t)out;
        n->sust = 0; n->seq = ++r->seq;
        r->nheld = slot + 1;
        dx_note_msg(r, c, e, 1, out);
    }
}

DX_FN void dx_note_off(struct dx_router *r, const struct dx_cfg *c, unsigned src, unsigned ch, unsigned note)
{
    unsigned key = src << 4 | ch, pedal = (r->pedal[src] >> ch) & 1U;
    for (unsigned i = 0; i < r->nheld;) {
        struct dx_note *n = &r->held[i];
        if (n->key != key || n->note != note || n->sust) { ++i; continue; }
        dx_note_msg(r, c, n->eng, 0, n->out);
        if (pedal) { n->sust = 1; ++i; } else dx_drop_at(r, i);
    }
}

/* Engines that hold at least one note of this source channel. */
DX_FN unsigned dx_holding(const struct dx_router *r, unsigned src, unsigned ch)
{
    unsigned key = src << 4 | ch, mask = 0, n = r->nheld;
    for (unsigned i = 0; i < n; ++i) if (r->held[i].key == key) mask |= 1U << r->held[i].eng;
    return mask;
}

/* Forget the sustained notes of one source channel (their note offs were sent
 * when the keys came up). */
DX_FN void dx_drop_sustained(struct dx_router *r, unsigned src, unsigned ch)
{
    unsigned key = src << 4 | ch;
    for (unsigned i = 0; i < r->nheld;) {
        if (r->held[i].key == key && r->held[i].sust) dx_drop_at(r, i); else ++i;
    }
}

/* All notes off for one source channel: release whatever is still down, wherever
 * it was sent, then forget the channel's notes. */
DX_FN void dx_all_off(struct dx_router *r, const struct dx_cfg *c, unsigned src, unsigned ch)
{
    unsigned key = src << 4 | ch;
    for (unsigned i = 0; i < r->nheld;) {
        if (r->held[i].key != key) { ++i; continue; }
        if (!r->held[i].sust) dx_note_msg(r, c, r->held[i].eng, 0, r->held[i].out);
        dx_drop_at(r, i);
    }
}

DX_FN void dx_log_event(struct dx_router *r, unsigned src, unsigned ch, const uint8_t *m, unsigned n, uint32_t now)
{
    unsigned type = m[0] & 0xf0U;
    r->act_seen = (uint16_t)(r->act_seen | (1U << ch)); r->act_ms[ch] = now; r->act_src[ch] = (uint8_t)src;
    if (type == 0xe0 || type == 0xd0 || type == 0xa0) return; /* streams would flood the log */
    if (r->log_n) {
        struct dx_log *l = &r->log[(r->log_n - 1) % DX_LOG_MAX];
        if (l->src == src && l->ch == ch && l->status == m[0] && type == 0xb0 && l->d1 == m[1]) {
            l->ms = now; l->d2 = n > 2 ? m[2] : 0; return;
        }
    }
    struct dx_log *l = &r->log[r->log_n % DX_LOG_MAX];
    l->ms = now; l->src = (uint8_t)src; l->ch = (uint8_t)ch; l->status = m[0];
    l->d1 = n > 1 ? m[1] : 0; l->d2 = n > 2 ? m[2] : 0;
    ++r->log_n;
}

DX_FN void dx_forward(struct dx_router *r, const struct dx_cfg *c, unsigned targets, const uint8_t *m, unsigned n)
{
    for (unsigned e = 0; e < DX_ENGINES; ++e) if (targets & (1U << e)) {
        uint8_t out[3] = {(uint8_t)((m[0] & 0xf0U) | c->priv[e]), m[1], n > 2 ? m[2] : 0};
        dx_send(r, out, n);
    }
}

DX_FN unsigned dx_size(unsigned status) { return (status & 0xe0U) == 0xc0U ? 2U : 3U; }

/* One complete short channel message from source src. Nothing is reported to
 * the sender: routing failures are counted, never returned. */
DX_FN void dx_router_event(struct dx_router *r, unsigned src, const uint8_t *m, unsigned n, uint32_t now)
{
    const struct dx_cfg *c = *r->cfg;
    if (!c || src >= DX_SRC_COUNT || !m || n < 2 || n > 3 || m[0] < 0x80 || m[0] >= 0xf0) return;
    if (n != dx_size(m[0]) || m[1] >= 128 || (n > 2 && m[2] >= 128)) return;
    dx_producer_sync(r);
    dx_refill(r, now);
    unsigned ch = m[0] & 15U, type = m[0] & 0xf0U, rx = 0, ctl = 0;
    dx_log_event(r, src, ch, m, n, now);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        if (!(c->up & (1U << e))) continue;
        if ((c->e[e].rx >> ch) & 1U) rx |= 1U << e;
        if (c->e[e].ctl == ch + 1U) ctl |= 1U << e;
    }
    if (type == 0x90 && m[2]) { dx_note_on(r, c, src, ch, m[1]); return; }
    if (type == 0x80 || type == 0x90) { dx_note_off(r, c, src, ch, m[1]); return; }
    if (type == 0xa0) {
        for (unsigned e = 0; e < DX_ENGINES; ++e) {
            unsigned out;
            if ((rx & (1U << e)) && dx_map_note(&c->e[e], m[1], &out)) {
                uint8_t o[3] = {(uint8_t)(0xa0U | c->priv[e]), (uint8_t)out, m[2]};
                dx_send(r, o, 3);
            }
        }
        return;
    }
    unsigned targets = rx | ctl;
    if (r->mail && (type == 0xb0 || type == 0xc0)) {
        unsigned cc = m[1], v = n > 2 ? m[2] : 0;
        if (type == 0xc0) {
            /* the engine loads a new voice: the BRIGHT macro has to start from it */
            for (unsigned e = 0; e < DX_ENGINES; ++e) if (targets & (1U << e)) dx_mail_put(r->mail, DX_MAIL_PC + e, 0);
        } else if (c->master_ch == ch + 1U && (cc == 70 || cc == 71 || cc == 74)) {
            /* master filter controls: consumed here, they never reach an engine */
            dx_mail_put(r->mail, cc == 74 ? DX_MAIL_CUTOFF : cc == 71 ? DX_MAIL_RES : DX_MAIL_TYPE, v);
            return;
        } else if (cc == 74 || cc == 71 || cc == 10 || cc == 11) {
            unsigned base = cc == 10 ? DX_MAIL_PAN : cc == 11 ? DX_MAIL_EXPR : DX_MAIL_BRIGHT;
            for (unsigned e = 0; e < DX_ENGINES; ++e) if (targets & (1U << e)) dx_mail_put(r->mail, base + e, v);
        }
    }
    if (type == 0xb0) {
        unsigned holding = dx_holding(r, src, ch);
        if (m[1] == 64 && m[2] >= 64) {
            r->pedal[src] = (uint16_t)(r->pedal[src] | (1U << ch));
            r->pedal_eng[src][ch] = (uint8_t)(r->pedal_eng[src][ch] | targets);
        } else if (m[1] == 64 || m[1] == 121) {
            /* Pedal up reaches every engine that was told the pedal went down. */
            r->pedal[src] = (uint16_t)(r->pedal[src] & ~(1U << ch));
            targets |= holding | r->pedal_eng[src][ch];
            r->pedal_eng[src][ch] = 0;
            dx_drop_sustained(r, src, ch);
        } else if (m[1] == 120 || m[1] == 123) {
            dx_all_off(r, c, src, ch);
            targets |= holding;
        }
    }
    if (targets) dx_forward(r, c, targets, m, n);
}

/* Built-in keys: a note on the keyboard channel, fixed velocity like stock. */
DX_FN void dx_router_key(struct dx_router *r, unsigned note, unsigned on, uint32_t now)
{
    const struct dx_cfg *c = *r->cfg;
    if (!c || note > 127) return;
    uint8_t m[3] = {(uint8_t)((on ? 0x90U : 0x80U) | (c->kbd_ch & 15U)), (uint8_t)note, (uint8_t)(on ? 100 : 0)};
    dx_router_event(r, DX_SRC_KEYS, m, 3, now);
}

/* Send a note-off for everything this router holds, then forget it. Producer
 * context only; use the panic epoch to cut an external router from elsewhere. */
DX_FN void dx_router_release_all(struct dx_router *r)
{
    const struct dx_cfg *c = *r->cfg;
    dx_producer_sync(r);
    unsigned n = r->nheld;
    if (c) for (unsigned i = 0; i < n; ++i) dx_note_msg(r, c, r->held[i].eng, 0, r->held[i].out);
    dx_clear_held(r);
}

/* Activity for the Setup and MIDI-in screens (read from the control task). */
DX_FN int dx_router_active(const struct dx_router *r, unsigned ch, uint32_t now, uint32_t window_ms)
{
    return ch < 16 && ((r->act_seen >> ch) & 1U) && now - r->act_ms[ch] <= window_ms;
}

/* ---- consumer side ------------------------------------------------------ */

DX_FN void dx_refill_c(struct dx_router *r, uint32_t now)
{
    if (!r->cstarted) { r->cstarted = 1; r->ctok = DX_TOKENS; r->clast_ms = now; return; }
    uint32_t dt = now - r->clast_ms;
    if (!dt) return;
    r->clast_ms = now;
    unsigned add = dt > 8 ? DX_TOKENS : (unsigned)dt * DX_RATE;
    r->ctok = r->ctok + add > DX_TOKENS ? DX_TOKENS : r->ctok + add;
}

/* Control-loop call: refill the budget, drain what the producer queued and, if
 * the pending ring ever overflowed, release every engine with all-notes-off. */
DX_FN void dx_router_poll(struct dx_router *r, uint32_t now)
{
    const struct dx_cfg *c = *r->cfg;
    if (r->panic_epoch && *r->panic_epoch != r->panic_seen_c) {
        r->panic_seen_c = *r->panic_epoch;
        r->pr = r->pw; /* whatever was queued belongs to a world that no longer exists */
    }
    dx_refill_c(r, now);
    while (r->pr != r->pw && r->ctok) {
        unsigned pr = r->pr;
        uint32_t q = r->pend[pr % DX_PEND_MAX];
        uint8_t m[3] = {(uint8_t)q, (uint8_t)(q >> 8), (uint8_t)(q >> 16)};
        --r->ctok;
        if (r->post(m, q >> 24)) { ++r->fail_c; r->drop_mark = r->seq; DX_BARRIER(); ++r->drop_seq; }
        DX_BARRIER();
        r->pr = pr + 1; /* only after delivery, so the producer never overtakes it */
    }
    uint32_t need = r->need_seq;
    if (need != r->need_done && r->pr == r->pw && c) {
        /* Resumable: the budget may cover only part of the eight messages. */
        if (!r->rec_next) r->rec_mark = r->seq;
        while (r->ctok && r->rec_next < DX_ENGINES) {
            uint8_t off[3] = {(uint8_t)(0xb0U | c->priv[r->rec_next]), 123, 0};
            --r->ctok; ++r->rec_next;
            if (r->post(off, 3)) ++r->fail_c;
        }
        if (r->rec_next == DX_ENGINES) {
            r->rec_next = 0; r->need_done = need;
            r->drop_mark = r->rec_mark; DX_BARRIER(); ++r->drop_seq;
        }
    }
}
