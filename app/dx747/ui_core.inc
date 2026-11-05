/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 screen core: palette, 5x7 font and a band renderer. Included by
 * app/screen.c only. The 240x240 LCD is drawn in 16-row bands: each band is
 * cleared, every primitive clips itself to it, the finished band is hashed and
 * sent to the panel only when its hash changed, so an unchanged screen costs
 * no SPI traffic. Two band buffers let the next band be drawn while the DMA of
 * the previous one runs. Text at scale 1 is the 5x7 pixel font with softened corners; at larger scales it is a stroke
 * font (vfont.inc) drawn at the screen's own resolution with soft edges, not the bitmap magnified. */

#define RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BG 0x0000
#define C_TX 0xFFFF
#define C_DIM RGB(0xAA, 0xAA, 0xAA)  /* 9.0:1 on black */
#define C_LN RGB(0x6A, 0x6A, 0x6A)   /* outlines, 3.5:1 */
#define C_GD RGB(0x26, 0x26, 0x26)
#define C_SEL RGB(0x30, 0x30, 0x34)  /* the selected row's background: its marker is a white bar at the left edge */
#define C_MU RGB(0x5B, 0x5B, 0x5B)
#define C_AC RGB(0x00, 0xE5, 0xFF)
#define C_DXG RGB(0x04, 0xDF, 0xBF)  /* the green of the DX7's buttons (boot screen) */
#define C_AM RGB(0xFF, 0xB0, 0x00)
#define C_GR RGB(0x44, 0xFF, 0x6E)
#define C_RD RGB(0xFF, 0x4D, 0x4D)
#define C_BLACK 0x0000
static const uint16_t EC[8] = {
    RGB(0xFF, 0x5A, 0x5A), RGB(0xFF, 0xA2, 0x3A), RGB(0xFF, 0xE3, 0x4A), RGB(0x5C, 0xF0, 0x6A),
    RGB(0x2D, 0xE0, 0xD0), RGB(0x5A, 0xA9, 0xFF), RGB(0xB6, 0x8C, 0xFF), RGB(0xFF, 0x7A, 0xD2)};

/* Five columns per glyph, bit 0 at the top. Digits followed by uppercase. */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punctuation[][6] = {
    {45,8,8,8,8,8}, {47,0x60,0x18,6,1,0}, {43,8,8,0x3e,8,8},
    {35,0x14,0x7f,0x14,0x7f,0x14}, {37,0x63,0x13,8,0x64,0x63},
    {62,0x41,0x22,0x14,8,0}, {63,2,1,0x51,9,6},
    {33,0,0,0x5f,0,0}, {40,0,0x1c,0x22,0x41,0}, {41,0,0x41,0x22,0x1c,0},
    {44,0,0x50,0x30,0,0}, {46,0,0x60,0x60,0,0}, {58,0,0x36,0x36,0,0},
    {60,8,0x14,0x22,0x41,0}, {61,0x14,0x14,0x14,0x14,0x14},
    {95,0x40,0x40,0x40,0x40,0x40}, {124,0,0,0x7f,0,0}
};

enum { BAND_H = 16, BAND_N = FM1_SCREEN_HEIGHT / BAND_H, BAND_W = FM1_SCREEN_WIDTH };
_Static_assert(BAND_W * BAND_H <= FM1_SCREEN_DMA_PIXELS, "LCD band capacity");
static uint16_t band_px[2][BAND_W * BAND_H];
static unsigned band_buf, band_top;
static uint32_t band_sig[BAND_N];
static uint8_t band_known[BAND_N];

static void px_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, x1 = x + w > BAND_W ? BAND_W : x + w;
    int top = (int)band_top, y0 = y < top ? top : y, y1 = y + h > top + BAND_H ? top + BAND_H : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    uint16_t *row = band_px[band_buf] + (y0 - top) * BAND_W;
    for (int r = y0; r < y1; ++r, row += BAND_W)
        for (int c = x0; c < x1; ++c) row[c] = color;
}
static void px_frame(int x, int y, int w, int h, uint16_t color)
{
    px_rect(x, y, w, 1, color); px_rect(x, y + h - 1, w, 1, color);
    px_rect(x, y, 1, h, color); px_rect(x + w - 1, y, 1, h, color);
}
/* Dotted frame, for "not routed yet". */
static void px_dotted(int x, int y, int w, int h, uint16_t color)
{
    for (int i = 0; i < w; i += 4) { px_rect(x + i, y, 2, 1, color); px_rect(x + i, y + h - 1, 2, 1, color); }
    for (int i = 0; i < h; i += 4) { px_rect(x, y + i, 1, 2, color); px_rect(x + w - 1, y + i, 1, 2, color); }
}
static unsigned text_w(const char *s, unsigned scale)
{
    unsigned n = 0;
    while (s[n]) ++n;
    return n ? n * 6U * scale - scale : 0;
}
#include "vfont.inc"
/* Blend colour over a pixel of the band being drawn; alpha 0..255. */
static void px_blend(int x, int y, uint16_t color, unsigned alpha)
{
    int row = y - (int)band_top;
    if (x < 0 || x >= BAND_W || row < 0 || row >= BAND_H || !alpha) return;
    uint16_t *p = &band_px[band_buf][row * BAND_W + x];
    uint32_t a = (alpha + 4U) >> 3;                       /* 0..32 */
    uint32_t pv = *p, cv = color;                          /* widen before shifting: a uint16_t promotes to a signed int */
    uint32_t bg = (pv | (pv << 16)) & 0x07E0F81FU, fg = (cv | (cv << 16)) & 0x07E0F81FU;
    uint32_t res = ((((fg - bg) * a) >> 5) + bg) & 0x07E0F81FU;
    *p = (uint16_t)(res | (res >> 16));
}
static unsigned isqrt32(unsigned v)
{
    unsigned r = 0, b = 1U << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
enum { VF_MAX_SCALE = 6 };
static uint8_t vf_mask[(5 * VF_MAX_SCALE + 2) * BAND_H];
static const uint8_t *vf_glyph(unsigned char c)
{
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    for (unsigned i = 0; vf_order[i]; ++i) if ((unsigned char)vf_order[i] == c) return vf_data + vf_off[i];
    return NULL;
}
/* One glyph of the stroke font in the box 5*scale x 7*scale at x, y, clipped to the band. Every stroke segment is a capsule
 * (a line with round ends) of a fixed width; a pixel's coverage falls from 1 to 0 across one pixel at the capsule's edge.
 * Coordinates are in sixteenths of a pixel. The coverage of the whole glyph is collected first (the largest of the
 * strokes' at each pixel, so joints do not double) and then blended over what is there. */
static void px_vglyph(int x, int y, const uint8_t *g, unsigned scale, uint16_t color)
{
    const int top = (int)band_top, bw = 5 * (int)scale, bh = 7 * (int)scale;
    int r0 = y - 1 < top ? top : y - 1, r1 = y + bh + 1 > top + BAND_H ? top + BAND_H : y + bh + 1;
    int c0 = x - 1 < 0 ? 0 : x - 1, c1 = x + bw + 1 > BAND_W ? BAND_W : x + bw + 1;
    if (r0 >= r1 || c0 >= c1) return;
    const int mw = c1 - c0, mh = r1 - r0;
    memset(vf_mask, 0, (size_t)(mw * mh));
    const int hw = (int)scale * 7 + 3;                    /* half the stroke width in sixteenths of a pixel (the stroke is (14 * scale + 6) / 16 pixels) */
    const int outer = hw + 8, inner = hw - 8;             /* coverage 0 beyond outer, 1 within inner */
    const int r2 = inner * inner, o2 = outer * outer;
    const int rcp = (255 << 16) / (o2 - r2);
    const int wq = bw * 16 - 2 * hw, hq = bh * 16 - 2 * hw;
    int ax = 0, ay = 0, have = 0;
    for (const uint8_t *p = g; *p != 0xFE; ++p) {
        if (*p == 0xFF) { have = 0; continue; }
        int bx = hw + (*p >> 4) * wq / 8, by = hw + (*p & 15) * hq / 12;
        if (have) {
            int dx = bx - ax, dy = by - ay, len = (int)isqrt32((unsigned)(dx * dx + dy * dy)), ux = 0, uy = 0;
            if (len) { ux = dx * 16384 / len; uy = dy * 16384 / len; }
            int minx = (ax < bx ? ax : bx) - outer, maxx = (ax > bx ? ax : bx) + outer;
            int miny = (ay < by ? ay : by) - outer, maxy = (ay > by ? ay : by) + outer;
            int pc0 = (minx >> 4) + x, pc1 = (maxx >> 4) + x + 1, pr0 = (miny >> 4) + y, pr1 = (maxy >> 4) + y + 1;
            if (pc0 < c0) pc0 = c0;
            if (pc1 > c1) pc1 = c1;
            if (pr0 < r0) pr0 = r0;
            if (pr1 > r1) pr1 = r1;
            for (int row = pr0; row < pr1; ++row) {
                int vy = (row - y) * 16 + 8 - ay;
                uint8_t *m = vf_mask + (row - r0) * mw - c0;
                for (int col = pc0; col < pc1; ++col) {
                    int vx = (col - x) * 16 + 8 - ax, along = (vx * ux + vy * uy) >> 14, perp = (vy * ux - vx * uy) >> 14;
                    int e = along < 0 ? -along : along > len ? along - len : 0;
                    int d2 = perp * perp + e * e;
                    if (d2 >= o2) continue;
                    unsigned a = d2 <= r2 ? 255U : (unsigned)(((o2 - d2) * rcp) >> 16);
                    if (a > m[col]) m[col] = (uint8_t)a;
                }
            }
        }
        ax = bx; ay = by; have = 1;
    }
    for (int row = r0; row < r1; ++row)
        for (int col = c0; col < c1; ++col) px_blend(col, row, color, vf_mask[(row - r0) * mw + (col - c0)]);
}
/* The smallest text keeps its 5x7 pixel design but its edges are softened: every source pixel is cut into 3x3 sub-pixels
 * with the Scale3x rule (a pixel at a corner or a staircase takes the colour of the neighbour that continues the line) and
 * its coverage is the share of lit sub-pixels. Straight strokes stay exactly as bright as before; only corners and
 * diagonals get partial coverage, in the pixel's own cell. Built once per character, kept in a small table. */
static uint8_t sg_alpha[96][35];
static uint8_t sg_ready[96 / 8];
static const uint8_t *sg_glyph(char letter, const uint8_t shape[5])
{
    unsigned code = (unsigned char)letter, slot = code >= 32 && code < 128 ? code - 32U : 0U;
    uint8_t *out = sg_alpha[slot];
    if ((sg_ready[slot / 8U] >> (slot % 8U)) & 1U) return out;
#define SG_LIT(c, r) ((c) >= 0 && (c) < 5 && (r) >= 0 && (r) < 7 && ((shape[c] >> (r)) & 1))
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 5; ++c) {
            int A = SG_LIT(c - 1, r - 1), B = SG_LIT(c, r - 1), C = SG_LIT(c + 1, r - 1);
            int D = SG_LIT(c - 1, r), E = SG_LIT(c, r), F = SG_LIT(c + 1, r);
            int G = SG_LIT(c - 1, r + 1), H = SG_LIT(c, r + 1), I = SG_LIT(c + 1, r + 1);
            unsigned lit = E ? 9U : 0U;
            if (B != H && D != F) {
                lit = (unsigned)((D == B ? D : E) + ((D == B && E != C) || (B == F && E != A) ? B : E) + (B == F ? F : E) +
                                 ((D == B && E != G) || (D == H && E != A) ? D : E) + E +
                                 ((B == F && E != I) || (H == F && E != C) ? F : E) + (D == H ? D : E) +
                                 ((D == H && E != I) || (H == F && E != G) ? H : E) + (H == F ? F : E));
            }
            out[r * 5 + c] = (uint8_t)(lit * 255U / 9U);
        }
#undef SG_LIT
    sg_ready[slot / 8U] = (uint8_t)(sg_ready[slot / 8U] | (1U << (slot % 8U)));
    return out;
}
/* Transparent text. Scale 1 is the 5x7 font with softened corners; larger scales draw the stroke font. Lower case is
 * upper-cased. */
static void px_text(int x, int y, const char *s, unsigned scale, uint16_t color)
{
    int top = (int)band_top;
    if (!s || !scale || y >= top + BAND_H || y + (int)(7 * scale) <= top) return;
    for (; *s; ++s, x += (int)(6 * scale)) {
        if (x >= BAND_W) break;
        if (x + (int)(5 * scale) <= 0) continue;
        if (scale >= 2 && scale <= VF_MAX_SCALE) {
            const uint8_t *g = vf_glyph((unsigned char)*s);
            if (g) px_vglyph(x, y, g, scale, color);
            continue;
        }
        char letter = *s >= 'a' && *s <= 'z' ? (char)(*s - 'a' + 'A') : *s;
        static const uint8_t blank[5] = {0};
        const uint8_t *shape = blank;
        if (letter >= '0' && letter <= '9') shape = font[letter - '0'];
        else if (letter >= 'A' && letter <= 'Z') shape = font[10 + letter - 'A'];
        else if (letter != ' ')
            for (unsigned i = 0; i < sizeof punctuation / sizeof punctuation[0]; ++i)
                if ((uint8_t)letter == punctuation[i][0]) { shape = punctuation[i] + 1; break; }
        if (shape == blank) continue;
        if (scale == 1) {                                  /* the 5x7 font with softened corners (sg_glyph) */
            const uint8_t *alpha = sg_glyph(letter, shape);
            for (int r = 0; r < 7; ++r)
                if (y + r >= top && y + r < top + BAND_H)
                    for (int c = 0; c < 5; ++c) if (alpha[r * 5 + c]) px_blend(x + c, y + r, color, alpha[r * 5 + c]);
            continue;
        }
        for (unsigned col = 0; col < 5; ++col) {
            uint8_t bits = shape[col];
            for (unsigned row = 0; bits; ++row, bits >>= 1)
                if (bits & 1U) px_rect(x + (int)(col * scale), y + (int)(row * scale), (int)scale, (int)scale, color);
        }
    }
}
static void px_text_c(int cx, int y, const char *s, unsigned scale, uint16_t color)
{ px_text(cx - (int)(text_w(s, scale) / 2), y, s, scale, color); }
static void px_text_r(int right, int y, const char *s, unsigned scale, uint16_t color)
{ px_text(right - (int)text_w(s, scale), y, s, scale, color); }

/* Filled polygon, scanline by scanline within the current band. With stripe_period set only the rows (y - stripe_y0) %
 * stripe_period < stripe_on are drawn: the lines of the DX7 logo. */
static unsigned stripe_period, stripe_on;
static int stripe_y0;
static void px_poly(const int16_t (*p)[2], unsigned n, uint16_t color)
{
    int y0 = (int)band_top, y1 = y0 + BAND_H;
    for (int y = y0; y < y1; ++y) {
        if (stripe_period && (unsigned)(y - stripe_y0) % stripe_period >= stripe_on) continue;
        int xs[16], count = 0;
        for (unsigned i = 0; i < n && count < 16; ++i) {
            int ax = p[i][0], ay = p[i][1], bx = p[(i + 1) % n][0], by = p[(i + 1) % n][1];
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                int step = (y - ay) * 2 + 1; /* sample the pixel centre */
                xs[count++] = ax + ((bx - ax) * step + (by - ay)) / (2 * (by - ay)) ;
            }
        }
        for (int i = 1; i < count; ++i) for (int j = i; j > 0 && xs[j] < xs[j - 1]; --j) { int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (int i = 0; i + 1 < count; i += 2) px_rect(xs[i], y, xs[i + 1] - xs[i], 1, color);
    }
}
static void px_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        px_rect(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

/* Squared distance, in sixteenths of a pixel squared, from the point (px, py) to the segment (ax, ay)-(bx, by); bx > ax. */
static int seg_dist2(int ax, int ay, int bx, int by, int px, int py)
{
    int dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy, t = (px - ax) * dx + (py - ay) * dy;
    if (t <= 0) return (px - ax) * (px - ax) + (py - ay) * (py - ay);
    if (t >= len2) return (px - bx) * (px - bx) + (py - by) * (py - by);
    int cross = (px - ax) * dy - (py - ay) * dx;
    return cross * cross / len2;
}
/* An anti-aliased curve through the points (x0 + i, y16[i]), i < n, about 1.25 pixels wide, drawn only on rows top..bottom. y is in
 * sixteenths of a pixel with the centre of row r at 16 * r + 8. A pixel's coverage falls from 1 to 0 across one pixel of its distance to
 * the nearest of the two segments that meet in its column. */
static void px_curve(const int16_t *y16, unsigned n, int x0, int top, int bottom, uint16_t color)
{
    enum { HALF = 10, REACH = HALF + 8 };                  /* half the line width, and how far a pixel can still be touched (1/16 px) */
    int r_min = top > (int)band_top ? top : (int)band_top, r_max = bottom < (int)band_top + BAND_H - 1 ? bottom : (int)band_top + BAND_H - 1;
    for (unsigned i = 0; i < n; ++i) {
        int cx = (x0 + (int)i) * 16 + 8, ya = y16[i ? i - 1 : 0], yb = y16[i], yc = y16[i + 1 < n ? i + 1 : n - 1];
        int lo = ya < yb ? ya : yb, hi = ya > yb ? ya : yb;
        if (yc < lo) lo = yc;
        if (yc > hi) hi = yc;
        int r0 = (lo - REACH - 8) / 16, r1 = (hi + REACH - 8) / 16 + 1;
        if (r0 < r_min) r0 = r_min;
        if (r1 > r_max) r1 = r_max;
        for (int r = r0; r <= r1; ++r) {
            int py = r * 16 + 8, d2a = seg_dist2(cx - 16, ya, cx, yb, cx, py), d2b = seg_dist2(cx, yb, cx + 16, yc, cx, py);
            int cov = REACH - (int)isqrt32((unsigned)(d2a < d2b ? d2a : d2b));
            if (cov > 16) cov = 16;
            if (cov > 0) px_blend(x0 + (int)i, r, color, (unsigned)cov * 255U / 16U);
        }
    }
}

/* Draws one frame, band by band, through the callback. */
static void band_begin(unsigned b)
{
    band_top = b * BAND_H;
    uint16_t *p = band_px[band_buf];
    for (unsigned i = 0; i < BAND_W * BAND_H; ++i) p[i] = C_BG;
}
static void band_end(unsigned b)
{
    uint32_t sig = 2166136261U;
    const uint8_t *p = (const uint8_t *)band_px[band_buf];
    for (unsigned i = 0; i < BAND_W * BAND_H * 2U; ++i) sig = (sig ^ p[i]) * 16777619U;
    if (band_known[b] && band_sig[b] == sig) return;
    uint32_t deadline = fm1_millis() + 25U;
    while (fm1_screen_pending() > 0 && (int32_t)(fm1_millis() - deadline) < 0) {}
    int r = fm1_screen_submit(0, b * BAND_H, BAND_W, BAND_H, band_px[band_buf], BAND_W * BAND_H);
    while (r == 1 && (int32_t)(fm1_millis() - deadline) < 0) {
        r = fm1_screen_submit(0, b * BAND_H, BAND_W, BAND_H, band_px[band_buf], BAND_W * BAND_H);
    }
    if (r == 0) { band_known[b] = 1; band_sig[b] = sig; band_buf ^= 1U; }
    else band_known[b] = 0; /* try this band again at the next frame */
}
static void band_invalidate(void) { memset(band_known, 0, sizeof band_known); }
