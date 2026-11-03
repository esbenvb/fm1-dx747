/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 screens, as in docs/sketches/dx747-screens-v2.html. Every function draws
 * the current band from the snapshot fr and the state ui; nothing here reads the
 * engine. Included by app/screen.c only, after ui_data.inc. */

#define C_OUT RGB(0x55, 0x55, 0x55)
static char tb[64];

/* ---- shared pieces ------------------------------------------------------- */

static void draw_head(void)
{
    char name[13];
    unsigned cur = fr.current_scene;
    if (cur < DX_SCENES && scene_exists(cur)) {
        put_2(tb, cur + 1U);
        px_text(4, 5, tb, 2, C_AC);
        trim_name(name, fr.scene_name[cur], 12);
        px_text(30, 5, name, 2, C_TX);
    } else {
        px_text(4, 5, "--", 2, C_AC);
        px_text(30, 5, "NO SCENE", 2, C_DIM);
    }
    char *o = put_s(tb, "VOICES ");
    o = put_u(o, fr.voices); *o++ = '/'; put_u(o, fr.cfg.pool);
    px_text_r(236, 2, tb, 1, C_DIM);
    unsigned pool = fr.cfg.pool ? fr.cfg.pool : 1U, used = (fr.voices * 20U + pool - 1U) / pool;
    for (unsigned i = 0; i < 20; ++i)
        px_rect(157 + (int)i * 4, 12, 3, 9, i < used ? (fr.voices * 100U >= pool * 85U ? C_AM : C_GR) : RGB(0x2C, 0x2C, 0x2C));
    px_rect(0, 25, 240, 1, C_LN);
}
static void draw_title(const char *t, const char *right)
{
    px_text(4, 29, t, 2, C_AC);
    if (right) px_text_r(236, 33, right, 1, C_DIM);
}
static void draw_hint(const char *s)
{
    if (ui.notice && (int32_t)(ui.notice_until - fr.now) > 0) px_text_c(120, 230, ui.notice, 1, C_AM);
    else px_text_c(120, 230, s, 1, C_DIM);
}
static void chip(unsigned e, int x, int y, int w, int h, unsigned sel)
{
    char d[2] = {(char)('1' + e), 0};
    px_rect(x, y, w, h, engine_color(e));
    if (h >= 16) px_text_c(x + w / 2, y + (h - 14) / 2, d, 2, C_BLACK);
    else { /* a 14 px glyph would touch the chip edges: a bold 7 px digit instead */
        int ty = y + (h - 7) / 2;
        px_text_c(x + w / 2, ty, d, 1, C_BLACK);
        px_text_c(x + w / 2 + 1, ty, d, 1, C_BLACK);
    }
    if (sel) px_frame(x - 2, y - 2, w + 4, h + 4, C_TX);
}
static void tile(unsigned i, const char *label, const char *val, uint16_t color)
{
    int x = 6 + 59 * (int)i, y = 195;
    unsigned scale = text_w(val, 2) > 49 ? 1U : 2U;
    px_frame(x, y, 55, 32, C_LN);
    px_text(x + 3, y + 3, label, 1, C_DIM);
    px_text_c(x + 27, y + (scale == 2 ? 14 : 19), val, scale, color);
}
static void tile_u(unsigned i, const char *label, unsigned v, uint16_t color) { put_u(tb, v); tile(i, label, tb, color); }
static void bar(int x, int y, int w, int h, uint16_t color, unsigned num, unsigned den)
{
    px_frame(x, y, w, h, C_LN);
    px_rect(x + 1, y + 1, (w - 2) * (int)(num > den ? den : num) / (int)(den ? den : 1U), h - 2, color);
}
struct cell { char t[4]; uint16_t fill; uint8_t sel; };
static void strip(int y, const struct cell *c)
{
    for (unsigned i = 0; i < 16; ++i) {
        int x = 8 + 14 * (int)i;
        if (c[i].fill) px_rect(x, y, 12, 12, c[i].fill); else px_frame(x, y, 12, 12, C_OUT);
        if (c[i].t[0]) px_text_c(x + 6, y + 3, c[i].t, 1, c[i].fill ? C_BLACK : C_DIM);
        if (c[i].sel) px_frame(x - 1, y - 1, 14, 14, C_TX);
    }
}
static void cell_num(struct cell *c, unsigned i) { char *o = put_u(c->t, i + 1U); (void)o; }

/* ---- boot ----------------------------------------------------------------- */

static void draw_boot(void)
{
    static const int16_t body[][2] = {{12,103},{17,97},{30,94},{42,93},{55,70},{100,70},{130,92},{190,92},{222,86},{222,95},{196,106},{30,106},{17,107}};
    static const int16_t fin[][2] = {{172,92},{192,56},{210,56},{207,89}};
    static const int16_t stab[][2] = {{190,98},{164,104},{184,104},{208,98}};
    static const int16_t wing[][2] = {{96,106},{140,106},{170,128},{150,128}};
    static const int16_t cockpit[][2] = {{22,97},{34,94},{37,97},{26,99}};
    px_text_c(120, 10, "FM-1 CUSTOM FIRMWARE", 1, C_DIM);
    px_poly(body, 13, C_TX);
    px_poly(fin, 4, C_AC);
    px_poly(stab, 4, RGB(0xCF, 0xCF, 0xCF));
    px_poly(wing, 4, RGB(0xD8, 0xD8, 0xD8));
    px_rect(112, 116, 16, 8, RGB(0x9A, 0x9A, 0x9A)); px_rect(110, 117, 2, 6, C_AC);
    px_rect(134, 120, 14, 7, RGB(0x9A, 0x9A, 0x9A)); px_rect(132, 121, 2, 5, C_AC);
    px_poly(cockpit, 4, C_BLACK);
    for (int x = 50; x < 186; x += 6) px_rect(x, 99, 2, 2, C_AC);
    for (int x = 62; x < 98; x += 5) px_rect(x, 77, 2, 2, C_BLACK);
    px_rect(0, 140, 240, 1, C_GD);
    px_text(47, 148, "DX", 5, C_TX);
    px_text(107, 148, "747", 5, C_AC);
    px_text_c(120, 194, "8-PART MULTITIMBRAL DX7 MODULE", 1, C_DIM);
    px_text_c(120, 218, "DX747 PREVIEW", 1, C_LN);
}

/* ---- setup: the brief overview, one row per channel ---------------------- */

static int lane_x(unsigned note) { unsigned n = note < 24 ? 24 : note > 120 ? 120 : note; return 34 + (int)(n - 24) * 2; }

/* Lanes that do not overlap share a band row, overlapping ones stack. */
static void draw_lanes(unsigned k, int y, int h)
{
    uint8_t list[DX_ENGINES], rowend[DX_ENGINES], rowof[DX_ENGINES];
    unsigned n = engines_on(k, list), rows = 0;
    /* insertion sort by low note */
    for (unsigned i = 1; i < n; ++i)
        for (unsigned j = i; j > 0 && fr.cfg.e[list[j]].lo < fr.cfg.e[list[j - 1]].lo; --j) { uint8_t t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
    for (unsigned i = 0; i < n; ++i) {
        const struct dx_engine_cfg *e = &fr.cfg.e[list[i]];
        unsigned r = 0;
        while (r < rows && e->lo <= rowend[r]) ++r;
        if (r == rows) ++rows;
        rowend[r] = e->hi; rowof[i] = (uint8_t)r;
    }
    if (!rows) return;
    int bh = h / (int)rows; if (bh > 12) bh = 12;
    int top = y + (h - (int)rows * bh) / 2;
    for (unsigned i = 0; i < n; ++i) {
        const struct dx_engine_cfg *e = &fr.cfg.e[list[i]];
        int x0 = lane_x(e->lo), x1 = lane_x(e->hi + 1U), by = top + (int)rowof[i] * bh, bhh = rows > 1 ? bh - 1 : bh;
        px_rect(x0, by, x1 - x0 > 2 ? x1 - x0 : 2, bhh, engine_color(list[i]));
        if (bhh >= 7 && x1 - x0 >= 10) { char d[2] = {(char)('1' + list[i]), 0}; px_text(x0 + 2, by + (bhh - 7) / 2, d, 1, C_BLACK); }
        if (e->shift) px_rect(x1 - 3 > x0 ? x1 - 3 : x0, by, 3, bhh, C_AM);
    }
}
static void draw_setup(void)
{
    /* Seven rows. Channels with engines scroll around the selected channel;
     * channels that are sending with no engine (a keyboard that was just
     * plugged in) and the selected channel are pinned below them, at most two,
     * so a new keyboard is always on screen. */
    uint8_t routed_ch[16], loose_ch[16], rl[7];
    unsigned nr = 0, nu = 0, nvl = 0, nrows = 0;
    uint8_t tmp[DX_ENGINES];
    for (unsigned k = 0; k < 16; ++k) {
        if (engines_on(k, tmp)) routed_ch[nr++] = (uint8_t)k;
        else if (k == ui.channel || ((fr.active >> k) & 1U)) loose_ch[nu++] = (uint8_t)k;
    }
    uint8_t shown_loose[2];
    for (unsigned i = 0; i < nu && nvl < 2; ++i) if (loose_ch[i] == ui.channel) shown_loose[nvl++] = loose_ch[i];
    for (unsigned i = 0; i < nu && nvl < 2; ++i) if (loose_ch[i] != ui.channel) shown_loose[nvl++] = loose_ch[i];
    unsigned slots = 7U - nvl, sel_at = 0, first = 0;
    for (unsigned i = 0; i < nr; ++i) if (routed_ch[i] == ui.channel) sel_at = i;
    if (sel_at >= slots) first = sel_at - slots + 1U;
    for (unsigned i = 0; i < slots && first + i < nr; ++i) rl[nrows++] = routed_ch[first + i];
    for (unsigned i = 0; i < nvl; ++i) rl[nrows++] = shown_loose[i];
    draw_head();
    draw_title("SETUP", "1/2  HOME FOR ENGINES");
    static const uint8_t marks[4] = {36, 60, 84, 108};
    for (unsigned i = 0; i < 4; ++i) { put_note(tb, marks[i]); px_text_c(lane_x(marks[i]), 45, tb, 1, C_DIM); }
    for (unsigned i = 0; i < nrows; ++i) {
        unsigned k = rl[i];
        int y = 53 + 20 * (int)i;
        unsigned routed = engines_on(k, tmp) != 0, act = (fr.active >> k) & 1U;
        px_frame(4, y, 26, 18, ui.channel == k ? C_TX : C_LN);
        put_u(tb, k + 1U); px_text_c(17, y + 2, tb, 2, C_TX);
        if (act) px_rect(25, y + 2, 4, 4, C_AM);
        px_rect(34, y, 192, 18, RGB(0x10, 0x10, 0x10));
        for (int n = 24; n <= 120; n += 12) px_rect(lane_x((unsigned)n), y, 1, 18, RGB(0x26, 0x26, 0x26));
        if (!routed) {
            px_dotted(34, y, 192, 18, act ? C_AM : C_LN);
            if (act) px_text_c(130, y + 5, "ACTIVE, NOT ROUTED YET", 1, C_AM);
            else px_text_c(130, y + 5, "NO ENGINE ON THIS CHANNEL", 1, C_DIM);
        }
        else draw_lanes(k, y, 18);
        px_rect(230, y + 6, 5, 5, act ? C_TX : RGB(0x2A, 0x2A, 0x2A));
    }
    if (nr + nu > nrows) { char *o = put_s(tb, "+"); o = put_u(o, nr + nu - nrows); put_s(o, " MORE"); px_text(4, 45, tb, 1, C_DIM); }
    for (unsigned e = 0; e < DX_ENGINES; ++e) chip(e, 4 + 29 * (int)e, 196, 26, 14, e == ui.engine);
    struct cell c[16];
    memset(c, 0, sizeof c);
    for (unsigned k = 0; k < 16; ++k) {
        cell_num(&c[k], k);
        if (engines_on(k, tmp)) c[k].fill = engine_color(tmp[0]);
        c[k].sel = k == ui.channel;
    }
    strip(214, c);
    for (unsigned k = 0; k < 16; ++k) if ((fr.active >> k) & 1U) px_frame(8 + 14 * (int)k - 1, 213, 14, 14, C_AM);
    draw_hint("KEY = OPEN CHANNEL  HOME = ENGINE LIST");
}

/* ---- engines: the same setup per engine ---------------------------------- */

static void draw_engines(void)
{
    draw_head();
    draw_title("ENGINES", "2/2  HOME FOR SETUP");
    px_text(24, 45, "ENGINE", 1, C_DIM); px_text(84, 45, "CH", 1, C_DIM); px_text(132, 45, "PLAYS", 1, C_DIM); px_text(180, 45, "SHIFT", 1, C_DIM);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        int y = 54 + 19 * (int)e;
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        char name[11];
        chip(e, 4, y, 16, 16, e == ui.engine);
        trim_name(name, fr.e[e].name, 10);
        px_text(24, y + 4, name, 1, c->mute ? C_DIM : C_TX);
        put_channels(tb, c->rx); px_text(84, y + 4, tb, 1, c->rx ? C_TX : C_DIM);
        put_range(tb, c->lo, c->hi); px_text(132, y + 4, tb, 1, c->lo == 0 && c->hi == 127 ? C_DIM : C_AM);
        if (c->shift) put_shift(tb, c->shift); else put_s(tb, "-");
        px_text(180, y + 4, tb, 1, c->shift ? C_AM : C_DIM);
        bar(212, y + 4, 24, 8, c->mute ? C_MU : EC[e], fr.e[e].volume, 127);
    }
    struct cell c[16];
    memset(c, 0, sizeof c);
    for (unsigned i = 0; i < 8; ++i) { c[i].t[0] = 'E'; c[i].t[1] = (char)('1' + i); c[i].fill = engine_color(i); c[i].sel = i == ui.engine; }
    strip(214, c);
    draw_hint("KEY 1-8 = OPEN ENGINE  HOME = SETUP");
}

/* ---- channel detail: lanes over a keyboard ------------------------------ */

static int key_x(unsigned note)
{
    unsigned n = note < 24 ? 24 : note > 119 ? 119 : note;
    return 8 + (int)(((n - 24U) * 28U + 6U) / 12U);
}
static void draw_keyboard(int y, int h_white, int h_black)
{
    for (int o = 0; o < 8; ++o) {
        for (int k = 0; k < 7; ++k) px_rect(8 + o * 28 + k * 4, y, 3, h_white, C_TX);
        static const uint8_t black[5] = {2, 6, 14, 18, 22};
        for (unsigned b = 0; b < 5; ++b) px_rect(8 + o * 28 + black[b], y, 3, h_black, C_BG);
        put_note(tb, 24U + (unsigned)o * 12U);
        px_text(8 + o * 28, y + h_white + 3, tb, 1, C_DIM);
    }
}
static void draw_channel(void)
{
    uint8_t list[DX_ENGINES];
    unsigned n = engines_on(ui.channel, list), lane = ui.lane < n ? ui.lane : (n ? n - 1U : 0U);
    struct cell c[16];
    memset(c, 0, sizeof c);
    for (unsigned i = 0; i < 8; ++i) {
        c[i].t[0] = 'E'; c[i].t[1] = (char)('1' + i);
        if ((fr.cfg.e[i].rx >> ui.channel) & 1U) c[i].fill = engine_color(i);
        if (n && list[lane] == i) c[i].sel = 1;
    }
    draw_head();
    char *o = put_s(tb, "CH "); put_u(o, ui.channel + 1U);
    px_text(4, 29, tb, 2, C_AC);
    if (n) {
        unsigned layers = max_layers(ui.channel);
        o = put_u(tb, n); o = put_s(o, n == 1 ? " ENGINE  MAX " : " ENGINES  MAX ");
        o = put_u(o, layers); put_s(o, layers == 1 ? " LAYER" : " LAYERS");
    }
    else put_s(tb, "NOTHING ROUTED");
    px_text_r(236, 33, tb, 1, C_DIM);
    if (!n) {
        px_text(4, 64, "NO ENGINE PLAYS", 2, C_AM); px_text(4, 82, "ON THIS CHANNEL", 2, C_AM);
        px_text(4, 116, "PRESS WHITE KEYS", 2, C_TX); px_text(4, 134, "1-8 TO ADD ENGINES", 2, C_TX);
        px_text(4, 160, "ONE KEY = ONE ENGINE, FULL RANGE.", 1, C_DIM); px_text(4, 171, "TAP AGAIN TO REMOVE IT.", 1, C_DIM);
        strip(181, c);
        tile(0, "LANE", "-", C_TX); tile(1, "LOW", "-", C_TX); tile(2, "HIGH", "-", C_TX); tile(3, "SHIFT", "-", C_TX);
        draw_hint("KEYS 1-8 = ADD / REMOVE ENGINE HERE");
        return;
    }
    unsigned first = lane >= 3 ? lane - 3U : 0U, vis = n < 4 ? n : 4U;
    if (first + vis > n) first = n - vis;
    for (unsigned k = 0; k < vis; ++k) {
        unsigned idx = first + k, e = list[idx];
        const struct dx_engine_cfg *ec = &fr.cfg.e[e];
        int y = 47 + 11 * (int)k, x0 = key_x(ec->lo), x1 = key_x((ec->hi < 119 ? ec->hi : 119U) + 1U);
        px_rect(x0, y, x1 - x0 > 2 ? x1 - x0 : 2, 9, engine_color(e));
        if (x1 - x0 >= 10) { char d[2] = {(char)('1' + e), 0}; px_text(x0 + 2, y + 1, d, 1, C_BLACK); }
        if (ec->shift) px_rect(x1 - 3 > x0 ? x1 - 3 : x0, y, 3, 9, C_AM);
        if (idx == lane) px_frame(x0 - 1, y - 1, (x1 - x0 > 4 ? x1 - x0 : 4) + 2, 11, C_TX);
    }
    draw_keyboard(94, 14, 8);
    const struct dx_engine_cfg *se = &fr.cfg.e[list[lane]];
    unsigned e = list[lane];
    int l2 = (int)se->lo + se->shift, h2 = (int)se->hi + se->shift;
    l2 = l2 < 0 ? 0 : l2 > 127 ? 127 : l2; h2 = h2 < 0 ? 0 : h2 > 127 ? 127 : h2;
    if (se->shift) {
        int a = key_x((unsigned)l2), b = key_x((h2 < 119 ? (unsigned)h2 : 119U) + 1U);
        px_frame(a, 121, b - a > 3 ? b - a : 3, 6, C_AM);
    }
    char name[11];
    trim_name(name, fr.e[e].name, 10);
    o = put_s(tb, "E"); o = put_u(o, e + 1U); o = put_s(o, " "); put_s(o, name);
    px_text(4, 130, tb, 2, se->mute ? C_DIM : EC[e]);
    o = put_range(tb, se->lo, se->hi);
    if (se->shift) { o = put_s(o, " > "); o = put_note(o, (unsigned)l2); *o++ = '-'; put_note(o, (unsigned)h2); }
    px_text(4, 148, tb, 2, C_TX);
    char also[40]; unsigned a = 0;
    char *ao = put_s(also, "LAYERED WITH ENGINE");
    for (unsigned i = 0; i < n; ++i) if (i != lane) { const struct dx_engine_cfg *x = &fr.cfg.e[list[i]]; if (x->lo <= se->hi && x->hi >= se->lo) { *ao++ = ' '; *ao++ = (char)('1' + list[i]); ++a; } }
    *ao = 0;
    px_text(4, 168, a ? also : "NO OTHER ENGINE ON THESE KEYS", 1, a ? C_AM : C_DIM);
    strip(181, c);
    o = put_u(tb, lane + 1U); *o++ = '/'; put_u(o, n);
    tile(0, "LANE", tb, C_TX);
    put_note(tb, se->lo); tile(1, "LOW", tb, EC[e]);
    put_note(tb, se->hi); tile(2, "HIGH", tb, EC[e]);
    o = put_shift(tb, se->shift); put_s(o, se->shift % 12 ? "" : " OCT");
    tile(3, "SHIFT", tb, C_AM);
    draw_hint(ui.learn == 1 ? "LEARN: PLAY THE LOWEST KEY" : ui.learn == 2 ? "LEARN: PLAY THE HIGHEST KEY" : "KEYS 1-8 = ADD / REMOVE ENGINE HERE");
}

/* ---- engine pages --------------------------------------------------------- */

static void draw_ebar(void) { for (unsigned e = 0; e < DX_ENGINES; ++e) chip(e, 4 + 29 * (int)e, 28, 26, 16, e == ui.engine); }
static void draw_pills(unsigned page)
{
    static const char *const names[3] = {"SOUND", "PLAYS", "SENDS"};
    int x = 4;
    for (unsigned i = 0; i < 3; ++i) {
        int w = (int)text_w(names[i], 1) + 10;
        if (i == page) { px_rect(x, 52, w, 14, C_AC); px_text(x + 5, 56, names[i], 1, C_BLACK); }
        else { px_frame(x, 52, w, 14, C_LN); px_text(x + 5, 56, names[i], 1, C_DIM); }
        x += w + 6;
    }
    px_text_r(236, 56, "EDIT = NEXT", 1, C_DIM);
}
static void draw_esound(void)
{
    unsigned e = ui.engine;
    const struct dx_engine_cfg *c = &fr.cfg.e[e];
    uint16_t col = engine_color(e);
    char name[11];
    draw_head(); draw_ebar(); draw_pills(0);
    trim_name(name, fr.e[e].name, 10);
    px_text(4, 72, name, 3, col);
    char *o = put_s(tb, "PROGRAM "); put_u(o, fr.e[e].program + 1U);
    px_text(4, 98, tb, 1, C_DIM);
    px_text(4, 115, "LEVEL", 1, C_DIM); bar(64, 112, 150, 12, col, fr.e[e].volume, 127); put_u(tb, fr.e[e].volume); px_text_r(236, 115, tb, 1, C_TX);
    px_text(4, 137, "PAN", 1, C_DIM); px_frame(64, 134, 150, 12, C_LN); px_rect(139, 134, 1, 12, C_DIM);
    px_rect(139 + fr.live.pan[e] * 7 / 5 - 2, 136, 5, 8, col);
    px_text(4, 159, "BRIGHT", 1, C_DIM); px_frame(64, 156, 150, 12, C_LN); px_rect(139, 156, 1, 12, C_DIM);
    { int b = fr.live.bright[e] * 7 / 5; if (b >= 0) px_rect(140, 158, b, 8, C_AM); else px_rect(139 + b, 158, -b, 8, C_AM); }
    o = put_s(tb, "PLAYS: CH "); o = put_channels(o, c->rx); o = put_s(o, "  "); o = put_range(o, c->lo, c->hi);
    if (c->shift) { o = put_s(o, "  "); put_shift(o, c->shift); }
    px_text(4, 180, tb, 1, C_AC);
    o = put_u(tb, fr.e[e].program + 1U); tile(0, "PATCH", tb, col);
    tile_u(1, "LEVEL", fr.e[e].volume, C_TX);
    put_pan(tb, fr.live.pan[e]); tile(2, "PAN", tb, C_TX);
    o = tb; if (fr.live.bright[e] > 0) *o++ = '+'; put_i(o, fr.live.bright[e]); tile(3, "BRIGHT", tb, C_AM);
    draw_hint("KEYS PLAY THIS ENGINE FOR TESTING");
}
static void draw_eplays(void)
{
    unsigned e = ui.engine;
    const struct dx_engine_cfg *c = &fr.cfg.e[e];
    uint16_t col = engine_color(e);
    int l2 = (int)c->lo + c->shift, h2 = (int)c->hi + c->shift;
    l2 = l2 < 0 ? 0 : l2 > 127 ? 127 : l2; h2 = h2 < 0 ? 0 : h2 > 127 ? 127 : h2;
    draw_head(); draw_ebar(); draw_pills(1);
    px_text(4, 72, "LISTENS ON CHANNELS", 1, C_DIM); put_channels(tb, c->rx); px_text(4, 82, tb, 2, col);
    px_text(4, 102, "PLAYS NOTES", 1, C_DIM); put_range(tb, c->lo, c->hi); px_text(4, 112, tb, 2, C_TX);
    px_text(4, 132, "SOUNDS AT", 1, C_DIM);
    char *o = put_note(tb, (unsigned)l2); *o++ = '-'; o = put_note(o, (unsigned)h2);
    if (c->shift) { o = put_s(o, "  ("); o = put_shift(o, c->shift); put_s(o, " OCT)"); }
    px_text(4, 142, tb, 2, c->shift ? C_AM : C_TX);
    char with[40]; unsigned shared = 0;
    char *wo = put_s(with, "LAYERED WITH ENGINE");
    for (unsigned i = 0; i < DX_ENGINES; ++i) if (i != e && (fr.cfg.e[i].rx & c->rx) && fr.cfg.e[i].lo <= c->hi && fr.cfg.e[i].hi >= c->lo) { *wo++ = ' '; *wo++ = (char)('1' + i); ++shared; }
    *wo = 0;
    px_text(4, 166, shared ? with : "NOT LAYERED WITH ANOTHER ENGINE", 1, shared ? C_AM : C_DIM);
    struct cell cells[16];
    memset(cells, 0, sizeof cells);
    for (unsigned k = 0; k < 16; ++k) { cell_num(&cells[k], k); if ((c->rx >> k) & 1U) cells[k].fill = col; }
    strip(181, cells);
    put_note(tb, c->lo); tile(0, "LOW", tb, col);
    put_note(tb, c->hi); tile(1, "HIGH", tb, col);
    o = put_shift(tb, c->shift); put_s(o, c->shift % 12 ? "" : " OCT"); tile(2, "SHIFT", tb, C_AM);
    tile(3, "FOLD", c->fold == DX_FOLD_WRAP ? "WRAP" : c->fold == DX_FOLD_CLIP ? "CLIP" : "DROP", C_TX);
    draw_hint("WHITE KEYS 1-16 = TOGGLE CHANNEL");
}
static void draw_esends(void)
{
    unsigned e = ui.engine;
    const struct dx_engine_cfg *c = &fr.cfg.e[e];
    draw_head(); draw_ebar(); draw_pills(2);
    char *o = put_u(tb, fr.e[e].rev); put_s(o, " / 15"); px_text(4, 72, "REVERB SEND", 1, C_DIM); px_text(4, 82, tb, 2, C_TX);
    o = put_u(tb, fr.e[e].dly); put_s(o, " / 15"); px_text(4, 99, "DELAY SEND", 1, C_DIM); px_text(4, 109, tb, 2, C_TX);
    put_u(tb, c->poly); px_text(4, 126, "MAX VOICES", 1, C_DIM); px_text(4, 136, tb, 2, C_TX);
    if (c->ctl) put_u(tb, c->ctl); else put_s(tb, "OFF");
    px_text(4, 153, "CONTROL CHANNEL", 1, C_DIM); px_text(4, 163, tb, 2, C_AC);
    bar(100, 84, 136, 12, RGB(0x8F, 0xB4, 0xFF), fr.e[e].rev, 15);
    bar(100, 111, 136, 12, RGB(0xFF, 0xB8, 0x6B), fr.e[e].dly, 15);
    for (unsigned v = 0; v < 8; ++v) { /* one square per voice this engine may use */
        if (v < c->poly) px_rect(100 + 17 * (int)v, 138, 15, 12, engine_color(e)); else px_frame(100 + 17 * (int)v, 138, 15, 12, C_LN);
    }
    px_text(100, 164, c->ctl ? "DX7 EDITS ARRIVE" : "OFF = CC ONLY FROM", 1, C_DIM);
    px_text(100, 174, c->ctl ? "ON THIS CHANNEL" : "THE PLAY CHANNELS", 1, C_DIM);
    tile_u(0, "REV", fr.e[e].rev, C_TX); tile_u(1, "DLY", fr.e[e].dly, C_TX); tile_u(2, "POLY", c->poly, C_TX);
    if (c->ctl) put_u(tb, c->ctl); else put_s(tb, "OFF");
    tile(3, "CTRL CH", tb, C_AC);
    draw_hint("CTRL CH = OWN CHANNEL FOR DX7 EDITS");
}

/* ---- midi in ---------------------------------------------------------------- */

static void log_text(char *o, const struct dx_log *l)
{
    unsigned type = l->status & 0xf0U;
    if (type == 0x90 && l->d2) { o = put_s(o, "NOTE "); o = put_note(o, l->d1); *o++ = ' '; put_u(o, l->d2); }
    else if (type == 0x80 || type == 0x90) { o = put_s(o, "OFF  "); put_note(o, l->d1); }
    else if (type == 0xb0) { o = put_s(o, "CC "); o = put_u(o, l->d1); o = put_s(o, " = "); put_u(o, l->d2); }
    else if (type == 0xc0) { o = put_s(o, "PROGRAM "); put_u(o, l->d1 + 1U); }
    else put_s(o, "OTHER");
}
static void draw_mon(void)
{
    static const char *const src[4] = {"DIN", "USB", "BLE", "KEY"};
    draw_head();
    draw_title("MIDI IN", "WHO IS SENDING?");
    for (unsigned i = 0; i < (fr.nlog < 7 ? fr.nlog : 7U); ++i) {
        int y = 48 + 20 * (int)i;
        const struct dx_log *l = &fr.log[i];
        px_text(4, y, src[l->src & 3U], 2, C_AC);
        put_2(tb, l->ch + 1U); px_text(44, y, tb, 2, C_TX);
        log_text(tb, l); px_text(80, y, tb, 2, i == 0 ? C_TX : C_DIM);
    }
    if (!fr.nlog) px_text(4, 60, "NO MIDI YET", 2, C_DIM);
    struct cell c[16];
    memset(c, 0, sizeof c);
    for (unsigned k = 0; k < 16; ++k) { cell_num(&c[k], k); if ((fr.active >> k) & 1U) c[k].fill = C_AM; }
    strip(196, c);
    px_text(4, 212, "AMBER = SENDING NOW", 1, C_DIM);
    draw_hint("PLAY EACH KEYBOARD TO FIND ITS CH");
}

/* ---- mix, fx, master ------------------------------------------------------- */

static void draw_mix(void)
{
    draw_head();
    draw_title("MIX", "SELECT KEY SHOWS THIS");
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        int x = 4 + 29 * (int)e;
        uint16_t col = engine_color(e);
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        int fill = 98 * (int)fr.e[e].volume / 127, meter = c->mute ? 0 : 98 * (int)fr.e[e].level / 127;
        chip(e, x, 46, 26, 16, e == ui.engine);
        px_frame(x + 3, 66, 12, 100, C_LN); px_rect(x + 4, 166 - fill, 10, fill, col);
        px_frame(x + 18, 66, 6, 100, C_LN); px_rect(x + 19, 165 - meter, 4, meter, fr.e[e].level >= 100 ? C_AM : C_GR);
        px_rect(x + 3, 170, 22, 1, C_LN); px_rect(x + 14 + fr.live.pan[e] / 5, 169, 3, 3, col);
        px_frame(x + 2, 176, 11, 11, c->mute ? C_RD : C_LN); px_text_c(x + 7, 178, "M", 1, c->mute ? C_RD : C_DIM);
        px_frame(x + 14, 176, 11, 11, C_LN); px_text_c(x + 19, 178, "S", 1, C_DIM);
    }
    px_text(4, 206, "FADER + METER PER ENGINE", 1, C_DIM);
    draw_hint("WHITE KEYS 1-8 MUTE  9-16 SOLO");
}
static void draw_fx(void)
{
    draw_head();
    draw_title("FX", "SHARED REVERB + DELAY");
    px_text(40, 46, "REV SEND", 1, C_DIM); px_text(136, 46, "DLY SEND", 1, C_DIM);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        int y = 56 + 17 * (int)e;
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        chip(e, 4, y, 16, 14, e == ui.engine);
        bar(40, y + 2, 80, 10, c->mute ? C_MU : RGB(0x8F, 0xB4, 0xFF), fr.e[e].rev, 15);
        put_u(tb, fr.e[e].rev); px_text(128, y + 3, tb, 1, C_TX);
        bar(136, y + 2, 80, 10, c->mute ? C_MU : RGB(0xFF, 0xB8, 0x6B), fr.e[e].dly, 15);
        put_u(tb, fr.e[e].dly); px_text(224, y + 3, tb, 1, C_TX);
    }
    uint8_t v[4];
    groove_effects_get(v);
    tile_u(0, "DECAY", v[0], C_TX);
    if (v[1] >= GROOVE_DELAY_SYNC && v[1] <= GROOVE_DELAY_LAST) {
        static const char *const sync[9] = {"1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/8D", "1/4"};
        tile(1, "DLY TIME", sync[v[1] - GROOVE_DELAY_SYNC], C_TX);
    } else { unsigned ms = v[1] < 128 ? v[1] * 3U : v[1] <= 201 ? 360U + (v[1] - 137U) * 10U : 1020U + (v[1] - 202U) * 20U; char *o = put_u(tb, ms); put_s(o, "MS"); tile(1, "DLY TIME", tb, C_TX); }
    tile_u(2, "FEEDBK", v[2], C_TX); tile_u(3, "RETURN", v[3], C_TX);
    draw_hint("FX AGAIN = MASTER FILTER");
}
/* Position of a frequency on the 20 Hz to 20 kHz log axis, 0..223. */
static unsigned log2_q8(unsigned v)
{
    static const uint8_t frac[17] = {0, 22, 43, 63, 82, 100, 117, 134, 149, 164, 178, 192, 205, 218, 230, 243, 255};
    unsigned bits = 0, t = v ? v : 1U;
    while (t >>= 1) ++bits;
    unsigned mant = bits >= 4 ? (v >> (bits - 4)) & 15U : (v << (4 - bits)) & 15U;
    return bits * 256U + frac[mant];
}
static int axis_x(unsigned hz)
{
    unsigned lo = log2_q8(20), hi = log2_q8(20000), v = log2_q8(hz < 20 ? 20U : hz > 20000 ? 20000U : hz);
    return 8 + (int)((v - lo) * 224U / (hi - lo));
}
static void draw_master(void)
{
    const struct dx_live *l = &fr.live;
    draw_head();
    draw_title("MASTER", "FILTER + USB TAP");
    px_frame(8, 48, 224, 80, C_LN);
    static const unsigned grid[3] = {100, 1000, 10000};
    for (unsigned i = 0; i < 3; ++i) px_rect(axis_x(grid[i]), 49, 1, 78, C_GD);
    px_rect(9, 68, 222, 1, C_GD);
    int xc = axis_x(l->filter_hz ? l->filter_hz : 1000U);
    int bump = (int)l->filter_res * 18 / 127;
    int py = -1, ppx = 0;
    for (int x = 9; x < 231; ++x) {
        int d = x - xc, db = 0, ad = d < 0 ? -d : d;
        if (l->filter_type == DX_FILTER_LP) db = d > 0 ? -d * 534 / 1000 : 0;
        else if (l->filter_type == DX_FILTER_HP) db = d < 0 ? d * 534 / 1000 : 0;
        else if (l->filter_type == DX_FILTER_BP) db = -ad * 267 / 1000;
        if (l->filter_type != DX_FILTER_OFF && ad < 10) db += bump * (10 - ad) / 10;
        int y = 68 - db * 5 / 3;
        y = y < 50 ? 50 : y > 126 ? 126 : y;
        if (py >= 0) px_line(ppx, py, x, y, C_AC);
        ppx = x; py = y;
    }
    if (l->filter_type != DX_FILTER_OFF) for (int y = 50; y < 127; y += 4) px_rect(xc, y, 1, 2, C_AM);
    px_text_c(axis_x(100), 131, "100", 1, C_DIM); px_text_c(axis_x(1000), 131, "1K", 1, C_DIM); px_text_c(axis_x(10000), 131, "10K", 1, C_DIM);
    px_text(8, 146, "USB OUT TAP", 1, C_DIM); px_text(8, 158, "AFTER FILTER, BEFORE VOLUME", 1, C_GR);
    px_text(8, 174, "BT AND USB IN", 1, C_DIM); px_text(8, 186, "MIXED AFTER VOLUME", 1, C_TX);
    static const char *const types[4] = {"OFF", "LP", "HP", "BP"};
    tile(0, "TYPE", types[l->filter_type & 3U], C_TX);
    unsigned hz = l->filter_hz ? l->filter_hz : 1000U;
    if (hz >= 1000) { char *o = put_u(tb, hz / 1000U); *o++ = '.'; *o++ = (char)('0' + (hz / 100U) % 10U); *o++ = 'K'; *o = 0; } else put_u(tb, hz);
    tile(1, "CUTOFF", tb, C_AM);
    tile_u(2, "RES", l->filter_res, C_TX);
    tile(3, "USB OUT", "PRE VOL", C_GR);
    draw_hint("FX AGAIN = BACK TO FX PAGE");
}

/* ---- scenes ------------------------------------------------------------------ */

static void draw_scenes(void)
{
    unsigned bank = ui.scene / DX_SCENES_PER_BANK;
    draw_head();
    char *o = put_s(tb, "BANK "); *o++ = (char)('A' + bank); o = put_s(o, "  "); o = put_u(o, bank * 16U + 1U); *o++ = '-'; put_u(o, bank * 16U + 16U);
    draw_title("SCENES", tb);
    for (unsigned s = 0; s < 16; ++s) {
        unsigned idx = bank * 16U + s;
        int x = 4 + 58 * (int)(s % 4), y = 48 + 38 * (int)(s / 4);
        unsigned has = scene_exists(idx), cur = idx == fr.current_scene, sel = idx == ui.scene;
        if (cur) { px_rect(x, y, 56, 34, RGB(0x00, 0x3C, 0x44)); px_frame(x, y, 56, 34, C_AC); px_frame(x + 1, y + 1, 54, 32, C_AC); }
        else px_frame(x, y, 56, 34, sel ? C_TX : has ? C_LN : RGB(0x3C, 0x3C, 0x3C));
        if (sel && cur) px_frame(x - 1, y - 1, 58, 36, C_TX);
        put_2(tb, idx + 1U); px_text(x + 4, y + 4, tb, 2, has ? (cur ? C_AC : C_TX) : RGB(0x6A, 0x6A, 0x6A));
        if (has) {
            char name[13]; trim_name(name, fr.scene_name[idx], 12); name[8] = 0;
            px_text(x + 4, y + 23, name, 1, C_TX);
            for (unsigned j = 0; j < 8; ++j) px_rect(x + 30 + (int)(j % 4) * 6, y + 5 + (int)(j / 4) * 6, 4, 4, (fr.scene_engines[idx] >> j) & 1U ? EC[j] : RGB(0x2C, 0x2C, 0x2C));
        } else px_text(x + 4, y + 23, "--", 1, RGB(0x6A, 0x6A, 0x6A));
    }
    if (ui.mode == M_NAME) {
        px_rect(10, 90, 220, 60, C_BLACK); px_frame(10, 90, 220, 60, C_AC);
        o = put_s(tb, "SAVE AS SCENE "); put_u(o, ui.scene + 1U);
        px_text(18, 96, tb, 1, C_DIM);
        for (unsigned i = 0; i < 12; ++i) {
            char ch[2] = {ui.name[i], 0};
            px_text(18 + 17 * (int)i, 112, ch, 2, i == ui.name_pos ? C_BLACK : C_TX);
            if (i == ui.name_pos) { px_rect(17 + 17 * (int)i, 110, 14, 18, C_AM); px_text(18 + 17 * (int)i, 112, ch, 2, C_BLACK); }
        }
        px_text(18, 136, "K1 CURSOR  K2 LETTER  SAVE = STORE", 1, C_DIM);
    } else if (ui.mode == M_CLAIM) {
        px_rect(10, 78, 220, 84, C_BLACK); px_frame(10, 78, 220, 84, C_AM);
        px_text(18, 84, "FIRST SAVE: FLASH SETUP", 1, C_AM);
        px_text(18, 100, "SCENES NEED THE FLASH THAT HOLDS", 1, C_TX);
        px_text(18, 111, "THE STOCK SAMPLE BANK. IT WILL BE", 1, C_TX);
        px_text(18, 122, "ERASED. BACK IT UP FIRST.", 1, C_TX);
        px_text(18, 142, "SAVE = ERASE AND CONTINUE", 1, C_AM);
        px_text(18, 152, "HOME = CANCEL", 1, C_DIM);
    }
    draw_hint(ui.mode == M_BROWSE ? "WHITE KEYS = RECALL  SAVE = STORE" : "HOME = CANCEL");
}

/* ---- system --------------------------------------------------------------------- */

static void draw_sys(void)
{
    static const char *const labels[7] = {"BLUETOOTH MIDI", "BLUETOOTH AUDIO IN", "USB AUDIO OUT", "BUTTON LIGHT", "KEYBOARD CHANNEL", "CPU", "SCENE FLASH"};
    const struct dx_settings *s = &fr.settings;
    draw_head();
    draw_title("SYSTEM", "GLO");
    for (unsigned i = 0; i < 7; ++i) {
        int y = 47 + 26 * (int)i;
        uint16_t col = C_TX;
        const char *val = tb;
        char *o = tb;
        switch (i) {
        case 0: o = put_s(tb, s->role ? "CENTRAL" : "PERIPHERAL"); col = C_GR; break;
        case 1: val = (s->radio_off & GROOVE_RADIO_AUDIO) ? "OFF" : "ON"; col = (s->radio_off & GROOVE_RADIO_AUDIO) ? C_DIM : C_GR; break;
        case 2: val = "PRE-VOLUME"; col = C_GR; break;
        case 3: o = put_u(tb, s->backlight); put_s(o, " / 8"); col = C_AM; break;
        case 4: o = put_s(tb, "CH "); put_u(o, s->kbd_ch + 1U); break;
        case 5: o = put_u(tb, fr.cpu); put_s(o, "%"); break;
        default: val = fr.storage ? (fr.storage > 0 ? "NEEDS SETUP" : "ERROR") : "READY"; col = fr.storage ? C_AM : C_GR; break;
        }
        if (ui.sys_row == i) { px_rect(0, y, 2, 24, C_AC); }
        px_text(4, y, labels[i], 1, ui.sys_row == i ? C_AC : C_DIM);
        px_text(4, y + 9, val, 2, col);
    }
    for (unsigned b = 0; b < 8; ++b) px_rect(150 + (int)b * 10, 47 + 26 * 3 + 9, 8, 12, b < fr.settings.backlight ? C_AM : RGB(0x3C, 0x3C, 0x3C));
    px_frame(150, 47 + 26 * 5 + 9, 80, 12, C_LN);
    px_rect(151, 47 + 26 * 5 + 10, (int)(fr.cpu * 78U / 100U), 10, fr.cpu >= 85 ? C_AM : C_GR);
    switch (ui.sys_row) {
    case 0: draw_hint("PHONE OR COMPUTER CONNECTS TO THIS UNIT"); break;
    case 2: draw_hint("NOT AFFECTED BY THE MASTER KNOB"); break;
    case 1: case 3: case 4: draw_hint("SELECT = ROW  K1 = CHANGE"); break;
    default: draw_hint("SELECT = ROW"); break;
    }
}

static void draw_screen(void)
{
    switch (ui.screen) {
    case S_BOOT: draw_boot(); break;
    case S_SETUP: draw_setup(); break;
    case S_ENGINES: draw_engines(); break;
    case S_CHANNEL: draw_channel(); break;
    case S_ESOUND: draw_esound(); break;
    case S_EPLAYS: draw_eplays(); break;
    case S_ESENDS: draw_esends(); break;
    case S_MON: draw_mon(); break;
    case S_MIX: draw_mix(); break;
    case S_FX: draw_fx(); break;
    case S_MASTER: draw_master(); break;
    case S_SCENES: draw_scenes(); break;
    default: draw_sys(); break;
    }
}
