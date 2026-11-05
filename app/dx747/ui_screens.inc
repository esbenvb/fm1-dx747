/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 screens (docs/design/PARTS-MODEL.md; the earlier engine-centred sketches are in docs/sketches). Every function draws
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
        name[10] = 0;                                  /* ten letters at double size end before the counters */
        px_text(30, 5, name, 2, C_TX);
    } else {
        px_text(4, 5, "--", 2, C_AC);
        px_text(30, 5, "NO SCENE", 2, C_DIM);
    }
    unsigned up = engines_up();
    char *o = put_s(tb, "ENGINES ");
    o = put_u(o, engines_in_use()); *o++ = '/'; put_u(o, up);
    px_text_r(236, 2, tb, 1, up != DX_ENGINES ? C_AM : C_DIM);   /* amber: the boot recipe could not start every engine (memory) */
    o = put_s(tb, "VOICES ");
    o = put_u(o, fr.voices); *o++ = '/'; put_u(o, fr.cfg.pool);
    px_text_r(236, 11, tb, 1, C_DIM);
    unsigned pool = fr.cfg.pool ? fr.cfg.pool : 1U, used = (fr.voices * 20U + pool - 1U) / pool;
    for (unsigned i = 0; i < 20; ++i)
        px_rect(157 + (int)i * 4, 20, 3, 4, i < used ? (fr.voices * 100U >= pool * 85U ? C_AM : C_GR) : RGB(0x2C, 0x2C, 0x2C));
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
/* A coloured chip with a one or two digit number in it. */
static void chip_n(unsigned number, int x, int y, int w, int h, uint16_t color, unsigned sel)
{
    char d[3];
    put_u(d, number);
    px_rect(x, y, w, h, color);
    if (h >= 16) px_text_c(x + w / 2, y + (h - 14) / 2, d, 2, C_BLACK);
    else { /* a 14 px glyph would touch the chip edges: a bold 7 px digit instead */
        int ty = y + (h - 7) / 2;
        px_text_c(x + w / 2, ty, d, 1, C_BLACK);
        px_text_c(x + w / 2 + 1, ty, d, 1, C_BLACK);
    }
    if (sel) px_frame(x - 2, y - 2, w + 4, h + 4, C_TX);
}
/* A part's chip: its MIDI channel. An empty slot is an outline with the slot number. */
static void part_chip(unsigned p, int x, int y, int w, int h, unsigned sel)
{
    const struct dx_part *pt = &fr.live.parts.part[p];
    if (pt->channel) { chip_n(pt->channel, x, y, w, h, part_color(p), sel); return; }
    char d[3];
    put_u(d, p + 1U);
    px_dotted(x, y, w, h, sel ? C_TX : C_LN);
    px_text_c(x + w / 2, y + (h - 7) / 2, d, 1, C_DIM);
}
/* An engine's chip: the channel of the part it is a layer of, or an outline when the engine is free. */
static void engine_chip(unsigned e, int x, int y, int w, int h, unsigned sel)
{
    unsigned owner = fr.live.parts.layer[e].owner;
    if (owner < DX_PARTS && fr.live.parts.part[owner].channel) { chip_n(fr.live.parts.part[owner].channel, x, y, w, h, engine_color(e), sel); return; }
    char d[2] = {(char)('1' + e), 0};
    px_frame(x, y, w, h, C_LN);
    px_text_c(x + w / 2, y + (h - 7) / 2, d, 1, C_MU);
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
    /* A 747 in side view, gear up, traced from a reference drawing at 0.23 scale: fuselage with the upper-deck hump and
     * the fin in one outline, a shaded belly, the stabiliser, the swept wing and two engines, then the windows. */
    static const int16_t body[][2] = {{8,99},{8,97},{13,93},{19,90},{25,86},{26,85},{30,85},{34,83},{72,83},{78,84},{96,86},{106,88},{178,88},{183,86},{195,76},{218,56},{231,56},{221,83},{220,87},{224,88},{231,88},{226,91},{223,94},{220,95},{211,97},{197,100},{181,104},{173,105},{164,107},{148,109},{128,109},{127,110},{67,110},{66,109},{40,109},{26,107},{21,106},{15,104},{11,102}};
    static const int16_t belly[][2] = {{223,94},{220,95},{211,97},{197,100},{181,104},{173,105},{164,107},{148,109},{128,109},{127,110},{67,110},{66,109},{40,109},{26,107},{21,106},{15,104},{11,102},{8,99},{8,98},{10,98},{14,100},{17,102},{26,104},{32,105},{40,106},{66,106},{67,107},{127,107},{128,106},{148,106},{164,104},{173,102},{181,101},{197,97},{220,92}};
    static const int16_t cap[][2] = {{218,56},{231,56},{229,62},{211,62}};
    static const int16_t stab[][2] = {{193,92},{197,90},{224,87},{231,88},{226,91},{222,94},{218,96},{201,99},{200,97}};
    static const int16_t wing[][2] = {{74,103},{80,102},{100,100},{125,99},{157,98},{125,102},{110,104},{101,105},{75,105}};
    static const int16_t wdark[][2] = {{157,98},{125,102},{110,104},{101,105},{101,103},{110,102},{125,100},{141,98}};
    static const int16_t cone1[][2] = {{103,106},{107,108},{103,110}};
    static const int16_t cone2[][2] = {{130,104},{134,106},{130,108}};
    static const int16_t door[] = {38, 68, 104, 137, 182};
    px_text_c(120, 10, "FM-1 CUSTOM FIRMWARE", 1, C_DIM);
    px_poly(body, sizeof body / sizeof *body, C_TX);
    px_poly(belly, sizeof belly / sizeof *belly, RGB(0xCC, 0xCC, 0xCC));
    px_poly(cap, sizeof cap / sizeof *cap, C_AC);
    px_poly(stab, sizeof stab / sizeof *stab, RGB(0x70, 0x70, 0x74));
    px_line(198, 90, 224, 88, RGB(0xA4, 0xA4, 0xA8));
    for (unsigned i = 0; i < sizeof door / sizeof *door; ++i) px_frame(door[i] - 1, 93, 3, 8, RGB(0xB0, 0xB0, 0xB4));
    px_frame(55, 86, 3, 6, RGB(0xB0, 0xB0, 0xB4));
    px_poly(wing, sizeof wing / sizeof *wing, RGB(0xC0, 0xC0, 0xC4));
    px_poly(wdark, sizeof wdark / sizeof *wdark, RGB(0x78, 0x78, 0x7C));
    px_rect(83, 104, 10, 8, RGB(0x8C, 0x8C, 0x90));
    px_rect(83, 104, 10, 1, RGB(0xD4, 0xD4, 0xD4));
    px_rect(83, 111, 10, 1, RGB(0x58, 0x58, 0x5C));
    px_rect(93, 106, 10, 4, RGB(0x66, 0x66, 0x6A));
    px_poly(cone1, 3, RGB(0xA8, 0xA8, 0xA8));
    px_rect(110, 102, 10, 8, RGB(0x8C, 0x8C, 0x90));
    px_rect(110, 102, 10, 1, RGB(0xD4, 0xD4, 0xD4));
    px_rect(110, 109, 10, 1, RGB(0x58, 0x58, 0x5C));
    px_rect(120, 104, 10, 4, RGB(0x66, 0x66, 0x6A));
    px_poly(cone2, 3, RGB(0xA8, 0xA8, 0xA8));
    px_rect(82, 105, 1, 6, C_AC);                      /* intake lips: the accent colour */
    px_rect(109, 103, 1, 6, C_AC);
    for (int x = 14; x < 178; x += 3) {                /* main deck windows, a gap at each door */
        int gap = 0;
        for (unsigned i = 0; i < sizeof door / sizeof *door; ++i) gap |= x >= door[i] - 3 && x <= door[i] + 3;
        if (!gap) px_rect(x, 96, 1, 2, C_BLACK);
    }
    for (int x = 39; x < 75; x += 3)                   /* upper deck windows, a gap at its door */
        if (x < 53 || x > 59) px_rect(x, 87, 1, 2, C_BLACK);
    px_rect(24, 89, 5, 1, C_BLACK);
    px_rect(22, 90, 5, 1, C_BLACK);
    px_rect(0, 140, 240, 1, C_GD);
    px_text(47, 148, "DX", 5, C_TX);
    px_text(107, 148, "747", 5, C_AC);
    px_text_c(120, 194, "8-PART MULTITIMBRAL DX7 MODULE", 1, C_DIM);
    px_text_c(120, 218, "DX747 " FM1_RELEASE_VERSION " PREVIEW", 1, C_LN);
}

/* ---- safe boot: the only screen while dx_safe_boot() is on ----------------- */

static void draw_safe(void)
{
    px_text_c(120, 40, "SAFE BOOT", 3, C_AM);
    px_rect(20, 74, 200, 1, C_LN);
    px_text_c(120, 90, "DX747 STARTED WITHOUT ITS ENGINES,", 1, C_TX);
    px_text_c(120, 104, "SCENES, SETTINGS AND RADIO SETUP.", 1, C_TX);
    px_text_c(120, 126, "NOTHING IS LOADED OR SAVED.", 1, C_DIM);
    px_text_c(120, 150, "USB MIDI AND THE FIRMWARE UPDATE", 1, C_DIM);
    px_text_c(120, 164, "PAGE STILL WORK.", 1, C_DIM);
    px_text_c(120, 196, "POWER OFF, RELEASE ENV + LFO,", 1, C_AC);
    px_text_c(120, 210, "POWER ON FOR A NORMAL START.", 1, C_AC);
}

/* ---- parts: the home screen, one row per part ------------------------------------ */

/* Half intensity: every other layer is drawn in a darker tone so that a split reads as two bars, not one. */
static uint16_t shade(uint16_t c) { return (uint16_t)((c & 0xF7DEU) >> 1); }
static int lane_x(int x, int w, unsigned note)
{
    unsigned n = note < 24 ? 24 : note > 120 ? 120 : note;
    return x + (int)(n - 24U) * w / 97;
}
/* The layers of a part over the keys: ranges that do not overlap share a row (a split), overlapping ones stack (layers). */
static void draw_lanes(unsigned p, int x, int y, int w, int h)
{
    uint8_t list[DX_ENGINES], rowend[DX_ENGINES], rowof[DX_ENGINES];
    unsigned n = part_layers(p, list), rows = 0;
    for (unsigned i = 1; i < n; ++i) /* insertion sort by low note */
        for (unsigned j = i; j > 0 && fr.cfg.e[list[j]].lo < fr.cfg.e[list[j - 1]].lo; --j) { uint8_t t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
    for (unsigned i = 0; i < n; ++i) {
        const struct dx_engine_cfg *e = &fr.cfg.e[list[i]];
        unsigned r = 0;
        while (r < rows && e->lo <= rowend[r]) ++r;
        if (r == rows) ++rows;
        rowend[r] = e->hi; rowof[i] = (uint8_t)r;
    }
    if (!rows) return;
    int bh = h / (int)rows; if (bh > 8) bh = 8;
    int top = y + (h - (int)rows * bh) / 2;
    for (unsigned i = 0; i < n; ++i) {
        const struct dx_engine_cfg *e = &fr.cfg.e[list[i]];
        int x0 = lane_x(x, w, e->lo), x1 = lane_x(x, w, e->hi + 1U), by = top + (int)rowof[i] * bh, bhh = rows > 1 ? bh - 1 : bh;
        px_rect(x0, by, x1 - x0 > 2 ? x1 - x0 : 2, bhh, i % 2U ? shade(part_color(p)) : part_color(p));
        if (e->shift) px_rect(x1 - 2 > x0 ? x1 - 2 : x0, by, 2, bhh, C_AM);
    }
}
static void draw_parts(void)
{
    draw_head();
    for (unsigned p = 0; p < DX_PARTS; ++p) {
        int y = 26 + 25 * (int)p;
        unsigned used = part_used(p), sel = p == sel_part();
        if (sel) px_frame(1, y, 238, 25, C_TX);
        part_chip(p, 4, y + 2, 26, 21, 0);
        if (!used) {
            px_text(36, y + 9, sel ? "EMPTY  ENV = NEW PART" : "EMPTY", 1, sel ? C_AM : C_LN);
            continue;
        }
        const struct dx_part *pt = &fr.live.parts.part[p];
        uint8_t list[DX_ENGINES];
        unsigned n = part_layers(p, list), muted = part_muted(p);
        if ((fr.active >> (pt->channel - 1U)) & 1U) px_rect(25, y + 3, 4, 4, C_AM);
        char name[11];
        trim_name(name, fr.e[part_main(p)].name, 10);
        px_text(36, y + 1, name, 2, muted ? C_DIM : C_TX);
        px_rect(158, y + 3, 58, 19, RGB(0x10, 0x10, 0x10));
        draw_lanes(p, 158, y + 3, 58, 19);
        if (n > 1) { char *o = put_s(tb, "+"); put_u(o, n - 1U); px_text(220, y + 9, tb, 1, muted ? C_DIM : C_TX); }
        if (sel && n > 1) { /* the selected part names its other layers; the rows stay uniform without it */
            char line[40], *o = line;
            *o++ = '+'; *o++ = ' ';
            for (unsigned i = 0; i < n; ++i) {
                if (list[i] == (unsigned)part_main(p)) continue;
                char nm[11];
                trim_name(nm, fr.e[list[i]].name, 10);
                if ((o - line) + (int)strlen(nm) + 2 > 38) break;
                if (o > line + 2) { *o++ = ','; *o++ = ' '; }
                o = put_s(o, nm);
            }
            *o = 0;
            px_text(36, y + 17, line, 1, C_DIM);
        }
    }
    draw_hint("SELECT = PART  SEQ = LAYERS  ENV = ADD");
}

/* ---- one part: its layers --------------------------------------------------------- */

static void draw_part_title(void)
{
    unsigned p = sel_part();
    char *o = put_s(tb, "PART "); put_u(o, p + 1U);
    px_text(4, 29, tb, 2, part_used(p) ? part_color(p) : C_DIM);
    if (part_used(p)) {
        uint8_t list[DX_ENGINES];
        unsigned n = part_layers(p, list);
        o = put_s(tb, "CH "); o = put_u(o, fr.live.parts.part[p].channel); o = put_s(o, n == 1 ? "  1 LAYER" : "  "); if (n > 1) { o = put_u(o, n); put_s(o, " LAYERS"); }
        px_text_r(236, 33, tb, 1, C_DIM);
    } else px_text_r(236, 33, "EMPTY", 1, C_DIM);
}
static void draw_no_part(void)
{
    px_text(4, 72, "NO SOUND IN THIS PART", 1, C_AM);
    px_text(4, 90, "ENV ADDS ONE ON THE LOWEST", 1, C_TX);
    px_text(4, 101, "FREE MIDI CHANNEL, WITH ONE", 1, C_TX);
    px_text(4, 112, "LAYER. SELECT PICKS A PART.", 1, C_TX);
}
static void draw_part(void)
{
    unsigned p = sel_part();
    draw_head();
    draw_part_title();
    if (!part_used(p)) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), sl = fr.live.parts.part[p].sel < n ? fr.live.parts.part[p].sel : 0U;
    for (unsigned i = 0; i < n; ++i) {
        unsigned e = list[i];
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        int y = 49 + 14 * (int)i;
        uint16_t col = engine_color(e);
        if (i == sl) px_frame(2, y - 2, 236, 14, C_TX);
        char d[2] = {(char)('1' + i), 0};
        px_rect(5, y, 12, 10, col); px_text_c(11, y + 2, d, 1, C_BLACK);
        char name[11];
        trim_name(name, fr.e[e].name, 10);
        px_text(22, y + 2, name, 1, c->mute ? C_DIM : C_TX);
        px_rect(88, y, 64, 10, RGB(0x10, 0x10, 0x10));
        int x0 = lane_x(88, 64, c->lo), x1 = lane_x(88, 64, c->hi + 1U);
        px_rect(x0, y + 1, x1 - x0 > 2 ? x1 - x0 : 2, 8, i % 2U ? shade(col) : col);
        if (c->shift) px_rect(x1 - 2 > x0 ? x1 - 2 : x0, y + 1, 2, 8, C_AM);
        if (c->shift) { put_shift(tb, c->shift); px_text(156, y + 2, tb, 1, C_AM); }
        unsigned vlo = c->vlo ? c->vlo : 1U, vhi = c->vhi ? c->vhi : 127U;
        if (vlo != 1 || vhi != 127) { char *o = put_s(tb, "V"); o = put_u(o, vlo); *o++ = '-'; put_u(o, vhi); px_text(190, y + 2, tb, 1, C_AM); }
    }
    int e = list[sl];
    const struct dx_engine_cfg *se = &fr.cfg.e[e];
    char *o = put_s(tb, "LAYER "); o = put_u(o, sl + 1U); o = put_s(o, ": "); o = put_range(o, se->lo, se->hi);
    px_text(4, 49 + 14 * (int)n + 4, tb, 1, C_DIM);
    px_text(4, 49 + 14 * (int)n + 15, "DX7 SYSEX EDITS THE FRAMED LAYER", 1, C_DIM);
    put_u(tb, fr.live.parts.part[p].channel); tile(0, "CHANNEL", tb, part_color(p));
    tile_u(1, "LEVEL", fr.live.parts.part[p].level, C_TX);
    o = put_u(tb, sl + 1U); *o++ = '/'; put_u(o, n); tile(2, "LAYER", tb, C_TX);
    o = put_u(tb, engines_in_use()); put_s(o, "/8"); tile(3, "ENGINES", tb, C_AM);
    draw_hint(ui.learn == 1 ? "LEARN: PLAY THE LOWEST KEY  ARP=STOP" : ui.learn == 2 ? "LEARN: PLAY THE HIGHEST KEY  ARP=STOP" : "ALGO LAYER  ENV ADD  LFO DEL  ARP LEARN");
}

/* ---- layer pages ------------------------------------------------------------------- */

static void draw_pbar(unsigned page)
{
    unsigned p = sel_part();
    part_chip(p, 4, 28, 26, 16, 0);
    if (part_used(p)) {
        uint8_t list[DX_ENGINES];
        unsigned n = part_layers(p, list), sl = fr.live.parts.part[p].sel < n ? fr.live.parts.part[p].sel : 0U;
        char *o = put_s(tb, "PART "); o = put_u(o, p + 1U); o = put_s(o, "  LAYER "); o = put_u(o, sl + 1U); *o++ = '/'; put_u(o, n);
        px_text(38, 32, tb, 1, C_TX);
    } else px_text(38, 32, "EMPTY PART", 1, C_DIM);
    static const char *const names[4] = {"SOUND", "RANGE", "VEL", "SENDS"};
    int x = 4;
    for (unsigned i = 0; i < 4; ++i) {
        int w = (int)text_w(names[i], 1) + 10;
        if (i == page) { px_rect(x, 52, w, 14, C_AC); px_text(x + 5, 56, names[i], 1, C_BLACK); }
        else { px_frame(x, 52, w, 14, C_LN); px_text(x + 5, 56, names[i], 1, C_DIM); }
        x += w + 6;
    }
    px_text_r(236, 56, "EDIT = NEXT", 1, C_DIM);
}
static void draw_esound(void)
{
    int e = sel_engine();
    draw_head(); draw_pbar(0);
    if (e < 0) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    unsigned en = (unsigned)e, p = fr.live.parts.layer[en].owner;
    const struct dx_engine_cfg *c = &fr.cfg.e[en];
    uint16_t col = engine_color(en);
    unsigned lvl = fr.live.parts.layer[en].lvl;
    char name[11];
    trim_name(name, fr.e[en].name, 10);
    px_text(4, 72, name, 3, col);
    char *o = put_s(tb, "PROGRAM "); put_u(o, fr.e[en].program + 1U);
    px_text(4, 98, tb, 1, C_DIM);
    px_text(4, 115, "LEVEL", 1, C_DIM); bar(64, 112, 150, 12, col, lvl, 127); put_u(tb, lvl); px_text_r(236, 115, tb, 1, C_TX);
    px_text(4, 137, "PAN", 1, C_DIM); px_frame(64, 134, 150, 12, C_LN); px_rect(139, 134, 1, 12, C_DIM);
    px_rect(139 + fr.live.pan[en] * 7 / 5 - 2, 136, 5, 8, col);
    px_text(4, 159, "BRIGHT", 1, C_DIM); px_frame(64, 156, 150, 12, C_LN); px_rect(139, 156, 1, 12, C_DIM);
    { int b = fr.live.bright[en] * 7 / 5; if (b >= 0) px_rect(140, 158, b, 8, C_AM); else px_rect(139 + b, 158, -b, 8, C_AM); }
    o = put_s(tb, "PLAYS: CH "); o = put_u(o, fr.live.parts.part[p].channel); o = put_s(o, "  "); o = put_range(o, c->lo, c->hi);
    if (c->shift) { o = put_s(o, "  "); put_shift(o, c->shift); }
    px_text(4, 180, tb, 1, C_AC);
    o = put_u(tb, fr.e[en].program + 1U); tile(0, "PATCH", tb, col);
    tile_u(1, "LEVEL", lvl, C_TX);
    put_pan(tb, fr.live.pan[en]); tile(2, "PAN", tb, C_TX);
    o = tb; if (fr.live.bright[en] > 0) *o++ = '+'; put_i(o, fr.live.bright[en]); tile(3, "BRIGHT", tb, C_AM);
    draw_hint("KEYS PLAY THE WHOLE PART");
}
static void draw_eplays(void)
{
    int e = sel_engine();
    draw_head(); draw_pbar(1);
    if (e < 0) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    unsigned en = (unsigned)e, p = fr.live.parts.layer[en].owner;
    const struct dx_engine_cfg *c = &fr.cfg.e[en];
    uint16_t col = engine_color(en);
    int l2 = (int)c->lo + c->shift, h2 = (int)c->hi + c->shift;
    l2 = l2 < 0 ? 0 : l2 > 127 ? 127 : l2; h2 = h2 < 0 ? 0 : h2 > 127 ? 127 : h2;
    px_text(4, 72, "THE PART LISTENS ON CHANNEL", 1, C_DIM); put_u(tb, fr.live.parts.part[p].channel); px_text(4, 82, tb, 2, col);
    px_text(4, 102, "THIS LAYER PLAYS NOTES", 1, C_DIM); put_range(tb, c->lo, c->hi); px_text(4, 112, tb, 2, C_TX);
    px_text(4, 132, "SOUNDS AT", 1, C_DIM);
    char *o = put_note(tb, (unsigned)l2); *o++ = '-'; o = put_note(o, (unsigned)h2);
    if (c->shift) { o = put_s(o, "  ("); o = put_shift(o, c->shift); put_s(o, " OCT)"); }
    px_text(4, 142, tb, 2, c->shift ? C_AM : C_TX);
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), shared = 0;
    char with[40];
    char *wo = put_s(with, "OVERLAPS LAYER");
    for (unsigned i = 0; i < n; ++i) {
        const struct dx_engine_cfg *x = &fr.cfg.e[list[i]];
        if (list[i] != en && x->lo <= c->hi && x->hi >= c->lo) { *wo++ = ' '; *wo++ = (char)('1' + i); ++shared; }
    }
    *wo = 0;
    px_text(4, 166, shared ? with : "NO OTHER LAYER ON THESE KEYS", 1, shared ? C_AM : C_DIM);
    put_note(tb, c->lo); tile(0, "LOW", tb, col);
    put_note(tb, c->hi); tile(1, "HIGH", tb, col);
    o = put_shift(tb, c->shift); put_s(o, c->shift % 12 ? "" : " OCT"); tile(2, "SHIFT", tb, C_AM);
    tile(3, "FOLD", c->fold == DX_FOLD_WRAP ? "WRAP" : c->fold == DX_FOLD_CLIP ? "CLIP" : "DROP", C_TX);
    draw_hint(ui.learn == 1 ? "LEARN: PLAY THE LOWEST KEY  ARP=STOP" : ui.learn == 2 ? "LEARN: PLAY THE HIGHEST KEY  ARP=STOP" : "ARP = LEARN THE RANGE FROM TWO KEYS");
}
static void draw_evel(void)
{
    int e = sel_engine();
    draw_head(); draw_pbar(2);
    if (e < 0) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    unsigned en = (unsigned)e, p = fr.live.parts.layer[en].owner;
    const struct dx_engine_cfg *c = &fr.cfg.e[en];
    uint16_t col = engine_color(en);
    unsigned vlo = c->vlo ? c->vlo : 1U, vhi = c->vhi ? c->vhi : 127U;
    px_text(4, 72, "THIS LAYER PLAYS VELOCITIES", 1, C_DIM);
    char *o = put_u(tb, vlo); *o++ = '-'; put_u(o, vhi);
    px_text(4, 82, tb, 2, vlo == 1 && vhi == 127 ? C_TX : C_AM);
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list);
    px_text(4, 106, "ALL LAYERS OF THE PART (SOFT TO HARD)", 1, C_DIM);
    for (unsigned i = 0; i < n; ++i) {
        const struct dx_engine_cfg *x = &fr.cfg.e[list[i]];
        unsigned lo = x->vlo ? x->vlo : 1U, hi = x->vhi ? x->vhi : 127U;
        int y = 118 + 8 * (int)i, x0 = 8 + (int)(lo - 1U) * 224 / 126, x1 = 8 + (int)hi * 224 / 127;
        px_rect(8, y, 224, 6, RGB(0x10, 0x10, 0x10));
        px_rect(x0, y, x1 - x0 > 2 ? x1 - x0 : 2, 6, list[i] == en ? col : C_MU);
    }
    tile_u(0, "LOW", vlo, col); tile_u(1, "HIGH", vhi, col);
    o = put_u(tb, layer_index(en) + 1U); *o++ = '/'; put_u(o, n); tile(2, "LAYER", tb, C_TX);
    draw_hint("K1 LOW  K2 HIGH  SPLIT LAYERS BY TOUCH");
}
static void draw_esends(void)
{
    int e = sel_engine();
    draw_head(); draw_pbar(3);
    if (e < 0) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    unsigned en = (unsigned)e;
    const struct dx_engine_cfg *c = &fr.cfg.e[en];
    char *o = put_u(tb, fr.e[en].rev); put_s(o, " / 15"); px_text(4, 72, "REVERB SEND", 1, C_DIM); px_text(4, 82, tb, 2, C_TX);
    o = put_u(tb, fr.e[en].dly); put_s(o, " / 15"); px_text(4, 99, "DELAY SEND", 1, C_DIM); px_text(4, 109, tb, 2, C_TX);
    put_u(tb, c->poly); px_text(4, 126, "MAX VOICES", 1, C_DIM); px_text(4, 136, tb, 2, C_TX);
    bar(100, 84, 136, 12, RGB(0x8F, 0xB4, 0xFF), fr.e[en].rev, 15);
    bar(100, 111, 136, 12, RGB(0xFF, 0xB8, 0x6B), fr.e[en].dly, 15);
    for (unsigned v = 0; v < 8; ++v) { /* one square per voice this layer may use */
        if (v < c->poly) px_rect(100 + 17 * (int)v, 138, 15, 12, engine_color(en)); else px_frame(100 + 17 * (int)v, 138, 15, 12, C_LN);
    }
    px_text(4, 160, "THIS LAYER USES ENGINE", 1, C_DIM); put_u(tb, en + 1U); px_text(4, 170, tb, 2, C_TX);
    px_text(4, 190, "A NOTE ON THE PART USES ONE VOICE", 1, C_DIM);
    tile_u(0, "REV", fr.e[en].rev, C_TX); tile_u(1, "DLY", fr.e[en].dly, C_TX); tile_u(2, "POLY", c->poly, C_TX);
    tile_u(3, "ENGINE", en + 1U, C_AC);
    draw_hint("K1 REVERB  K2 DELAY  K3 MAX VOICES");
}

/* ---- midi in ---------------------------------------------------------------- */

static void log_text(char *o, const struct dx_log *l)
{
    unsigned type = l->status & 0xf0U;
    if (type == 0x90 && l->d2) { o = put_s(o, "NOTE "); o = put_note(o, l->d1); *o++ = ' '; put_u(o, l->d2); }
    else if (type == 0x80 || type == 0x90) { o = put_s(o, "OFF  "); put_note(o, l->d1); }
    else if (type == 0xb0) { o = put_s(o, "CC "); o = put_u(o, l->d1); o = put_s(o, " = "); put_u(o, l->d2); }
    else if (type == 0xc0) { o = put_s(o, "PROGRAM "); put_u(o, l->d1 + 1U); }
    else if (l->status == 0xf3) { o = put_s(o, "SONG "); put_u(o, l->d1 + 1U); }
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
        if (l->status == 0xf3) px_text(44, y, "--", 2, C_DIM); else { put_2(tb, l->ch + 1U); px_text(44, y, tb, 2, C_TX); }
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
    draw_title("MIX", "ONE FADER PER PART");
    for (unsigned p = 0; p < DX_PARTS; ++p) {
        int x = 4 + 29 * (int)p;
        part_chip(p, x, 46, 26, 16, p == sel_part());
        if (!part_used(p)) continue;
        uint8_t list[DX_ENGINES];
        unsigned n = part_layers(p, list), muted = part_muted(p), solo = part_solo(p), audible = 0, level = 0;
        for (unsigned i = 0; i < n; ++i) { audible |= fr.e[list[i]].audible; if (fr.e[list[i]].level > level) level = fr.e[list[i]].level; }
        uint16_t col = !muted && !audible ? C_MU : part_color(p);   /* silenced by another part's solo */
        int fill = 98 * (int)fr.live.parts.part[p].level / 127, meter = muted || !audible ? 0 : 98 * (int)level / 127;
        px_frame(x + 3, 66, 12, 100, C_LN); px_rect(x + 4, 166 - fill, 10, fill, col);
        px_frame(x + 18, 66, 6, 100, C_LN); px_rect(x + 19, 165 - meter, 4, meter, level >= 100 ? C_AM : C_GR);
        int m = part_main(p) >= 0 ? part_main(p) : 0;
        px_rect(x + 3, 170, 22, 1, C_LN); px_rect(x + 14 + fr.live.pan[m] / 5, 169, 3, 3, col);
        if (muted) px_rect(x + 2, 176, 11, 11, C_RD); else px_frame(x + 2, 176, 11, 11, C_LN);
        px_text_c(x + 7, 178, "M", 1, muted ? C_BLACK : C_DIM);
        if (solo) px_rect(x + 14, 176, 11, 11, C_AM); else px_frame(x + 14, 176, 11, 11, C_LN);
        px_text_c(x + 19, 178, "S", 1, solo ? C_BLACK : C_DIM);
        if (n > 1) { char d[3]; put_u(d, n); px_text_c(x + 13, 190, d, 1, C_DIM); }
    }
    px_text(4, 206, "FADER = PART LEVEL  DIGIT = LAYERS", 1, C_DIM);
    draw_hint("K1 LEVEL  K2 PAN  ENV MUTE  LFO SOLO");
}
static void draw_fx(void)
{
    draw_head();
    draw_title("FX", "SHARED REVERB + DELAY");
    px_text(40, 46, "REV SEND", 1, C_DIM); px_text(136, 46, "DLY SEND", 1, C_DIM);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        int y = 56 + 17 * (int)e;
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        engine_chip(e, 4, y, 16, 14, (int)e == sel_engine());
        bar(40, y + 2, 80, 10, c->mute || fr.live.parts.layer[e].owner == DX_NO_PART ? C_MU : RGB(0x8F, 0xB4, 0xFF), fr.e[e].rev, 15);
        put_u(tb, fr.e[e].rev); px_text(128, y + 3, tb, 1, C_TX);
        bar(136, y + 2, 80, 10, c->mute || fr.live.parts.layer[e].owner == DX_NO_PART ? C_MU : RGB(0xFF, 0xB8, 0x6B), fr.e[e].dly, 15);
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
    px_text(8, 174, "BT AND USB IN", 1, C_DIM); px_text(8, 186, "FOLLOW THE KNOB", 1, C_TX);
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
            for (unsigned j = 0; j < 8; ++j) px_rect(x + 30 + (int)(j % 4) * 6, y + 5 + (int)(j / 4) * 6, 4, 4, (fr.scene_parts[idx] >> j) & 1U ? EC[j] : RGB(0x2C, 0x2C, 0x2C));
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
        px_text(18, 100, "SCENES NEED FLASH THAT HOLDS STOCK", 1, C_TX);
        px_text(18, 111, "DATA (SAMPLE BANK OR UNSAVED STOCK", 1, C_TX);
        px_text(18, 122, "SAVE). IT WILL BE ERASED.", 1, C_TX);
        px_text(18, 132, "DX747 CANNOT BACK IT UP.", 1, C_TX);
        px_text(18, 142, "SAVE = ERASE AND CONTINUE", 1, C_AM);
        px_text(18, 152, "HOME = CANCEL", 1, C_DIM);
    }
    draw_hint(ui.mode == M_BROWSE ? "KEYS RECALL  ENV/LFO STEP  SAVE STORES" : "HOME = CANCEL");
}

/* ---- system --------------------------------------------------------------------- */

static void draw_sys(void)
{
    static const char *const labels[7] = {"BLUETOOTH MIDI", "BLUETOOTH AUDIO IN", "BUTTON LIGHT", "SCENE CHANNEL", "MASTER CONTROL CHANNEL", "CPU", "SCENE FLASH"};
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
        case 2: o = put_u(tb, s->backlight); put_s(o, " / 8"); col = C_AM; break;
        case 3: if (s->scene_ch) { o = put_s(tb, "CH "); put_u(o, s->scene_ch); col = C_AC; } else { val = "OFF"; col = C_DIM; } break;
        case 4: if (s->master_ch) { o = put_s(tb, "CH "); put_u(o, s->master_ch); col = C_AC; } else { val = "OFF"; col = C_DIM; } break;
        case 5: o = put_u(tb, fr.cpu); put_s(o, "%"); break;
        default: val = fr.storage ? (fr.storage > 0 ? "NEEDS SETUP" : "ERROR") : "READY"; col = fr.storage ? C_AM : C_GR; break;
        }
        if (ui.sys_row == i) { px_rect(0, y, 2, 24, C_AC); }
        px_text(4, y, labels[i], 1, ui.sys_row == i ? C_AC : C_DIM);
        px_text(4, y + 9, val, 2, col);
    }
    for (unsigned b = 0; b < 8; ++b) px_rect(150 + (int)b * 10, 47 + 26 * 2 + 9, 8, 12, b < fr.settings.backlight ? C_AM : RGB(0x3C, 0x3C, 0x3C));
    px_frame(150, 47 + 26 * 5 + 9, 80, 12, C_LN);
    px_rect(151, 47 + 26 * 5 + 10, (int)(fr.cpu * 78U / 100U), 10, fr.cpu >= 85 ? C_AM : C_GR);
    switch (ui.sys_row) {
    case 0: draw_hint("PHONE OR COMPUTER CONNECTS TO THIS UNIT"); break;
    case 4: draw_hint("K1 = CHANGE  CC74 CUTOFF 71 RES 70 TYPE"); break;
    case 3:
        if (s->scene_ch && dx_part_on_channel(&fr.live.parts, s->scene_ch) != DX_NO_PART) draw_hint("A PART USES THIS CH: NO PROGRAM CHANGE");
        else draw_hint(s->scene_ch ? "PROGRAM CHANGE ON THIS CH = SCENE" : "K1 = CHANGE  OFF: NO SCENE CHANNEL");
        break;
    case 1: case 2: draw_hint("SELECT = ROW  K1 = CHANGE"); break;
    default: draw_hint("SELECT = ROW"); break;
    }
}

static void draw_screen(void)
{
    switch (ui.screen) {
    case S_BOOT: draw_boot(); break;
    case S_PARTS: draw_parts(); break;
    case S_PART: draw_part(); break;
    case S_ESOUND: draw_esound(); break;
    case S_EPLAYS: draw_eplays(); break;
    case S_EVEL: draw_evel(); break;
    case S_ESENDS: draw_esends(); break;
    case S_MON: draw_mon(); break;
    case S_MIX: draw_mix(); break;
    case S_FX: draw_fx(); break;
    case S_MASTER: draw_master(); break;
    case S_SCENES: draw_scenes(); break;
    case S_SAFE: draw_safe(); break;
    default: draw_sys(); break;
    }
}
