/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 screen core: palette, 5x7 font and a band renderer. Included by
 * app/screen.c only. The 240x240 LCD is drawn in 16-row bands: each band is
 * cleared, every primitive clips itself to it, the finished band is hashed and
 * sent to the panel only when its hash changed, so an unchanged screen costs
 * no SPI traffic. Two band buffers let the next band be drawn while the DMA of
 * the previous one runs. */

#define RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BG 0x0000
#define C_TX 0xFFFF
#define C_DIM RGB(0xAA, 0xAA, 0xAA)  /* 9.0:1 on black */
#define C_LN RGB(0x6A, 0x6A, 0x6A)   /* outlines, 3.5:1 */
#define C_GD RGB(0x26, 0x26, 0x26)
#define C_MU RGB(0x5B, 0x5B, 0x5B)
#define C_AC RGB(0x00, 0xE5, 0xFF)
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
/* Transparent text: only the glyph pixels are drawn. Lower case is upper-cased. */
static void px_text(int x, int y, const char *s, unsigned scale, uint16_t color)
{
    int top = (int)band_top;
    if (!s || !scale || y >= top + BAND_H || y + (int)(7 * scale) <= top) return;
    for (; *s; ++s, x += (int)(6 * scale)) {
        if (x >= BAND_W) break;
        if (x + (int)(5 * scale) <= 0) continue;
        char letter = *s >= 'a' && *s <= 'z' ? (char)(*s - 'a' + 'A') : *s;
        static const uint8_t blank[5] = {0};
        const uint8_t *shape = blank;
        if (letter >= '0' && letter <= '9') shape = font[letter - '0'];
        else if (letter >= 'A' && letter <= 'Z') shape = font[10 + letter - 'A'];
        else if (letter != ' ')
            for (unsigned i = 0; i < sizeof punctuation / sizeof punctuation[0]; ++i)
                if ((uint8_t)letter == punctuation[i][0]) { shape = punctuation[i] + 1; break; }
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

/* Filled polygon, scanline by scanline within the current band. */
static void px_poly(const int16_t (*p)[2], unsigned n, uint16_t color)
{
    int y0 = (int)band_top, y1 = y0 + BAND_H;
    for (int y = y0; y < y1; ++y) {
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
