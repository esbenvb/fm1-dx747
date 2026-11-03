/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX747_H
#define DX747_H
/* What the DX747 screens call: scenes, settings, storage setup and the panel
 * brightness hooks. Control task only. The router API is in dx747/route.h and
 * the scene store in dx747/scene.h; both are pulled in here. */
#include <stddef.h>
#include <stdint.h>
#include "route.h"
#include "scene.h"

/* ---- app/project.c ---- */
#define DX_NO_SCENE DX_SCENES
unsigned dx_engines_up(void);                          /* bit e: engine e is a live DX7 */
struct dx_live *dx_live_edit(void);                    /* pan, expression, BRIGHT, filter: owned by the UI */
void dx_apply_filter(void);                         /* push the live master-filter settings to the audio stage */
void dx_bright_changed(unsigned engine);               /* the UI changed live.bright[engine]: apply it */
void dx_bright_force_voice(unsigned engine);           /* the engine just loaded a new voice: re-base BRIGHT on it */
int dx_sysex_in(const uint8_t *message, size_t bytes); /* DX7 SysEx from the MIDI hook; 1 = consumed. Any task. */
unsigned dx_sysex_dropped(void);                       /* DX7 SysEx that could not be queued, since power-on */
void dx_bright_unbias(unsigned engine, uint8_t voice[156]); /* an exported voice back to its own levels (BRIGHT removed) */
int dx_scene_recall(unsigned index);                   /* read and apply; remembered for power-on */
int dx_scene_save(unsigned index, const char name[12]);/* capture the live state into a slot */
unsigned dx_scene_current(void);                       /* last recalled or saved, DX_NO_SCENE if none */
void dx_settings_get(struct dx_settings *out);
void dx_settings_update(const struct dx_settings *s);  /* applied now, saved after 1.5 s of quiet */
int dx_storage_state(void);                            /* 0 usable, 1 needs dx_storage_claim, <0 error */
int dx_storage_state_cached(void);                     /* the same, from the last check; no flash access */
int dx_storage_claim(void);                            /* erases the stock sample bank: ask the user first */

/* ---- app/board/panel.c ---- */
void fm1_led_level(unsigned key, unsigned level);      /* 0..8 */
void fm1_led_idle_level(unsigned level);               /* every LED not set on or to a level */

#endif
