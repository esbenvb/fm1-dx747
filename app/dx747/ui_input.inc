/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 panel handling: buttons, encoders and keys act on the parts, the router
 * configuration, the engine and the scene store. Included by app/screen.c only,
 * after ui_screens.inc. All of it runs on the control task.
 *
 * The built-in keys always play the selected part, on every screen (the white keys
 * recall scenes on the Scenes screen). The SELECT encoder picks the part, ALGO the
 * layer; ENV and LFO add and remove layers (and mute and solo on the mixer). */

static struct fm1_controls ctl, prev_ctl;
static uint32_t keys_playing, led_state_lo, led_state_hi;
static struct dx_cfg cw;               /* working copy of the router configuration */

static void notice(const char *text) { ui.notice = text; ui.notice_until = fr.now + 1800U; }
static struct dx_cfg *cfg_begin(void) { cw = *dx_route_cfg(); return &cw; }
static void cfg_commit(void) { dx_route_publish(&cw); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static unsigned wrap(int v, unsigned n) { int m = v % (int)n; return (unsigned)(m < 0 ? m + (int)n : m); }
static int wkey_index(unsigned key) { for (unsigned i = 0; i < 16; ++i) if (white_key[i] == key) return (int)i; return -1; }

/* The parts as they are right now. The frame snapshot (fr) is up to 33 ms old while the panel is read every few
 * milliseconds, so a relative edit (one more detent, one more channel) must start from these, never from fr. */
static const struct dx_parts *LP(void) { return &dx_live_edit()->parts; }
static unsigned live_sel_part(void) { return LP()->selected; }
static int live_sel_engine(void) { return dx_part_sel_engine(LP(), LP()->selected); }

/* ---- keys that play ---------------------------------------------------- */

static unsigned key_note(unsigned key) { return (unsigned)clampi(53 + (int)key + 12 * ui.octave, 0, 127); }
/* Held keys are released before anything changes where they play (another screen's keys, another part). */
static void release_keys(void)
{
    for (unsigned k = 0; k < 27; ++k) if ((keys_playing >> k) & 1U) dx_route_key(key_note(k), 0);
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
    if (dx_storage_state() == 1) ui.mode = M_CLAIM; else begin_name(); /* a fresh read of the store: the bank may have changed since boot */
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
/* The next stored scene in a direction, wrapping, recalled at once: for stepping through a setlist while playing. */
static void step_scene(int d)
{
    unsigned from = dx_scene_current() < DX_SCENES ? dx_scene_current() : ui.scene;
    for (unsigned k = 1; k <= DX_SCENES; ++k) {
        unsigned idx = wrap((int)from + d * (int)k, DX_SCENES);
        if (!scene_exists(idx)) continue;
        ui.scene = (uint8_t)idx;
        int r = dx_scene_recall(idx);
        notice(r ? "RECALL FAILED" : "SCENE RECALLED");
        return;
    }
    notice("NO SCENES SAVED");
}

/* ---- parts and layers --------------------------------------------------------- */

static void select_part_step(int d)
{
    release_keys();                        /* the keys' note offs must go to the part they started on */
    dx_part_select(wrap((int)live_sel_part() + d, DX_PARTS));
}
static void select_layer_step(int d)
{
    unsigned p = live_sel_part();
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list);
    if (!n) return;
    unsigned cur = LP()->part[p].sel < n ? LP()->part[p].sel : 0U;
    dx_part_select_layer(p, (unsigned)clampi((int)cur + d, 0, (int)n - 1));
}
static void add_here(void)
{
    unsigned p = live_sel_part();
    int r;
    release_keys();
    if (!dx_part_used(LP(), p)) {
        r = dx_part_add(p);
        notice(r >= 0 ? "PART ADDED" : r == DX_PART_NO_ENGINE ? "NO FREE ENGINE" : r == DX_PART_NO_CHANNEL ? "NO FREE CHANNEL" : "NO FREE PART");
        if (r >= 0) { release_keys(); dx_part_select((unsigned)r); }
        return;
    }
    r = dx_part_add_layer(p);
    if (r < 0) { notice(r == DX_PART_NO_ENGINE ? "NO FREE ENGINE: 8 LAYERS IN USE" : "CANNOT ADD A LAYER"); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list);
    for (unsigned i = 0; i < n; ++i) if (list[i] == (unsigned)r) { dx_part_select_layer(p, i); break; }
    notice("LAYER ADDED");
}
static void remove_here(void)
{
    unsigned p = live_sel_part();
    release_keys();
    if (!dx_part_used(LP(), p)) { notice("NOTHING TO REMOVE"); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list), sl = LP()->part[p].sel < n ? LP()->part[p].sel : 0U;
    int r = dx_part_remove_layer(p, sl);
    notice(r < 0 ? "CANNOT REMOVE" : n == 1 ? "PART REMOVED" : "LAYER REMOVED");
}
/* The next channel in a direction that no other part uses (and that is not the scene channel). */
static void step_channel(int d)
{
    unsigned p = live_sel_part();
    if (!dx_part_used(LP(), p)) return;
    release_keys();                        /* before the keys' channel moves with the part: their note offs must match the note ons */
    int cur = LP()->part[p].channel;
    for (int k = 1; k <= 16; ++k) {
        unsigned ch = wrap(cur - 1 + d * k, 16) + 1U;
        if (dx_part_set_channel(p, ch)) return;
    }
}

/* The lowest and the highest note play: the first sets one end, the second the other, in either order. */
static void learn_note(unsigned note)
{
    if (ui.learn == 1) { ui.learn_lo = (uint8_t)note; ui.learn = 2; return; }
    int e = live_sel_engine();
    ui.learn = 0;
    if (e < 0) return;
    struct dx_cfg *c = cfg_begin();
    unsigned lo = ui.learn_lo < note ? ui.learn_lo : note, hi = ui.learn_lo < note ? note : ui.learn_lo;
    c->e[e].lo = (uint8_t)lo; c->e[e].hi = (uint8_t)hi;
    cfg_commit();
    notice("RANGE LEARNED");
}
/* Notes from MIDI come through the router log, from any channel and port. */
static void learn_poll(void)
{
    if (!ui.learn || fr.nlog == 0) return;
    const struct dx_log *l = &fr.log[0];
    uint32_t sig = l->ms ^ l->d1;
    if (sig == ui.learn_sig || (l->status & 0xf0U) != 0x90U || !l->d2) return;
    ui.learn_sig = sig;
    learn_note(l->d1);
}
static unsigned screen_learns(void) { return ui.screen == S_PART || ui.screen == S_EPLAYS; }

/* The mixer decides what is muted (soloing an engine clears its own mute); the router's flag follows it, or an engine could
 * be soloed and never receive a note. */
static void sync_mute(unsigned e)
{
    struct groove_view v;
    groove_get_view(e, 0, &v);
    if (!!dx_route_cfg()->e[e].mute == !!v.muted) return;
    struct dx_cfg *c = cfg_begin();
    c->e[e].mute = (uint8_t)!!v.muted;
    cfg_commit();
}
static void set_mute(unsigned e, unsigned mute)
{
    struct groove_view v;
    groove_get_view(e, 0, &v);
    if (!!v.muted != !!mute) groove_mix_toggle(e, 0);
    sync_mute(e);
}
/* Mute and solo act on every layer of a part together. */
static void toggle_part_mute(unsigned p)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list), muted = n != 0;
    for (unsigned i = 0; i < n; ++i) muted &= dx_route_cfg()->e[list[i]].mute != 0;
    for (unsigned i = 0; i < n; ++i) set_mute(list[i], !muted);
}
static unsigned engine_soloed(unsigned e)
{
    struct groove_view v;
    groove_get_view(e, 0, &v);
    return v.solo != 0;
}
static void toggle_part_solo(unsigned p)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list), want = n && !engine_soloed(list[0]);
    for (unsigned i = 0; i < n; ++i) {
        if (engine_soloed(list[i]) != want) groove_mix_toggle(list[i], 1);
        sync_mute(list[i]);
    }
}
static void send_pan(unsigned e)
{
    int v = 64 + dx_live_edit()->pan[e] * 63 / 50;
    const struct dx_cfg *c = dx_route_cfg();
    uint8_t m[3] = {(uint8_t)(0xb0U | c->priv[e]), 10, (uint8_t)clampi(v, 1, 127)};
    (void)fm1_midi_post(m, 3);
}

/* ---- the panel -------------------------------------------------------------- */

static unsigned screen_has_parts(void)
{
    return ui.screen == S_PARTS || ui.screen == S_PART || ui.screen == S_ESOUND || ui.screen == S_EPLAYS || ui.screen == S_EVEL || ui.screen == S_ESENDS;
}
static void on_button(unsigned pressed)
{
    if (ui.screen == S_BOOT) { go(S_PARTS); return; }
    if (ui.mode != M_BROWSE) { scene_overlay_button(pressed); return; }
    if ((ctl.buttons & BTN_BIT(BTN_HOME)) && (pressed & BTN_BIT(BTN_PLAY))) { dx_route_panic(); notice("ALL NOTES OFF"); return; }
    if ((ctl.buttons & BTN_BIT(BTN_PLAY)) && (pressed & (BTN_BIT(BTN_OCT_DOWN) | BTN_BIT(BTN_OCT_UP)))) { /* PLAY held: step through the scenes */
        step_scene((pressed & BTN_BIT(BTN_OCT_UP)) ? 1 : -1);
        return;
    }
    if (pressed & BTN_BIT(BTN_HOME)) go(S_PARTS);
    else if (pressed & BTN_BIT(BTN_SEQ)) go(S_PART);
    else if (pressed & BTN_BIT(BTN_EDIT)) go(ui.screen == S_ESOUND ? S_EPLAYS : ui.screen == S_EPLAYS ? S_EVEL : ui.screen == S_EVEL ? S_ESENDS : S_ESOUND);
    else if (pressed & BTN_BIT(BTN_REC)) go(S_MON);
    else if (pressed & BTN_BIT(BTN_FX)) go(ui.screen == S_FX ? S_MASTER : S_FX);
    else if (pressed & BTN_BIT(BTN_SEL)) go(S_MIX);
    else if (pressed & BTN_BIT(BTN_PLAY)) go(S_SCENES);
    else if (pressed & BTN_BIT(BTN_GLO)) go(S_SYS);
    else if (pressed & BTN_BIT(BTN_SAVE)) begin_save();
    else if (pressed & (BTN_BIT(BTN_ENV) | BTN_BIT(BTN_LFO))) {
        unsigned add = (pressed & BTN_BIT(BTN_ENV)) != 0;
        if (screen_has_parts()) { if (add) add_here(); else remove_here(); }
        else if (ui.screen == S_MIX) { if (dx_part_used(LP(), live_sel_part())) { if (add) toggle_part_mute(live_sel_part()); else toggle_part_solo(live_sel_part()); } }
        else if (ui.screen == S_SCENES) step_scene(add ? -1 : 1);
    }
    else if (pressed & BTN_BIT(BTN_ARP)) {
        if (screen_learns() && !ui.learn && live_sel_engine() < 0) notice("LEARN: ADD A PART FIRST");
        else if (screen_learns()) { ui.learn = ui.learn ? 0 : 1; ui.learn_sig = fr.nlog ? fr.log[0].ms ^ fr.log[0].d1 : 0; }
        else notice("LEARN: OPEN PART OR RANGE");
    }
    else if (pressed & (BTN_BIT(BTN_OCT_DOWN) | BTN_BIT(BTN_OCT_UP))) {
        int d = (pressed & BTN_BIT(BTN_OCT_UP)) ? 1 : -1;
        if (ui.screen == S_SCENES) ui.scene = (uint8_t)wrap((int)ui.scene + 16 * d, DX_SCENES);
        else { release_keys(); ui.octave = (int8_t)clampi(ui.octave + d, -2, 2); }
    }
}

/* White keys recall scenes on the Scenes screen; everywhere else every key plays the selected part. */
static void on_white_key(unsigned n /* 0..15 */)
{
    if (ui.screen != S_SCENES) return;
    unsigned idx = (ui.scene / 16U) * 16U + n;
    ui.scene = (uint8_t)idx;
    if (scene_exists(idx)) { int r = dx_scene_recall(idx); notice(r ? "RECALL FAILED" : "SCENE RECALLED"); }
    else notice("EMPTY SLOT: SAVE STORES HERE");
}

static void on_key(unsigned key, unsigned down)
{
    if (ui.screen == S_BOOT) { if (down) go(S_PARTS); return; }
    /* A key that started a note always ends it, whatever the screen or mode has become since (LEARN armed, a name being edited). */
    if (!down && ((keys_playing >> key) & 1U)) { dx_route_key(key_note(key), 0); keys_playing &= ~(1U << key); return; }
    /* While LEARN waits for notes, the built-in keys are the notes. */
    if (ui.learn && ui.mode == M_BROWSE && screen_learns()) { if (down) learn_note(key_note(key)); return; }
    int w = wkey_index(key);
    if (ui.screen == S_SCENES && w >= 0) { if (down && ui.mode == M_BROWSE) on_white_key((unsigned)w); return; }
    if (ui.mode != M_BROWSE) return;
    dx_route_key(key_note(key), down);
    if (down) keys_playing |= 1U << key; else keys_playing &= ~(1U << key);
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
static void edit_velocity(unsigned e, int which, int delta)
{
    struct dx_cfg *c = cfg_begin();
    struct dx_engine_cfg *x = &c->e[e];
    int lo = x->vlo ? x->vlo : 1, hi = x->vhi ? x->vhi : 127;
    if (which == 0) { lo = clampi(lo + delta, 1, 127); if (hi < lo) hi = lo; }
    else { hi = clampi(hi + delta, 1, 127); if (lo > hi) lo = hi; }
    x->vlo = (uint8_t)lo; x->vhi = (uint8_t)hi;
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
    if (ui.screen == S_BOOT) { go(S_PARTS); return; }
    /* SELECT picks the part on every screen that works on one; ALGO picks the layer where layers show */
    if (e[0] && (screen_has_parts() || ui.screen == S_MIX || ui.screen == S_FX)) select_part_step(e[0]);
    if (e[1] && (ui.screen == S_PART || ui.screen == S_ESOUND || ui.screen == S_EPLAYS || ui.screen == S_EVEL || ui.screen == S_ESENDS)) select_layer_step(e[1]);
    int se = live_sel_engine();            /* after the steps above: the selection as it is now */
    unsigned eng = se < 0 ? 0U : (unsigned)se, p = live_sel_part();
    switch (ui.screen) {
    case S_PART:
        if (e[2]) step_channel(e[2]);
        if (e[3] && dx_part_used(LP(), p)) {
            live->parts.part[p].level = (uint8_t)clampi(live->parts.part[p].level + e[3] * 2, 0, 127);
            dx_part_level_changed(p);
        }
        break;
    case S_ESOUND:
        if (se < 0) break;
        if (e[2] || e[6]) { groove_control_edit(eng, e[2] + e[6], 0, 0); dx_bright_force_voice(eng); } /* a new voice: BRIGHT starts from it */
        if (e[3]) { live->parts.layer[eng].lvl = (uint8_t)clampi(live->parts.layer[eng].lvl + e[3] * 2, 0, 127); dx_level_changed(eng); }
        if (e[4]) { live->pan[eng] = (int8_t)clampi(live->pan[eng] + e[4] * 2, -50, 50); send_pan(eng); }
        if (e[5]) { live->bright[eng] = (int8_t)clampi(live->bright[eng] + e[5], -50, 50); dx_bright_changed(eng); }
        break;
    case S_EPLAYS:
        if (se < 0) break;
        for (unsigned i = 0; i < 4; ++i) if (e[2 + i]) edit_range(eng, (int)i, e[2 + i]);
        break;
    case S_EVEL:
        if (se < 0) break;
        for (unsigned i = 0; i < 2; ++i) if (e[2 + i]) edit_velocity(eng, (int)i, e[2 + i]);
        break;
    case S_ESENDS:
        if (se < 0) break;
        if (e[2]) groove_sends_edit(eng, 0, e[2]);
        if (e[3]) groove_sends_edit(eng, 1, e[3]);
        if (e[4]) {
            struct dx_cfg *c = cfg_begin();
            c->e[eng].poly = (uint8_t)clampi(c->e[eng].poly + e[4], 1, 8);
            cfg_commit();
        }
        break;
    case S_MIX:
        if (e[2] && dx_part_used(LP(), p)) {
            live->parts.part[p].level = (uint8_t)clampi(live->parts.part[p].level + e[2] * 2, 0, 127);
            dx_part_level_changed(p);
        }
        if (se < 0) break;
        if (e[3]) { live->pan[eng] = (int8_t)clampi(live->pan[eng] + e[3] * 2, -50, 50); send_pan(eng); }
        if (e[4]) groove_sends_edit(eng, 0, e[4]);
        if (e[5]) groove_sends_edit(eng, 1, e[5]);
        break;
    case S_FX:
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
        case 3: s.scene_ch = (uint8_t)wrap((int)s.scene_ch + d, 17); break; /* 0 = off, 1..16 */
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
    case S_PARTS: active_button = BTN_HOME; break;
    case S_PART: active_button = BTN_SEQ; break;
    case S_ESOUND: case S_EPLAYS: case S_EVEL: case S_ESENDS: active_button = BTN_EDIT; break;
    case S_MON: active_button = BTN_REC; break;
    case S_FX: case S_MASTER: active_button = BTN_FX; break;
    case S_MIX: active_button = BTN_SEL; break;
    case S_SCENES: active_button = BTN_PLAY; break;
    case S_SYS: active_button = BTN_GLO; break;
    default: break;
    }
    for (unsigned b = 0; b < 14; ++b) led_set(b, b == active_button || (b == BTN_ARP && ui.learn && fr.lit_blink));
    uint32_t lit = 0;
    if (ui.mode == M_BROWSE && ui.screen == S_SCENES) for (unsigned k = 0; k < 16; ++k) if (scene_exists((ui.scene / 16U) * 16U + k)) lit |= 1U << k;
    for (unsigned i = 0; i < 16; ++i) led_set(14U + white_key[i], (lit >> i) & 1U);
}
