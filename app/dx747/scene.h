/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX747_SCENE_H
#define DX747_SCENE_H
/* DX747 scenes: what the eight engines play, where they listen and how they are
 * mixed, saved as a unit. 64 scenes (4 banks of 16) live in the former sample
 * region of the user flash, two per 4 KiB sector, written through the shared
 * staging sector so a power cut leaves either the old or the new scene.
 *
 * Everything here is control-task only. The engine is reached through struct
 * dx_port so the same code runs on the device and in the host tests. */
#include <stdint.h>
#include "route.h"

enum {
    DX_SCENES = 64, DX_SCENE_BANKS = 4, DX_SCENES_PER_BANK = 16,
    DX_PRESET_BYTES = 223,        /* the engine's struct groove_preset */
    DX_SCENE_BYTES = 1916,        /* packed scene image */
    DX_SLOT_BYTES = 2028,         /* two slots plus a 20 byte header fill a sector */
    DX_SCENE_SECTORS = 32,
    DX_SETTINGS_SECTOR = 32,      /* sector 32 holds the device settings in slot 0 */
    DX_STORE_SECTORS = 33
};
/* Flash layout. DX_STORE_BASE is where the stock sample bank starts. */
enum { DX_STORE_BASE = 69632, DX_STAGE = 65536, DX_SECTOR_BYTES = 4096 };
enum { DX_SCENE_EMPTY = 1, DX_SCENE_INVALID = -7, DX_SCENE_MEMORY = -8, DX_SCENE_NEEDS_CLAIM = -20 };
enum { DX_FILTER_OFF, DX_FILTER_LP, DX_FILTER_HP, DX_FILTER_BP };

struct dx_scene_engine {
    uint8_t preset[DX_PRESET_BYTES]; /* voice, mono/ADSR/LFO, volume, program, sends */
    uint16_t rx;                     /* routing, as in struct dx_engine_cfg */
    uint8_t lo, hi;
    int8_t shift;
    uint8_t fold, ctl, poly, mute;
    int8_t pan;                      /* -50..50, the engine does not persist it */
    uint8_t expr;                    /* 0..127 */
    int8_t bright;                   /* BRIGHT macro offset, -50..50 */
    uint8_t flags;                   /* reserved */
};
struct dx_scene {
    char name[12];                   /* space padded printable ASCII */
    uint8_t kbd_ch, pool;
    uint8_t effects[4];              /* shared reverb decay, delay time, feedback, return */
    uint8_t filter_type, filter_res;
    uint16_t filter_hz;
    struct dx_scene_engine e[DX_ENGINES];
};

/* State the engine cannot report back and the UI therefore owns. */
struct dx_live {
    int8_t pan[DX_ENGINES];
    uint8_t expr[DX_ENGINES];
    int8_t bright[DX_ENGINES];
    uint8_t filter_type, filter_res;
    uint16_t filter_hz;
};

/* The engine and the rest of the app, as scenes need them. */
struct dx_port {
    int (*export_preset)(unsigned engine, uint8_t preset[DX_PRESET_BYTES]);
    int (*import_preset)(unsigned engine, const uint8_t preset[DX_PRESET_BYTES]);
    int (*muted)(unsigned engine);
    void (*toggle_mute)(unsigned engine);
    void (*effects_get)(uint8_t values[4]);
    void (*effects_set)(const uint8_t values[4]);
    void (*cc)(unsigned engine, unsigned controller, unsigned value); /* on the engine's private channel */
    void (*bright)(unsigned engine, int offset);
    void (*filter)(unsigned type, unsigned hz, unsigned resonance);
    void (*panic)(void);                                              /* release every held note */
    void (*publish)(const struct dx_cfg *cfg);
};

/* Flash. index 0..63. All return 0 or a negative error; reads of an unused
 * scene return DX_SCENE_EMPTY. dx_scene_write and dx_scene_clear return
 * DX_SCENE_NEEDS_CLAIM until the user has agreed to give up what stands in the
 * way (dx_scene_claim): the stock sample bank or other foreign data in the store
 * flash, or an unfinished stock save in the staging sector. */
int dx_scene_store_state(void);       /* 0 usable, 1 holds foreign data (needs claim), <0 error */
int dx_scene_claim(void);
int dx_scene_recover(void);           /* called with the rest of the staging recovery */
int dx_scene_scan(void);              /* refresh the catalogue below; run at boot and after claim */
int dx_scene_read(unsigned index, struct dx_scene *out);
int dx_scene_write(unsigned index, const struct dx_scene *s);
int dx_scene_clear(unsigned index);
/* Catalogue for the Scenes screen, valid after dx_scene_scan. */
int dx_scene_info(unsigned index, char name[13], uint8_t *engines_used);

/* Device settings, kept in the same journalled store as the scenes. */
struct dx_settings {
    uint8_t radio_off;   /* GROOVE_RADIO_* bits that are switched off */
    uint8_t role;        /* BLE MIDI role: 0 peripheral, 1 central */
    uint8_t backlight;   /* idle button light, 0 (dark) to 8 */
    uint8_t last_scene;  /* scene recalled at power-on when flags bit 0 is set */
    uint8_t kbd_ch;      /* channel the built-in keys play on, 0..15 */
    uint8_t flags;       /* bit 0: last_scene is valid */
    uint8_t master_ch;   /* master control channel 1..16, 0 = none */
};
int dx_settings_read(struct dx_settings *out);   /* DX_SCENE_EMPTY when never saved */
int dx_settings_write(const struct dx_settings *s);
int dx_settings_valid(const struct dx_settings *s);

/* Packing: little-endian image of DX_SCENE_BYTES with every field range checked. */
int dx_scene_valid(const struct dx_scene *s);
void dx_scene_pack(const struct dx_scene *s, uint8_t out[DX_SCENE_BYTES]);
int dx_scene_unpack(const uint8_t in[DX_SCENE_BYTES], struct dx_scene *s);

/* Live state to and from a scene. */
int dx_scene_capture(const struct dx_port *port, const struct dx_cfg *cfg, const struct dx_live *live,
                     const char name[12], struct dx_scene *out);
int dx_scene_apply(const struct dx_port *port, const struct dx_scene *s, struct dx_cfg *cfg, struct dx_live *live);

#endif
