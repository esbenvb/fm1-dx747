/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 parts (see parts.h): engine allocation, the routing derived from the parts, and the
 * edits the screens and the control task make (add and remove parts and layers, change a
 * part's channel, replace a layered sound by one voice). Portable: it needs only route.h, so it
 * runs on the device and in tools/dx747/host-test/parts alike. Control task only.
 *
 * Engines stay where they are: a layer keeps its engine until it is removed, and a new layer
 * takes the lowest free engine. Nothing is ever moved from one engine to another, because that
 * would mean exporting and importing a voice. A part's layers are its engines in engine order,
 * so "layer 1" is the part's lowest engine. */
#ifndef DX747_PARTS_INC
#define DX747_PARTS_INC
#include <string.h>
#include "parts.h"

DX_FN unsigned dx_layer_volume(unsigned lvl, unsigned level) { return (lvl * level + 63U) / 127U; }

/* A fresh layer plays every key at every velocity, unshifted. */
DX_FN void dx_layer_defaults(struct dx_engine_cfg *e)
{
    e->lo = 0; e->hi = 127; e->shift = 0; e->fold = DX_FOLD_DROP; e->poly = 4; e->mute = 0; e->vlo = 1; e->vhi = 127;
}

/* The same layout as a unit before any scene: engine e is part e on channel e + 1. */
DX_FN void dx_parts_default(struct dx_parts *p)
{
    memset(p, 0, sizeof *p);
    for (unsigned i = 0; i < DX_PARTS; ++i) {
        p->part[i].channel = (uint8_t)(i + 1U); p->part[i].level = 127;
        memset(p->part[i].name, ' ', sizeof p->part[i].name);
        p->layer[i].owner = (uint8_t)i; p->layer[i].lvl = 127;
    }
    p->selected = 0;
}

DX_FN int dx_part_used(const struct dx_parts *p, unsigned part) { return part < DX_PARTS && p->part[part].channel != 0; }

/* Engines that are layers of the part, in engine order (layer 1 first). Returns how many. */
DX_FN unsigned dx_part_layers(const struct dx_parts *p, unsigned part, uint8_t list[DX_ENGINES])
{
    unsigned n = 0;
    if (!dx_part_used(p, part)) return 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) if (p->layer[e].owner == part) list[n++] = (uint8_t)e;
    return n;
}

/* The engine of layer idx of a part (clamped to the last layer), -1 when the part has none. */
DX_FN int dx_part_engine(const struct dx_parts *p, unsigned part, unsigned idx)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(p, part, list);
    return n ? list[idx < n ? idx : n - 1U] : -1;
}

/* The engine that takes DX7 SysEx for the part: its selected layer. */
DX_FN int dx_part_sel_engine(const struct dx_parts *p, unsigned part)
{
    return part < DX_PARTS ? dx_part_engine(p, part, p->part[part].sel) : -1;
}

DX_FN unsigned dx_parts_mask(const struct dx_parts *p)
{
    unsigned m = 0;
    for (unsigned i = 0; i < DX_PARTS; ++i) if (p->part[i].channel) m |= 1U << i;
    return m;
}

DX_FN unsigned dx_parts_free_engines(const struct dx_parts *p, const struct dx_cfg *c)
{
    unsigned m = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) if (p->layer[e].owner == DX_NO_PART && ((c->up >> e) & 1U)) m |= 1U << e;
    return m;
}

/* The part that uses a channel (1..16), or DX_NO_PART. */
DX_FN unsigned dx_part_on_channel(const struct dx_parts *p, unsigned channel)
{
    for (unsigned i = 0; i < DX_PARTS; ++i) if (p->part[i].channel && p->part[i].channel == channel) return i;
    return DX_NO_PART;
}

/* Fill the routing fields of c from the parts: the channel each engine listens on, the engine that takes DX7
 * SysEx and control changes for its part (the selected layer), and the channel the built-in keys play. */
DX_FN void dx_parts_derive(const struct dx_parts *p, struct dx_cfg *c)
{
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        unsigned owner = p->layer[e].owner;
        if (owner < DX_PARTS && p->part[owner].channel) {
            unsigned ch = p->part[owner].channel;
            c->e[e].rx = (uint16_t)(1U << (ch - 1U));
            c->e[e].ctl = dx_part_sel_engine(p, owner) == (int)e ? (uint8_t)ch : 0;
        } else {
            c->e[e].rx = 0; c->e[e].ctl = 0;
        }
    }
    /* An empty selected part has no channel: kbd_ch 16 means the keys play nothing (dx_router_key). */
    c->kbd_ch = p->selected < DX_PARTS && p->part[p->selected].channel ? (uint8_t)(p->part[p->selected].channel - 1U) : 16;
}

/* The invariants every stored or edited set of parts keeps. 1 when they hold. */
DX_FN int dx_parts_valid(const struct dx_parts *p)
{
    if (p->selected >= DX_PARTS) return 0;
    uint32_t seen = 0;
    for (unsigned i = 0; i < DX_PARTS; ++i) {
        const struct dx_part *x = &p->part[i];
        uint8_t list[DX_ENGINES];
        unsigned n = dx_part_layers(p, i, list);
        for (unsigned k = 0; k < sizeof x->name; ++k) if (x->name[k] < 32 || x->name[k] > 126) return 0;
        if (!x->channel) {
            if (x->sel) return 0;
            continue;
        }
        if (x->channel > 16 || (seen >> x->channel) & 1U || x->level > 127 || !n || x->sel >= n) return 0;
        seen |= 1U << x->channel;
    }
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        const struct dx_layer *l = &p->layer[e];
        if (l->lvl > 127) return 0;
        if (l->owner != DX_NO_PART && (l->owner >= DX_PARTS || !p->part[l->owner].channel)) return 0;
    }
    return 1;
}

/* ---- edits -------------------------------------------------------------------- */

enum { DX_PART_OK = 0, DX_PART_NO_SLOT = -1, DX_PART_NO_ENGINE = -2, DX_PART_NO_CHANNEL = -3, DX_PART_REFUSED = -4 };

DX_FN int dx_parts_take_engine(struct dx_parts *p, struct dx_cfg *c, unsigned part)
{
    unsigned free = dx_parts_free_engines(p, c);
    for (unsigned e = 0; e < DX_ENGINES; ++e) if ((free >> e) & 1U) {
        p->layer[e].owner = (uint8_t)part; p->layer[e].lvl = 127;
        dx_layer_defaults(&c->e[e]);
        return (int)e;
    }
    return DX_PART_NO_ENGINE;
}

/* A new part with one layer on the lowest channel that no part uses and that is not a control channel (scene or master),
 * in the slot `preferred` when that is unused (a screen showing an empty slot), else in the lowest unused slot. Returns
 * the part. (dx_parts_set_channel refuses only the scene channel: a part may be put on the master channel by hand.) */
DX_FN int dx_parts_add_part(struct dx_parts *p, struct dx_cfg *c, unsigned preferred)
{
    unsigned part = DX_PARTS, channel = 0;
    if (preferred < DX_PARTS && !p->part[preferred].channel) part = preferred;
    else for (unsigned i = 0; i < DX_PARTS; ++i) if (!p->part[i].channel) { part = i; break; }
    if (part == DX_PARTS) return DX_PART_NO_SLOT;
    for (unsigned ch = 1; ch <= 16; ++ch)
        if (dx_part_on_channel(p, ch) == DX_NO_PART && ch != c->scene_ch && ch != c->master_ch) { channel = ch; break; }
    if (!channel) return DX_PART_NO_CHANNEL;
    p->part[part].channel = (uint8_t)channel; /* the engine is taken while the slot is already counted as used */
    int e = dx_parts_take_engine(p, c, part);
    if (e < 0) { p->part[part].channel = 0; return e; }
    p->part[part].level = 127; p->part[part].sel = 0;
    memset(p->part[part].name, ' ', sizeof p->part[part].name);
    if (!dx_part_used(p, p->selected) || p->selected == part) p->selected = (uint8_t)part; /* the first part is the one the keys play */
    dx_parts_derive(p, c);
    return (int)part;
}

/* One more layer for a part, on the lowest free engine. Returns the engine. */
DX_FN int dx_parts_add_layer(struct dx_parts *p, struct dx_cfg *c, unsigned part)
{
    if (!dx_part_used(p, part)) return DX_PART_REFUSED;
    int e = dx_parts_take_engine(p, c, part);
    if (e >= 0) dx_parts_derive(p, c);
    return e;
}

/* Remove layer idx of a part. The last layer takes the part with it. Returns the engine that became free. */
DX_FN int dx_parts_remove_layer(struct dx_parts *p, struct dx_cfg *c, unsigned part, unsigned idx)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(p, part, list);
    if (!n || idx >= n) return DX_PART_REFUSED;
    unsigned e = list[idx];
    p->layer[e].owner = DX_NO_PART;
    struct dx_part *x = &p->part[part];
    if (n == 1) {
        x->channel = 0; x->sel = 0; x->level = 127;
        memset(x->name, ' ', sizeof x->name);
        if (p->selected == part) {
            for (unsigned k = 1; k <= DX_PARTS; ++k) if (p->part[(part + k) % DX_PARTS].channel) { p->selected = (uint8_t)((part + k) % DX_PARTS); break; }
        }
    } else if (x->sel >= n - 1U) x->sel = (uint8_t)(n - 2U);
    dx_parts_derive(p, c);
    return (int)e;
}

/* Move a part to another channel. Refused (0) for a channel another part or the scene channel has. */
DX_FN int dx_parts_set_channel(struct dx_parts *p, struct dx_cfg *c, unsigned part, unsigned channel)
{
    if (!dx_part_used(p, part) || channel < 1 || channel > 16 || channel == c->scene_ch) return 0;
    unsigned other = dx_part_on_channel(p, channel);
    if (other != DX_NO_PART && other != part) return 0;
    p->part[part].channel = (uint8_t)channel;
    dx_parts_derive(p, c);
    return 1;
}

/* A program change on a layered part: its first layer stays (at its own level) and plays the new voice over the whole
 * keyboard, the other layers go. Returns the mask of engines that were freed. */
DX_FN unsigned dx_parts_collapse(struct dx_parts *p, struct dx_cfg *c, unsigned part)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(p, part, list), freed = 0;
    if (n < 2) return 0;
    for (unsigned i = 1; i < n; ++i) { p->layer[list[i]].owner = DX_NO_PART; freed |= 1U << list[i]; }
    p->part[part].sel = 0;
    struct dx_engine_cfg *e = &c->e[list[0]];
    uint8_t mute = e->mute, poly = e->poly;
    dx_layer_defaults(e);
    e->mute = mute; e->poly = poly;
    dx_parts_derive(p, c);
    return freed;
}

/* The parts' own edit of one channel for the screens: wrap a selected-part change. */
DX_FN void dx_parts_select(struct dx_parts *p, unsigned part)
{
    if (part < DX_PARTS) p->selected = (uint8_t)part;
}
#endif
