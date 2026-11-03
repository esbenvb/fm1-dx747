/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 screen state, text formatting and the per-frame snapshot. Included by
 * app/screen.c only, after ui_core.inc. */

enum { S_BOOT, S_SETUP, S_ENGINES, S_CHANNEL, S_ESOUND, S_EPLAYS, S_ESENDS, S_MON, S_MIX, S_FX, S_MASTER, S_SCENES, S_SYS, S_COUNT };
enum { M_BROWSE, M_NAME, M_CLAIM };                      /* scene screen modes */
enum { BTN_OCT_DOWN, BTN_OCT_UP, BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC };
#define BTN_BIT(b) (1U << (b))
static const uint8_t white_key[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static struct ui {
    uint8_t screen, engine, channel, lane, scene, mode, sys_row, learn, learn_lo, name_pos;
    int8_t octave;
    char name[12];
    uint32_t notice_until, boot_until, learn_sig;
    const char *notice;
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
/* C3 is note 60, as on a DX7. */
static char *put_note(char *o, unsigned n)
{
    static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    o = put_s(o, names[n % 12U]);
    return put_i(o, (int)(n / 12U) - 2);
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
    uint8_t level, volume, muted, program, rev, dly;
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
    uint32_t now;
    uint8_t scene_used[DX_SCENES / 8];
    char scene_name[DX_SCENES][12];
    uint8_t scene_engines[DX_SCENES];
    uint8_t scenes_known;
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
        e->level = v.level; e->volume = v.volume; e->muted = v.muted; e->program = v.program;
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
    struct fm1_render_stats st;
    groove_audio_stats(&st);
    fr.cpu = st.deadline_ticks ? (st.render_ticks * 100U) / st.deadline_ticks : 0U;
    if (fr.cpu > 99U) fr.cpu = 99U;
    /* The scene catalogue changes only when a scene is written. */
    if (!fr.scenes_known) {
        memset(fr.scene_used, 0, sizeof fr.scene_used);
        for (unsigned i = 0; i < DX_SCENES; ++i) {
            char name[13]; uint8_t used = 0;
            if (!dx_scene_info(i, name, &used)) {
                fr.scene_used[i / 8] |= (uint8_t)(1U << (i % 8)); memcpy(fr.scene_name[i], name, 12); fr.scene_engines[i] = used;
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
/* Engines that listen to channel k, lowest first. */
static unsigned engines_on(unsigned k, uint8_t list[DX_ENGINES])
{
    unsigned n = 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) if ((fr.cfg.e[e].rx >> k) & 1U) list[n++] = (uint8_t)e;
    return n;
}
static uint16_t engine_color(unsigned e) { return fr.cfg.e[e].mute ? C_MU : EC[e]; }
static unsigned max_layers(unsigned k)
{
    uint8_t list[DX_ENGINES];
    unsigned n = engines_on(k, list), best = 0;
    for (unsigned note = 0; note < 128; ++note) {
        unsigned c = 0;
        for (unsigned i = 0; i < n; ++i) {
            const struct dx_engine_cfg *e = &fr.cfg.e[list[i]];
            c += !e->mute && note >= e->lo && note <= e->hi;
        }
        if (c > best) best = c;
    }
    return best;
}
