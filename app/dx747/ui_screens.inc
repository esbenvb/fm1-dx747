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
    if (right) px_text_r(236, 33, right, 1, C_TX);
}
static void draw_hint(const char *s) { px_text_c(120, 230, s, 1, C_TX); }
/* Page tabs: the current one filled. */
static void draw_tabs(int x, int y, const char *const *names, unsigned n, unsigned cur)
{
    for (unsigned i = 0; i < n; ++i) {
        int w = (int)text_w(names[i], 1) + 10;
        if (i == cur) { px_rect(x, y, w, 14, C_AC); px_text(x + 5, y + 4, names[i], 1, C_BLACK); }
        else { px_frame(x, y, w, 14, C_LN); px_text(x + 5, y + 4, names[i], 1, C_TX); }
        x += w + 6;
    }
}
static int tabs_w(const char *const *names, unsigned n)
{
    int w = -6;
    for (unsigned i = 0; i < n; ++i) w += (int)text_w(names[i], 1) + 16;
    return w;
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
    if (sel) px_rect(x - 3, y - 1, 2, h + 2, C_TX);
}
/* A part's chip: the part number, in the part's colour. The MIDI channel is always written out ("CH 5"), never a bare
 * number, so the two cannot be mistaken for each other. An empty slot is an outline with the slot number. */
static void part_chip(unsigned p, int x, int y, int w, int h, unsigned sel)
{
    if (part_used(p)) { chip_n(p + 1U, x, y, w, h, part_color(p), sel); return; }
    char d[3];
    put_u(d, p + 1U);
    px_dotted(x, y, w, h, sel ? C_TX : C_LN);
    px_text_c(x + w / 2, y + (h - 7) / 2, d, 1, C_DIM);
    if (sel) px_rect(x - 3, y - 1, 2, h + 2, C_TX);
}
/* An engine's chip: the number of the part it is a layer of, or an outline when the engine is free. */
static void engine_chip(unsigned e, int x, int y, int w, int h, unsigned sel)
{
    unsigned owner = fr.live.parts.layer[e].owner;
    if (owner < DX_PARTS && part_used(owner)) { chip_n(owner + 1U, x, y, w, h, engine_color(e), sel); return; }
    char d[2] = {(char)('1' + e), 0};
    px_frame(x, y, w, h, C_LN);
    px_text_c(x + w / 2, y + (h - 7) / 2, d, 1, C_DIM);
    if (sel) px_rect(x - 3, y - 1, 2, h + 2, C_TX);
}
static void tile(unsigned i, const char *label, const char *val, uint16_t color)
{
    int x = 6 + 59 * (int)i, y = 195;
    unsigned scale = text_w(val, 2) > 49 ? 1U : 2U;
    px_frame(x, y, 55, 32, C_LN);
    px_text(x + 3, y + 3, label, 1, C_TX);
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
        if (c[i].fill) px_rect(x, y, 13, 12, c[i].fill); else px_frame(x, y, 13, 12, C_OUT);   /* 13 wide: two digits (11 px) fit inside the frame */
        if (c[i].t[0]) px_text_c(x + 6, y + 3, c[i].t, 1, c[i].fill ? C_BLACK : C_DIM);
        if (c[i].sel) px_frame(x - 1, y - 1, 15, 14, C_TX);
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
    static const int16_t fin[][2] = {{178,88},{183,86},{195,76},{218,56},{231,56},{221,83},{220,87},{220,88}};
    static const int16_t fedge[][2] = {{183,86},{195,76},{218,56},{221,56},{198,76},{186,86}};
    static const int16_t stab[][2] = {{193,92},{197,90},{224,87},{231,88},{226,91},{222,94},{218,96},{201,99},{200,97}};
    static const int16_t wlo[][2] = {{72,104},{77,101},{100,99},{125,99},{158,98},{146,101},{138,103},{132,105},{112,107},{104,107},{76,107}};
    static const int16_t wmd[][2] = {{72,104},{77,101},{100,99},{125,99},{158,98},{146,100},{138,101},{128,103},{110,104},{104,105},{76,105}};
    static const int16_t whi[][2] = {{77,101},{100,99},{125,99},{158,98},{146,99},{125,100},{104,102},{78,102}};
    static const int16_t door[] = {38, 68, 104, 137, 182};
    static const int16_t eng[2][2] = {{80,105},{110,103}};
    const uint16_t hi = RGB(0xC4, 0xC4, 0xC8), md = RGB(0x8C, 0x8C, 0x90), lo = RGB(0x54, 0x54, 0x58), cn = RGB(0x98, 0x98, 0x9C);
    px_text_c(120, 10, "Esben von Buchwald, 2026", 1, C_DIM);
    px_poly(body, sizeof body / sizeof *body, C_TX);
    px_poly(belly, sizeof belly / sizeof *belly, RGB(0xCC, 0xCC, 0xCC));
    px_poly(fin, sizeof fin / sizeof *fin, C_AC);      /* the fin in the accent colour, a lighter line along its leading edge */
    px_poly(fedge, sizeof fedge / sizeof *fedge, RGB(0x90, 0xF4, 0xFF));
    px_poly(stab, sizeof stab / sizeof *stab, RGB(0x70, 0x70, 0x74));
    px_line(198, 90, 224, 88, RGB(0xA4, 0xA4, 0xA8));
    for (unsigned i = 0; i < sizeof door / sizeof *door; ++i) px_frame(door[i] - 1, 93, 3, 8, RGB(0xB0, 0xB0, 0xB4));
    px_frame(55, 86, 3, 6, RGB(0xB0, 0xB0, 0xB4));
    px_poly(wlo, sizeof wlo / sizeof *wlo, lo);        /* swept wing: dark underside, mid body, light top */
    px_poly(wmd, sizeof wmd / sizeof *wmd, md);
    px_poly(whi, sizeof whi / sizeof *whi, hi);
    for (unsigned e = 0; e < 2; ++e) {                 /* engines: pylon, cowl in three tones, dark inlet, core, plug, accent lip */
        int x = eng[e][0], y = eng[e][1];
        px_rect(x + 12, y, 2, 1, lo);
        px_rect(x + 14, y, 8, 3, lo);
        px_rect(x + 1, y, 14, 10, md);
        px_rect(x + 1, y, 14, 2, hi);
        px_rect(x + 1, y + 7, 14, 3, lo);
        px_rect(x + 15, y + 3, 7, 5, RGB(0x66, 0x66, 0x6A));
        px_rect(x + 15, y + 3, 7, 1, md);
        px_rect(x + 22, y + 3, 2, 5, cn);
        px_rect(x + 24, y + 4, 2, 3, cn);
        px_rect(x + 26, y + 5, 1, 1, cn);
        px_rect(x + 1, y + 2, 2, 5, RGB(0x3C, 0x3C, 0x40));
        px_rect(x, y + 1, 1, 8, C_AC);
    }
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
    px_text_c(120, 126, "NOTHING IS LOADED OR SAVED.", 1, C_TX);
    px_text_c(120, 150, "USB MIDI AND THE FIRMWARE UPDATE", 1, C_TX);
    px_text_c(120, 164, "PAGE STILL WORK.", 1, C_TX);
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
        if (sel) { px_rect(1, y, 238, 25, C_SEL); px_rect(1, y, 2, 25, C_TX); }
        part_chip(p, 4, y + 2, 26, 21, 0);
        if (!used) {
            px_text(36, y + 9, sel ? "EMPTY  ENV = NEW PART" : "EMPTY", 1, sel ? C_AM : C_DIM);
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
        { /* the channel is always written out; the selected part names its other layers after it (twenty letters fit left of the bars) */
            char line[24], *o = put_s(line, "CH "); o = put_u(o, pt->channel);
            if (sel && n > 1) {
                o = put_s(o, " +");
                for (unsigned i = 0; i < n; ++i) {
                    if (list[i] == (unsigned)part_main(p)) continue;
                    char nm[11];
                    trim_name(nm, fr.e[list[i]].name, 10);
                    if ((o - line) + (int)strlen(nm) + 1 > 20) break;
                    *o++ = ' '; o = put_s(o, nm);
                }
            }
            px_text(36, y + 17, line, 1, sel ? C_AC : C_TX);
        }
    }
    draw_hint("SELECT PART  PRESETS SOUND  HOME DETAIL");
}

/* ---- one part: its layers --------------------------------------------------------- */

/* The identity row under the header: a small coloured caption and a large number for each of the part, the layer, the MIDI
 * channel and the part level, so none of them is mistaken for another. */
static int ident_group(int x, const char *cap, uint16_t cap_color, const char *num, const char *suffix, uint16_t num_color)
{
    px_text(x, 42, cap, 1, cap_color);                  /* the caption sits on the baseline of the number */
    x += (int)text_w(cap, 1) + 5;
    px_text(x, 28, num, 3, num_color);
    x += (int)text_w(num, 3);
    if (suffix) { px_text(x + 3, 35, suffix, 2, C_DIM); x += 3 + (int)text_w(suffix, 2); }
    return x;
}
static void draw_ident(unsigned with_layer, unsigned with_channel, unsigned with_level)
{
    unsigned p = sel_part();
    char t[8], u[8];
    put_u(t, p + 1U);
    int x = ident_group(4, "PART", part_used(p) ? part_color(p) : C_DIM, t, NULL, C_TX);
    if (!part_used(p)) { px_text(x + 14, 35, "EMPTY", 2, C_TX); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), sl = fr.live.parts.part[p].sel < n ? fr.live.parts.part[p].sel : 0U;
    x += 12;
    if (with_layer) { put_u(t, sl + 1U); put_u(put_s(u, "/"), n); x = ident_group(x, "LAYER", C_AM, t, u, C_TX) + 12; }
    if (with_channel) { put_u(t, fr.live.parts.part[p].channel); x = ident_group(x, "CH", C_AC, t, NULL, C_TX) + 12; }
    if (with_level) { put_u(t, fr.live.parts.part[p].level); (void)ident_group(x, "LEVEL", C_GR, t, NULL, C_TX); }
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
    draw_ident(1, 1, 0);
    if (!part_used(p)) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), sl = fr.live.parts.part[p].sel < n ? fr.live.parts.part[p].sel : 0U;
    for (unsigned i = 0; i < n; ++i) {
        unsigned e = list[i];
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        int y = 56 + 14 * (int)i;
        uint16_t col = engine_color(e);
        if (i == sl) { px_rect(2, y - 2, 236, 14, C_SEL); px_rect(2, y - 2, 2, 14, C_TX); }
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
    px_text(4, 56 + 14 * (int)n + 4, tb, 1, C_TX);
    px_text(4, 56 + 14 * (int)n + 15, "DX7 SYSEX EDITS THE FRAMED LAYER", 1, C_TX);
    put_u(tb, fr.live.parts.part[p].channel); tile(0, "CHANNEL", tb, part_color(p));
    tile_u(1, "LEVEL", fr.live.parts.part[p].level, C_TX);
    o = put_u(tb, sl + 1U); *o++ = '/'; put_u(o, n); tile(2, "LAYER", tb, C_TX);
    o = put_u(tb, engines_in_use()); put_s(o, "/8"); tile(3, "ENGINES", tb, C_AM);
    draw_hint(ui.learn == 1 ? "LEARN: PLAY THE LOWEST KEY  ARP=STOP" : ui.learn == 2 ? "LEARN: PLAY THE HIGHEST KEY  ARP=STOP" : "ALGO LAYER  ENV ADD  LFO DEL  ARP LEARN");
}

/* ---- the part in detail: HOME a second time ---------------------------------------- */

/* Note 0..127 along a 224 px line, 1.75 px to the note. */
static int note_x(unsigned note) { return 8 + (int)(note > 127 ? 127U : note) * 224 / 128; }
/* One line per layer over a keyboard ruler: its sound, its keys as a bar, an octave shift as a small amber tag, its level,
 * pan and velocity window. The channel and the part level are the big numbers above and are edited with K1 and K2.
 * Up to three layers get roomy lines (the name large, the figures spelled out); more are packed as tightly as eight need. */
static void draw_detail(void)
{
    unsigned p = sel_part();
    draw_head();
    draw_ident(0, 1, 1);
    if (!part_used(p)) { draw_no_part(); draw_hint("ENV = ADD A PART HERE"); return; }
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), sl = fr.live.parts.part[p].sel < n ? fr.live.parts.part[p].sel : 0U;
    px_rect(8, 64, 224, 1, C_LN);
    for (unsigned c = 0; c <= 10; ++c) {            /* the ruler: a tick at every C, a label at every other one */
        int x = note_x(c * 12U);
        px_rect(x, c == 5 ? 60 : 62, 1, c == 5 ? 5 : 3, c == 5 ? C_TX : C_LN);
        if (c % 2U == 1U) { char *o = put_s(tb, "C"); put_i(o, (int)c - 1); px_text_c(x, 52, tb, 1, c == 5 ? C_TX : C_DIM); }
    }
    int big = n <= 3, rh = big ? 46 : 152 / (int)n;
    for (unsigned i = 0; i < n; ++i) {
        unsigned e = list[i];
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        int y = 69 + rh * (int)i, ly = y + (big ? 27 : 8), lh = big ? 14 : rh - 12 > 14 ? 14 : rh - 12;
        uint16_t col = engine_color(e);
        if (i == sl) { px_rect(2, y - 3, 236, rh, C_SEL); px_rect(2, y - 3, 2, rh, C_TX); }
        char d[2] = {(char)('1' + i), 0};
        char name[11];
        trim_name(name, fr.e[e].name, 10);
        unsigned vlo = c->vlo ? c->vlo : 1U, vhi = c->vhi ? c->vhi : 127U, narrow = vlo != 1 || vhi != 127;
        int pan = fr.live.pan[e];
        char *o;
        if (big) {
            px_rect(5, y, 14, 15, col); px_text_c(12, y + 4, d, 1, C_BLACK);
            px_text(24, y + 1, name, 2, c->mute ? C_DIM : C_TX);
            o = put_s(tb, "LEVEL "); o = put_u(o, fr.live.parts.layer[e].lvl);
            if (pan) { o = put_s(o, "  PAN "); o = put_pan(o, pan); }
            if (narrow) { o = put_s(o, "  VEL "); o = put_u(o, vlo); *o++ = '-'; put_u(o, vhi); }
            px_text(24, y + 18, tb, 1, C_TX);
        } else {
            px_rect(5, y, 12, 9, col); px_text_c(11, y + 1, d, 1, C_BLACK);
            px_text(20, y + 1, name, 1, c->mute ? C_DIM : C_TX);
            o = put_s(tb, "LVL "); put_u(o, fr.live.parts.layer[e].lvl);
            px_text(86, y + 1, tb, 1, C_TX);
            int tx = 138;
            if (pan) { o = put_s(tb, "PAN "); put_pan(o, pan); px_text(tx, y + 1, tb, 1, C_TX); tx += 52; }
            if (narrow) { o = put_s(tb, "V"); o = put_u(o, vlo); *o++ = '-'; put_u(o, vhi); px_text(tx, y + 1, tb, 1, C_AM); }
        }
        px_rect(8, ly, 224, lh, RGB(0x10, 0x10, 0x10));
        for (unsigned k = 0; k <= 10; ++k) px_rect(note_x(k * 12U), ly, 1, lh, k == 5 ? C_MU : C_GD);
        int x0 = note_x(c->lo), x1 = note_x(c->hi + 1U);
        if (c->hi == 127) x1 = 232;
        if (x1 - x0 < 3) x1 = x0 + 3;
        px_rect(x0, ly + 1, x1 - x0, lh - 2, c->mute ? C_MU : i % 2U ? shade(col) : col);
        if (c->shift) {                             /* the shift as a small tag beside the bar, on its right if there is room */
            put_shift(tb, c->shift);
            int w = (int)text_w(tb, 1), tag = x1 + 3, ty = ly + (lh - 7) / 2;
            if (tag + w > 232) {
                tag = x0 - 3 - w;
                if (tag < 8) { tag = x1 - w - 3; px_rect(tag - 2, ty, w + 4, 7, C_BLACK); } /* a bar from end to end: the tag sits on its right end */
            }
            px_text(tag, ty, tb, 1, C_AM);
        }
    }
    if (big) px_text(4, 214, "BARS = KEYS  +N = SHIFT  V = VELOCITY", 1, C_TX);
    draw_hint(ui.learn == 1 ? "LEARN: PLAY THE LOWEST KEY  ARP=STOP" : ui.learn == 2 ? "LEARN: PLAY THE HIGHEST KEY  ARP=STOP" : "K1 CH K2 VOL K3 LAYER VOL PRESETS SOUND");
}

/* ---- layer pages ------------------------------------------------------------------- */

static void draw_pbar(unsigned page)
{
    draw_ident(1, 1, 0);
    static const char *const names[4] = {"SOUND", "RANGE", "VEL", "SENDS"};
    draw_tabs(4, 52, names, 4, page);
    px_text_r(236, 56, "OCT = PAGE", 1, C_TX);
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
    px_text(4, 98, tb, 1, C_TX);
    px_text(4, 115, "LEVEL", 1, C_TX); bar(64, 112, 150, 12, col, lvl, 127); put_u(tb, lvl); px_text_r(236, 115, tb, 1, C_TX);
    px_text(4, 137, "PAN", 1, C_TX); px_frame(64, 134, 150, 12, C_LN); px_rect(139, 134, 1, 12, C_DIM);
    px_rect(139 + fr.live.pan[en] * 7 / 5 - 2, 136, 5, 8, col);
    px_text(4, 159, "BRIGHT", 1, C_TX); px_frame(64, 156, 150, 12, C_LN); px_rect(139, 156, 1, 12, C_DIM);
    { int b = fr.live.bright[en] * 7 / 5; if (b >= 0) px_rect(140, 158, b, 8, C_AM); else px_rect(139 + b, 158, -b, 8, C_AM); }
    o = put_s(tb, "PLAYS: CH "); o = put_u(o, fr.live.parts.part[p].channel); o = put_s(o, "  "); o = put_range(o, c->lo, c->hi);
    if (c->shift) { o = put_s(o, "  "); put_shift(o, c->shift); }
    px_text(4, 180, tb, 1, C_AC);
    tile_u(0, "LEVEL", lvl, C_TX);
    put_pan(tb, fr.live.pan[en]); tile(1, "PAN", tb, C_TX);
    o = tb; if (fr.live.bright[en] > 0) *o++ = '+'; put_i(o, fr.live.bright[en]); tile(2, "BRIGHT", tb, C_AM);
    put_u(tb, fr.e[en].program + 1U); tile(3, "PRESETS", tb, col);
    draw_hint("K1 LEVEL  K2 PAN  K3 BRIGHT");
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
    px_text(4, 72, "THE PART LISTENS ON CHANNEL", 1, C_TX); put_u(tb, fr.live.parts.part[p].channel); px_text(4, 82, tb, 2, col);
    px_text(4, 102, "THIS LAYER PLAYS NOTES", 1, C_TX); put_range(tb, c->lo, c->hi); px_text(4, 112, tb, 2, C_TX);
    px_text(4, 132, "SOUNDS AT", 1, C_TX);
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
    px_text(4, 166, shared ? with : "NO OTHER LAYER ON THESE KEYS", 1, shared ? C_AM : C_TX);
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
    px_text(4, 72, "THIS LAYER PLAYS VELOCITIES", 1, C_TX);
    char *o = put_u(tb, vlo); *o++ = '-'; put_u(o, vhi);
    px_text(4, 82, tb, 2, vlo == 1 && vhi == 127 ? C_TX : C_AM);
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list);
    px_text(4, 106, "ALL LAYERS OF THE PART (SOFT TO HARD)", 1, C_TX);
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
    char *o = put_u(tb, fr.e[en].rev); put_s(o, " / 15"); px_text(4, 72, "REVERB SEND", 1, C_TX); px_text(4, 82, tb, 2, C_TX);
    o = put_u(tb, fr.e[en].dly); put_s(o, " / 15"); px_text(4, 99, "DELAY SEND", 1, C_TX); px_text(4, 109, tb, 2, C_TX);
    put_u(tb, c->poly); px_text(4, 126, "MAX VOICES", 1, C_TX); px_text(4, 136, tb, 2, C_TX);
    bar(100, 84, 136, 12, RGB(0x8F, 0xB4, 0xFF), fr.e[en].rev, 15);
    bar(100, 111, 136, 12, RGB(0xFF, 0xB8, 0x6B), fr.e[en].dly, 15);
    for (unsigned v = 0; v < 8; ++v) { /* one square per voice this layer may use */
        if (v < c->poly) px_rect(100 + 17 * (int)v, 138, 15, 12, engine_color(en)); else px_frame(100 + 17 * (int)v, 138, 15, 12, C_LN);
    }
    px_text(4, 160, "THIS LAYER USES ENGINE", 1, C_TX); put_u(tb, en + 1U); px_text(4, 170, tb, 2, C_TX);
    px_text(4, 186, "A NOTE ON THE PART USES ONE VOICE", 1, C_TX);
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
    if (!fr.nlog) px_text(4, 60, "NO MIDI YET", 2, C_TX);
    struct cell c[16];
    memset(c, 0, sizeof c);
    for (unsigned k = 0; k < 16; ++k) { cell_num(&c[k], k); if ((fr.active >> k) & 1U) c[k].fill = C_AM; }
    strip(196, c);
    px_text(4, 212, "AMBER = SENDING NOW", 1, C_TX);
    draw_hint("PLAY EACH KEYBOARD TO FIND ITS CH");
}

/* ---- mix, fx, master ------------------------------------------------------- */

/* The pan of a part: where its layers sit, the middle of the spread (one layer: its own pan). min and max come back too. */
static int part_pan(unsigned p, int *lo, int *hi)
{
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list);
    int mn = 50, mx = -50;
    for (unsigned i = 0; i < n; ++i) { int v = fr.live.pan[list[i]]; if (v < mn) mn = v; if (v > mx) mx = v; }
    if (!n) mn = mx = 0;
    if (lo) *lo = mn;
    if (hi) *hi = mx;
    return (mn + mx) / 2;
}
/* A part's sends, the average over its layers. */
static unsigned part_send(unsigned p, unsigned which)
{
    uint8_t list[DX_ENGINES];
    unsigned n = part_layers(p, list), sum = 0;
    for (unsigned i = 0; i < n; ++i) sum += which ? fr.e[list[i]].dly : fr.e[list[i]].rev;
    return n ? (sum + n / 2U) / n : 0U;
}
static void draw_mix(void)
{
    draw_head();
    draw_title("MIX", NULL);
    px_text_r(236, 33, ui.mixkeys ? "WHITE KEYS = MUTE / SOLO" : "ONE FADER PER PART", 1, ui.mixkeys ? C_AM : C_TX);
    for (unsigned p = 0; p < DX_PARTS; ++p) {
        int x = 4 + 29 * (int)p;
        unsigned sel = p == sel_part();
        if (sel) px_rect(x - 3, 44, 29, 156, C_SEL);
        part_chip(p, x, 46, 26, 16, sel);
        if (!part_used(p)) continue;
        uint8_t list[DX_ENGINES];
        unsigned n = part_layers(p, list), muted = part_muted(p), solo = part_solo(p), audible = 0, level = 0;
        for (unsigned i = 0; i < n; ++i) { audible |= fr.e[list[i]].audible; if (fr.e[list[i]].level > level) level = fr.e[list[i]].level; }
        uint16_t col = !muted && !audible ? C_MU : part_color(p);   /* silenced by another part's solo */
        char ch[6], *o = put_s(ch, "CH"); put_u(o, fr.live.parts.part[p].channel);
        px_text_c(x + 13, 66, ch, 1, sel ? C_AC : C_TX);
        int fill = 88 * (int)fr.live.parts.part[p].level / 127, meter = muted || !audible ? 0 : 88 * (int)level / 127;
        px_frame(x + 3, 76, 12, 90, C_LN); px_rect(x + 4, 165 - fill, 10, fill, col);
        px_frame(x + 18, 76, 6, 90, C_LN); px_rect(x + 19, 165 - meter, 4, meter, level >= 100 ? C_AM : C_GR);
        int lo, hi, pan = part_pan(p, &lo, &hi), cx = x + 14;        /* the pan track: the whole part, with its spread when its layers differ */
        px_rect(x + 3, 170, 22, 1, C_LN); px_rect(cx, 168, 1, 5, C_LN);
        if (hi > lo) px_rect(cx + lo * 11 / 50, 170, (hi - lo) * 11 / 50 + 1, 1, col);
        px_rect(cx + pan * 11 / 50 - 1, 167, 3, 7, col);
        if (muted) px_rect(x + 2, 176, 11, 11, C_RD); else px_frame(x + 2, 176, 11, 11, ui.mixkeys ? C_AC : C_LN);
        px_text_c(x + 7, 178, "M", 1, muted ? C_BLACK : C_TX);
        if (solo) px_rect(x + 14, 176, 11, 11, C_AM); else px_frame(x + 14, 176, 11, 11, ui.mixkeys ? C_AC : C_LN);
        px_text_c(x + 19, 178, "S", 1, solo ? C_BLACK : C_TX);
        if (n > 1) { char d[3]; put_u(d, n); px_text_c(x + 13, 190, d, 1, C_TX); }
    }
    if (part_used(sel_part())) {                    /* the selected part in words: what the knobs are changing */
        unsigned p = sel_part();
        int pan = part_pan(p, NULL, NULL);
        char *o = put_s(tb, "PART "); o = put_u(o, p + 1U); o = put_s(o, "  CH "); o = put_u(o, fr.live.parts.part[p].channel);
        o = put_s(o, "  LEVEL "); o = put_u(o, fr.live.parts.part[p].level); o = put_s(o, "  PAN "); put_pan(o, pan);
        px_text(4, 206, tb, 1, C_TX);
    } else px_text(4, 206, "EMPTY PART  ENV ADDS A SOUND", 1, C_TX);
    draw_hint(ui.mixkeys ? "KEYS MUTE/SOLO  SELECT PART  EDIT = PLAY" : "K1 LVL K2 PAN K3 REV K4 DLY ALGO CH");
}
/* The value of effect knob i (decay, delay time, feedback, return) as the tiles and the overlay write it. */
static void fx_text(unsigned i, const uint8_t v[4], char *o)
{
    if (i != 1) { put_u(o, v[i]); return; }
    if (v[1] >= GROOVE_DELAY_SYNC && v[1] <= GROOVE_DELAY_LAST) {
        static const char *const sync[9] = {"1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/8D", "1/4"};
        put_s(o, sync[v[1] - GROOVE_DELAY_SYNC]);
    } else { unsigned ms = v[1] < 128 ? v[1] * 3U : v[1] <= 201 ? 360U + (v[1] - 137U) * 10U : 1020U + (v[1] - 202U) * 20U; put_s(put_u(o, ms), "MS"); }
}
static void draw_fx_tabs(unsigned page)
{
    static const char *const names[2] = {"FX", "FILTER"};
    draw_tabs(236 - tabs_w(names, 2), 29, names, 2, page);
}
static void draw_fx(void)
{
    draw_head();
    draw_title("FX", NULL);
    draw_fx_tabs(0);
    px_text(40, 46, "REV SEND", 1, C_TX); px_text(136, 46, "DLY SEND", 1, C_TX);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        int y = 56 + 17 * (int)e;
        const struct dx_engine_cfg *c = &fr.cfg.e[e];
        if ((int)e == sel_engine()) px_rect(0, y - 1, 240, 16, C_SEL);
        engine_chip(e, 4, y, 16, 14, (int)e == sel_engine());
        bar(40, y + 2, 80, 10, c->mute || fr.live.parts.layer[e].owner == DX_NO_PART ? C_MU : RGB(0x8F, 0xB4, 0xFF), fr.e[e].rev, 15);
        put_u(tb, fr.e[e].rev); px_text(123, y + 3, tb, 1, C_TX);
        bar(136, y + 2, 80, 10, c->mute || fr.live.parts.layer[e].owner == DX_NO_PART ? C_MU : RGB(0xFF, 0xB8, 0x6B), fr.e[e].dly, 15);
        put_u(tb, fr.e[e].dly); px_text(224, y + 3, tb, 1, C_TX);
    }
    uint8_t v[4];
    groove_effects_get(v);
    static const char *const labels[4] = {"DECAY", "DLY TIME", "FEEDBK", "RETURN"};
    for (unsigned i = 0; i < 4; ++i) { fx_text(i, v, tb); tile(i, labels[i], tb, C_TX); }
    draw_hint("OCT OR FX AGAIN = MASTER FILTER");
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
    draw_title("MASTER", NULL);
    draw_fx_tabs(1);
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
    px_text(8, 146, "USB OUT TAP", 1, C_TX); px_text(8, 158, "AFTER FILTER, BEFORE VOLUME", 1, C_GR);
    px_text(8, 174, "BT AND USB IN", 1, C_TX); px_text(8, 186, "FOLLOW THE KNOB", 1, C_TX);
    static const char *const types[4] = {"OFF", "LP", "HP", "BP"};
    tile(0, "TYPE", types[l->filter_type & 3U], C_TX);
    unsigned hz = l->filter_hz ? l->filter_hz : 1000U;
    if (hz >= 1000) { char *o = put_u(tb, hz / 1000U); *o++ = '.'; *o++ = (char)('0' + (hz / 100U) % 10U); *o++ = 'K'; *o = 0; } else put_u(tb, hz);
    tile(1, "CUTOFF", tb, C_AM);
    tile_u(2, "RES", l->filter_res, C_TX);
    tile(3, "USB OUT", "PRE VOL", C_GR);
    draw_hint("OCT OR FX AGAIN = BACK TO FX PAGE");
}

/* ---- scenes ------------------------------------------------------------------ */

static void draw_scenes(void)
{
    unsigned bank = ui.scene / DX_SCENES_PER_BANK;
    draw_head();
    char *o;
    static const char *const banks[DX_SCENES / DX_SCENES_PER_BANK] = {"A", "B", "C", "D"};
    draw_title("SCENES", NULL);
    px_text(116, 33, "BANK", 1, C_TX);
    draw_tabs(236 - tabs_w(banks, DX_SCENES / DX_SCENES_PER_BANK), 29, banks, DX_SCENES / DX_SCENES_PER_BANK, bank);
    for (unsigned s = 0; s < 16; ++s) {
        unsigned idx = bank * 16U + s;
        int x = 4 + 58 * (int)(s % 4), y = 48 + 38 * (int)(s / 4);
        unsigned has = scene_exists(idx), cur = idx == fr.current_scene, sel = idx == ui.scene;
        if (cur) { px_rect(x, y, 56, 34, RGB(0x00, 0x3C, 0x44)); px_frame(x, y, 56, 34, C_AC); px_frame(x + 1, y + 1, 54, 32, C_AC); }
        else { if (sel) px_rect(x, y, 56, 34, C_SEL); px_frame(x, y, 56, 34, has ? C_LN : RGB(0x3C, 0x3C, 0x3C)); }
        if (sel) px_rect(x, y, 2, 34, C_TX);                    /* the cursor: a white bar at the left edge */
        put_2(tb, idx + 1U); px_text(x + 4, y + 4, tb, 2, has ? (cur ? C_AC : C_TX) : RGB(0x90, 0x90, 0x90));
        if (has) {
            char name[13]; trim_name(name, fr.scene_name[idx], 12); name[8] = 0;
            px_text(x + 4, y + 23, name, 1, C_TX);
            for (unsigned j = 0; j < 8; ++j) px_rect(x + 30 + (int)(j % 4) * 6, y + 5 + (int)(j / 4) * 6, 4, 4, (fr.scene_parts[idx] >> j) & 1U ? EC[j] : RGB(0x2C, 0x2C, 0x2C));
        } else px_text(x + 4, y + 23, "--", 1, RGB(0x90, 0x90, 0x90));
    }
    if (ui.mode == M_NAME) {
        px_rect(10, 90, 220, 60, C_BLACK); px_frame(10, 90, 220, 60, C_AC);
        o = put_s(tb, "SAVE AS SCENE "); put_u(o, ui.scene + 1U);
        px_text(18, 96, tb, 1, C_TX);
        for (unsigned i = 0; i < 12; ++i) {
            char ch[2] = {ui.name[i], 0};
            px_text(18 + 17 * (int)i, 112, ch, 2, i == ui.name_pos ? C_BLACK : C_TX);
            if (i == ui.name_pos) { px_rect(17 + 17 * (int)i, 110, 14, 18, C_AM); px_text(18 + 17 * (int)i, 112, ch, 2, C_BLACK); }
        }
        px_text(18, 136, "K1 CURSOR  K2 LETTER  SAVE = STORE", 1, C_TX);
    } else if (ui.mode == M_CLAIM) {
        px_rect(10, 78, 220, 84, C_BLACK); px_frame(10, 78, 220, 84, C_AM);
        px_text(18, 84, "FIRST SAVE: FLASH SETUP", 1, C_AM);
        px_text(18, 100, "SCENES NEED FLASH THAT HOLDS STOCK", 1, C_TX);
        px_text(18, 111, "DATA (SAMPLE BANK OR UNSAVED STOCK", 1, C_TX);
        px_text(18, 122, "SAVE). IT WILL BE ERASED.", 1, C_TX);
        px_text(18, 132, "DX747 CANNOT BACK IT UP.", 1, C_TX);
        px_text(18, 142, "SAVE = ERASE AND CONTINUE", 1, C_AM);
        px_text(18, 152, "HOME = CANCEL", 1, C_TX);
    }
    draw_hint(ui.mode == M_BROWSE ? "KEYS RECALL  OCT BANK  SAVE STORES" : "HOME = CANCEL");
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
        px_text(4, y, labels[i], 1, ui.sys_row == i ? C_AC : C_TX);
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
    case 1: case 2: draw_hint("SELECT OR OCT = ROW  K1 = CHANGE"); break;
    default: draw_hint("SELECT OR OCT = ROW"); break;
    }
}

/* ---- the overlay ------------------------------------------------------------------ */

/* The box shows a label (what, in small capitals) and a value (large); a message is one to three lines of text. A kind's
 * words are built here from the snapshot, so they are always what the screen shows. */
struct ov { char label[20], value[20]; uint16_t color; uint8_t bar; int16_t num, den; }; /* bar: 0 none, 1 filled num/den, 2 centred -50..50 */
static void ov_layer_label(struct ov *o, unsigned e, const char *what)
{
    char *l = put_s(o->label, "LAYER "); l = put_u(l, layer_index(e) + 1U); *l++ = ' '; put_s(l, what);
}
static void ov_part_label(struct ov *o, unsigned p, const char *what)
{
    char *l = put_s(o->label, "PART "); l = put_u(l, p + 1U); *l++ = ' '; put_s(l, what);
}
static unsigned ov_describe(struct ov *o)
{
    unsigned k = ui.ov_kind, a = ui.ov_arg;
    char *v = o->value;
    o->label[0] = o->value[0] = 0; o->color = C_TX; o->bar = 0; o->num = 0; o->den = 1;
    if (k >= OV_LAYER && k <= OV_POLY && a < DX_ENGINES) {
        const struct dx_engine_cfg *c = &fr.cfg.e[a];
        unsigned owner = fr.live.parts.layer[a].owner;
        if (owner >= DX_PARTS) return 0;       /* the layer is gone (a program change or a scene took it): nothing to say */
        o->color = engine_color(a);
        switch (k) {
        case OV_LAYER: {
            uint8_t list[DX_ENGINES];
            unsigned n = owner < DX_PARTS ? part_layers(owner, list) : 0U;
            char *l = put_s(o->label, "PART "); l = put_u(l, owner + 1U); l = put_s(l, "  LAYER "); l = put_u(l, layer_index(a) + 1U); *l++ = '/'; put_u(l, n);
            trim_name(v, fr.e[a].name, 10); break; }
        case OV_SOUND: {
            char *l = put_s(o->label, "LAYER "); l = put_u(l, layer_index(a) + 1U); l = put_s(l, "  SOUND "); put_u(l, fr.e[a].program + 1U);
            trim_name(v, fr.e[a].name, 10); break; }
        case OV_LVL: ov_layer_label(o, a, "LEVEL"); put_u(v, fr.live.parts.layer[a].lvl); o->bar = 1; o->num = (int16_t)fr.live.parts.layer[a].lvl; o->den = 127; break;
        case OV_PAN: ov_layer_label(o, a, "PAN"); put_pan(v, fr.live.pan[a]); o->bar = 2; o->num = fr.live.pan[a]; break;
        case OV_BRIGHT: ov_layer_label(o, a, "BRIGHT"); if (fr.live.bright[a] > 0) *v++ = '+'; put_i(v, fr.live.bright[a]); o->bar = 2; o->num = fr.live.bright[a]; break;
        case OV_LO: ov_layer_label(o, a, "LOW KEY"); put_note(v, c->lo); o->bar = 1; o->num = c->lo; o->den = 127; break;
        case OV_HI: ov_layer_label(o, a, "HIGH KEY"); put_note(v, c->hi); o->bar = 1; o->num = c->hi; o->den = 127; break;
        case OV_SHIFT: ov_layer_label(o, a, "SHIFT"); v = put_shift(v, c->shift); if (c->shift && c->shift % 12 == 0) put_s(v, " OCT"); o->color = c->shift ? C_AM : C_TX; break;
        case OV_FOLD: ov_layer_label(o, a, "FOLD"); put_s(v, c->fold == DX_FOLD_WRAP ? "WRAP" : c->fold == DX_FOLD_CLIP ? "CLIP" : "DROP"); break;
        case OV_VLO: ov_layer_label(o, a, "LOW VEL"); put_u(v, c->vlo ? c->vlo : 1U); o->bar = 1; o->num = (int16_t)(c->vlo ? c->vlo : 1U); o->den = 127; break;
        case OV_VHI: ov_layer_label(o, a, "HIGH VEL"); put_u(v, c->vhi ? c->vhi : 127U); o->bar = 1; o->num = (int16_t)(c->vhi ? c->vhi : 127U); o->den = 127; break;
        case OV_REV: ov_layer_label(o, a, "REVERB"); put_u(v, fr.e[a].rev); o->bar = 1; o->num = fr.e[a].rev; o->den = 15; break;
        case OV_DLY: ov_layer_label(o, a, "DELAY"); put_u(v, fr.e[a].dly); o->bar = 1; o->num = fr.e[a].dly; o->den = 15; break;
        default: ov_layer_label(o, a, "MAX VOICES"); put_u(v, c->poly); o->bar = 1; o->num = c->poly; o->den = 8; break;
        }
        return 1;
    }
    if (k >= OV_PART && k <= OV_PMIX && (k == OV_PART || k >= OV_PLEVEL) && a < DX_PARTS) {
        unsigned used = part_used(a);
        o->color = part_color(a);
        switch (k) {
        case OV_PART: {
            char *l = put_s(o->label, "PART "); l = put_u(l, a + 1U);
            if (used) { l = put_s(l, "  CH "); put_u(l, fr.live.parts.part[a].channel); trim_name(v, fr.e[part_main(a)].name, 10); }
            else { put_s(v, "EMPTY"); o->color = C_DIM; }
            break; }
        case OV_PLEVEL: ov_part_label(o, a, "LEVEL"); put_u(v, fr.live.parts.part[a].level); o->bar = 1; o->num = fr.live.parts.part[a].level; o->den = 127; break;
        case OV_PCH: ov_part_label(o, a, "CHANNEL"); put_u(v, fr.live.parts.part[a].channel); break;
        case OV_PPAN: ov_part_label(o, a, "PAN"); put_pan(v, part_pan(a, NULL, NULL)); o->bar = 2; o->num = (int16_t)part_pan(a, NULL, NULL); break;
        case OV_PREV: ov_part_label(o, a, "REVERB"); put_u(v, part_send(a, 0)); o->bar = 1; o->num = (int16_t)part_send(a, 0); o->den = 15; break;
        case OV_PDLY: ov_part_label(o, a, "DELAY"); put_u(v, part_send(a, 1)); o->bar = 1; o->num = (int16_t)part_send(a, 1); o->den = 15; break;
        default: /* OV_PMIX */
            ov_part_label(o, a, "MIX");
        {
            uint8_t list[DX_ENGINES];
            unsigned n = part_layers(a, list), audible = 0;
            for (unsigned i = 0; i < n; ++i) audible |= fr.e[list[i]].audible;
            put_s(v, part_solo(a) ? "SOLO" : part_muted(a) ? "MUTED" : audible || !n ? "ON" : "SILENCED");
            o->color = part_solo(a) ? C_AM : part_muted(a) ? C_RD : audible || !n ? C_GR : C_DIM;
        }
        break;
        }
        return 1;
    }
    switch (k) {
    case OV_FX: {
        static const char *const names[4] = {"REVERB DECAY", "DELAY TIME", "DELAY FEEDBACK", "FX RETURN"};
        uint8_t f[4];
        groove_effects_get(f);
        put_s(o->label, names[a & 3U]); fx_text(a & 3U, f, v); break; }
    case OV_FTYPE: { static const char *const types[4] = {"OFF", "LOW PASS", "HIGH PASS", "BAND PASS"}; put_s(o->label, "FILTER TYPE"); put_s(v, types[fr.live.filter_type & 3U]); break; }
    case OV_FHZ: {
        unsigned hz = fr.live.filter_hz ? fr.live.filter_hz : 1000U;
        put_s(o->label, "FILTER CUTOFF");
        if (hz >= 1000) { v = put_u(v, hz / 1000U); *v++ = '.'; *v++ = (char)('0' + (hz / 100U) % 10U); put_s(v, " KHZ"); } else put_s(put_u(v, hz), " HZ");
        o->color = C_AM; break; }
    case OV_FRES: put_s(o->label, "FILTER RESONANCE"); put_u(v, fr.live.filter_res); o->bar = 1; o->num = fr.live.filter_res; o->den = 127; break;
    case OV_SCENE: {
        char *l = put_s(o->label, "SCENE "); put_u(l, a + 1U);
        if (scene_exists(a)) { trim_name(v, fr.scene_name[a], 12); if (a == fr.current_scene) o->color = C_AC; } else { put_s(v, "EMPTY"); o->color = C_DIM; }
        break; }
    case OV_OCT: put_s(o->label, "KEY OCTAVE"); if (ui.octave > 0) *v++ = '+'; put_i(v, ui.octave); break;
    case OV_SYS: {
        const struct dx_settings *s = &fr.settings;
        switch (a) {
        case 1: put_s(o->label, "BT AUDIO IN"); put_s(v, (s->radio_off & GROOVE_RADIO_AUDIO) ? "OFF" : "ON"); break;
        case 2: put_s(o->label, "BUTTON LIGHT"); put_u(v, s->backlight); o->bar = 1; o->num = s->backlight; o->den = 8; o->color = C_AM; break;
        case 3: put_s(o->label, "SCENE CHANNEL"); if (s->scene_ch) put_u(put_s(v, "CH "), s->scene_ch); else put_s(v, "OFF"); break;
        default: put_s(o->label, "MASTER CHANNEL"); if (s->master_ch) put_u(put_s(v, "CH "), s->master_ch); else put_s(v, "OFF"); break;
        }
        break; }
    default: break;
    }
    return 1;
}

/* Word wrap of a message into at most three lines of up to 18 letters; a newline breaks the line where the words belong together. */
static unsigned ov_wrap(const char *s, char lines[3][20])
{
    unsigned n = 0, len = 0;
    lines[0][0] = 0;
    while (*s) {
        if (*s == '\n') { if (n == 2) break; lines[++n][0] = 0; len = 0; ++s; continue; }
        const char *w = s;
        unsigned wl = 0;
        while (s[wl] && s[wl] != ' ' && s[wl] != '\n') ++wl;
        if (len && len + 1U + wl > 18U) { if (n == 2) break; lines[++n][0] = 0; len = 0; }
        if (len) lines[n][len++] = ' ';
        for (unsigned i = 0; i < wl && len < 18U; ++i) lines[n][len++] = w[i];
        lines[n][len] = 0;
        s += wl;
        while (*s == ' ') ++s;
    }
    return n + 1U;
}
/* The sound overlay (PRESETS): the current program and the two before and two after it, in the middle of the screen. */
static void draw_sound_list(void)
{
    unsigned a = ui.ov_arg;
    if (a >= DX_ENGINES || fr.live.parts.layer[a].owner >= DX_PARTS) return;
    const int x = 8, w = 224, y = 57, h = 126;
    px_rect(x, y, w, h, C_BLACK); px_frame(x, y, w, h, C_AC); px_frame(x + 1, y + 1, w - 2, h - 2, C_AC);
    char *l = put_s(tb, "LAYER "); l = put_u(l, layer_index(a) + 1U); put_s(l, " SOUND");
    px_text(x + 10, y + 7, tb, 1, C_TX);
    l = put_u(tb, fr.pprog + 1U); put_s(l, " / 128");
    px_text_r(x + w - 10, y + 7, tb, 1, C_TX);
    for (int k = 0; k < 5; ++k) {
        int ry = y + 20 + 20 * k;
        char num[4], name[11];
        put_u(num, (fr.pprog + FM1_FM_PROGRAMS + (unsigned)k - 2U) % FM1_FM_PROGRAMS + 1U);
        trim_name(name, fr.pname[k], 10);
        if (k == 2) { px_rect(x + 4, ry, w - 8, 20, C_SEL); px_rect(x + 4, ry, 2, 20, C_TX); }
        uint16_t tc = k == 2 ? C_TX : k == 1 || k == 3 ? RGB(0xC0, 0xC0, 0xC0) : RGB(0x88, 0x88, 0x88);
        px_text_r(x + 50, ry + 7, num, 1, tc);
        px_text(x + 60, ry + 3, name, 2, tc);
    }
}
static void draw_overlay(void)
{
    if (!fr.ov_on || ui.mode != M_BROWSE) return;
    if (ui.ov_kind == OV_SOUND) { draw_sound_list(); return; }
    const int x = 0, w = 240;
    if (ui.ov_kind == OV_TEXT) {
        char lines[3][20];
        unsigned n = ov_wrap(ui.notice ? ui.notice : "", lines);
        int h = 14 + 16 * (int)n, y = 237 - h;
        px_rect(x, y, w, h, C_BLACK); px_frame(x, y, w, h, C_AM); px_frame(x + 1, y + 1, w - 2, h - 2, C_AM);
        for (unsigned i = 0; i < n; ++i) px_text_c(120, y + 8 + 16 * (int)i, lines[i], 2, C_AM);
        return;
    }
    struct ov o;
    if (!ov_describe(&o)) return;
    int h = 55, y = 237 - h;
    px_rect(x, y, w, h, C_BLACK); px_frame(x, y, w, h, C_AC); px_frame(x + 1, y + 1, w - 2, h - 2, C_AC);
    px_text(x + 9, y + 7, o.label, 2, C_TX);
    px_text(x + 9, y + 27, o.value, 3, o.color);
    int bx = x + 9 + (int)text_w(o.value, 3) + 14, bw = x + w - 10 - bx;
    if (o.bar && bw >= 40) {
        px_frame(bx, y + 30, bw, 15, C_LN);
        if (o.bar == 1) px_rect(bx + 1, y + 31, (bw - 2) * (o.num < 0 ? 0 : o.num > o.den ? o.den : o.num) / (o.den ? o.den : 1), 13, o.color);
        else { int mid = bx + bw / 2, d = (bw / 2 - 2) * o.num / 50; px_rect(mid, y + 31, 1, 13, C_DIM); px_rect(mid + d - 2, y + 32, 5, 11, o.color); }
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
    case S_DETAIL: draw_detail(); break;
    default: draw_sys(); break;
    }
    draw_overlay();
}
