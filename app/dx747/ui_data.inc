/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 screen state, text formatting and the per-frame snapshot. Included by
 * app/screen.c only, after ui_core.inc. */

enum { S_BOOT, S_PARTS, S_PART, S_ESOUND, S_EPLAYS, S_EVEL, S_ESENDS, S_MON, S_MIX, S_FX, S_MASTER, S_SCENES, S_SYS, S_SAFE, S_DETAIL, S_COUNT };
enum { M_BROWSE, M_NAME, M_CLAIM };                      /* scene screen modes */
enum { BTN_OCT_DOWN, BTN_OCT_UP, BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC };
#define BTN_BIT(b) (1U << (b))
static const uint8_t white_key[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

/* The part being edited is the selected part (live.parts.selected, the one the keyboard plays); the layer is its
 * selected layer. Neither is kept here: they belong to the scene. */
/* What the overlay says: a box over the lower part of the screen that follows a knob or a message for a moment. A kind
 * with an argument names what it is about: a layer kind the engine, a part kind the part, OV_FX the effect knob,
 * OV_SCENE the scene, OV_SYS the system row. The words and numbers are read from the frame snapshot when it is drawn. */
enum { OV_NONE, OV_TEXT, OV_PART, OV_LAYER, OV_SOUND, OV_LVL, OV_PAN, OV_BRIGHT, OV_LO, OV_HI, OV_SHIFT, OV_FOLD, OV_VLO, OV_VHI,
       OV_REV, OV_DLY, OV_POLY, OV_PLEVEL, OV_PCH, OV_PPAN, OV_PREV, OV_PDLY, OV_PMIX, OV_FX, OV_FTYPE, OV_FHZ, OV_FRES, OV_SCENE,
       OV_OCT, OV_SYS };

#define SL_ROWS 7U                  /* programs in the sound overlay: the current one in the middle, three before and three after */
#define BOOT_MS 5000U               /* the boot screen: the 747 takes off, then the Parts screen (any button skips it) */
static struct ui {
    uint8_t screen, scene, mode, sys_row, learn, learn_lo, name_pos;
    uint8_t mixkeys;                 /* Mix: the white keys mute and solo (EDIT toggles it) */
    uint8_t home_from;               /* the screen HOME was pressed on, while HOME is held (the HOME + PLAY chord puts it back), else 0xff */
    uint8_t home_learn, home_learn_lo, home_mixkeys; /* what that screen had armed, put back with it */
    uint8_t ov_kind, ov_arg;
    int8_t octave;
    char name[12];
    uint32_t ov_until, boot_until, learn_sig;
    uint16_t boot_t;                 /* boot screen: milliseconds since power-on, 0 to BOOT_MS (the clock of the animation) */
    const char *notice;              /* the words of OV_TEXT */
} ui;

/* ---- text ---------------------------------------------------------------- */

static char *put_s(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static char *put_u(char *o, unsigned v)
{
    char tmp[10]; unsigned n = 0;
    do { tmp[n++] = (char)('0' + v % 10U); v /= 10U; } while (v);
    while (n) *o++ = tmp[--n];
    *o = 0; return o;
}
static char *put_i(char *o, int v) { if (v < 0) { *o++ = '-'; v = -v; } return put_u(o, (unsigned)v); }
static char *put_2(char *o, unsigned v) { *o++ = (char)('0' + (v / 10U) % 10U); *o++ = (char)('0' + v % 10U); *o = 0; return o; }
/* Standard naming: note 60 is C4, as on the keys' own labels and in most DAWs
 * (not the Yamaha DX7 convention, where 60 is C3). */
static char *put_note(char *o, unsigned n)
{
    static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    o = put_s(o, names[n % 12U]);
    return put_i(o, (int)(n / 12U) - 1);
}
static char *put_range(char *o, unsigned lo, unsigned hi)
{
    if (lo == 0 && hi == 127) return put_s(o, "ALL");
    o = put_note(o, lo); *o++ = '-';
    return put_note(o, hi);
}
/* Octave shift as +2 for whole octaves, +7ST otherwise. */
static char *put_shift(char *o, int s)
{
    if (!s) return put_s(o, "0");
    if (s > 0) *o++ = '+';
    if (s % 12 == 0) return put_i(o, s / 12);
    o = put_i(o, s);
    return put_s(o, "ST");
}
static char *put_pan(char *o, int p)
{
    if (!p) return put_s(o, "C");
    *o++ = p < 0 ? 'L' : 'R';
    return put_u(o, (unsigned)(p < 0 ? -p : p));
}
/* "1,3,4" or "6 CH" when the list is long. */
static char *put_channels(char *o, unsigned mask)
{
    unsigned count = 0, first = 1;
    for (unsigned k = 0; k < 16; ++k) count += (mask >> k) & 1U;
    if (!count) return put_s(o, "NONE");
    if (count > 5) { o = put_u(o, count); return put_s(o, " CH"); }
    for (unsigned k = 0; k < 16; ++k) if ((mask >> k) & 1U) {
        if (!first) *o++ = ',';
        first = 0; o = put_u(o, k + 1U);
    }
    *o = 0; return o;
}

/* ---- the snapshot every draw function reads ------------------------------- */

struct fr_engine {
    uint8_t level, volume, muted, solo, audible, program, rev, dly;
    char name[11];
};
static struct frame {
    struct dx_cfg cfg;
    struct fr_engine e[DX_ENGINES];
    struct dx_live live;
    struct dx_settings settings;
    struct dx_log log[DX_LOG_MAX];
    unsigned voices, nlog, cpu, current_scene, storage;
    uint16_t active;                 /* channels with notes in the last 1.2 s */
    uint8_t src[16];
    uint8_t lit_blink;               /* 2 Hz phase for flashing cues */
    uint8_t ov_on;                   /* the overlay is showing (decided on the real clock; now below is coarser) */
    uint8_t pprog;                   /* the sound overlay: the program in the middle of its list, and the names of the seven around it */
    char pname[SL_ROWS][11];
    uint32_t now;
    uint8_t scene_used[DX_SCENES / 8];
    char scene_name[DX_SCENES][12];
    uint8_t scene_parts[DX_SCENES];
    uint8_t scenes_known;
    unsigned scene_rev;              /* dx_scene_revision when the catalogue above was read */
} fr;

static void frame_collect(uint32_t now)
{
    fr.cfg = *dx_route_cfg();
    fr.voices = dx_route_voices();
    fr.active = 0;
    for (unsigned k = 0; k < 16; ++k) if (dx_route_active(k)) { fr.active |= (uint16_t)(1U << k); fr.src[k] = (uint8_t)dx_route_source(k); }
    fr.nlog = dx_route_log(fr.log, DX_LOG_MAX);
    fr.live = *dx_live_edit();
    dx_settings_get(&fr.settings);
    fr.current_scene = dx_scene_current();
    fr.storage = (unsigned)dx_storage_state_cached();
    fr.lit_blink = (uint8_t)((now / 250U) & 1U);
    fr.now = now;
    for (unsigned t = 0; t < DX_ENGINES; ++t) {
        struct groove_view v;
        groove_get_view(t, 0, &v);
        struct fr_engine *e = &fr.e[t];
        e->level = v.level; e->volume = v.volume; e->muted = v.muted; e->solo = v.solo; e->audible = v.audible; e->program = v.program;
        uint8_t sends[2] = {0, 0}, patch[156];
        groove_sends_get(t, sends);
        e->rev = sends[0] & 15U; e->dly = sends[1] & 15U;
        memset(e->name, ' ', 10); e->name[10] = 0;
        if (groove_fm_copy(t, patch, NULL) == 0)
            for (unsigned i = 0; i < 10; ++i) {
                char c = (char)patch[145 + i];
                e->name[i] = c >= 32 && c <= 126 ? c : ' ';
            }
    }
    if (ui.ov_kind == OV_SOUND && ui.ov_arg < DX_ENGINES) {      /* the sound overlay lists the three programs before and after the current one */
        unsigned prog = fr.e[ui.ov_arg].program % FM1_FM_PROGRAMS;
        fr.pprog = (uint8_t)prog;
        for (unsigned k = 0; k < SL_ROWS; ++k) {
            uint8_t patch[156];
            groove_fm_program(patch, (prog + FM1_FM_PROGRAMS + k - SL_ROWS / 2U) % FM1_FM_PROGRAMS);
            for (unsigned i = 0; i < 10; ++i) { char c = (char)patch[145 + i]; fr.pname[k][i] = c >= 32 && c <= 126 ? c : ' '; }
            fr.pname[k][10] = 0;
        }
    }
    struct fm1_render_stats st;
    groove_audio_stats(&st);
    fr.cpu = st.deadline_ticks ? (st.render_ticks * 100U) / st.deadline_ticks : 0U;
    if (fr.cpu > 99U) fr.cpu = 99U;
    /* The scene catalogue changes only when a scene is written. */
    if (fr.scene_rev != dx_scene_revision()) { fr.scene_rev = dx_scene_revision(); fr.scenes_known = 0; }   /* the workbench can write scenes too */
    if (!fr.scenes_known) {
        memset(fr.scene_used, 0, sizeof fr.scene_used);
        for (unsigned i = 0; i < DX_SCENES; ++i) {
            char name[13]; uint8_t used = 0;
            if (!dx_scene_info(i, name, &used)) {
                fr.scene_used[i / 8] |= (uint8_t)(1U << (i % 8)); memcpy(fr.scene_name[i], name, 12); fr.scene_parts[i] = used;
            }
        }
        fr.scenes_known = 1;
    }
}
static unsigned scene_exists(unsigned i) { return i < DX_SCENES && ((fr.scene_used[i / 8] >> (i % 8)) & 1U); }
static void trim_name(char *out, const char *name, unsigned n)
{
    unsigned len = n;
    while (len && name[len - 1] == ' ') --len;
    memcpy(out, name, len); out[len] = 0;
}
/* ---- parts as the screens see them -------------------------------------------- */

static unsigned sel_part(void) { return fr.live.parts.selected; }
static unsigned part_used(unsigned p) { return dx_part_used(&fr.live.parts, p); }
static unsigned part_layers(unsigned p, uint8_t list[DX_ENGINES]) { return dx_part_layers(&fr.live.parts, p, list); }
/* The engine of the selected layer of the selected part, -1 when the selected part is empty. */
static int sel_engine(void) { return dx_part_sel_engine(&fr.live.parts, sel_part()); }
static unsigned engines_in_use(void)
{
    unsigned n = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) n += fr.live.parts.layer[e].owner != DX_NO_PART;
    return n;
}
static unsigned engines_up(void)
{
    unsigned n = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) n += (fr.cfg.up >> e) & 1U;
    return n;
}
static unsigned part_muted(unsigned p)
{
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), muted = n != 0;
    for (unsigned i = 0; i < n; ++i) muted &= fr.cfg.e[list[i]].mute != 0;
    return muted;
}
static unsigned part_solo(unsigned p)
{
    uint8_t list[DX_ENGINES];
    return part_layers(p, list) && fr.e[list[0]].solo;
}
/* The layer that names a part on the overview: the one that plays the most keys, the first of equals. */
static int part_main(unsigned p)
{
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), best = 0;
    for (unsigned i = 1; i < n; ++i) {
        const struct dx_engine_cfg *a = &fr.cfg.e[list[i]], *b = &fr.cfg.e[list[best]];
        if (a->hi - a->lo > b->hi - b->lo) best = i;
    }
    return n ? list[best] : -1;
}
static uint16_t part_color(unsigned p) { return part_muted(p) ? C_MU : EC[p & 7U]; }
/* The colour of an engine: its part's, or dim when the engine is free. */
static uint16_t engine_color(unsigned e)
{
    unsigned owner = fr.live.parts.layer[e].owner;
    return owner < DX_PARTS ? part_color(owner) : C_MU;
}
/* Position of an engine among its part's layers (0-based), 0 for a free engine. */
static unsigned layer_index(unsigned e)
{
    uint8_t list[DX_ENGINES];
    unsigned owner = fr.live.parts.layer[e].owner, n = owner < DX_PARTS ? part_layers(owner, list) : 0;
    for (unsigned i = 0; i < n; ++i) if (list[i] == e) return i;
    return 0;
}
