/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Scripted driver for the native simulator build. Reads a script of simple
 * commands (see README below), drives the same sim_* entry points the browser
 * uses, and writes screenshots as PNG (scaled 2x).
 *
 *   boot [clear] [logo] restart the instrument; "clear" also restores the factory sample bank,
 *                       "logo" starts on the boot screen instead of skipping it
 *   wipe                erase the scene store and staging sectors (flash offsets 65536..204800)
 *   run MS              let MS milliseconds of simulated time pass
 *   press|release btn N / tap btn N     panel button 0..13 (OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC)
 *   press|release key N / tap key N     physical key 0..26 (F3..G5)
 *   white N             tap white key 1..16
 *   enc N DELTA         encoder 0..6 (SELECT ALGO K1 K2 K3 K4 PRESETS)
 *   knob V              master potentiometer 0..1023
 *   midi SRC HEX...     one message from source 0 DIN, 1 USB, 2 BLE, e.g. "midi 1 90 3c 64"
 *   shot NAME           write OUTDIR/NAME.png
 *   leds                print the LED states as a 41 character string
 *   echo TEXT           print TEXT
 *   expect_screen N     fail unless the DX747 screen id is N (0 boot, 1 setup, 2 engines, 3 channel,
 *                       4 engine sound, 5 plays, 6 sends, 7 midi in, 8 mix, 9 fx, 10 master, 11 scenes, 12 system)
 *   expect_px X Y HEX   fail unless the RGB565 pixel at X,Y (0..239) equals HEX
 *   expect_leds PATTERN fail unless the 41 LED states match PATTERN (# on, . off, ? ignore)
 *   expect_voices N     fail unless the router holds N engine-voices (one per engine a held key reaches)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern unsigned dx_route_voices(void);
extern void sim_boot(unsigned);
extern void sim_control(unsigned, unsigned, int);
extern const float *sim_render(void);
extern const uint16_t *sim_pixels(void);
extern uint8_t *sim_file(void);
extern uint8_t *sim_flash(void);
extern const uint8_t *sim_state(void);
extern int sim_midi_from(unsigned, unsigned);

static const char *outdir = ".";
static unsigned long long sim_ms_total;

static void run_ms(unsigned ms)
{
    /* one block is 128 frames = 2.9025 ms */
    unsigned blocks = (unsigned)((ms * 44100ULL + 127999ULL) / 128000ULL);
    for (unsigned i = 0; i < blocks; ++i) (void)sim_render();
    sim_ms_total += ms;
}

static int write_png(const char *path, const uint16_t *px, unsigned w, unsigned h, unsigned scale)
{
    unsigned W = w * scale, H = h * scale;
    size_t rowbytes = 1 + (size_t)W * 3, raw_size = rowbytes * H;
    uint8_t *raw = malloc(raw_size);
    if (!raw) return -1;
    for (unsigned y = 0; y < H; ++y) {
        uint8_t *row = raw + y * rowbytes;
        *row++ = 0;
        for (unsigned x = 0; x < W; ++x) {
            uint16_t v = px[(y / scale) * w + x / scale];
            *row++ = (uint8_t)(((v >> 11) & 31) * 255 / 31);
            *row++ = (uint8_t)(((v >> 5) & 63) * 255 / 63);
            *row++ = (uint8_t)((v & 31) * 255 / 31);
        }
    }
    uLongf zsize = compressBound((uLong)raw_size);
    uint8_t *z = malloc(zsize);
    if (!z || compress2(z, &zsize, raw, (uLong)raw_size, 9) != Z_OK) { free(raw); free(z); return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {(uint8_t)(W >> 24), (uint8_t)(W >> 16), (uint8_t)(W >> 8), (uint8_t)W,
                        (uint8_t)(H >> 24), (uint8_t)(H >> 16), (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
    struct { const char *type; const uint8_t *data; uLong len; } chunks[3] = {{"IHDR", ihdr, 13}, {"IDAT", z, (uLong)zsize}, {"IEND", (const uint8_t *)"", 0}};
    for (int i = 0; i < 3; ++i) {
        uint8_t len[4] = {(uint8_t)(chunks[i].len >> 24), (uint8_t)(chunks[i].len >> 16), (uint8_t)(chunks[i].len >> 8), (uint8_t)chunks[i].len};
        fwrite(len, 1, 4, f);
        fwrite(chunks[i].type, 1, 4, f);
        if (chunks[i].len) fwrite(chunks[i].data, 1, chunks[i].len, f);
        uLong crc = crc32(0, (const Bytef *)chunks[i].type, 4);
        if (chunks[i].len) crc = crc32(crc, chunks[i].data, (uInt)chunks[i].len);
        uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
        fwrite(c, 1, 4, f);
    }
    fclose(f); free(raw); free(z);
    return 0;
}

static void press(unsigned kind, unsigned index, int on) { sim_control(kind, index, on); }
static void tap(unsigned kind, unsigned index) { press(kind, index, 1); run_ms(60); press(kind, index, 0); run_ms(60); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s OUTDIR [script]\n", argv[0]); return 2; }
    outdir = argv[1];
    FILE *in = argc > 2 ? fopen(argv[2], "r") : stdin;
    if (!in) { perror("script"); return 2; }
    char line[512];
    static const unsigned white[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    int errors = 0;
    sim_boot(0);
    while (fgets(line, sizeof line, in)) {
        char *p = strtok(line, " \t\r\n");
        if (!p || p[0] == '#') continue;
        char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n");
        if (!strcmp(p, "boot")) { unsigned flags = 0; for (char *t = a; t; t = (t == a ? b : NULL)) { if (!strcmp(t, "clear")) flags |= 1U; if (!strcmp(t, "logo")) flags |= 2U; } sim_boot(flags); run_ms(100); }
        else if (!strcmp(p, "wipe")) { memset(sim_flash() + 65536, 0xff, 4096U * 34U); }
        else if (!strcmp(p, "run") && a) run_ms((unsigned)atoi(a));
        else if ((!strcmp(p, "tap") || !strcmp(p, "press") || !strcmp(p, "release")) && a && b) {
            unsigned kind = !strcmp(a, "btn") ? 0U : 1U;
            unsigned index = (unsigned)atoi(b);
            if (!strcmp(p, "tap")) tap(kind, index); else press(kind, index, !strcmp(p, "press"));
            if (strcmp(p, "tap")) run_ms(60);
        }
        else if (!strcmp(p, "white") && a) { int n = atoi(a); if (n >= 1 && n <= 16) tap(1, white[n - 1]); else ++errors; }
        else if (!strcmp(p, "enc") && a && b) { sim_control(2, (unsigned)atoi(a), atoi(b)); run_ms(60); }
        else if (!strcmp(p, "knob") && a) { sim_control(3, 0, atoi(a)); run_ms(60); }
        else if (!strcmp(p, "midi") && a) {
            unsigned n = 0;
            for (char *t = b; t; t = strtok(NULL, " \t\r\n")) if (n < 1024) sim_file()[n++] = (uint8_t)strtoul(t, NULL, 16);
            if (sim_midi_from((unsigned)atoi(a), n)) { fprintf(stderr, "midi rejected\n"); ++errors; }
            run_ms(30);
        }
        else if (!strcmp(p, "shot") && a) {
            char path[1024]; snprintf(path, sizeof path, "%s/%s.png", outdir, a);
            if (write_png(path, sim_pixels(), 240, 240, 2)) { fprintf(stderr, "cannot write %s\n", path); ++errors; }
            else printf("wrote %s\n", path);
        }
        else if (!strcmp(p, "leds")) { const uint8_t *s = sim_state(); char out[42]; for (int i = 0; i < 41; ++i) out[i] = s[168 + i] ? '#' : '.'; out[41] = 0; printf("leds %s\n", out); }
        else if (!strcmp(p, "expect_screen") && a) { int got = sim_state()[1]; if (got != atoi(a)) { fprintf(stderr, "expect_screen: wanted %s, on %d\n", a, got); ++errors; } }
        else if (!strcmp(p, "expect_px") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n"); unsigned x = (unsigned)atoi(a), y = (unsigned)atoi(b), want = c3 ? (unsigned)strtoul(c3, NULL, 16) : 0;
            unsigned got = x < 240 && y < 240 ? sim_pixels()[y * 240 + x] : 0xffffffU;
            if (got != want) { fprintf(stderr, "expect_px %u %u: wanted %04x, got %04x\n", x, y, want, got); ++errors; }
        }
        else if (!strcmp(p, "expect_leds") && a) {
            const uint8_t *s = sim_state(); int bad = 0;
            for (int i = 0; i < 41 && a[i]; ++i) if (a[i] != '?' && (a[i] == '#') != (s[168 + i] != 0)) bad = 1;
            if (bad) { char out[42]; for (int i = 0; i < 41; ++i) out[i] = s[168 + i] ? '#' : '.'; out[41] = 0; fprintf(stderr, "expect_leds: wanted %s, got %s\n", a, out); ++errors; }
        }
        else if (!strcmp(p, "expect_voices") && a) {
            unsigned want = (unsigned)atoi(a), got = dx_route_voices();
            if (want != got) { fprintf(stderr, "expect_voices: wanted %u, got %u\n", want, got); ++errors; }
        }
        else if (!strcmp(p, "echo")) { printf("%s%s%s\n", a ? a : "", b ? " " : "", b ? b : ""); }
        else { fprintf(stderr, "unknown or incomplete command: %s\n", p); ++errors; }
    }
    return errors ? 1 : 0;
}
