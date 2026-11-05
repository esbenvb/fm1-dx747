/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 boot, settings and storage ownership. This replaces the stock project
 * slots, tuning journal and session restore, none of which a DX7 module uses.
 *
 * At power-on the poll below (a) turns the eight tracks into DX7 engines in the
 * PLAY role (dx747/boot.inc), (b) reads the device settings from the scene
 * store and applies them, (c) catalogues the scenes and recalls the last one,
 * and keeps the Bluetooth radio configured. Boot never writes flash. Settings
 * changes are saved once they have been quiet for 1.5 s. The shared staging
 * sector is recovered here for every user of it: the DX7 voice library and the
 * scene store. */
#include "instrument.h"
#include "tuning_hook.h"
#include "fm_patch.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "dx747/dx747.h"
#include "version.h"
#include "dx747/scene.inc"
#include "dx747/boot.inc"
#include "dx747/ctl.inc"

/* The engine as the control logic edits it. */
static int ep_copy(unsigned e, uint8_t v[156]) { return groove_fm_copy(e, v, NULL); }
static void ep_edit(unsigned e, unsigned parameter, int delta) { groove_fm_edit(e, parameter, delta); }
static void ep_name(unsigned e, unsigned index, char c) { groove_fm_name_edit(e, index, c); }
static void ep_toggle(unsigned e, unsigned op) { groove_operator_toggle(e, op); }
static int ep_import(unsigned e, const uint8_t v[156]) { return groove_fm_import(e, v); }
/* Breath fully open, the engine's own default: it makes the engine apply its controller state again. */
static int ep_refresh(unsigned e)
{
    const struct dx_cfg *c = dx_route_cfg();
    uint8_t m[3] = {(uint8_t)(0xb0U | c->priv[e]), 2, 127};
    return fm1_midi_post(m, 3);
}
/* CC7 on the engine's own channel: layer level x part level. A full ring is reported and retried by the caller. */
static int ep_level(unsigned e, unsigned value)
{
    const struct dx_cfg *c = dx_route_cfg();
    uint8_t m[3] = {(uint8_t)(0xb0U | c->priv[e]), 7, (uint8_t)value};
    return fm1_midi_post(m, 3);
}
static const struct dx_edit_port edit_port = {ep_copy, ep_edit, ep_name, ep_toggle, ep_import, ep_refresh, ep_level};
static struct dx_ctl ctl;
#include "dx747/scene_port.inc"

/* The build fails when these functions are missing from the image, and the
 * linker keeps them only while the app references them, but DX747 has no
 * screen that calls them (no tuning, no Bluetooth scan or connect page). The
 * link uses --gc-sections, which drops a data array nothing points at even when
 * it is marked used, so the references sit in code the poll can reach. The
 * trigger is volatile and never set; it only stops the compiler proving that. */
extern int groove_project_apply_edit(const struct groove_project *, unsigned) __attribute__((weak));
static volatile unsigned keep_trigger;
static const void *volatile keep_sink;
static void keep_references(void)
{
    static const void *const refs[] = {
        (const void *)groove_tuning_capture, (const void *)groove_tuning_apply, (const void *)groove_project_apply_edit,
        (const void *)fm1_ble_scan, (const void *)fm1_ble_connect, (const void *)fm1_ble_disconnect,
        (const void *)fm1_ble_get_status, (const void *)fm1_ble_get_device, (const void *)fm1_bluetooth_get_status
    };
    if (!keep_trigger) return;
    for (unsigned i = 0; i < sizeof refs / sizeof refs[0]; ++i) keep_sink = refs[i];
}

/* The engine captures and applies projects through this buffer. */
static struct groove_project scratch;
struct groove_project *groove_project_scratch(void) { return &scratch; }

uint32_t groove_crc32(uint32_t crc, const void *data, unsigned bytes) { return dx_crc32(crc, data, bytes); }

static struct dx_settings settings = {0, 0, 3, 0, 0, 0, 0};
static struct dx_live live;
static struct dx_boot booted;
static unsigned stage, stage2_busy, radio_pending, settings_dirty, settings_failures, current_scene = DX_NO_SCENE, safe_boot;
static uint32_t settings_changed;
static int settings_result, storage_state;
static char project_name[12] = {'D', 'X', '7', '4', '7', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
static uint32_t project_revision;

static int recover_staging(void)
{
    int r = groove_fm_library_recover();
    return r ? r : dx_scene_recover();
}
int groove_storage_prepare(void) { return recover_staging(); }

static void settings_mark(void)
{
    settings_dirty = 1;
    settings_failures = 0;
    settings_changed = fm1_millis();
    settings_result = 0;
}

/* ---- what the screens use ---------------------------------------------- */

unsigned dx_engines_up(void) { return booted.up; }
unsigned dx_safe_boot(void) { return safe_boot; }
void dx_safe_boot_enter(void) { safe_boot = 1; }
struct dx_live *dx_live_edit(void) { return &live; }
unsigned dx_scene_current(void) { return current_scene; }
void dx_settings_get(struct dx_settings *out) { *out = settings; }
void dx_settings_update(const struct dx_settings *s)
{
    if (!dx_settings_valid(s) || !memcmp(s, &settings, sizeof settings)) return;
    unsigned radio = s->radio_off != settings.radio_off || s->role != settings.role;
    unsigned light = s->backlight != settings.backlight, scene = s->scene_ch != settings.scene_ch, master = s->master_ch != settings.master_ch;
    settings = *s;
    if (radio) radio_pending = 2;
    if (light) fm1_led_idle_level(settings.backlight);
    if (scene || master) { /* the routing follows both control channels */
        struct dx_cfg c = *dx_route_cfg();
        c.scene_ch = settings.scene_ch; c.master_ch = settings.master_ch;
        dx_route_publish(&c);
    }
    settings_mark();
}
int dx_storage_state(void) { return storage_state = dx_scene_store_state(); }
int dx_storage_state_cached(void) { return storage_state; }
void dx_apply_filter(void) { port_filter(live.filter_type, live.filter_hz, live.filter_res); }
void dx_bright_changed(unsigned engine) { dx_bright_changed_ctl(&ctl, engine); }
void dx_bright_force_voice(unsigned engine) { dx_voice_loaded(&ctl, engine); }
int dx_sysex_in(const uint8_t *message, size_t bytes) { return dx_ctl_sysex_in(&ctl, message, bytes); }
unsigned dx_sysex_dropped(void) { return ctl.sx_dropped; }
void dx_bright_unbias(unsigned engine, uint8_t voice[156]) { dx_bright_export_fix(&ctl, engine, voice); }
int dx_storage_claim(void)
{
    int r = dx_scene_claim();
    if (!r) { storage_state = 0; (void)dx_scene_scan(); settings_mark(); }
    return r;
}

static unsigned engine_solo(unsigned e);
int dx_scene_recall(unsigned index)
{
    struct dx_scene *s = malloc(sizeof *s);
    if (!s) return DX_SCENE_MEMORY;
    int r = dx_scene_read(index, s);
    if (!r) {
        struct dx_cfg c = *dx_route_cfg();
        r = dx_scene_apply(&dx_device_port, s, &c, &live);
        for (unsigned e = 0; e < DX_ENGINES; ++e)       /* solo is not part of a scene: an engine the scene left free must not stay soloed, it would silence every part */
            if (live.parts.layer[e].owner >= DX_PARTS && engine_solo(e)) groove_mix_toggle(e, 1);
        current_scene = index;
        settings.last_scene = (uint8_t)index; settings.flags |= 1;
        settings_mark();
    }
    free(s);
    return r;
}

int dx_scene_save(unsigned index, const char name[12])
{
    struct dx_scene *s = malloc(sizeof *s);
    if (!s) return DX_SCENE_MEMORY;
    int r = dx_scene_capture(&dx_device_port, dx_route_cfg(), &live, name, s);
    if (!r) r = dx_scene_write(index, s);
    if (!r) { current_scene = index; settings.last_scene = (uint8_t)index; settings.flags |= 1; settings_mark(); }
    free(s);
    return r;
}

/* ---- parts: what the screens edit ------------------------------------------- */

/* A layer that was just given an engine starts clean: centred, full expression, BRIGHT 0, unmuted, and at a level the
 * engine is told. The voice it holds is whatever the engine last had; the screens then offer the voice to change. */
/* Solo is the mixer's flag on the engine, so it has to follow the layers: an engine that is free is never left soloed (it
 * would silence every part once the part's own solo is switched off), and a layer added to a soloed part joins the solo. */
static unsigned engine_solo(unsigned e)
{
    struct groove_view v;
    groove_get_view(e, 0, &v);
    return v.solo;
}
static void layer_begin(unsigned e, unsigned part)
{
    live.pan[e] = 0; live.bright[e] = 0; live.expr[e] = 127;
    live.parts.layer[e].lvl = 100;
    port_cc(e, 10, 64); port_cc(e, 11, 127);
    if (engine_solo(e)) groove_mix_toggle(e, 1);
    if (port_muted(e)) port_toggle(e);
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(&live.parts, part, list), soloed = 0;
    for (unsigned i = 0; i < n; ++i) soloed |= list[i] != e && engine_solo(list[i]);
    if (soloed) groove_mix_toggle(e, 1);          /* soloing it also clears its own mute */
    dx_bright_changed_ctl(&ctl, e);
    dx_level_changed_ctl(&ctl, e);
}
/* A layer that lost its engine stops sounding, and stops being soloed. */
static void layer_end(unsigned e)
{
    port_cc(e, 123, 0); port_cc(e, 120, 0);
    if (engine_solo(e)) groove_mix_toggle(e, 1);
}
static void parts_publish(const struct dx_cfg *c) { dx_route_publish(c); }

int dx_part_add(unsigned slot)
{
    struct dx_cfg c = *dx_route_cfg();
    int part = dx_parts_add_part(&live.parts, &c, slot);
    if (part < 0) return part;
    uint8_t list[DX_ENGINES];
    if (dx_part_layers(&live.parts, (unsigned)part, list)) layer_begin(list[0], (unsigned)part);
    parts_publish(&c);
    return part;
}
int dx_part_add_layer(unsigned part)
{
    struct dx_cfg c = *dx_route_cfg();
    int e = dx_parts_add_layer(&live.parts, &c, part);
    if (e < 0) return e;
    layer_begin((unsigned)e, part);
    parts_publish(&c);
    return e;
}
int dx_part_remove_layer(unsigned part, unsigned layer)
{
    struct dx_cfg c = *dx_route_cfg();
    int e = dx_parts_remove_layer(&live.parts, &c, part, layer);
    if (e < 0) return e;
    parts_publish(&c);
    layer_end((unsigned)e);
    return e;
}
int dx_part_set_channel(unsigned part, unsigned channel)
{
    struct dx_cfg c = *dx_route_cfg();
    if (!dx_parts_set_channel(&live.parts, &c, part, channel)) return 0;
    parts_publish(&c);
    return 1;
}
void dx_part_select(unsigned part)
{
    if (part >= DX_PARTS || live.parts.selected == part) return;
    struct dx_cfg c = *dx_route_cfg();
    dx_parts_select(&live.parts, part);
    dx_parts_derive(&live.parts, &c);
    parts_publish(&c);
}
void dx_part_select_layer(unsigned part, unsigned layer)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(&live.parts, part, list);
    if (!n || layer >= n || live.parts.part[part].sel == layer) return;
    struct dx_cfg c = *dx_route_cfg();
    live.parts.part[part].sel = (uint8_t)layer;
    dx_parts_derive(&live.parts, &c);
    parts_publish(&c);
}
void dx_level_changed(unsigned engine) { dx_level_changed_ctl(&ctl, engine); }
void dx_part_level_changed(unsigned part)
{
    for (unsigned e = 0; e < DX_ENGINES; ++e) if (live.parts.layer[e].owner == part) dx_level_changed_ctl(&ctl, e);
}

/* A program change on a layered part: the part becomes the one voice, on its first engine, over the whole keyboard. */
static void part_program(unsigned first_engine, unsigned program)
{
    unsigned part = live.parts.layer[first_engine].owner;
    uint8_t list[DX_ENGINES];
    if (part >= DX_PARTS || !dx_part_layers(&live.parts, part, list)) return;
    unsigned e0 = list[0];
    struct dx_cfg c = *dx_route_cfg();
    unsigned freed = dx_parts_collapse(&live.parts, &c, part);
    parts_publish(&c);
    for (unsigned e = 0; e < DX_ENGINES; ++e) if ((freed >> e) & 1U) layer_end(e);
    const uint8_t pc[2] = {(uint8_t)(0xc0U | c.priv[e0]), (uint8_t)program};
    (void)fm1_midi_post(pc, 2);
    dx_ctl_program_sent(&ctl, e0, fm1_millis());   /* BRIGHT starts from the new voice once the engine has loaded it */
    dx_level_changed_ctl(&ctl, e0);
}
/* Work the router could only record: layered program changes and scene requests from MIDI. */
static void service_midi_requests(void)
{
    unsigned pcx = ctl.pcx;
    ctl.pcx = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) if ((pcx >> e) & 1U) part_program(e, ctl.pcx_prog[e]);
    if (ctl.scene_req) {
        unsigned index = ctl.scene_req - 1U;
        ctl.scene_req = 0;
        if (index < DX_SCENES) (void)dx_scene_recall(index); /* an empty slot does nothing */
    }
}

#include "dx747/config.inc"

/* ---- project hooks the engine links against ----------------------------- */

void groove_project_name(char name[13]) { memcpy(name, project_name, 12); name[12] = 0; }
void groove_project_metadata(const struct groove_project *p)
{
    memcpy(project_name, p->name, sizeof project_name);
    ++project_revision;
}
uint32_t groove_project_revision(void) { return project_revision; }

void groove_project_init(void)
{
    memset(&live, 0, sizeof live);
    memset(&ctl, 0, sizeof ctl);
    ctl.port = &edit_port;
    for (unsigned e = 0; e < DX_ENGINES; ++e) live.expr[e] = 127;
    live.filter_hz = 1000;
    dx_parts_default(&live.parts);
    memset(&booted, 0, sizeof booted);
    settings = (struct dx_settings){0, 0, 3, 0, 0, 0, 0};
    stage = 0; stage2_busy = 0; radio_pending = 0; settings_dirty = 0; settings_failures = 0; settings_result = 0; storage_state = 0; safe_boot = 0;
    current_scene = DX_NO_SCENE; project_revision = 0;
}

void groove_project_poll(void)
{
    keep_references();
    if (safe_boot) return; /* the engine calls this every tick and ignores what it does; stage stays 0 */
    if (stage >= 3) {
        if (dx_ctl_poll(&ctl, dx_route_mailbox(), dx_route_cfg(), &live, fm1_millis()) & DX_CTL_FILTER) dx_apply_filter();
        service_midi_requests();
    }
    if (stage == 0) {
        dx_boot_engines(&booted);
        struct dx_cfg c = *dx_route_cfg();
        c.up = booted.up;
        dx_route_publish(&c);
        ctl.refresh |= (uint8_t)booted.up; /* creating the engines panicked them */
        for (unsigned e = 0; e < DX_ENGINES; ++e) { /* a layer starts at the level its engine has: part level 127 changes nothing */
            struct groove_view v;
            if (!((booted.up >> e) & 1U)) continue;
            groove_get_view(e, 0, &v);
            live.parts.layer[e].lvl = (uint8_t)(v.volume > 127 ? 127 : v.volume);
        }
        stage = 1;
        return;
    }
    if (stage == 1) {
        struct dx_settings stored;
        int r = dx_settings_read(&stored);
        if (r == FM1_FLASH_BUSY) return;
        if (!r) settings = stored;
        settings.role = FM1_BLE_PERIPHERAL; /* Central needs a scan page, which DX747 does not have yet */
        fm1_led_idle_level(settings.backlight);
        struct dx_cfg c = *dx_route_cfg();
        c.scene_ch = settings.scene_ch; c.master_ch = settings.master_ch;
        dx_route_publish(&c);
        radio_pending = 2;
        stage = 2;
        return;
    }
    if (stage == 2) {
        storage_state = dx_scene_store_state();
        if (storage_state == FM1_FLASH_BUSY) return; /* try again on the next poll */
        /* Reading is CRC-gated and writes nothing, so scenes that are already there show up and recall even
         * while the store still has foreign data in it (state 1): the claim only matters for writing. */
        if (storage_state >= 0) {
            int scanned = dx_scene_scan();
            if (scanned == FM1_FLASH_BUSY && ++stage2_busy < 100) return; /* the whole stage again: nothing was published */
            if (!scanned && (settings.flags & 1) && settings.last_scene < DX_SCENES) (void)dx_scene_recall(settings.last_scene);
            settings_dirty = 0; /* recalling at boot is not a change worth a flash write */
        }
        stage = 3;
        return;
    }
    if (radio_pending == 2) {
        unsigned flags = groove_settings_radio_flags();
        unsigned features = flags & GROOVE_RADIO_ON ? flags >> 1 : 0;
        if (!fm1_bluetooth_configure(features)) radio_pending = 1;
    }
    if (radio_pending == 1) {
        int r = fm1_ble_set_role(settings.role);
        if (!r || (r != -2 && r != -3)) radio_pending = 0;
    }
    if (settings_dirty && fm1_millis() - settings_changed >= 1500U) {
        int r = dx_settings_write(&settings);
        if (r == DX_SCENE_NEEDS_CLAIM) { storage_state = 1; settings_dirty = 0; } /* the screens ask the user, then dx_storage_claim */
        else {
            settings_result = r;
            /* A failing write erased the shared staging sector on the way: three tries, then wait for the next change. */
            if (!r) settings_dirty = 0;
            else if (++settings_failures >= 3) { settings_dirty = 0; storage_state = r; } /* the System screen then says ERROR */
            else settings_changed = fm1_millis();
        }
    }
}

/* ---- Bluetooth page (stock screen code) --------------------------------- */

unsigned groove_settings_radio_flags(void) { return (GROOVE_RADIO_ALL ^ settings.radio_off) | GROOVE_RADIO_MIDI; }
unsigned groove_settings_role_value(void) { return settings.role; }
int groove_settings_radio(unsigned flags, unsigned role)
{
    if ((flags & ~GROOVE_RADIO_ALL) || role > FM1_BLE_CENTRAL) return -1;
    flags |= GROOVE_RADIO_MIDI;
    if (flags == groove_settings_radio_flags() && role == settings.role) return 0;
    struct dx_settings s = settings;
    s.radio_off = (uint8_t)(GROOVE_RADIO_ALL ^ flags);
    s.role = (uint8_t)role;
    dx_settings_update(&s);
    return 0;
}
int groove_settings_status(void) { return settings_result; }

/* ---- stand-ins for the stock project and settings API -------------------
 * Removed together with the stock screens; until then they keep the other
 * stock files linkable and do nothing. */
unsigned groove_project_slot(void) { return 0; }
void groove_project_rename(unsigned character, char value)
{
    if (character < 12 && value >= 32 && value <= 126) project_name[character] = value;
}
/* Every slot is empty. The workbench page reads the name of all sixteen before it unlocks its voice-bank controls, so
 * the name has to be defined (twelve spaces) rather than whatever the caller's stack held. */
int groove_project_info(unsigned slot, char name[13])
{
    (void)slot;
    memset(name, ' ', 12);
    name[12] = 0;
    return GROOVE_PROJECT_EMPTY;
}
int groove_project_save_slot(unsigned slot) { (void)slot; return GROOVE_PROJECT_EMPTY; }
int groove_project_load_slot(unsigned slot) { (void)slot; return GROOVE_PROJECT_EMPTY; }
int groove_project_import(const uint8_t *bytes, unsigned length) { (void)bytes; (void)length; return GROOVE_PROJECT_INVALID; }
/* DX747 has no project, but the workbench page asks for the working project (size 4064, or 4264 with the tuning
 * block) before it enables the bank read, import and voice controls, and it checks the file's format: version byte
 * 4 at 3852, the project name at 3836, and for the tuned size the "FM1T" block with a CRC-32 over its first 196
 * bytes and bytes 4..195 below 128 (sdk/WebUpdate.js projectVersion, sdk/Update.html.in refreshLibrary). Refusing
 * it, as the first version of this stand-in did, kept the whole tab locked. So hand out a valid, empty project
 * (tempo 120 and the default sequence options, whose version byte is the 4 at 3852). */
int groove_project_export(uint8_t *bytes, unsigned capacity)
{
    if (!bytes || (capacity != GROOVE_PROJECT_BYTES && capacity != GROOVE_PROJECT_TUNED_BYTES)) return GROOVE_PROJECT_INVALID;
    memset(bytes, 0, capacity);
    /* Fields set through offsets, because the caller's buffer is not necessarily aligned for the struct. The
     * firmware's own validator (groove_project_valid) accepts this: a stock or Open firmware can restore it. */
    const uint16_t tempo = 120;
    struct groove_sequence_options sequence;
    groove_sequence_defaults(&sequence);
    memcpy(bytes + offsetof(struct groove_project, tempo), &tempo, sizeof tempo);
    memcpy(bytes + offsetof(struct groove_project, sequence), &sequence, sizeof sequence);
    memcpy(bytes + offsetof(struct groove_project, name), project_name, 12);
    if (capacity == GROOVE_PROJECT_TUNED_BYTES) {
        uint8_t *block = bytes + GROOVE_PROJECT_BYTES;
        const uint32_t magic = 0x54314d46U; /* "FM1T" */
        memcpy(block, &magic, 4);
        const uint32_t crc = ~dx_crc32(~0U, block, 196);
        memcpy(block + 196, &crc, 4);
    }
    return 0;
}
int groove_factory_load(unsigned index) { (void)index; return GROOVE_PROJECT_INVALID; }
void groove_settings_clock(unsigned mode) { (void)mode; }
void groove_settings_midi(unsigned track, unsigned parameter, int delta) { (void)track; (void)parameter; (void)delta; }
int groove_settings_track(unsigned track, unsigned parameter, int delta) { (void)track; (void)parameter; (void)delta; return 0; }
