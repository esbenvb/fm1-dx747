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
static const struct dx_edit_port edit_port = {ep_copy, ep_edit, ep_name, ep_toggle, ep_import, ep_refresh};
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
static unsigned stage, radio_pending, settings_dirty, current_scene = DX_NO_SCENE;
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
    settings_changed = fm1_millis();
    settings_result = 0;
}

/* ---- what the screens use ---------------------------------------------- */

unsigned dx_engines_up(void) { return booted.up; }
struct dx_live *dx_live_edit(void) { return &live; }
unsigned dx_scene_current(void) { return current_scene; }
void dx_settings_get(struct dx_settings *out) { *out = settings; }
void dx_settings_update(const struct dx_settings *s)
{
    if (!dx_settings_valid(s) || !memcmp(s, &settings, sizeof settings)) return;
    unsigned radio = s->radio_off != settings.radio_off || s->role != settings.role;
    unsigned light = s->backlight != settings.backlight, kbd = s->kbd_ch != settings.kbd_ch, master = s->master_ch != settings.master_ch;
    settings = *s;
    if (radio) radio_pending = 2;
    if (light) fm1_led_idle_level(settings.backlight);
    if (kbd || master) { /* only the field that changed: a recalled scene may have set the other one */
        struct dx_cfg c = *dx_route_cfg();
        if (kbd) c.kbd_ch = settings.kbd_ch;
        if (master) c.master_ch = settings.master_ch;
        dx_route_publish(&c);
    }
    settings_mark();
}
/* One step of the built-in keyboard's channel from the channel that is active now (a scene may have set it); the setting
 * that comes back at power-on follows. */
void dx_keyboard_channel_step(int delta)
{
    struct dx_cfg c = *dx_route_cfg();
    int ch = ((int)c.kbd_ch + delta) % 16;
    c.kbd_ch = (uint8_t)(ch < 0 ? ch + 16 : ch);
    dx_route_publish(&c);
    if (settings.kbd_ch != c.kbd_ch) { settings.kbd_ch = c.kbd_ch; settings_mark(); }
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

int dx_scene_recall(unsigned index)
{
    struct dx_scene *s = malloc(sizeof *s);
    if (!s) return DX_SCENE_MEMORY;
    int r = dx_scene_read(index, s);
    if (!r) {
        struct dx_cfg c = *dx_route_cfg();
        r = dx_scene_apply(&dx_device_port, s, &c, &live);
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
    memset(&booted, 0, sizeof booted);
    settings = (struct dx_settings){0, 0, 3, 0, 0, 0, 0};
    stage = 0; radio_pending = 0; settings_dirty = 0; settings_result = 0; storage_state = 0;
    current_scene = DX_NO_SCENE; project_revision = 0;
}

void groove_project_poll(void)
{
    keep_references();
    if (stage >= 3 && (dx_ctl_poll(&ctl, dx_route_mailbox(), dx_route_cfg(), &live, fm1_millis()) & DX_CTL_FILTER)) dx_apply_filter();
    if (stage == 0) {
        dx_boot_engines(&booted);
        struct dx_cfg c = *dx_route_cfg();
        c.up = booted.up;
        dx_route_publish(&c);
        ctl.refresh |= (uint8_t)booted.up; /* creating the engines panicked them */
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
        c.kbd_ch = settings.kbd_ch; c.master_ch = settings.master_ch;
        dx_route_publish(&c);
        radio_pending = 2;
        stage = 2;
        return;
    }
    if (stage == 2) {
        storage_state = dx_scene_store_state();
        if (!storage_state) {
            (void)dx_scene_scan();
            if ((settings.flags & 1) && settings.last_scene < DX_SCENES) (void)dx_scene_recall(settings.last_scene);
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
            if (!r) settings_dirty = 0; else settings_changed = fm1_millis();
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
int groove_project_info(unsigned slot, char name[13]) { (void)slot; (void)name; return GROOVE_PROJECT_EMPTY; }
int groove_project_save_slot(unsigned slot) { (void)slot; return GROOVE_PROJECT_EMPTY; }
int groove_project_load_slot(unsigned slot) { (void)slot; return GROOVE_PROJECT_EMPTY; }
int groove_project_import(const uint8_t *bytes, unsigned length) { (void)bytes; (void)length; return GROOVE_PROJECT_INVALID; }
int groove_project_export(uint8_t *bytes, unsigned capacity) { (void)bytes; (void)capacity; return GROOVE_PROJECT_INVALID; }
int groove_factory_load(unsigned index) { (void)index; return GROOVE_PROJECT_INVALID; }
void groove_settings_clock(unsigned mode) { (void)mode; }
void groove_settings_midi(unsigned track, unsigned parameter, int delta) { (void)track; (void)parameter; (void)delta; }
int groove_settings_track(unsigned track, unsigned parameter, int delta) { (void)track; (void)parameter; (void)delta; return 0; }
