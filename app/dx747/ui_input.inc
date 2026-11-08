/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 panel handling: buttons, encoders and keys act on the parts, the router
 * configuration, the engine and the scene store. Included by app/screen.c only,
 * after ui_screens.inc. All of it runs on the control task.
 *
 * The built-in keys always play the selected part, on every screen (the white keys
 * recall scenes on the Scenes screen). The SELECT encoder picks the part, ALGO the
 * layer; ENV and LFO add and remove layers (and mute and solo on the mixer). */

static struct fm1_controls ctl, prev_ctl;
static uint32_t keys_playing;
static uint8_t led_code[41];           /* what each lamp was last told: 0 idle, 1 on, 2 + n at level n (0..8) */
static struct dx_cfg cw;               /* working copy of the router configuration */

/* The overlay: a message for a moment, or a knob's new value for a shorter one. The real clock decides when it ends. */
static void ov_show(unsigned kind, unsigned arg, uint32_t ms) { ui.ov_kind = (uint8_t)kind; ui.ov_arg = (uint8_t)arg; ui.ov_until = fm1_millis() + ms; }
static void notice(const char *text) { ui.notice = text; ov_show(OV_TEXT, 0, 1500U); }
static void knob(unsigned kind, unsigned arg) { ov_show(kind, arg, 900U); }
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
#ifdef DX_SMK37
static uint32_t smk_held[4];                           /* the notes the SMK-37's keys hold, one bit per note */
static uint8_t smk_pedal, smk_bend = 64, smk_mod;      /* what the pedal and the wheels have sent to the part the keys play: 0 / 64 / 0 is neutral */
/* Let go of the notes the SMK-37's keys hold. */
static void smk_release_notes(void)
{
    for (unsigned n = 0; n < 128; ++n) if ((smk_held[n >> 5] >> (n & 31U)) & 1U) dx_route_key_v(n, 0, 0);
    memset(smk_held, 0, sizeof smk_held);
}
/* Before the keys' channel moves to another part: lift the pedal and centre the wheels where they were sent, so that nothing stays
 * sustained, bent or modulated on the part that is no longer played. (Not on a screen change or an octave step: the part stays.) */
static void smk_release_controllers(void)
{
    if (smk_pedal) { dx_route_ctl(0xb0, 64, 0); smk_pedal = 0; }
    if (smk_bend != 64) { dx_route_ctl(0xe0, 0, 64); smk_bend = 64; }
    if (smk_mod) { dx_route_ctl(0xb0, 1, 0); smk_mod = 0; }
}
static void smk_reset(void) { memset(smk_held, 0, sizeof smk_held); smk_pedal = smk_mod = 0; smk_bend = 64; }
#endif
/* Held keys are released before anything changes where they play (another screen's keys, another part). */
static void release_keys(void)
{
    for (unsigned k = 0; k < 27; ++k) if ((keys_playing >> k) & 1U) dx_route_key(key_note(k), 0);
    keys_playing = 0;
#ifdef DX_SMK37
    smk_release_notes();
#endif
}
/* Held keys (and on the SMK-37 the pedal and the wheels) are released before the part the keys play changes. */
#ifdef DX_SMK37
static void release_part(void) { release_keys(); smk_release_controllers(); }
#else
#define release_part() release_keys()
#endif
static void go(unsigned screen)
{
    if (ui.screen == screen) return;
    release_keys();
    ui.screen = (uint8_t)screen; ui.mode = M_BROWSE; ui.learn = 0; ui.mixkeys = 0; ui.ov_kind = OV_NONE; /* a value shown for one screen's knob is not for the next */
#ifdef DX_SMK37
    if (screen == S_MIX) ui.mixkeys = 1;     /* the SMK-37's 16 pads are always the mixer's mute and solo there: no key to press first */
#endif
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
#ifdef DX_SMK37
    if (ui.mode == M_NAME && (pressed & (BTN_BIT(BTN_BACK) | BTN_BIT(BTN_FWD)))) {       /* < and >: the cursor, as K1 */
        ui.name_pos = (uint8_t)wrap((int)ui.name_pos + ((pressed & BTN_BIT(BTN_FWD)) ? 1 : -1), 12);
        return;
    }
#endif
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
    release_part();                        /* the keys' note offs must go to the part they started on */
    ui.learn = 0;                          /* LEARN was for the layer of the part left behind */
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
    release_part();
    if (!dx_part_used(LP(), p)) {
        r = dx_part_add(p);
        notice(r >= 0 ? "PART ADDED" : r == DX_PART_NO_ENGINE ? "NO FREE ENGINE" : r == DX_PART_NO_CHANNEL ? "NO FREE CHANNEL" : "NO FREE PART");
        if (r >= 0) { release_part(); dx_part_select((unsigned)r); }
        return;
    }
    r = dx_part_add_layer(p);
    if (r < 0) { notice(r == DX_PART_NO_ENGINE ? "NO FREE ENGINE\n8 LAYERS IN USE" : "CANNOT ADD A LAYER"); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list);
    for (unsigned i = 0; i < n; ++i) if (list[i] == (unsigned)r) { dx_part_select_layer(p, i); break; }
    notice("LAYER ADDED");
}
static void remove_here(void)
{
    unsigned p = live_sel_part();
    release_part();
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
    release_part();                        /* before the keys' channel moves with the part: their note offs must match the note ons */
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
static unsigned screen_learns(void) { return ui.screen == S_PART || ui.screen == S_EPLAYS || ui.screen == S_DETAIL; }

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
/* Pan and sends on the mixer belong to the whole part: every layer moves together. The pan step stops when the layer
 * furthest out reaches the edge, so layers that were set apart stay apart. */
static void part_pan_step(unsigned p, int d)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list);
    struct dx_live *live = dx_live_edit();
    int lo = 50, hi = -50;
    for (unsigned i = 0; i < n; ++i) { int v = live->pan[list[i]]; if (v < lo) lo = v; if (v > hi) hi = v; }
    int step = n ? clampi(d * 2, -50 - lo, 50 - hi) : 0;
    if (!step) return;
    for (unsigned i = 0; i < n; ++i) { live->pan[list[i]] = (int8_t)(live->pan[list[i]] + step); send_pan(list[i]); }
}
static void part_sends_step(unsigned p, unsigned which, int d)
{
    uint8_t list[DX_ENGINES];
    unsigned n = dx_part_layers(LP(), p, list);
    int lo = 15, hi = 0;
    for (unsigned i = 0; i < n; ++i) {
        uint8_t s[2];
        groove_sends_get(list[i], s);
        int v = s[which] & 15;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    int step = n ? clampi(d, -lo, 15 - hi) : 0;      /* like the pan: stop when the layer furthest out reaches the end */
    if (step) for (unsigned i = 0; i < n; ++i) groove_sends_edit(list[i], which, step);
}
/* On the mixer the sixteen white keys are the mute and solo keys of the eight parts, in the order the screen shows them:
 * part 1 mute, part 1 solo, part 2 mute and so on. Touching a part's key selects it too. */
static void mix_key(unsigned w)
{
    unsigned p = w / 2U;
#ifdef DX_SMK37
    if (p != live_sel_part()) release_part();             /* a key held on another part must be let go on that part */
#endif
    if (p != live_sel_part()) dx_part_select(p);
    if (dx_part_used(LP(), p)) { if (w & 1U) toggle_part_solo(p); else toggle_part_mute(p); }
    knob(dx_part_used(LP(), p) ? OV_PMIX : OV_PART, p);
}
static void toggle_mixkeys(void)
{
    release_keys();
    ui.mixkeys ^= 1U;
    notice(ui.mixkeys ? TXT("KEYS MUTE AND SOLO", "PADS MUTE AND SOLO") : TXT("KEYS PLAY AGAIN", "PADS RECALL AGAIN"));
}

/* ---- the panel -------------------------------------------------------------- */

static unsigned screen_has_parts(void)
{
    return ui.screen == S_PARTS || ui.screen == S_DETAIL || ui.screen == S_PART || ui.screen == S_ESOUND || ui.screen == S_EPLAYS || ui.screen == S_EVEL || ui.screen == S_ESENDS;
}
#ifdef DX_SMK37
/* The value of the System row on the cursor, a step down or up: K1 on the FM-1, and < and > on the SMK-37 (the same rows and limits). */
static void sys_value_step(int d)
{
    struct dx_settings s;
    dx_settings_get(&s);
    switch (ui.sys_row) {
    case 1: s.radio_off ^= GROOVE_RADIO_AUDIO; break;
    case 2: s.backlight = (uint8_t)clampi(s.backlight + d, 0, 8); break;
    case 3: s.scene_ch = (uint8_t)wrap((int)s.scene_ch + d, 17); break;
    case 4: s.master_ch = (uint8_t)wrap((int)s.master_ch + d, 17); break;
    default: return;
    }
    dx_settings_update(&s);
    knob(OV_SYS, ui.sys_row);
}
/* < and >, the SMK-37's back and forward: one step in what the screen is about. The part on Parts, Mix and FX, the layer on Part, Details and the
 * edit pages, the next stored scene on Scenes (it is recalled), the value of the row on System. Nothing on the other screens. */
static void step_here(int d)
{
    switch (ui.screen) {
    case S_PARTS: case S_MIX: case S_FX: select_part_step(d); knob(OV_PART, live_sel_part()); break;
    case S_PART: case S_DETAIL: case S_ESOUND: case S_EPLAYS: case S_EVEL: case S_ESENDS:
        select_layer_step(d);
        if (live_sel_engine() >= 0) knob(OV_LAYER, (unsigned)live_sel_engine());
        break;
    case S_SCENES: step_scene(d); break;
    case S_SYS: sys_value_step(d); break;
    default: break;
    }
}
#endif
static void on_button(unsigned pressed)
{
    if (ui.screen == S_BOOT) { go(S_PARTS); return; }
    if (ui.mode != M_BROWSE) { scene_overlay_button(pressed); return; }
    if ((ctl.buttons & BTN_BIT(BTN_HOME)) && (pressed & BTN_BIT(BTN_PLAY))) {
        /* HOME + PLAY: the HOME press that began the chord has already changed the screen; put it back */
        if (ui.home_from != 0xff && ui.home_from != S_BOOT && ui.screen != ui.home_from) {
            go(ui.home_from);
            ui.learn = ui.home_learn; ui.learn_lo = ui.home_learn_lo; ui.mixkeys = ui.home_mixkeys;   /* with what it had armed */
        }
        dx_route_panic(); notice("ALL NOTES OFF"); return;
    }
    if ((ctl.buttons & BTN_BIT(BTN_PLAY)) && (pressed & (BTN_BIT(BTN_OCT_DOWN) | BTN_BIT(BTN_OCT_UP)))) { /* PLAY held: step through the scenes */
        step_scene((pressed & BTN_BIT(BTN_OCT_UP)) ? 1 : -1);
        return;
    }
#ifdef DX_SMK37
    if (pressed & (BTN_BIT(BTN_BACK) | BTN_BIT(BTN_FWD))) { step_here((pressed & BTN_BIT(BTN_FWD)) ? 1 : -1); return; }
#endif
    if (pressed & BTN_BIT(BTN_HOME)) {
        ui.home_from = ui.screen; ui.home_learn = ui.learn; ui.home_learn_lo = ui.learn_lo; ui.home_mixkeys = ui.mixkeys;
        go(ui.screen == S_PARTS ? S_DETAIL : S_PARTS);
    } /* the second press shows the part in detail */
    else if (pressed & BTN_BIT(BTN_SEQ)) go(S_PART);
    else if (pressed & BTN_BIT(BTN_EDIT)) {
#ifndef DX_SMK37
        if (ui.screen == S_MIX) toggle_mixkeys();
        else
#endif
        go(ui.screen == S_ESOUND ? S_EPLAYS : ui.screen == S_EPLAYS ? S_EVEL : ui.screen == S_EVEL ? S_ESENDS : S_ESOUND);
    }
    else if (pressed & BTN_BIT(BTN_REC)) go(S_MON);
    else if (pressed & BTN_BIT(BTN_FX)) go(ui.screen == S_FX ? S_MASTER : S_FX);
    else if (pressed & BTN_BIT(BTN_SEL)) go(S_MIX);
    else if (pressed & BTN_BIT(BTN_PLAY)) go(S_SCENES);
    else if (pressed & BTN_BIT(BTN_GLO)) go(S_SYS);
    else if (pressed & BTN_BIT(BTN_SAVE)) begin_save();
    else if (pressed & (BTN_BIT(BTN_ENV) | BTN_BIT(BTN_LFO))) {
        unsigned add = (pressed & BTN_BIT(BTN_ENV)) != 0;
#ifdef DX_SMK37
        if (screen_has_parts() || ui.screen == S_MIX) { if (add) add_here(); else remove_here(); }
#else
        if (screen_has_parts()) { if (add) add_here(); else remove_here(); }
        else if (ui.screen == S_MIX) {
            unsigned p = live_sel_part();
            if (dx_part_used(LP(), p)) { if (add) toggle_part_mute(p); else toggle_part_solo(p); knob(OV_PMIX, p); }
            else if (add) add_here();                  /* nothing to mute in an empty slot: ENV makes a part there, as the screen says */
        }
        else if (ui.screen == S_SCENES) step_scene(add ? -1 : 1);
#endif
    }
    else if (pressed & BTN_BIT(BTN_ARP)) {
        if (screen_learns() && !ui.learn && live_sel_engine() < 0) notice("LEARN: ADD\nA PART FIRST");
        else if (screen_learns()) { ui.learn = ui.learn ? 0 : 1; ui.learn_sig = fr.nlog ? fr.log[0].ms ^ fr.log[0].d1 : 0; }
        else notice("LEARN: OPEN PART OR RANGE");
    }
    else if (pressed & (BTN_BIT(BTN_OCT_DOWN) | BTN_BIT(BTN_OCT_UP))) {
        /* The octave buttons step back and forward wherever a screen has pages, rows, banks or parts; on the screens
         * with none of those they move the keys' octave. */
        int d = (pressed & BTN_BIT(BTN_OCT_UP)) ? 1 : -1;
        switch (ui.screen) {
        case S_ESOUND: case S_EPLAYS: case S_EVEL: case S_ESENDS: go(S_ESOUND + wrap((int)ui.screen - S_ESOUND + d, 4)); break;
        case S_FX: case S_MASTER: go(ui.screen == S_FX ? S_MASTER : S_FX); break;
        case S_SCENES: ui.scene = (uint8_t)wrap((int)ui.scene + 16 * d, DX_SCENES); break;
        case S_SYS: ui.sys_row = (uint8_t)wrap((int)ui.sys_row + d, 7); break;
#ifdef DX_SMK37
        case S_DETAIL: select_part_step(d); knob(OV_PART, live_sel_part()); break;
#else
        case S_MIX: case S_DETAIL: select_part_step(d); knob(OV_PART, live_sel_part()); break;
#endif
        default: release_keys(); ui.octave = (int8_t)clampi(ui.octave + d, -2, 2); knob(OV_OCT, 0); break;
        }
    }
}

/* White keys recall scenes on the Scenes screen; everywhere else every key plays the selected part. */
static void on_white_key(unsigned n /* 0..15 */)
{
    if (ui.screen != S_SCENES) return;
    unsigned idx = (ui.scene / 16U) * 16U + n;
    ui.scene = (uint8_t)idx;
    if (scene_exists(idx)) { int r = dx_scene_recall(idx); notice(r ? "RECALL FAILED" : "SCENE RECALLED"); }
    else notice("EMPTY SLOT\nSAVE STORES HERE");
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
    if (ui.screen == S_MIX && ui.mixkeys && ui.mode == M_BROWSE) { if (down && w >= 0) mix_key((unsigned)w); return; }
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

/* A knob step on the volume of a part (all its layers) or of one layer inside its part: two units a click. */
static void part_level_step(unsigned part, int d)
{
    struct dx_live *live = dx_live_edit();
    live->parts.part[part].level = (uint8_t)clampi(live->parts.part[part].level + d * 2, 0, 127);
    dx_part_level_changed(part);
}
static void layer_level_step(unsigned engine, int d)
{
    struct dx_live *live = dx_live_edit();
    live->parts.layer[engine].lvl = (uint8_t)clampi(live->parts.layer[engine].lvl + d * 2, 0, 127);
    dx_level_changed(engine);
}
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
    if (e[0] && (screen_has_parts() || ui.screen == S_MIX || ui.screen == S_FX)) { select_part_step(e[0]); knob(OV_PART, live_sel_part()); }
    if (e[1] && (ui.screen == S_PART || ui.screen == S_DETAIL || ui.screen == S_ESOUND || ui.screen == S_EPLAYS || ui.screen == S_EVEL || ui.screen == S_ESENDS)) {
        select_layer_step(e[1]);
        if (live_sel_engine() >= 0) knob(OV_LAYER, (unsigned)live_sel_engine());
    }
    int se = live_sel_engine();            /* after the steps above: the selection as it is now */
    unsigned eng = se < 0 ? 0U : (unsigned)se, p = live_sel_part(), used = dx_part_used(LP(), p);
    /* PRESETS chooses the sound of the framed layer on every screen that shows a part and on every edit page */
    if (e[6] && se >= 0 && screen_has_parts()) { groove_control_edit(eng, e[6], 0, 0); dx_bright_force_voice(eng); ov_show(OV_SOUND, eng, 1800U); } /* a new voice: BRIGHT starts from it */
    switch (ui.screen) {
    case S_PARTS:                          /* K1: the volume of the selected part */
        if (e[2] && used) { part_level_step(p, e[2]); knob(OV_PLEVEL, p); }
        break;
    case S_PART:
        if (e[2] && used) { step_channel(e[2]); knob(OV_PCH, p); }
        if (e[3] && used) { part_level_step(p, e[3]); knob(OV_PLEVEL, p); }
        break;
    case S_DETAIL:                         /* K1 the framed layer's own level (the balance inside the part), K2 the part's level, K3 its channel */
        if (e[2] && se >= 0) { layer_level_step(eng, e[2]); knob(OV_LVL, eng); }
        if (e[3] && used) { part_level_step(p, e[3]); knob(OV_PLEVEL, p); }
        if (e[4] && used) { step_channel(e[4]); knob(OV_PCH, p); }
        break;
    case S_ESOUND:                         /* K1 the layer's volume, K2 its pan, K3 BRIGHT (PRESETS: the sound, above) */
        if (se < 0) break;
        if (e[2]) { layer_level_step(eng, e[2]); knob(OV_LVL, eng); }
        if (e[3]) { live->pan[eng] = (int8_t)clampi(live->pan[eng] + e[3] * 2, -50, 50); send_pan(eng); knob(OV_PAN, eng); }
        if (e[4]) { live->bright[eng] = (int8_t)clampi(live->bright[eng] + e[4], -50, 50); dx_bright_changed(eng); knob(OV_BRIGHT, eng); }
        break;
    case S_EPLAYS:
        if (se < 0) break;
        for (unsigned i = 0; i < 4; ++i) if (e[2 + i]) { edit_range(eng, (int)i, e[2 + i]); knob(OV_LO + i, eng); }
        break;
    case S_EVEL:
        if (se < 0) break;
        for (unsigned i = 0; i < 2; ++i) if (e[2 + i]) { edit_velocity(eng, (int)i, e[2 + i]); knob(OV_VLO + i, eng); }
        break;
    case S_ESENDS:
        if (se < 0) break;
        if (e[2]) { groove_sends_edit(eng, 0, e[2]); knob(OV_REV, eng); }
        if (e[3]) { groove_sends_edit(eng, 1, e[3]); knob(OV_DLY, eng); }
        if (e[4]) {
            struct dx_cfg *c = cfg_begin();
            c->e[eng].poly = (uint8_t)clampi(c->e[eng].poly + e[4], 1, 8);
            cfg_commit();
            knob(OV_POLY, eng);
        }
        break;
#ifdef DX_SMK37
    case S_MIX:                            /* K1 to K4: the pan of four parts, 1 to 4 or 5 to 8 (KNOB BANK); K6: the channel of the selected part */
        if (e[1] && used) { step_channel(e[1]); knob(OV_PCH, p); }
        for (unsigned i = 0; i < 4; ++i) {
            unsigned part = smk37_knob_bank() * 4U + i;
            if (e[2 + i] && dx_part_used(LP(), part)) { part_pan_step(part, e[2 + i]); knob(OV_PPAN, part); }
        }
        break;
#else
    case S_MIX:                            /* every knob here works on the whole part */
        if (!used) break;
        if (e[1]) { step_channel(e[1]); knob(OV_PCH, p); }
        if (e[2]) { part_level_step(p, e[2]); knob(OV_PLEVEL, p); }
        if (e[3]) { part_pan_step(p, e[3]); knob(OV_PPAN, p); }
        if (e[4]) { part_sends_step(p, 0, e[4]); knob(OV_PREV, p); }
        if (e[5]) { part_sends_step(p, 1, e[5]); knob(OV_PDLY, p); }
        break;
#endif
    case S_FX:
        for (unsigned i = 0; i < 4; ++i) if (e[2 + i]) { groove_effects_edit(i, e[2 + i]); knob(OV_FX, i); }
        break;
    case S_MASTER:
        if (e[2]) { live->filter_type = (uint8_t)wrap((int)live->filter_type + e[2], 4); apply_filter(); knob(OV_FTYPE, 0); }
        if (e[3]) {
            unsigned hz = live->filter_hz ? live->filter_hz : 1000U;
            for (int i = 0; i < (e[3] < 0 ? -e[3] : e[3]); ++i) hz = e[3] > 0 ? hz * 1059U / 1000U + 1U : hz * 944U / 1000U;
            live->filter_hz = (uint16_t)clampi((int)hz, 20, 20000); apply_filter();
            knob(OV_FHZ, 0);
        }
        if (e[4]) { live->filter_res = (uint8_t)clampi(live->filter_res + e[4] * 3, 0, 127); apply_filter(); knob(OV_FRES, 0); }
        break;
    case S_SCENES: if (e[0]) { ui.scene = (uint8_t)wrap((int)ui.scene + e[0], DX_SCENES); knob(OV_SCENE, ui.scene); } break;
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
        if (ui.sys_row >= 1 && ui.sys_row <= 4) knob(OV_SYS, ui.sys_row);
        break;
    }
    default: break;
    }
}

#ifdef DX_SMK37
/* ---- the SMK-37: keys with velocity, pads, faders, wheels, pedal ------------------------------------------------------
 * targets/smk37/app/smk37_controls.c turns the board's events into these calls; the FM-1 never makes them. The 37 keys always play
 * the selected part, at the velocity they are struck with. The 16 pads are what the FM-1's 16 white keys are on Scenes and on Mix
 * (EDIT): scene n of the bank, or mute and solo; the four faders are part levels, the wheels bend and modulate, the pedal sustains. */
void dx_ui_note(unsigned note, unsigned velocity, unsigned down)
{
    if (note > 127 || ui.screen == S_SAFE) return;
    if (ui.screen == S_BOOT) { if (down) go(S_PARTS); return; }
    uint32_t bit = 1U << (note & 31U);
    if (!down) { if (smk_held[note >> 5] & bit) { dx_route_key_v(note, 0, 0); smk_held[note >> 5] &= ~bit; } return; }
    if (ui.learn && ui.mode == M_BROWSE && screen_learns()) { learn_note(note); return; }     /* while LEARN waits, the keys are the notes */
    if (ui.mode != M_BROWSE) return;
    dx_route_key_v(note, velocity, 1);
    smk_held[note >> 5] |= bit;
}
/* Part, Details and the edit pages: the 16 pads are the layers of the selected part (the top row) and the eight parts (the lower row). */
static unsigned pad_layers(void)
{
    return ui.screen == S_PART || ui.screen == S_DETAIL || ui.screen == S_ESOUND || ui.screen == S_EPLAYS || ui.screen == S_EVEL || ui.screen == S_ESENDS;
}
void dx_ui_pad(unsigned pad, unsigned down)
{
    if (!down || pad > 15 || ui.screen == S_SAFE) return;
    if (ui.screen == S_BOOT) { go(S_PARTS); return; }
    if (ui.mode != M_BROWSE) return;
    if (ui.screen == S_MIX) { mix_key(pad < 8U ? pad * 2U : (pad - 8U) * 2U + 1U); return; }      /* top row: mute part 1 to 8, lower row: solo */
    if (pad_layers()) {
        if (pad >= 8U) {                                          /* lower row: the part */
            release_part(); ui.learn = 0;
            dx_part_select(pad - 8U);
            knob(OV_PART, live_sel_part());
        } else {                                                  /* top row: the layer of the selected part */
            uint8_t list[DX_ENGINES];
            unsigned n = dx_part_layers(LP(), live_sel_part(), list);
            if (pad < n) { dx_part_select_layer(live_sel_part(), pad); knob(OV_LAYER, list[pad]); }
        }
        return;
    }
    unsigned idx = (ui.scene / 16U) * 16U + pad;              /* the bank on screen (bank A at power-on) */
    ui.scene = (uint8_t)idx;
    if (scene_exists(idx)) { int r = dx_scene_recall(idx); notice(r ? "RECALL FAILED" : "SCENE RECALLED"); }
    else {
        static char text[44];                                  /* EMPTY SLOT, then the printed button that saves a scene */
        char *o = put_s(text, "EMPTY SLOT\n");
        o = put_s(o, smk37_button_label(BTN_SAVE));
        put_s(o, " STORES HERE");
        notice(text);
    }
}
int dx_ui_octave(void) { return ui.octave; }

/* The faders: what each one controls depends on the screen on view, and the controls layer asks; a fader sets its thing as soon as it moves.
 * A role is 0 to 7, the level of that part, or one of FR_*. Parts: the master filter; Master: the master filter; Details and FX: the shared effects;
 * everywhere else the levels of four parts, 1 to 4 or 5 to 8 (FADER BANK). */
enum { FR_TYPE = 8, FR_CUTOFF, FR_RES, FR_DECAY, FR_DTIME, FR_FEEDBACK, FR_RETURN, FR_NONE = 255 };
unsigned dx_ui_fader_role(unsigned fader, unsigned bank)
{
    if (ui.screen == S_SAFE || ui.screen == S_BOOT) return FR_NONE;
    switch (ui.screen) {
    case S_PARTS: return fader == 0 ? FR_CUTOFF : fader == 1 ? FR_RES : fader == 2 ? FR_TYPE : FR_NONE;
    case S_MASTER: return fader == 0 ? FR_TYPE : fader == 1 ? FR_CUTOFF : fader == 2 ? FR_RES : FR_NONE;
    case S_DETAIL: case S_FX: return FR_DECAY + fader;
    default: { unsigned part = bank * 4U + fader; return part < DX_PARTS && dx_part_used(LP(), part) ? part : FR_NONE; }
    }
}
/* The cutoff on a scale of 0 to 127: 20 Hz to 20 kHz in equal ratios (about 5.6% a step). */
static unsigned cutoff_hz(unsigned v)
{
    if (v == 0) return 20U;
    if (v >= 127U) return 20000U;
    unsigned h = 20U * 64U;
    for (unsigned i = 0; i < v; ++i) h += h * 559U / 10000U;
    h /= 64U;
    return h < 20U ? 20U : h > 20000U ? 20000U : h;
}
void dx_ui_role_set(unsigned role, unsigned v)
{
    struct dx_live *live = dx_live_edit();
    uint8_t fx[4];
    if (v > 127U) v = 127U;
    switch (role) {
    case FR_TYPE: live->filter_type = (uint8_t)(v >> 5); apply_filter(); knob(OV_FTYPE, 0); break;
    case FR_CUTOFF:                         /* a cutoff or resonance move turns an off filter into a low-pass so that it is heard, as over MIDI */
        live->filter_hz = (uint16_t)cutoff_hz(v);
        if (live->filter_type == DX_FILTER_OFF) live->filter_type = DX_FILTER_LP;
        apply_filter(); knob(OV_FHZ, 0); break;
    case FR_RES:
        live->filter_res = (uint8_t)v;
        if (live->filter_type == DX_FILTER_OFF) live->filter_type = DX_FILTER_LP;
        apply_filter(); knob(OV_FRES, 0); break;
    case FR_DECAY: groove_effects_get(fx); groove_effects_edit(0, (int)v - (int)fx[0]); knob(OV_FX, 0); break;
    case FR_DTIME:
        groove_effects_get(fx);
        if (groove_delay_synced(fx[1])) groove_effects_edit(1, (int)(GROOVE_DELAY_SYNC + v * 8U / 127U) - (int)fx[1]);
        else groove_effects_edit(1, (int)(v * 244U / 127U) - (int)(fx[1] > GROOVE_DELAY_LAST ? fx[1] - 9U : fx[1]));
        knob(OV_FX, 1); break;
    case FR_FEEDBACK: groove_effects_get(fx); groove_effects_edit(2, (int)v - (int)fx[2]); knob(OV_FX, 2); break;
    case FR_RETURN: groove_effects_get(fx); groove_effects_edit(3, (int)v - (int)fx[3]); knob(OV_FX, 3); break;
    default:
        if (role < DX_PARTS && dx_part_used(LP(), role)) {
            live->parts.part[role].level = (uint8_t)v;
            dx_part_level_changed(role);
            knob(OV_PLEVEL, role);
        }
    }
}
void dx_ui_wheel(unsigned wheel, unsigned value)
{
    if (ui.screen == S_SAFE || ui.screen == S_BOOT) return;    /* safe boot and the boot animation (which includes the chord's scan): no routing, no panel input */
    if (value > 127U) value = 127U;
    if (wheel) { smk_mod = (uint8_t)value; dx_route_ctl(0xb0, 1, value); } else { smk_bend = (uint8_t)value; dx_route_ctl(0xe0, 0, value); }
}
void dx_ui_pedal(unsigned down)
{
    if (ui.screen == S_SAFE || ui.screen == S_BOOT || (!down && !smk_pedal)) return;   /* an up without a down: nothing to lift */
    smk_pedal = down ? 1U : 0U; dx_route_ctl(0xb0, 64, down ? 127U : 0U);
}
static uint32_t rgb888(uint16_t c) { return ((uint32_t)(c >> 11) * 255U / 31U) << 16 | ((uint32_t)((c >> 5) & 63U) * 255U / 63U) << 8 | ((uint32_t)(c & 31U) * 255U / 31U); }
static uint32_t scaled(uint32_t rgb, unsigned num, unsigned den)
{ return (((rgb >> 16) & 255U) * num / den) << 16 | (((rgb >> 8) & 255U) * num / den) << 8 | ((rgb & 255U) * num / den); }
/* The pads' lights. Mix: the top row is the mute of part 1 to 8 (dim red, bright red while muted), the lower row the solo (dim amber, bright amber while
 * soloed), dark where there is no part. Part, Details, edit pages: the top row is the layers of the selected part in the part's colour (the framed one
 * bright), the lower row the eight parts in theirs (the selected one bright). Elsewhere the scenes: dark where empty, teal where stored, bright
 * teal for the scene in use, white for the cursor, bright white for both. */
uint32_t dx_ui_pad_color(unsigned pad)
{
    if (pad > 15) return 0;
    if (ui.screen == S_MIX) {
        unsigned p = pad & 7U;
        if (!part_used(p)) return 0;
        if (pad < 8U) return part_muted(p) ? 0xFF2020U : 0x300808U;
        return part_solo(p) ? 0xFFB000U : 0x302000U;
    }
    if (pad_layers()) {
        unsigned p = live_sel_part();
        if (pad >= 8U) {
            unsigned q = pad - 8U;
            if (!part_used(q)) return 0;
            return q == p ? rgb888(EC[q & 7U]) : scaled(rgb888(EC[q & 7U]), 1, 6);
        }
        uint8_t list[DX_ENGINES];
        unsigned n = dx_part_layers(LP(), p, list), framed = LP()->part[p].sel < n ? LP()->part[p].sel : 0U;
        if (pad >= n) return 0;
        return pad == framed ? rgb888(EC[p & 7U]) : scaled(rgb888(EC[p & 7U]), 1, 6);
    }
    unsigned idx = (ui.scene / 16U) * 16U + pad;
    unsigned cursor = idx == ui.scene, current = idx == fr.current_scene;
    if (!scene_exists(idx)) return cursor ? 0x282828U : 0U;
    if (current) return cursor ? 0xFFFFFFU : 0x20FFE0U;
    return cursor ? 0xC0C0C0U : 0x0B4A42U;
}
#endif

/* ---- lamps ------------------------------------------------------------------ */

static void led_put(unsigned index, unsigned code)
{
    if (index >= sizeof led_code || led_code[index] == code) return;
    led_code[index] = (uint8_t)code;
#ifdef DX_SMK37
    if (index < 14U) { fm1_led_level(index, code < 2U ? code : code - 2U); return; }    /* the lamp is on the printed button that carries it */
#endif
    if (code < 2U) fm1_led_set(index, (int)code); else fm1_led_level(index, code - 2U);
}
static void led_set(unsigned index, unsigned on) { led_put(index, on ? 1U : 0U); }
static void led_level(unsigned index, unsigned level) { led_put(index, 2U + level); }
static void update_leds(void)
{
    unsigned active_button = 99;
    switch (ui.screen) {
    case S_PARTS: case S_DETAIL: active_button = BTN_HOME; break;
    case S_PART: active_button = BTN_SEQ; break;
    case S_ESOUND: case S_EPLAYS: case S_EVEL: case S_ESENDS: active_button = BTN_EDIT; break;
    case S_MON: active_button = BTN_REC; break;
    case S_FX: case S_MASTER: active_button = BTN_FX; break;
    case S_MIX: active_button = BTN_SEL; break;
    case S_SCENES: active_button = BTN_PLAY; break;
    case S_SYS: active_button = BTN_GLO; break;
    default: break;
    }
    for (unsigned b = 0; b < 14; ++b) led_set(b, b == active_button || (b == BTN_ARP && ui.learn && fr.lit_blink) || (b == BTN_EDIT && MIX_EDIT_LAMP));
    /* The white keys: scenes that exist on the Scenes screen; on the mixer in key mode the mute and solo keys, dark for a
     * part that is not there, dim while they could be pressed and bright while the part is muted or soloed. */
    for (unsigned i = 0; i < 16; ++i) {
        unsigned led = 14U + white_key[i], lit = 0;
        if (ui.mode == M_BROWSE && ui.screen == S_SCENES) lit = scene_exists((ui.scene / 16U) * 16U + i);
        if (ui.mode == M_BROWSE && ui.screen == S_MIX && ui.mixkeys) {
            unsigned p = i / 2U, on = (i & 1U) ? part_solo(p) : part_muted(p);
            led_level(led, !part_used(p) ? 0U : on ? 8U : 2U);
        } else led_set(led, lit);
    }
    /* In the mixer's key mode the black keys do nothing: dark, so that a key that works is never dimmer than one that does not
     * (the idle light of the other lamps is BUTTON LIGHT, 3 by default). Otherwise they keep the idle light. */
    for (unsigned k = 0; k < 27; ++k) {
        if (wkey_index(k) >= 0) continue;
        if (ui.mode == M_BROWSE && ui.screen == S_MIX && ui.mixkeys) led_level(14U + k, 0U); else led_set(14U + k, 0U);
    }
}
