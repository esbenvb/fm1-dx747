/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 configuration protocol: the workbench page reads and changes parts, layers, filter, effects, scenes and settings
 * with these (docs/CONFIG-PROTOCOL.md). Included by app/project.c after dx747/scene_port.inc. Control task only: the
 * workbench transfer poll runs there, like the panel code, so everything the screens call may be called from here. */

enum { DX_CFG_PROTOCOL = 1 };                        /* DX_CFG_STATE_BYTES is in dx747.h */
_Static_assert(16 + 8 * 15 + 8 * 26 + 2 == DX_CFG_STATE_BYTES, "state image layout");
static uint32_t cfg_last_scene_write;
static uint8_t cfg_image[DX_CFG_STATE_BYTES];

static int cfg_printable(const uint8_t *s, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (s[i] < 32 || s[i] > 126) return 0;
    return 1;
}

/* The state image (transfer kind 5), a fresh snapshot every time. */
int dx_config_state(uint8_t *o)
{
    memset(o, 0, DX_CFG_STATE_BYTES);
    const struct dx_cfg *c = dx_route_cfg();
    o[0] = 1; o[1] = live.parts.selected; o[2] = (uint8_t)booted.up; o[3] = (uint8_t)current_scene;
    o[4] = settings.scene_ch; o[5] = settings.master_ch; o[6] = settings.backlight; o[7] = (settings.radio_off & GROOVE_RADIO_AUDIO) ? 1U : 0U; /* 1: Bluetooth audio in is off */
    o[8] = live.filter_type; o[9] = live.filter_res; o[10] = (uint8_t)live.filter_hz; o[11] = (uint8_t)(live.filter_hz >> 8);
    groove_effects_get(o + 12);
    for (unsigned p = 0; p < DX_PARTS; ++p) {
        uint8_t *q = o + 16 + 15U * p;
        const struct dx_part *pt = &live.parts.part[p];
        q[0] = pt->channel; q[1] = pt->level; q[2] = pt->sel;
        memcpy(q + 3, pt->name, 12);
    }
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        uint8_t *q = o + 136 + 26U * e, sends[2] = {0, 0}, patch[156];
        const struct dx_engine_cfg *x = &c->e[e];
        struct groove_view v;
        groove_get_view(e, 0, &v);
        groove_sends_get(e, sends);
        q[0] = live.parts.layer[e].owner; q[1] = live.parts.layer[e].lvl;
        q[2] = (uint8_t)(live.pan[e] + 50); q[3] = live.expr[e]; q[4] = (uint8_t)(live.bright[e] + 50);
        q[5] = sends[0] & 15U; q[6] = sends[1] & 15U; q[7] = v.program;
        q[8] = (uint8_t)((v.muted ? 1U : 0U) | (v.solo ? 2U : 0U) | (v.audible ? 4U : 0U) | (((booted.up >> e) & 1U) ? 8U : 0U));
        q[9] = x->lo; q[10] = x->hi; q[11] = (uint8_t)(x->shift + 64); q[12] = x->fold; q[13] = x->poly;
        q[14] = x->vlo ? x->vlo : 1U; q[15] = x->vhi ? x->vhi : 127U;
        memset(q + 16, ' ', 10);
        if (groove_fm_copy(e, patch, NULL) == 0)
            for (unsigned i = 0; i < 10; ++i) q[16 + i] = patch[145 + i] >= 32 && patch[145 + i] <= 126 ? patch[145 + i] : ' ';
    }
    o[344] = (uint8_t)dx_scene_revision(); o[345] = (uint8_t)(dx_scene_revision() >> 8);   /* changes whenever a scene is written or deleted */
    return 0;
}

/* A solo or a mute changes whether the router sends the engine notes. */
static void cfg_sync_mute(unsigned e)
{
    struct dx_cfg c = *dx_route_cfg();
    if (!!c.e[e].mute == !!port_muted(e)) return;
    c.e[e].mute = (uint8_t)!!port_muted(e);
    dx_route_publish(&c);
}

static int cfg_set_layer(unsigned e, unsigned f, unsigned v)
{
    if (e >= DX_ENGINES || live.parts.layer[e].owner >= DX_PARTS) return GROOVE_PROJECT_INVALID;
    struct dx_cfg c = *dx_route_cfg();
    struct dx_engine_cfg *x = &c.e[e];
    uint8_t sends[2];
    struct groove_view view;
    switch (f) {
    case 0: if (v > 127) return GROOVE_PROJECT_INVALID; live.parts.layer[e].lvl = (uint8_t)v; dx_level_changed(e); return 0;
    case 1: {
        if (v > 100) return GROOVE_PROJECT_INVALID;
        live.pan[e] = (int8_t)((int)v - 50);
        int cc = 64 + live.pan[e] * 63 / 50;
        port_cc(e, 10, (unsigned)(cc < 1 ? 1 : cc > 127 ? 127 : cc));       /* as the panel's pan knob does */
        return 0;
    }
    case 2: if (v > 100) return GROOVE_PROJECT_INVALID; live.bright[e] = (int8_t)((int)v - 50); dx_bright_changed(e); return 0;
    case 3: case 4:
        if (v > 15) return GROOVE_PROJECT_INVALID;
        groove_sends_get(e, sends);
        groove_sends_edit(e, f - 3U, (int)v - (int)(sends[f - 3U] & 15U));
        return 0;
    case 5: if (v > 127) return GROOVE_PROJECT_INVALID; x->lo = (uint8_t)v; if (x->hi < x->lo) x->hi = x->lo; break;
    case 6: if (v > 127) return GROOVE_PROJECT_INVALID; x->hi = (uint8_t)v; if (x->lo > x->hi) x->lo = x->hi; break;
    case 7: if (v < 16 || v > 112) return GROOVE_PROJECT_INVALID; x->shift = (int8_t)((int)v - 64); break;
    case 8: if (v > 2) return GROOVE_PROJECT_INVALID; x->fold = (uint8_t)v; break;
    case 9: case 10: {
        unsigned lo = x->vlo ? x->vlo : 1U, hi = x->vhi ? x->vhi : 127U;
        if (v < 1 || v > 127) return GROOVE_PROJECT_INVALID;
        if (f == 9) { lo = v; if (hi < lo) hi = lo; } else { hi = v; if (lo > hi) lo = hi; }
        x->vlo = (uint8_t)lo; x->vhi = (uint8_t)hi; break;
    }
    case 11: if (v < 1 || v > 8) return GROOVE_PROJECT_INVALID; x->poly = (uint8_t)v; break;
    case 12: if (v > 1) return GROOVE_PROJECT_INVALID; if (!!port_muted(e) != !!v) port_toggle(e); cfg_sync_mute(e); return 0;
    case 13: if (v > 1) return GROOVE_PROJECT_INVALID;
             if (!!engine_solo(e) != !!v) groove_mix_toggle(e, 1);
             for (unsigned i = 0; i < DX_ENGINES; ++i) cfg_sync_mute(i);
             return 0;
    case 14:
        if (v > 127) return GROOVE_PROJECT_INVALID;
        groove_get_view(e, 0, &view);
        groove_control_edit(e, (int)v - (int)view.program, 0, 0);
        dx_bright_force_voice(e);
        return 0;
    default: return GROOVE_PROJECT_INVALID;
    }
    dx_route_publish(&c);
    return 0;
}

static int cfg_set_master(unsigned f, unsigned lo, unsigned hi)
{
    uint8_t fx[4];
    unsigned v = lo | hi << 8;
    switch (f) {
    case 0: if (lo > 3 || hi) return GROOVE_PROJECT_INVALID; live.filter_type = (uint8_t)lo; dx_apply_filter(); return 0;
    case 1: if (v < 20 || v > 20000) return GROOVE_PROJECT_INVALID; live.filter_hz = (uint16_t)v; dx_apply_filter(); return 0;
    case 2: if (lo > 127 || hi) return GROOVE_PROJECT_INVALID; live.filter_res = (uint8_t)lo; dx_apply_filter(); return 0;
    case 3: case 5: case 6:                                   /* decay, feedback, return: 0..127 */
        if (lo > 127 || hi) return GROOVE_PROJECT_INVALID;
        groove_effects_get(fx);
        groove_effects_edit(f == 3 ? 0U : f == 5 ? 2U : 3U, (int)lo - (int)fx[f == 3 ? 0U : f == 5 ? 2U : 3U]);
        return 0;
    case 4:                                                   /* delay time in free mode, position 0..244 (the stored value skips the nine synced codes) */
        if (v > GROOVE_DELAY_FREE_LAST - 9 || hi > 1) return GROOVE_PROJECT_INVALID;
        groove_effects_get(fx);
        if (groove_delay_synced(fx[1])) { groove_effects_edit(4, -1); groove_effects_get(fx); }
        groove_effects_edit(1, (int)v - (int)(fx[1] > GROOVE_DELAY_LAST ? fx[1] - 9U : fx[1]));
        return 0;
    case 7:                                                   /* delay mode: 0 free, 1 synchronised */
        if (lo > 1 || hi) return GROOVE_PROJECT_INVALID;
        groove_effects_get(fx);
        if (!!groove_delay_synced(fx[1]) != !!lo) groove_effects_edit(4, lo ? 1 : -1);
        return 0;
    case 8:                                                   /* synchronised division 0..8 (switches to synchronised mode) */
        if (lo > GROOVE_DELAY_LAST - GROOVE_DELAY_SYNC || hi) return GROOVE_PROJECT_INVALID;
        groove_effects_get(fx);
        if (!groove_delay_synced(fx[1])) { groove_effects_edit(4, 1); groove_effects_get(fx); }
        groove_effects_edit(1, (int)(GROOVE_DELAY_SYNC + lo) - (int)fx[1]);
        return 0;
    default: return GROOVE_PROJECT_INVALID;
    }
}

static int cfg_scene(const uint8_t *d, unsigned n, uint8_t *out, unsigned *count)
{
    unsigned op = d[0], index = d[1];
    if (n < 2 || index >= DX_SCENES) return GROOVE_PROJECT_INVALID;
    if (op == 0) {                                            /* four scenes from a multiple of four */
        if (n != 2 || (index & 3U)) return GROOVE_PROJECT_INVALID;
        for (unsigned i = 0; i < 4; ++i) {
            char name[13];
            uint8_t used = 0, *q = out + 13U * i;
            memset(q, 0, 13);
            memset(q + 1, ' ', 12);
            if (!dx_scene_info(index + i, name, &used)) {
                q[0] = used; memcpy(q + 1, name, 12);
                if (!cfg_printable((const uint8_t *)name, 12) || !memcmp(name, "            ", 12)) memcpy(q + 1, "(NO NAME)   ", 12);   /* a scene that exists never looks like an empty slot */
            }
        }
        *count = 52;
        return 0;
    }
    if (op == 1) return n == 2 ? dx_scene_recall(index) : GROOVE_PROJECT_INVALID;
    if (op == 4 ? n != 2 : ((op != 2 && op != 3) || n != 14 || !cfg_printable(d + 2, 12))) return GROOVE_PROJECT_INVALID;
    if (dx_storage_state()) return GROOVE_PROJECT_INVALID;          /* the store is not claimed: only the panel can do that */
    uint32_t now = fm1_millis();
    if (cfg_last_scene_write && now - cfg_last_scene_write < 1500U) return FM1_FLASH_BUSY;
    int r;
    if (op == 4) r = dx_scene_clear(index);
    else if (op == 2) r = dx_scene_save(index, (const char *)(d + 2));
    else {                                                    /* rename: read the scene, write it back under the new name */
        struct dx_scene *s = malloc(sizeof *s);
        if (!s) return DX_SCENE_MEMORY;
        r = dx_scene_read(index, s);
        if (!r) { memcpy(s->name, d + 2, 12); r = dx_scene_write(index, s); }
        free(s);
    }
    if (!r) {
        cfg_last_scene_write = fm1_millis();
        if (op == 4 && current_scene == index) { current_scene = DX_NO_SCENE; settings.flags &= (uint8_t)~1U; settings_mark(); }   /* the deleted scene is no longer the current one */
    }
    return r;
}

/* Commands 23 to 31. Returns 0 or a negative code, the status byte of the reply is made from it. */
int dx_config_command(unsigned cmd, const uint8_t *d, unsigned n, uint8_t *out, unsigned *count)
{
    int r;
    *count = 0;
    switch (cmd) {
    case 23:                                                  /* hello */
        if (n) return GROOVE_PROJECT_INVALID;
        out[0] = 'D'; out[1] = 'X'; out[2] = DX_CFG_PROTOCOL; out[3] = 0; out[4] = (uint8_t)booted.up;
        out[5] = DX_PARTS; out[6] = DX_ENGINES; out[7] = DX_SCENES;
        out[8] = (uint8_t)FM1_RELEASE_VERSION[0]; out[9] = (uint8_t)FM1_RELEASE_VERSION[1]; out[10] = (uint8_t)FM1_RELEASE_VERSION[2];
        *count = 11;
        return 0;
    case 24: {                                                /* the state image's CRC and size */
        if (n) return GROOVE_PROJECT_INVALID;
        uint32_t crc;
        (void)dx_config_state(cfg_image);
        crc = ~groove_crc32(~0U, cfg_image, DX_CFG_STATE_BYTES);
        for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(crc >> (8U * i));
        out[4] = (uint8_t)DX_CFG_STATE_BYTES; out[5] = (uint8_t)(DX_CFG_STATE_BYTES >> 8);
        *count = 6;
        return 0;
    }
    case 25: return n == 3 ? cfg_set_layer(d[0], d[1], d[2]) : GROOVE_PROJECT_INVALID;
    case 26:
        if (n != 3 || d[0] >= DX_PARTS || !dx_part_used(&live.parts, d[0])) return GROOVE_PROJECT_INVALID;
        if (d[1] == 0 && d[2] <= 127) { live.parts.part[d[0]].level = d[2]; dx_part_level_changed(d[0]); return 0; }
        if (d[1] == 1 && d[2] >= 1 && d[2] <= 16) { dx_ui_release_keys(); return dx_part_set_channel(d[0], d[2]) ? 0 : GROOVE_PROJECT_INVALID; }
        return GROOVE_PROJECT_INVALID;
    case 27:
        if (n != 3 || d[1] >= DX_PARTS) return GROOVE_PROJECT_INVALID;
        dx_ui_release_keys();                                 /* the built-in keys' note offs must go where the note ons went */
        switch (d[0]) {
        case 0: r = dx_part_add(d[1]); break;
        case 1: r = dx_part_used(&live.parts, d[1]) ? dx_part_add_layer(d[1]) : GROOVE_PROJECT_INVALID; break;
        case 2: r = dx_part_used(&live.parts, d[1]) ? dx_part_remove_layer(d[1], d[2]) : GROOVE_PROJECT_INVALID; break;
        case 3: dx_part_select(d[1]); return 0;
        case 4: {
            uint8_t list[DX_ENGINES];
            if (!dx_part_layers(&live.parts, d[1], list) || d[2] >= dx_part_layers(&live.parts, d[1], list)) return GROOVE_PROJECT_INVALID;
            dx_part_select_layer(d[1], d[2]);
            return 0;
        }
        default: return GROOVE_PROJECT_INVALID;
        }
        if (r < 0) return r == GROOVE_PROJECT_INVALID ? r : r - 30;   /* a part code: -1 no slot, -2 no engine, -3 no channel, -4 refused: status 95 to 98 */
        out[0] = (uint8_t)r; *count = 1;
        return 0;
    case 28: return n == 3 ? cfg_set_master(d[0], d[1], d[2]) : GROOVE_PROJECT_INVALID;
    case 29: {
        struct dx_settings s = settings;
        if (n != 2) return GROOVE_PROJECT_INVALID;
        if (d[0] == 0 && d[1] <= 16) s.scene_ch = d[1];
        else if (d[0] == 1 && d[1] <= 16) s.master_ch = d[1];
        else if (d[0] == 2 && d[1] <= 8) s.backlight = d[1];
        else if (d[0] == 3 && d[1] <= 1) s.radio_off = (uint8_t)(d[1] ? s.radio_off & ~GROOVE_RADIO_AUDIO : s.radio_off | GROOVE_RADIO_AUDIO);
        else return GROOVE_PROJECT_INVALID;
        dx_settings_update(&s);
        return 0;
    }
    case 30: return n >= 2 ? cfg_scene(d, n, out, count) : GROOVE_PROJECT_INVALID;
    case 31:
        if (n != 13 || d[0] >= DX_PARTS || !cfg_printable(d + 1, 12)) return GROOVE_PROJECT_INVALID;
        memcpy(live.parts.part[d[0]].name, d + 1, 12);
        return 0;
    default: return GROOVE_PROJECT_INVALID;
    }
}
