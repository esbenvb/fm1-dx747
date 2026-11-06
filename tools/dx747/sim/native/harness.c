/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Scripted driver for the native simulator build. Reads a script of simple
 * commands (see README below), drives the same sim_* entry points the browser
 * uses, and writes screenshots as PNG (scaled 2x).
 *
 *   boot [clear] [logo|safe] restart the instrument; "clear" also restores the factory sample bank,
 *                       "logo" starts on the boot screen instead of skipping it, "safe" starts as if the
 *                       safe-boot chord (ENV + LFO) were held at power-on
 *   wipe                erase the scene store and staging sectors (flash offsets 65536..335872)
 *   run MS              let MS milliseconds of simulated time pass
 *   press|release btn N / tap btn N     panel button 0..13 (OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC)
 *   press|release key N / tap key N     physical key 0..26 (F3..G5)
 *   white N             tap white key 1..16
 *   enc N DELTA         encoder 0..6 (SELECT ALGO K1 K2 K3 K4 PRESETS); 60 ms pass afterwards
 *   encq N DELTA        the same, but no time passes: detents in quick succession
 *   knob V              master potentiometer 0..1023
 *   midi SRC HEX...     one message from source 0 DIN, 1 USB, 2 BLE, e.g. "midi 1 90 3c 64"
 *   SMK-37 build only (make dx747-sim-smk37; recipes in recipes-smk37/), events as the board's scanner reports them:
 *   skey N VEL          key N (0..36, C3 + N) goes down with velocity VEL (1..127), or up with 0
 *   spad N V            pad N (0..15) down (1) or up (0)       sfader N V   fader 0..3 (parts) or wheel 4/5, V 0..127
 *   spedal V            sustain pedal (>= 64 down)             sbtn N V     physical button N (0..17) down/up
 *   senc N D            encoder 0..6 turned D counts; 7 is the master level
 *   expect_pad_color N HEX  fail unless pad N is lit RRGGBB
 *   shot NAME           write OUTDIR/NAME.png
 *   leds                print the LED states as a 41 character string (. dark, # full, 1..7 a dimmer level)
 *   echo TEXT           print TEXT
 *   expect_screen N     fail unless the DX747 screen id is N (0 boot, 1 parts, 2 part, 3 layer sound, 4 range,
 *                       5 velocity, 6 sends, 7 midi in, 8 mix, 9 fx, 10 master, 11 scenes, 12 system,
 *                       13 safe boot, 14 part details)
 *   flash_mark          remember a checksum of the whole simulated flash
 *   expect_flash_same   fail unless the flash still has the checksum flash_mark took
 *   expect_up HEXMASK   fail unless the engines the boot recipe brought up are exactly HEXMASK (bit 0 = engine 1)
 *   expect_silent_sysex fail if the unit answers a workbench capability request (transfer command 22)
 *   expect_px X Y HEX   fail unless the RGB565 pixel at X,Y (0..239) equals HEX
 *   expect_leds PATTERN fail unless the 41 LED states match PATTERN (# full, . dark, 1..7 that brightness, ? ignore)
 *   expect_voices N     fail unless the router holds N engine-voices (one per engine a held key reaches)
 *   bank_copy SRC DST   DX7 ROM import as the workbench does it: wrap library group SRC (0..3) as a standard
 *                       4104-byte 32-voice .syx, check it like the workbench, upload it into group DST with
 *                       transfer commands 3/4/5/6, then fail unless the library and the engine's programs follow
 *   bank_badcrc SRC DST the same upload with a wrong CRC: fail unless the device refuses and DST is unchanged
 *   bank_load GROUP FILE  import a real 4104-byte DX7 bank file (for trying your own ROMs in the simulator);
 *                       fails unless the library read-back equals the file and all 32 program names match
 *   expect_peak MS MIN  render MS milliseconds and fail unless the output reaches at least MIN (0..1 full scale);
 *                       prints the peak it measured
 *   expect_below MS MAX render MS milliseconds and fail unless the output stays at or below MAX (0..1 full scale)
 *   expect_patch E I V  fail unless engine E (0..7) has VCED parameter I (0..155) equal to V
 *   expect_download K E I V  download engine E's voice (K=1, 156 bytes) or timbre (K=2, 223 bytes) the way the
 *                       workbench does and fail unless byte I equals V
 *   retouch_voice E I V download engine E's voice, set byte I to V and upload it back (workbench command sequence)
 *   expect_rx E HEXMASK fail unless engine E (0..7) plays on exactly the channels in HEXMASK (bit 0 = channel 1)
 *   expect_engine E LO HI  fail unless engine E (0..7) has the note window LO..HI
 *   expect_cfg NAME V   fail unless the active router configuration has kbd_ch, master_ch or scene_ch equal to V
 *   expect_part P CH N  fail unless part P (0..7) is on channel CH (0 = the slot is unused) with N layers
 *   expect_sel P        fail unless part P is the selected part (the keyboard plays it)
 *   expect_layer E P L  fail unless engine E (0..7) is a layer of part P (-1 = free) at level L (0..127; -1 = any)
 *   expect_level P V    fail unless part P has part level V (0..127)
 *   wb CMD STATUS [HEX...]  send workbench command CMD with the hex bytes as data (the framing, packing and checksum are
 *                       done here) and fail unless the reply status is STATUS (0 ok, 71 invalid, 67 busy, 95..98 part codes)
 *   expect_reply I V    fail unless byte I of the last reply (0 = the status byte, then its data) is V (decimal or 0x..)
 *   expect_state OFF V  read the DX747 state image (transfer kind 5, docs/CONFIG-PROTOCOL.md) and fail unless byte OFF is V
 *   expect_state_text OFF TEXT  the same for a text field ("_" stands for a space; compares as many characters as TEXT has)
 *   expect_sends E W V  fail unless engine E (0..7) has send W (0 reverb, 1 delay) at V (0..15)
 *   expect_volume E V   fail unless the engine's own volume (what CC7 on its private channel set) is V (0..127)
 *   expect_vel E LO HI  fail unless engine E (0..7) has the velocity window LO..HI
 *   expect_ctl E CH     fail unless engine E (0..7) has control channel CH (0 = none): the DX7 SysEx target
 *   expect_selayer P L  fail unless the selected layer of part P is L (0-based)
 *   expect_live NAME I V  fail unless the UI-owned live value matches: pan I, expr I, bright I (engine I),
 *                       filter_type, filter_res or filter_hz (I ignored)
 *   expect_project_export_valid  fail unless the project the workbench page asks for (sizes 4064 and 4264) is one the
 *                       engine's own validator accepts and the tuning block's magic, CRC and range checks pass
 *   expect_timbre_refused E TYPE  upload a 223-byte timbre of source TYPE (not DX7) to track E the way the page does and
 *                       fail unless the unit refuses it and every track still reports the DX7 source
 *   wbserve             a bridge for tools/dx747/sim/page-flow.mjs: read lines of hex from stdin, each one SysEx
 *                       message exactly as the workbench page sends it, deliver it to the unit as USB MIDI and print
 *                       "R <hex>" with the unit's reply ("R none" if it stays silent for a second of simulated
 *                       time). An empty line or end of input ends it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

#include "instrument.h"
#include "dx747/scene.h"
#include "dx747/dx747.h"
#ifdef DX_SMK37
#include "smk37_controls.h"
#endif
extern unsigned dx_route_voices(void);
extern unsigned dx_engines_up(void);
extern void sim_safe_boot(unsigned);
extern struct dx_live *dx_live_edit(void);
extern const struct dx_cfg *dx_route_cfg(void);
extern void sim_boot(unsigned);
#ifdef DX_SMK37
extern void sim_smk(unsigned, unsigned, int);
extern uint32_t sim_pad_color(unsigned);
#endif
extern void sim_control(unsigned, unsigned, int);
extern const float *sim_render(void);
extern const uint16_t *sim_pixels(void);
extern uint8_t *sim_file(void);
extern uint8_t *sim_flash(void);
extern const uint8_t *sim_state(void);
extern int sim_midi_from(unsigned, unsigned);
extern void sim_midi_capture(unsigned);
extern unsigned sim_midi_output(void);
extern int groove_fm_library_read(unsigned bank, uint8_t vmem[4096]);
extern int groove_project_export(uint8_t *bytes, unsigned capacity);
extern void groove_fm_program(uint8_t patch[156], unsigned program);
extern uint32_t groove_crc32(uint32_t crc, const void *data, unsigned bytes);

static const char *outdir = ".";
enum { SIM_FLASH_BYTES = 536576 }; /* GROOVE_FLASH_BYTES */
static uLong flash_mark_crc;
static unsigned long long sim_ms_total;

/* The 41 lamps as text: . dark, # full brightness, 1..7 a dimmer level. */
static void led_text(char out[42])
{
    const uint8_t *s = sim_state();
    for (int i = 0; i < 41; ++i) { uint8_t v = s[168 + i]; out[i] = v == 0 ? '.' : v >= 8 ? '#' : (char)('0' + v); }
    out[41] = 0;
}
static void run_ms(unsigned ms)
{
    /* one block is 128 frames = 2.9025 ms */
    unsigned blocks = (unsigned)((ms * 44100ULL + 127999ULL) / 128000ULL);
    for (unsigned i = 0; i < blocks; ++i) (void)sim_render();
    sim_ms_total += ms;
}

/* Renders MS milliseconds and returns the largest output sample (one block is 128 stereo frames). */
static float peak_ms(unsigned ms)
{
    unsigned blocks = (unsigned)((ms * 44100ULL + 127999ULL) / 128000ULL);
    float peak = 0.0f;
    for (unsigned i = 0; i < blocks; ++i) {
        const float *o = sim_render();
        for (unsigned j = 0; j < 256; ++j) { float v = o[j] < 0 ? -o[j] : o[j]; if (v > peak) peak = v; }
    }
    sim_ms_total += ms;
    return peak;
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

/* ---- DX7 ROM import, the way the workbench does it (sdk/WebUpdate.js) ---------- */

static unsigned wb_token;
static uint8_t wb_data[160];   /* the last reply: status byte, then its data */
static unsigned wb_data_len;

/* One file-protocol request: F0 7D 'F' 'M' 02 cmd token(14 bit) pack7(payload) checksum F7.
 * Returns the device's status byte (0 = ok), -1 for no reply, -2 if the frame was refused outright. */
static int wb_query(unsigned cmd, const uint8_t *payload, unsigned n)
{
    uint8_t f[160];
    unsigned at = 0, value = 0, bits = 0, sum = 0;
    wb_token = (wb_token + 1U) & 16383U;
    f[at++] = 0xf0; f[at++] = 0x7d; f[at++] = 'F'; f[at++] = 'M'; f[at++] = 2;
    f[at++] = (uint8_t)cmd; f[at++] = (uint8_t)(wb_token & 127U); f[at++] = (uint8_t)(wb_token >> 7);
    for (unsigned i = 0; i < n; ++i) {
        value |= (unsigned)payload[i] << bits; bits += 8;
        while (bits >= 7) { f[at++] = (uint8_t)(value & 127U); value >>= 7; bits -= 7; }
    }
    if (bits) f[at++] = (uint8_t)(value & 127U);
    for (unsigned i = 1; i < at; ++i) sum += f[i];
    f[at++] = (uint8_t)((0U - sum) & 127U); f[at++] = 0xf7;
    sim_midi_capture(1);
    memcpy(sim_file(), f, at);
    if (sim_midi_from(1, at)) return -2;
    for (int tries = 0; tries < 50; ++tries) {
        run_ms(20);
        unsigned out = sim_midi_output();
        if (!out) continue;
        const uint8_t *r = sim_file();
        if ((out & 255U) >= 11 && r[5] == (cmd | 64U) && r[6] == f[6] && r[7] == f[7]) {
            unsigned len = out & 255U, v = 0, nb = 0, k = 0;
            for (unsigned i = 8; i + 2 < len; ++i) {      /* unpack the 7-bit payload */
                v |= (unsigned)r[i] << nb; nb += 7;
                if (nb >= 8) { if (k < sizeof wb_data) wb_data[k++] = (uint8_t)v; v >>= 8; nb -= 8; }
            }
            wb_data_len = k;
            return k ? (int)wb_data[0] : -1;
        }
    }
    return -1;
}

/* Serve the real workbench page code (page-flow.mjs): one hex line in, one "R <hex>" line out. */
static void wb_serve(void)
{
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t f[1024];
        unsigned at = 0;
        for (const char *p = line; p[0] && p[1] && p[0] != '\n' && at < sizeof f; p += 2) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            f[at++] = (uint8_t)v;
        }
        if (!at) break;
        sim_midi_capture(1);
        memcpy(sim_file(), f, at);
        int refused = sim_midi_from(1, at);
        unsigned out = 0;
        for (int tries = 0; !refused && !out && tries < 50; ++tries) {
            run_ms(20);
            while ((out = sim_midi_output()) != 0 && sim_file()[0] != 0xf0) { /* skip anything that is not a SysEx reply */ }
        }
        if (refused || !(out & 255U)) { printf("R none\n"); fflush(stdout); continue; }
        const uint8_t *r = sim_file();
        printf("R ");
        for (unsigned i = 0; i < (out & 255U); ++i) printf("%02x", r[i]);
        printf("\n");
        fflush(stdout);
    }
}

/* Workbench upload: begin write (kind 1 voice, 2 timbre, 4 library group), 64-byte chunks, CRC, and the close
 * the workbench always sends. */
static int wb_upload(unsigned kind, unsigned target, const uint8_t *raw, unsigned size, int corrupt_crc)
{
    uint8_t chunk[66], begin[4] = {(uint8_t)kind, (uint8_t)target, (uint8_t)size, (uint8_t)(size >> 8)};
    int r = wb_query(3, begin, 4);
    for (unsigned at = 0; !r && at < size; at += 64) {
        unsigned n = size - at < 64U ? size - at : 64U;
        chunk[0] = (uint8_t)at; chunk[1] = (uint8_t)(at >> 8); memcpy(chunk + 2, raw + at, n);
        r = wb_query(4, chunk, 2 + n);
    }
    if (!r) {
        uint32_t c = ~groove_crc32(~0U, raw, size);
        if (corrupt_crc) c ^= 1U;
        uint8_t q[4] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), (uint8_t)(c >> 24)};
        r = wb_query(5, q, 4);
    }
    (void)wb_query(6, NULL, 0);
    return r;
}
static int wb_upload_bank(unsigned group, const uint8_t *raw, int corrupt_crc) { return wb_upload(4, group, raw, 4096, corrupt_crc); }

/* Workbench download: command 1 starts it and returns the size, command 2 reads 64-byte chunks, 6 ends it. */
static int wb_download(unsigned kind, unsigned engine, uint8_t *out, unsigned size)
{
    uint8_t p[2] = {(uint8_t)kind, (uint8_t)engine};
    if (wb_query(1, p, 2) != 0 || wb_data_len < 3) return -1;
    if ((wb_data[1] | wb_data[2] << 8) != size) return -2;
    for (unsigned at = 0; at < size; at += 64) {
        unsigned n = size - at < 64U ? size - at : 64U;
        uint8_t q[3] = {(uint8_t)at, (uint8_t)(at >> 8), (uint8_t)n};
        if (wb_query(2, q, 3) != 0 || wb_data_len < 1 + n) return -3;
        memcpy(out + at, wb_data + 1, n);
    }
    (void)wb_query(6, NULL, 0);
    return 0;
}

static void dx7_bank_file(uint8_t file[4104], const uint8_t raw[4096])
{
    unsigned sum = 0;
    static const uint8_t head[6] = {0xf0, 0x43, 0x00, 0x09, 0x20, 0x00};
    memcpy(file, head, 6); memcpy(file + 6, raw, 4096);
    for (unsigned i = 0; i < 4096; ++i) sum += raw[i];
    file[4102] = (uint8_t)((0U - sum) & 127U); file[4103] = 0xf7;
}

/* readVoices() of the workbench: the standard 32-voice VMEM dump, then the kind 4 upload. */
static int wb_import_syx(const uint8_t *file, unsigned n, unsigned group, int corrupt_crc)
{
    unsigned sum = 0;
    if (n != 4104 || file[0] != 0xf0 || file[1] != 0x43 || file[2] >= 16 || file[3] != 9 || file[4] != 0x20 || file[5] != 0 || file[n - 1] != 0xf7) return -3;
    for (unsigned i = 1; i + 1 < n; ++i) if (file[i] > 127) return -3;
    for (unsigned i = 6; i + 1 < n; ++i) sum += file[i];
    if (sum & 127U) return -3;
    return wb_upload_bank(group, file + 6, corrupt_crc);
}

/* What the workbench's read-back check proves, plus: the engine's programs now play the imported voices. */
static int bank_verify(unsigned group, const uint8_t *raw)
{
    static uint8_t got[4096];
    if (group > 3 || groove_fm_library_read(group, got) || memcmp(got, raw, sizeof got)) { fprintf(stderr, "bank_load: library read-back differs\n"); return 1; }
    for (unsigned k = 0; k < 32; ++k) {
        uint8_t patch[156];
        groove_fm_program(patch, group * 32U + k);
        if (memcmp(patch + 145, raw + k * 128U + 118U, 10)) { fprintf(stderr, "bank_load: program %u has the wrong name\n", group * 32U + k + 1U); return 1; }
    }
    return 0;
}

static int bank_copy(unsigned src, unsigned dst, int corrupt_crc)
{
    static uint8_t a[4096], before[4096], after[4096], file[4104];
    if (src > 3 || dst > 3 || groove_fm_library_read(src, a) || groove_fm_library_read(dst, before)) { fprintf(stderr, "bank: cannot read library group %u or %u\n", src, dst); return 1; }
    if (!corrupt_crc && !memcmp(a, before, sizeof a)) { fprintf(stderr, "bank_copy: groups %u and %u already match, nothing to prove\n", src, dst); return 1; }
    dx7_bank_file(file, a);
    int r = wb_import_syx(file, sizeof file, dst, corrupt_crc);
    if (corrupt_crc) {
        if (!r) { fprintf(stderr, "bank_badcrc: the device accepted a wrong CRC\n"); return 1; }
        if (groove_fm_library_read(dst, after) || memcmp(after, before, sizeof after)) { fprintf(stderr, "bank_badcrc: group %u changed anyway\n", dst); return 1; }
        return 0;
    }
    if (r) { fprintf(stderr, "bank_copy %u %u: device status %d\n", src, dst, r); return 1; }
    if (groove_fm_library_read(dst, after) || memcmp(after, a, sizeof after)) { fprintf(stderr, "bank_copy: library read-back differs\n"); return 1; }
    for (unsigned k = 0; k < 32; ++k) {
        uint8_t pa[156], pb[156];
        groove_fm_program(pa, src * 32U + k); groove_fm_program(pb, dst * 32U + k);
        if (memcmp(pa, pb, sizeof pa)) { fprintf(stderr, "bank_copy: program %u does not play voice %u of group %u\n", dst * 32U + k, k, src); return 1; }
    }
    return 0;
}

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
        if (!strcmp(p, "boot")) {
            unsigned flags = 0, safe = 0;
            for (char *t = a; t; t = (t == a ? b : NULL)) { if (!strcmp(t, "clear")) flags |= 1U; if (!strcmp(t, "logo")) flags |= 2U; if (!strcmp(t, "safe")) safe = 1U; }
            sim_safe_boot(safe); sim_boot(flags); run_ms(100);
        }
        else if (!strcmp(p, "flash_mark")) flash_mark_crc = crc32(0, sim_flash(), SIM_FLASH_BYTES);
        else if (!strcmp(p, "expect_flash_same")) {
            uLong now = crc32(0, sim_flash(), SIM_FLASH_BYTES);
            if (now != flash_mark_crc) { fprintf(stderr, "expect_flash_same: flash changed (crc %08lx, was %08lx)\n", (unsigned long)now, (unsigned long)flash_mark_crc); ++errors; }
        }
        else if (!strcmp(p, "expect_up") && a) {
            unsigned want = (unsigned)strtoul(a, NULL, 16), got = dx_engines_up();
            if (want != got) { fprintf(stderr, "expect_up: wanted %x, got %x\n", want, got); ++errors; }
        }
        else if (!strcmp(p, "expect_silent_sysex")) {
            if (wb_query(22, NULL, 0) != -1) { fprintf(stderr, "expect_silent_sysex: the unit answered\n"); ++errors; }
        }
        else if (!strcmp(p, "wbserve")) wb_serve();
        else if (!strcmp(p, "expect_project_export_valid")) {
            static uint8_t buf[4264] __attribute__((aligned(8)));
            for (unsigned size = 4064; size <= 4264; size += 200) {
                memset(buf, 0xaa, sizeof buf);
                if (groove_project_export(buf, size)) { fprintf(stderr, "expect_project_export_valid: export of %u bytes refused\n", size); ++errors; continue; }
                if (!groove_project_valid(buf)) { fprintf(stderr, "expect_project_export_valid: the engine rejects the %u byte export\n", size); ++errors; }
                if (buf[3852] != 4) { fprintf(stderr, "expect_project_export_valid: version byte %u, the page wants 4\n", buf[3852]); ++errors; }
                if (size == 4264) {
                    const uint8_t *b = buf + 4064;
                    uint32_t magic, crc;
                    memcpy(&magic, b, 4); memcpy(&crc, b + 196, 4);
                    int low = 1;
                    for (unsigned i = 4; i < 196; ++i) low &= b[i] < 128;
                    if (magic != 0x54314d46U || crc != ~groove_crc32(~0U, b, 196) || !low) { fprintf(stderr, "expect_project_export_valid: the tuning block fails the page's check\n"); ++errors; }
                }
            }
        }
        else if (!strcmp(p, "expect_timbre_refused") && a && b) {
            uint8_t t[223];
            memset(t, 0, sizeof t);
            t[0] = (uint8_t)atoi(b);
            int r = wb_upload(2, (unsigned)atoi(a), t, sizeof t, 0);
            if (!r) { fprintf(stderr, "expect_timbre_refused: a timbre of source %s was accepted\n", b); ++errors; }
            int q = wb_query(0, NULL, 0);
            int bad = q != 0 || wb_data_len < 15;
            for (unsigned i = 0; !bad && i < 8; ++i) bad |= wb_data[7 + i] != 0;
            if (bad || dx_engines_up() != 0xffU) { fprintf(stderr, "expect_timbre_refused: a track no longer reports the DX7 source (or an engine went down)\n"); ++errors; }
        }
#ifdef DX_SMK37
        else if (!strcmp(p, "skey") && a && b) { sim_smk(SMK37_KEY, (unsigned)atoi(a), atoi(b)); run_ms(30); }
        else if (!strcmp(p, "spad") && a && b) { sim_smk(SMK37_PAD, (unsigned)atoi(a), atoi(b)); run_ms(30); }
        else if (!strcmp(p, "sfader") && a && b) { sim_smk(SMK37_FADER, (unsigned)atoi(a), atoi(b)); run_ms(30); }
        else if (!strcmp(p, "spedal") && a) { sim_smk(SMK37_PEDAL, 0, atoi(a)); run_ms(30); }
        else if (!strcmp(p, "sbtn") && a && b) { sim_smk(SMK37_BUTTON, (unsigned)atoi(a), atoi(b)); run_ms(60); }
        else if (!strcmp(p, "senc") && a && b) { sim_smk(SMK37_ENCODER, (unsigned)atoi(a), atoi(b)); run_ms(60); }
        else if (!strcmp(p, "expect_pad_color") && a && b) {
            uint32_t got = sim_pad_color((unsigned)atoi(a)), want = (uint32_t)strtoul(b, NULL, 16);
            if (got != want) { fprintf(stderr, "expect_pad_color: pad %s is %06x, expected %06x\n", a, (unsigned)got, (unsigned)want); ++errors; }
        }
#endif
        else if (!strcmp(p, "wipe")) { memset(sim_flash() + 65536, 0xff, 4096U * 66U); }
        else if (!strcmp(p, "run") && a) run_ms((unsigned)atoi(a));
        else if ((!strcmp(p, "tap") || !strcmp(p, "press") || !strcmp(p, "release")) && a && b) {
            unsigned kind = !strcmp(a, "btn") ? 0U : 1U;
            unsigned index = (unsigned)atoi(b);
            if (!strcmp(p, "tap")) tap(kind, index); else press(kind, index, !strcmp(p, "press"));
            if (strcmp(p, "tap")) run_ms(60);
        }
        else if (!strcmp(p, "white") && a) { int n = atoi(a); if (n >= 1 && n <= 16) tap(1, white[n - 1]); else ++errors; }
        else if (!strcmp(p, "enc") && a && b) { sim_control(2, (unsigned)atoi(a), atoi(b)); run_ms(60); }
        else if (!strcmp(p, "encq") && a && b) { sim_control(2, (unsigned)atoi(a), atoi(b)); }
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
        else if (!strcmp(p, "leds")) { char out[42]; led_text(out); printf("leds %s\n", out); }
        else if (!strcmp(p, "expect_screen") && a) { int got = sim_state()[1]; if (got != atoi(a)) { fprintf(stderr, "expect_screen: wanted %s, on %d\n", a, got); ++errors; } }
        else if (!strcmp(p, "expect_px") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n"); unsigned x = (unsigned)atoi(a), y = (unsigned)atoi(b), want = c3 ? (unsigned)strtoul(c3, NULL, 16) : 0;
            unsigned got = x < 240 && y < 240 ? sim_pixels()[y * 240 + x] : 0xffffffU;
            if (got != want) { fprintf(stderr, "expect_px %u %u: wanted %04x, got %04x\n", x, y, want, got); ++errors; }
        }
        else if (!strcmp(p, "expect_leds") && a) {
            char out[42]; int bad = 0;
            led_text(out);
            for (int i = 0; i < 41 && a[i]; ++i) if (a[i] != '?' && a[i] != out[i]) bad = 1;
            if (bad) { fprintf(stderr, "expect_leds: wanted %s, got %s\n", a, out); ++errors; }
        }
        else if (!strcmp(p, "expect_voices") && a) {
            unsigned want = (unsigned)atoi(a), got = dx_route_voices();
            if (want != got) { fprintf(stderr, "expect_voices: wanted %u, got %u\n", want, got); ++errors; }
        }
        else if (!strcmp(p, "bank_copy") && a && b) errors += bank_copy((unsigned)atoi(a), (unsigned)atoi(b), 0);
        else if (!strcmp(p, "bank_badcrc") && a && b) errors += bank_copy((unsigned)atoi(a), (unsigned)atoi(b), 1);
        else if (!strcmp(p, "bank_load") && a && b) {
            static uint8_t file[8192];
            FILE *bf = fopen(b, "rb");
            size_t got = bf ? fread(file, 1, sizeof file, bf) : 0;
            if (bf) fclose(bf);
            int r = wb_import_syx(file, (unsigned)got, (unsigned)atoi(a), 0);
            if (r) { fprintf(stderr, "bank_load: %s: status %d\n", b, r); ++errors; }
            else errors += bank_verify((unsigned)atoi(a), file + 6);
        }
        else if (!strcmp(p, "expect_peak") && a && b) {
            float got = peak_ms((unsigned)atoi(a)), want = (float)atof(b);
            printf("peak %.4f\n", (double)got);
            if (got < want) { fprintf(stderr, "expect_peak: wanted at least %g, got %g\n", (double)want, (double)got); ++errors; }
        }
        else if (!strcmp(p, "expect_below") && a && b) {
            float got = peak_ms((unsigned)atoi(a)), limit = (float)atof(b);
            printf("peak %.4f\n", (double)got);
            if (got > limit) { fprintf(stderr, "expect_below: wanted at most %g, got %g\n", (double)limit, (double)got); ++errors; }
        }
        else if (!strcmp(p, "expect_patch") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned e = (unsigned)atoi(a), i = (unsigned)atoi(b), want = c3 ? (unsigned)atoi(c3) : 0U;
            uint8_t v[156];
            if (groove_fm_copy(e, v, NULL) || i >= 156 || v[i] != want) {
                fprintf(stderr, "expect_patch %u %u: wanted %u, got %d\n", e, i, want, i < 156 && !groove_fm_copy(e, v, NULL) ? (int)v[i] : -1);
                ++errors;
            }
        }
        else if (!strcmp(p, "expect_live") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned i = (unsigned)atoi(b);
            int want = c3 ? atoi(c3) : 0, got = -9999;
            if (!c3 && !strncmp(a, "filter_", 7)) { want = atoi(b); i = 0; } /* filter_*: the value may stand in place of the index */
            const struct dx_live *l = dx_live_edit();
            if (!strcmp(a, "pan") && i < 8) got = l->pan[i];
            else if (!strcmp(a, "expr") && i < 8) got = l->expr[i];
            else if (!strcmp(a, "bright") && i < 8) got = l->bright[i];
            else if (!strcmp(a, "filter_type")) got = l->filter_type;
            else if (!strcmp(a, "filter_res")) got = l->filter_res;
            else if (!strcmp(a, "filter_hz")) got = l->filter_hz;
            if (got != want) { fprintf(stderr, "expect_live %s %u: wanted %d, got %d\n", a, i, want, got); ++errors; }
        }
        else if (!strcmp(p, "expect_download") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n"), *c4 = strtok(NULL, " \t\r\n");
            unsigned kind = (unsigned)atoi(a), e = (unsigned)atoi(b), i = c3 ? (unsigned)atoi(c3) : 0U, want = c4 ? (unsigned)atoi(c4) : 0U;
            uint8_t buf[256];
            unsigned size = kind == 1 ? 156U : kind == 2 ? 223U : 0U;
            int r = size ? wb_download(kind, e, buf, size) : -9;
            if (r || i >= size || buf[i] != want) {
                fprintf(stderr, "expect_download %u %u %u: wanted %u, got %d (status %d)\n", kind, e, i, want, !r && i < size ? (int)buf[i] : -1, r);
                ++errors;
            }
        }
        else if (!strcmp(p, "retouch_voice") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned e = (unsigned)atoi(a), i = (unsigned)atoi(b);
            uint8_t v[156];
            int r = i < 156 && c3 ? wb_download(1, e, v, 156) : -9;
            if (!r) { v[i] = (uint8_t)atoi(c3); r = wb_upload(1, e, v, 156, 0); }
            if (r) { fprintf(stderr, "retouch_voice %u %u: status %d\n", e, i, r); ++errors; }
        }
        else if (!strcmp(p, "expect_rx") && a && b) {
            unsigned e = (unsigned)atoi(a), want = (unsigned)strtoul(b, NULL, 16);
            const struct dx_cfg *cf = dx_route_cfg();
            if (e >= 8 || cf->e[e].rx != want) { fprintf(stderr, "expect_rx %s: wanted %x, got %x\n", a, want, e < 8 ? cf->e[e].rx : 0U); ++errors; }
        }
        else if (!strcmp(p, "expect_engine") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned e = (unsigned)atoi(a);
            const struct dx_cfg *cf = dx_route_cfg();
            if (e >= 8 || !c3 || cf->e[e].lo != (unsigned)atoi(b) || cf->e[e].hi != (unsigned)atoi(c3)) {
                fprintf(stderr, "expect_engine %s: wanted %s..%s, got %d..%d\n", a, b, c3 ? c3 : "?", e < 8 ? cf->e[e].lo : -1, e < 8 ? cf->e[e].hi : -1);
                ++errors;
            }
        }
        else if (!strcmp(p, "expect_cfg") && a && b) {
            const struct dx_cfg *cf = dx_route_cfg();
            int got = !strcmp(a, "kbd_ch") ? cf->kbd_ch : !strcmp(a, "master_ch") ? cf->master_ch : !strcmp(a, "scene_ch") ? cf->scene_ch : -9999;
            if (got != atoi(b)) { fprintf(stderr, "expect_cfg %s: wanted %d, got %d\n", a, atoi(b), got); ++errors; }
        }
        else if (!strcmp(p, "expect_part") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned part = (unsigned)atoi(a);
            const struct dx_parts *pp = &dx_live_edit()->parts;
            unsigned layers = 0;
            if (part < DX_PARTS) for (unsigned e = 0; e < DX_ENGINES; ++e) layers += pp->layer[e].owner == part;
            if (part >= DX_PARTS || !c3 || pp->part[part].channel != (unsigned)atoi(b) || layers != (unsigned)atoi(c3)) {
                fprintf(stderr, "expect_part %s: wanted channel %s with %s layers, got channel %d with %u\n", a, b, c3 ? c3 : "?", part < DX_PARTS ? pp->part[part].channel : -1, layers);
                ++errors;
            }
        }
        else if (!strcmp(p, "expect_sel") && a) {
            const struct dx_parts *pp = &dx_live_edit()->parts;
            if (pp->selected != (unsigned)atoi(a)) { fprintf(stderr, "expect_sel: wanted %s, got %u\n", a, pp->selected); ++errors; }
        }
        else if (!strcmp(p, "expect_layer") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned e = (unsigned)atoi(a);
            const struct dx_parts *pp = &dx_live_edit()->parts;
            int owner = e < 8 ? (pp->layer[e].owner == DX_NO_PART ? -1 : pp->layer[e].owner) : -2, lvl = e < 8 ? pp->layer[e].lvl : -2;
            if (e >= 8 || !c3 || owner != atoi(b) || (atoi(c3) >= 0 && lvl != atoi(c3))) {
                fprintf(stderr, "expect_layer %s: wanted part %s level %s, got part %d level %d\n", a, b, c3 ? c3 : "?", owner, lvl);
                ++errors;
            }
        }
        else if (!strcmp(p, "expect_level") && a && b) {
            unsigned part = (unsigned)atoi(a);
            const struct dx_parts *pp = &dx_live_edit()->parts;
            if (part >= DX_PARTS || pp->part[part].level != (unsigned)atoi(b)) { fprintf(stderr, "expect_level %s: wanted %s, got %d\n", a, b, part < DX_PARTS ? pp->part[part].level : -1); ++errors; }
        }
        else if (!strcmp(p, "wb") && a && b) {
            unsigned cmd = (unsigned)atoi(a), want = (unsigned)atoi(b), n = 0;
            uint8_t d[140];
            for (char *t; (t = strtok(NULL, " \t\r\n")) && n < sizeof d;) d[n++] = (uint8_t)strtoul(t, NULL, 16);
            int r = wb_query(cmd, d, n);
            if (r != (int)want) { fprintf(stderr, "wb %u: wanted status %u, got %d\n", cmd, want, r); ++errors; }
        }
        else if (!strcmp(p, "expect_reply") && a && b) {
            unsigned i = (unsigned)atoi(a), want = (unsigned)strtoul(b, NULL, 0);
            if (i >= wb_data_len || wb_data[i] != want) { fprintf(stderr, "expect_reply %u: wanted %u, got %d\n", i, want, i < wb_data_len ? wb_data[i] : -1); ++errors; }
        }
        else if (!strcmp(p, "expect_state") && a && b) {
            uint8_t img[DX_CFG_STATE_BYTES];
            unsigned off = (unsigned)atoi(a), want = (unsigned)strtoul(b, NULL, 0);
            if (wb_download(5, 0, img, sizeof img)) { fprintf(stderr, "expect_state: the state image could not be read\n"); ++errors; }
            else if (off >= sizeof img || img[off] != want) { fprintf(stderr, "expect_state %u: wanted %u, got %d\n", off, want, off < sizeof img ? img[off] : -1); ++errors; }
        }
        else if (!strcmp(p, "expect_state_text") && a && b) {
            uint8_t img[DX_CFG_STATE_BYTES];
            unsigned off = (unsigned)atoi(a), len = (unsigned)strlen(b), bad = 0;
            if (wb_download(5, 0, img, sizeof img)) { fprintf(stderr, "expect_state_text: the state image could not be read\n"); ++errors; }
            else {
                for (unsigned i = 0; i < len; ++i) if (off + i >= sizeof img || img[off + i] != (uint8_t)(b[i] == '_' ? ' ' : b[i])) bad = 1;
                if (bad) { fprintf(stderr, "expect_state_text %u: wanted %s, got %.*s\n", off, b, (int)(len < sizeof img - off ? len : sizeof img - off), (const char *)img + off); ++errors; }
            }
        }
        else if (!strcmp(p, "expect_sends") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned e = (unsigned)atoi(a), w = (unsigned)atoi(b), want = c3 ? (unsigned)atoi(c3) : 0;
            uint8_t sends[2] = {0, 0};
            if (e < 8) groove_sends_get(e, sends);
            if (e >= 8 || w > 1 || (sends[w] & 15U) != want) { fprintf(stderr, "expect_sends %s %s: wanted %u, got %u\n", a, b, want, e < 8 && w < 2 ? sends[w] & 15U : 0U); ++errors; }
        }
        else if (!strcmp(p, "expect_volume") && a && b) {
            unsigned e = (unsigned)atoi(a);
            struct groove_view v;
            memset(&v, 0, sizeof v);
            if (e < 8) groove_get_view(e, 0, &v);
            if (e >= 8 || v.volume != (unsigned)atoi(b)) { fprintf(stderr, "expect_volume %s: wanted %s, got %u\n", a, b, e < 8 ? (unsigned)v.volume : 0U); ++errors; }
        }
        else if (!strcmp(p, "expect_vel") && a && b) {
            char *c3 = strtok(NULL, " \t\r\n");
            unsigned e = (unsigned)atoi(a);
            const struct dx_cfg *cf = dx_route_cfg();
            if (e >= 8 || !c3 || cf->e[e].vlo != (unsigned)atoi(b) || cf->e[e].vhi != (unsigned)atoi(c3)) {
                fprintf(stderr, "expect_vel %s: wanted %s..%s, got %d..%d\n", a, b, c3 ? c3 : "?", e < 8 ? cf->e[e].vlo : -1, e < 8 ? cf->e[e].vhi : -1);
                ++errors;
            }
        }
        else if (!strcmp(p, "expect_ctl") && a && b) {
            unsigned e = (unsigned)atoi(a);
            const struct dx_cfg *cf = dx_route_cfg();
            if (e >= 8 || cf->e[e].ctl != (unsigned)atoi(b)) { fprintf(stderr, "expect_ctl %s: wanted %s, got %d\n", a, b, e < 8 ? cf->e[e].ctl : -1); ++errors; }
        }
        else if (!strcmp(p, "expect_selayer") && a && b) {
            unsigned part = (unsigned)atoi(a);
            const struct dx_parts *pp = &dx_live_edit()->parts;
            if (part >= DX_PARTS || pp->part[part].sel != (unsigned)atoi(b)) { fprintf(stderr, "expect_selayer %s: wanted %s, got %d\n", a, b, part < DX_PARTS ? pp->part[part].sel : -1); ++errors; }
        }
        else if (!strcmp(p, "echo")) { printf("%s%s%s\n", a ? a : "", b ? " " : "", b ? b : ""); }
        else { fprintf(stderr, "unknown or incomplete command: %s\n", p); ++errors; }
    }
    return errors ? 1 : 0;
}
