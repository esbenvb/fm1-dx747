/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 panel handling: buttons, encoders and keys act on the router
 * configuration, the engine and the scene store. Included by app/screen.c only,
 * after ui_screens.inc. All of it runs on the control task. */

static struct fm1_controls ctl, prev_ctl;
static uint32_t keys_playing, led_state_lo, led_state_hi;
static uint8_t audition_note[27];      /* note sounding on the engine under test, 255 = none */
static struct dx_cfg cw;               /* working copy of the router configuration */

static void notice(const char *text) { ui.notice = text; ui.notice_until = fr.now + 1800U; }
static struct dx_cfg *cfg_begin(void) { cw = *dx_route_cfg(); return &cw; }
static void cfg_commit(void) { dx_route_publish(&cw); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static unsigned wrap(int v, unsigned n) { int m = v % (int)n; return (unsigned)(m < 0 ? m + (int)n : m); }
static int wkey_index(unsigned key) { for (unsigned i = 0; i < 16; ++i) if (white_key[i] == key) return (int)i; return -1; }

/* ---- keys that play ---------------------------------------------------- */

static unsigned key_note(unsigned key) { return (unsigned)clampi(53 + (int)key + 12 * ui.octave, 0, 127); }
static void audition(unsigned e, unsigned note, unsigned on)
{
    const struct dx_cfg *c = dx_route_cfg();
    uint8_t m[3] = {(uint8_t)((on ? 0x90U : 0x80U) | c->priv[e]), (uint8_t)note, (uint8_t)(on ? 100 : 0)};
    (void)fm1_midi_post(m, 3);
}
static unsigned screen_audition(void) { return ui.screen == S_ESOUND || ui.screen == S_ESENDS; }
static unsigned screen_plays(void)
{
    return screen_audition() || ui.screen == S_FX || ui.screen == S_MASTER || ui.screen == S_SYS || ui.screen == S_MON;
}
static void release_keys(void)
{
    for (unsigned k = 0; k < 27; ++k) {
        if ((keys_playing >> k) & 1U) dx_route_key(key_note(k), 0);
        if (audition_note[k] != 255) { audition(ui.engine, audition_note[k], 0); audition_note[k] = 255; }
    }
    keys_playing = 0;
}
static void go(unsigned screen)
{
    if (ui.screen == screen) return;
    release_keys();
    ui.screen = (uint8_t)screen; ui.mode = M_BROWSE; ui.learn = 0;
}

/* ---- scenes ------------------------------------------------------------------ */

static void begin_name(void)
{
    char name[13];
    if (scene_exists(ui.scene)) { memcpy(ui.name, fr.scene_name[ui.scene], 12); }
    else { memcpy(ui.name, "SCENE       ", 12); put_2(name, ui.scene + 1U); ui.name[6] = name[0]; ui.name[7] = name[1]; }
    ui.name_pos = 0; ui.mode = M_NAME;
}
static void begin_save(void)
{
    go(S_SCENES);
    if (dx_storage_state() == 1) ui.mode = M_CLAIM; else begin_name(); /* a fresh 16-byte read: the bank may have changed since boot */
}
static void scene_overlay_button(unsigned pressed)
{
    if (pressed & BTN_BIT(BTN_HOME)) { ui.mode = M_BROWSE; return; }
    if (!(pressed & BTN_BIT(BTN_SAVE))) return;
    if (ui.mode == M_CLAIM) {
        int r = dx_storage_claim();
        if (r) { notice("FLASH SETUP FAILED"); ui.mode = M_BROWSE; }
        else begin_name();
        return;
    }
    int r = dx_scene_save(ui.scene, ui.name);
    fr.scenes_known = 0;
    ui.mode = M_BROWSE;
    notice(r ? "SAVE FAILED" : "SCENE SAVED");
}
static void scene_overlay_encoder(void)
{
    if (ui.mode != M_NAME) return;
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+";
    if (ctl.encoders[2]) ui.name_pos = (uint8_t)wrap((int)ui.name_pos + ctl.encoders[2], 12);
    if (ctl.encoders[3]) {
        unsigned n = sizeof chars - 1U, at = 0;
        for (unsigned i = 0; i < n; ++i) if (chars[i] == ui.name[ui.name_pos]) at = i;
        ui.name[ui.name_pos] = chars[wrap((int)at + ctl.encoders[3], n)];
    }
}

/* ---- the panel -------------------------------------------------------------- */

static void on_button(unsigned pressed)
{
    if (ui.screen == S_BOOT) { go(S_SETUP); return; }
    if (ui.mode != M_BROWSE) { scene_overlay_button(pressed); return; }
    if ((ctl.buttons & BTN_BIT(BTN_HOME)) && (pressed & BTN_BIT(BTN_PLAY))) { dx_route_panic(); notice("ALL NOTES OFF"); return; }
    if (pressed & BTN_BIT(BTN_HOME)) go(ui.screen == S_SETUP ? S_ENGINES : S_SETUP);
    else if (pressed & BTN_BIT(BTN_SEQ)) go(S_CHANNEL);
    else if (pressed & BTN_BIT(BTN_EDIT)) go(ui.screen == S_ESOUND ? S_EPLAYS : ui.screen == S_EPLAYS ? S_ESENDS : S_ESOUND);
    else if (pressed & BTN_BIT(BTN_REC)) go(S_MON);
    else if (pressed & BTN_BIT(BTN_FX)) go(ui.screen == S_FX ? S_MASTER : S_FX);
    else if (pressed & BTN_BIT(BTN_SEL)) go(S_MIX);
    else if (pressed & BTN_BIT(BTN_PLAY)) go(S_SCENES);
    else if (pressed & BTN_BIT(BTN_GLO)) go(S_SYS);
    else if (pressed & BTN_BIT(BTN_SAVE)) begin_save();
    else if (pressed & BTN_BIT(BTN_ARP)) {
        if (ui.screen == S_CHANNEL || ui.screen == S_EPLAYS) { ui.learn = ui.learn ? 0 : 1; ui.learn_sig = fr.nlog ? fr.log[0].ms ^ fr.log[0].d1 : 0; }
        else notice("LEARN: OPEN CHANNEL OR ENGINE PLAYS");
    }
    else if (pressed & (BTN_BIT(BTN_OCT_DOWN) | BTN_BIT(BTN_OCT_UP))) {
        int d = (pressed & BTN_BIT(BTN_OCT_UP)) ? 1 : -1;
        if (ui.screen == S_SCENES) ui.scene = (uint8_t)wrap((int)ui.scene + 16 * d, DX_SCENES);
        else { release_keys(); ui.octave = (int8_t)clampi(ui.octave + d, -2, 2); }
    }
}

/* The engine that the channel screen's selected lane belongs to, or -1. */
static int lane_engine(void)
{
    uint8_t list[DX_ENGINES];
    unsigned n = engines_on(ui.channel, list);
    return n ? list[ui.lane < n ? ui.lane : n - 1U] : -1;
}

static void learn_poll(void)
{
    if (!ui.learn || fr.nlog == 0) return;
    const struct dx_log *l = &fr.log[0];
    uint32_t sig = l->ms ^ l->d1;
    if (sig == ui.learn_sig || (l->status & 0xf0U) != 0x90U || !l->d2) return;
    ui.learn_sig = sig;
    if (ui.learn == 1) { ui.learn_lo = l->d1; ui.learn = 2; return; }
    int e = ui.screen == S_CHANNEL ? lane_engine() : (int)ui.engine;
    ui.learn = 0;
    if (e < 0) return;
    struct dx_cfg *c = cfg_begin();
    unsigned lo = ui.learn_lo < l->d1 ? ui.learn_lo : l->d1, hi = ui.learn_lo < l->d1 ? l->d1 : ui.learn_lo;
    c->e[e].lo = (uint8_t)lo; c->e[e].hi = (uint8_t)hi;
    cfg_commit();
    notice("RANGE LEARNED");
}

static void set_mute(unsigned e, unsigned mute)
{
    struct dx_cfg *c = cfg_begin();
    c->e[e].mute = (uint8_t)mute;
    cfg_commit();
    struct groove_view v;
    groove_get_view(e, 0, &v);
    if (!!v.muted != !!mute) groove_mix_toggle(e, 0);
}
static void send_pan(unsigned e)
{
    int v = 64 + dx_live_edit()->pan[e] * 63 / 50;
    const struct dx_cfg *c = dx_route_cfg();
    uint8_t m[3] = {(uint8_t)(0xb0U | c->priv[e]), 10, (uint8_t)clampi(v, 1, 127)};
    (void)fm1_midi_post(m, 3);
}

static void on_white_key(unsigned n /* 0..15 */)
{
    switch (ui.screen) {
    case S_SETUP: ui.channel = (uint8_t)n; ui.lane = 0; go(S_CHANNEL); break;
    case S_ENGINES: if (n < 8) { ui.engine = (uint8_t)n; go(S_ESOUND); } break;
    case S_CHANNEL:
        if (n < 8) {
            struct dx_cfg *c = cfg_begin();
            c->e[n].rx ^= (uint16_t)(1U << ui.channel);
            cfg_commit();
            unsigned lane = 0;
            for (unsigned i = 0; i < n; ++i) lane += (c->e[i].rx >> ui.channel) & 1U;
            ui.lane = (uint8_t)lane;
        }
        break;
    case S_EPLAYS: { struct dx_cfg *c = cfg_begin(); c->e[ui.engine].rx ^= (uint16_t)(1U << n); cfg_commit(); break; }
    case S_MIX:
        if (n < 8) set_mute(n, !fr.cfg.e[n].mute);
        else groove_mix_toggle(n - 8U, 1);
        break;
    case S_SCENES: {
        unsigned idx = (ui.scene / 16U) * 16U + n;
        ui.scene = (uint8_t)idx;
        if (scene_exists(idx)) { int r = dx_scene_recall(idx); notice(r ? "RECALL FAILED" : "SCENE RECALLED"); }
        else notice("EMPTY SLOT: SAVE STORES HERE");
        break;
    }
    default: break;
    }
}

static void on_key(unsigned key, unsigned down)
{
    if (ui.screen == S_BOOT) { if (down) go(S_SETUP); return; }
    int w = wkey_index(key);
    if (screen_plays()) {
        if (screen_audition()) {
            if (down) { audition_note[key] = (uint8_t)key_note(key); audition(ui.engine, audition_note[key], 1); }
            else if (audition_note[key] != 255) { audition(ui.engine, audition_note[key], 0); audition_note[key] = 255; }
        } else {
            dx_route_key(key_note(key), down);
            if (down) keys_playing |= 1U << key; else keys_playing &= ~(1U << key);
        }
        return;
    }
    if (down && w >= 0 && ui.mode == M_BROWSE) on_white_key((unsigned)w);
}

static void edit_range(unsigned e, int which, int delta)
{
    struct dx_cfg *c = cfg_begin();
    struct dx_engine_cfg *x = &c->e[e];
    if (which == 0) { int v = clampi((int)x->lo + delta, 0, 127); x->lo = (uint8_t)v; if (x->hi < x->lo) x->hi = x->lo; }
    else if (which == 1) { int v = clampi((int)x->hi + delta, 0, 127); x->hi = (uint8_t)v; if (x->lo > x->hi) x->lo = x->hi; }
    else if (which == 2) x->shift = (int8_t)clampi(x->shift + delta * 12, -48, 48);
    else x->fold = (uint8_t)wrap((int)x->fold + delta, 3);
    cfg_commit();
}
static void apply_filter(void) { dx_apply_filter(); }

static void on_encoders(void)
{
    const int16_t *e = ctl.encoders;
    int any = 0;
    for (unsigned i = 0; i < 7; ++i) any |= e[i] != 0;
    if (!any) return;
    if (ui.mode != M_BROWSE) { scene_overlay_encoder(); return; }
    struct dx_live *live = dx_live_edit();
    unsigned eng = ui.engine;
    switch (ui.screen) {
    case S_BOOT: go(S_SETUP); break;
    case S_SETUP: case S_MON: if (e[0]) { ui.channel = (uint8_t)wrap((int)ui.channel + e[0], 16); ui.lane = 0; } break;
    case S_ENGINES: if (e[0]) ui.engine = (uint8_t)wrap((int)ui.engine + e[0], 8); break;
    case S_CHANNEL: {
        if (e[0]) { ui.channel = (uint8_t)wrap((int)ui.channel + e[0], 16); ui.lane = 0; }
        uint8_t list[DX_ENGINES];
        unsigned n = engines_on(ui.channel, list);
        if (!n) break;
        if (e[2]) ui.lane = (uint8_t)clampi((int)(ui.lane < n ? ui.lane : n - 1U) + e[2], 0, (int)n - 1);
        unsigned lane = ui.lane < n ? ui.lane : n - 1U;
        if (e[3]) edit_range(list[lane], 0, e[3]);
        if (e[4]) edit_range(list[lane], 1, e[4]);
        if (e[5]) edit_range(list[lane], 2, e[5]);
        break;
    }
    case S_ESOUND:
        if (e[0]) ui.engine = (uint8_t)wrap((int)ui.engine + e[0], 8);
        if (e[2] || e[6]) { groove_control_edit(eng, e[2] + e[6], 0, 0); dx_bright_force_voice(eng); } /* a new voice: BRIGHT starts from it */
        if (e[3]) groove_control_edit(eng, 0, e[3], 0);
        if (e[4]) { live->pan[eng] = (int8_t)clampi(live->pan[eng] + e[4] * 2, -50, 50); send_pan(eng); }
        if (e[5]) { live->bright[eng] = (int8_t)clampi(live->bright[eng] + e[5], -50, 50); dx_bright_changed(eng); }
        break;
    case S_EPLAYS:
        if (e[0]) ui.engine = (uint8_t)wrap((int)ui.engine + e[0], 8);
        for (unsigned i = 0; i < 4; ++i) if (e[2 + i]) edit_range(eng, (int)i, e[2 + i]);
        break;
    case S_ESENDS:
        if (e[0]) ui.engine = (uint8_t)wrap((int)ui.engine + e[0], 8);
        if (e[2]) groove_sends_edit(eng, 0, e[2]);
        if (e[3]) groove_sends_edit(eng, 1, e[3]);
        if (e[4] || e[5]) {
            struct dx_cfg *c = cfg_begin();
            if (e[4]) c->e[eng].poly = (uint8_t)clampi(c->e[eng].poly + e[4], 1, 8);
            if (e[5]) c->e[eng].ctl = (uint8_t)clampi(c->e[eng].ctl + e[5], 0, 16);
            cfg_commit();
        }
        break;
    case S_MIX:
        if (e[0]) ui.engine = (uint8_t)wrap((int)ui.engine + e[0], 8);
        if (e[2]) groove_control_edit(eng, 0, e[2], 0);
        if (e[3]) { live->pan[eng] = (int8_t)clampi(live->pan[eng] + e[3] * 2, -50, 50); send_pan(eng); }
        if (e[4]) groove_sends_edit(eng, 0, e[4]);
        if (e[5]) groove_sends_edit(eng, 1, e[5]);
        break;
    case S_FX:
        if (e[0]) ui.engine = (uint8_t)wrap((int)ui.engine + e[0], 8);
        for (unsigned i = 0; i < 4; ++i) if (e[2 + i]) groove_effects_edit(i, e[2 + i]);
        break;
    case S_MASTER:
        if (e[2]) { live->filter_type = (uint8_t)wrap((int)live->filter_type + e[2], 4); apply_filter(); }
        if (e[3]) {
            unsigned hz = live->filter_hz ? live->filter_hz : 1000U;
            for (int i = 0; i < (e[3] < 0 ? -e[3] : e[3]); ++i) hz = e[3] > 0 ? hz * 1059U / 1000U + 1U : hz * 944U / 1000U;
            live->filter_hz = (uint16_t)clampi((int)hz, 20, 20000); apply_filter();
        }
        if (e[4]) { live->filter_res = (uint8_t)clampi(live->filter_res + e[4] * 3, 0, 127); apply_filter(); }
        break;
    case S_SCENES: if (e[0]) ui.scene = (uint8_t)wrap((int)ui.scene + e[0], DX_SCENES); break;
    case S_SYS: {
        if (e[0]) ui.sys_row = (uint8_t)wrap((int)ui.sys_row + e[0], 7);
        int d = e[2] + e[6];
        if (!d) break;
        struct dx_settings s;
        dx_settings_get(&s);
        switch (ui.sys_row) {
        case 1: s.radio_off ^= GROOVE_RADIO_AUDIO; break;
        case 2: s.backlight = (uint8_t)clampi(s.backlight + d, 0, 8); break;
        case 3: s.kbd_ch = (uint8_t)wrap((int)s.kbd_ch + d, 16); break;
        case 4: s.master_ch = (uint8_t)wrap((int)s.master_ch + d, 17); break; /* 0 = off, 1..16 */
        default: break;
        }
        dx_settings_update(&s);
        break;
    }
    default: break;
    }
}

/* ---- lamps ------------------------------------------------------------------ */

static void led_set(unsigned index, unsigned on)
{
    uint32_t *word = index < 32 ? &led_state_lo : &led_state_hi;
    uint32_t bit = 1U << (index & 31U);
    if (!!(*word & bit) == !!on) return;
    if (on) *word |= bit; else *word &= ~bit;
    fm1_led_set(index, (int)on);
}
static void update_leds(void)
{
    unsigned active_button = 99;
    switch (ui.screen) {
    case S_SETUP: case S_ENGINES: active_button = BTN_HOME; break;
    case S_CHANNEL: active_button = BTN_SEQ; break;
    case S_ESOUND: case S_EPLAYS: case S_ESENDS: active_button = BTN_EDIT; break;
    case S_MON: active_button = BTN_REC; break;
    case S_FX: case S_MASTER: active_button = BTN_FX; break;
    case S_MIX: active_button = BTN_SEL; break;
    case S_SCENES: active_button = BTN_PLAY; break;
    case S_SYS: active_button = BTN_GLO; break;
    default: break;
    }
    for (unsigned b = 0; b < 14; ++b) led_set(b, b == active_button || (b == BTN_ARP && ui.learn && fr.lit_blink));
    uint32_t lit = 0;
    if (ui.mode == M_BROWSE) switch (ui.screen) {
    case S_SETUP: for (unsigned k = 0; k < 16; ++k) { uint8_t t[DX_ENGINES]; if (engines_on(k, t) || ((fr.active >> k) & 1U)) lit |= 1U << k; } break;
    case S_ENGINES: lit = 0xffU; break;
    case S_CHANNEL: for (unsigned e = 0; e < 8; ++e) if ((fr.cfg.e[e].rx >> ui.channel) & 1U) lit |= 1U << e; break;
    case S_EPLAYS: lit = fr.cfg.e[ui.engine].rx; break;
    case S_MIX: for (unsigned e = 0; e < 8; ++e) if (fr.cfg.e[e].mute) lit |= 1U << e; break;
    case S_SCENES: for (unsigned k = 0; k < 16; ++k) if (scene_exists((ui.scene / 16U) * 16U + k)) lit |= 1U << k; break;
    default: break;
    }
    for (unsigned i = 0; i < 16; ++i) led_set(14U + white_key[i], (lit >> i) & 1U);
}
