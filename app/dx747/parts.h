/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX747_PARTS_H
#define DX747_PARTS_H
/* DX747 parts: the layer above the router that the screens and the scenes work with
 * (docs/design/PARTS-MODEL.md).
 *
 * A PART is one MIDI channel with one sound. A sound is one to eight LAYERS, each a DX7 voice
 * on its own engine with its own key range, velocity range and shift. There are at most eight
 * parts and eight engines; every layer needs an engine, so the engines are the only limit.
 *
 * The router (route.h) knows nothing of parts. It reads a struct dx_cfg, and dx_parts_derive
 * (parts.inc) fills the routing fields of that from the parts: which channel an engine listens
 * on, which engine takes DX7 SysEx, which channel the keyboard plays. The layer settings that
 * the router needs (note and velocity window, shift, fold, voice limit, mute) stay in the
 * engine's entry of struct dx_cfg. What the router does not need lives here.
 *
 * Control task only, like everything that edits the configuration. */
#include <stdint.h>
#include "route.h"

enum { DX_PARTS = 8, DX_NO_PART = 0xff };

struct dx_part {
    uint8_t channel;   /* MIDI channel 1..16, 0 = this part slot is unused */
    uint8_t level;     /* part level 0..127 (CC7): scales every layer */
    uint8_t sel;       /* the selected layer, 0-based among the part's layers: the DX7 SysEx target */
    char name[12];     /* space padded printable ASCII; shown when the sound has several layers */
};
struct dx_layer {
    uint8_t owner;     /* the part this engine is a layer of, DX_NO_PART = the engine is free */
    uint8_t lvl;       /* level inside the part 0..127; the engine plays at lvl * part level / 127 */
};
struct dx_parts {
    struct dx_part part[DX_PARTS];
    struct dx_layer layer[DX_ENGINES];  /* by engine */
    uint8_t selected;                   /* the part the built-in keyboard plays and the editing screens work on (it may be an empty slot) */
};

#endif
