/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host tests for the DX747 audio out stage (app/audio_output.c + master_fx.inc).
 * The unit under test is included so that tests can read the ISR state; the SDK
 * calls are mocks. Build: see Makefile (clang, ASan + UBSan, -Werror). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../../../app/audio_output.c"

#define PI 3.14159265358979323846
#define FS 44100.0
#define FULL 8388608.0 /* one full scale in Q23 */

static int failures, checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- SDK mocks ---- */
static unsigned mock_level = 127;
static int mock_usb_frames;
static int32_t mock_usb_pcm[FM1_BLOCK_WORDS];
static uint16_t mock_usb_gain[2];
static int mock_bt_frames;
static int16_t mock_bt_pcm[FM1_BLOCK_WORDS];
static int32_t tap[FM1_BLOCK_WORDS];
static unsigned tap_calls, tap_frames;
static const int32_t *tap_ptr;

unsigned fm1_engine_output_level(void) { return mock_level; }
int fm1_usb_audio_read(int32_t *pcm, unsigned frames, uint16_t gains_q7[2])
{
    if (mock_usb_frames == FM1_BLOCK_FRAMES && frames == FM1_BLOCK_FRAMES)
        memcpy(pcm, mock_usb_pcm, sizeof mock_usb_pcm);
    gains_q7[0] = mock_usb_gain[0]; gains_q7[1] = mock_usb_gain[1];
    return mock_usb_frames;
}
int fm1_bluetooth_audio_read(int16_t *pcm, unsigned frames)
{
    if (mock_bt_frames == FM1_BLOCK_FRAMES && frames == FM1_BLOCK_FRAMES)
        memcpy(pcm, mock_bt_pcm, sizeof mock_bt_pcm);
    return mock_bt_frames;
}
int fm1_usb_audio_write(const int32_t *pcm, unsigned frames)
{
    memcpy(tap, pcm, sizeof tap); /* the real call copies and keeps nothing */
    tap_ptr = pcm; tap_frames = frames; ++tap_calls;
    return (int)frames;
}

/* ---- the stage as it was before M5, verbatim, for bit-exact comparison ---- */
static unsigned ref_gain;
static int32_t ref_usb[FM1_BLOCK_WORDS];
static int16_t ref_bt[FM1_BLOCK_WORDS];
static int32_t ref_tap[FM1_BLOCK_WORDS];
static inline int32_t ref_limit(int64_t value)
{
    if (((uint64_t)value + 8388608U) >> 24) return value < 0 ? -8388608 : 8388607;
    return (int32_t)value;
}
static void ref_mix(int32_t *output)
{
    unsigned target = fm1_engine_output_level();
    if (target > 127U) target = 127U;
    int delta = (int)target - (int)ref_gain;
    ref_gain = (unsigned)((int)ref_gain + (delta > 8 ? 8 : delta < -8 ? -8 : delta));
    uint16_t usb_gain[2] = {0, 0};
    int usb_frames = fm1_usb_audio_read(ref_usb, FM1_BLOCK_FRAMES, usb_gain);
    unsigned left = ((unsigned)usb_gain[0] * ref_gain) >> 7;
    unsigned right = ((unsigned)usb_gain[1] * ref_gain) >> 7;
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        int64_t value = ((int64_t)output[i] * ref_gain) >> 7;
        if (usb_frames == FM1_BLOCK_FRAMES)
            value += ((int64_t)ref_usb[i] * ((i & 1U) ? right : left)) >> 7;
        output[i] = usb_frames == -2 ? 0 : ref_limit(value);
    }
    if (fm1_bluetooth_audio_read(ref_bt, FM1_BLOCK_FRAMES) == FM1_BLOCK_FRAMES) {
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            int32_t value = output[i] + (int32_t)ref_bt[i] * (int32_t)(ref_gain * 2U);
            output[i] = value > 8388607 ? 8388607 : value < -8388608 ? -8388608 : value;
        }
    }
    (void)fm1_usb_audio_write(output, FM1_BLOCK_FRAMES);
    memcpy(ref_tap, tap, sizeof ref_tap);
}

/* ---- helpers ---- */
static uint32_t rng = 0x1234567u;
static uint32_t seed_base; /* FILTER_TEST_SEED varies every random stream */
static void seed(uint32_t v) { rng = (v + seed_base * 40503u) | 1u; }
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd_q23(void) { return (int32_t)(rnd() % 16777216U) - 8388608; }
static double rnd_unit(void) { return (double)(rnd() >> 8) / 16777216.0; }

static void mocks_reset(void)
{
    mock_level = 127; mock_usb_frames = 0; mock_bt_frames = 0;
    mock_usb_gain[0] = mock_usb_gain[1] = 128;
    memset(mock_usb_pcm, 0, sizeof mock_usb_pcm); memset(mock_bt_pcm, 0, sizeof mock_bt_pcm);
    tap_calls = 0;
}
static void fresh(void)
{
    int32_t b[FM1_BLOCK_WORDS];
    mocks_reset();
    fm1_master_fx_set(FM1_MFX_OFF, 1000.0f, 0);
    for (int i = 0; i < 4; ++i) { memset(b, 0, sizeof b); fm1_output_mix(b); }
    fm1_output_reset();
    ref_gain = 0;
    mfx.guard = 0;
    mocks_reset();
}
/* Run frames of stereo input through the stage; the tap is the filter output. */
typedef void (*block_cb)(size_t block, void *ctx);
static void process(const int32_t *in, int32_t *tapo, int32_t *outo, size_t frames,
                    block_cb cb, void *ctx)
{
    int32_t b[FM1_BLOCK_WORDS];
    for (size_t f = 0; f < frames; f += FM1_BLOCK_FRAMES) {
        if (cb) cb(f / FM1_BLOCK_FRAMES, ctx);
        memcpy(b, in + 2 * f, sizeof b);
        fm1_output_mix(b);
        if (tapo) memcpy(tapo + 2 * f, tap, sizeof tap);
        if (outo) memcpy(outo + 2 * f, b, sizeof b);
    }
}
static double max_state_fs(void)
{
    double m = 0;
    for (int c = 0; c < 2; ++c) {
        double a = fabs((double)mfx.s1[c]) / 36028797018963968.0, b = fabs((double)mfx.s2[c]) / 36028797018963968.0;
        if (a > m) m = a;
        if (b > m) m = b;
    }
    return m;
}

/* ---- analytic response of the ideal TPT SVF with the same g and k ---- */
static double k_of(unsigned res) { return pow(2.0, 0.5 - 4.5 * res / 127.0); }
/* Q is capped at 24 cos(pi fc / fs), see master_fx.h */
static double k_eff(double fc, unsigned res) { return fmax(k_of(res), 1.0 / (24.0 * cos(PI * fc / FS))); }
static double ideal_mag(unsigned type, double fc, unsigned res, double f)
{
    double g = tan(PI * fc / FS), k = k_eff(fc, res), u = tan(PI * f / FS) / g;
    double re = 1.0 - u * u, im = k * u, den = sqrt(re * re + im * im);
    return type == FM1_MFX_LP ? 1.0 / den : type == FM1_MFX_HP ? u * u / den : k * u / den;
}
static const char *type_name(unsigned t) { return t == FM1_MFX_LP ? "LP" : t == FM1_MFX_HP ? "HP" : t == FM1_MFX_BP ? "BP" : "OFF"; }

/* ---- 1. bypass ---- */
static void fill_random(int32_t *b)
{
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) b[i] = rnd_q23();
}
static void test_bypass(void)
{
    seed(11u * 2654435761u);
    printf("bypass is bit-exact and the tap is the engine block\n");
    fresh();
    static const unsigned levels[] = {0, 1, 40, 127, 127, 3, 99, 127};
    unsigned diffs = 0, tapdiffs = 0;
    for (int blk = 0; blk < 2000; ++blk) {
        int32_t engine[FM1_BLOCK_WORDS], a[FM1_BLOCK_WORDS], b[FM1_BLOCK_WORDS];
        fill_random(engine);
        if (blk % 5 == 0) for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) engine[i] = (i & 1) ? 8388607 : -8388608;
        mock_level = levels[(blk / 7) % 8];
        mock_usb_frames = (blk % 11 == 0) ? 0 : (blk % 13 == 0) ? -2 : FM1_BLOCK_FRAMES;
        mock_usb_gain[0] = (uint16_t)(rnd() % 140); mock_usb_gain[1] = (uint16_t)(rnd() % 140);
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) mock_usb_pcm[i] = rnd_q23();
        mock_bt_frames = (blk % 3 == 0) ? 0 : FM1_BLOCK_FRAMES;
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) mock_bt_pcm[i] = (int16_t)(rnd() & 0xffff);
        memcpy(a, engine, sizeof a); memcpy(b, engine, sizeof b);
        unsigned calls = tap_calls;
        fm1_output_mix(a);
        CHECK(tap_calls == calls + 1 && tap_frames == FM1_BLOCK_FRAMES, "usb write called once per block with 128 frames");
        CHECK(tap_ptr == a, "the tap is written straight from the block");
        tapdiffs += memcmp(tap, engine, sizeof tap) != 0;
        ref_mix(b);
        diffs += memcmp(a, b, sizeof a) != 0;
    }
    CHECK(diffs == 0, "output differs from the pre-M5 stage in %u of 2000 blocks", diffs);
    CHECK(tapdiffs == 0, "tap differs from the engine block in %u of 2000 blocks", tapdiffs);
    CHECK(mfx.cur == FM1_MFX_OFF && mfx.guard == 0, "idle filter state touched");

    /* After use, back to OFF: bit-exact again once the fade-out has finished. */
    printf("bypass again after an on/off cycle\n");
    for (int cycle = 0; cycle < 6; ++cycle) {
        fm1_master_fx_set(1 + cycle % 3, 300.0f * (cycle + 1), 20 * cycle);
        for (int blk = 0; blk < 5; ++blk) { int32_t e[FM1_BLOCK_WORDS]; fill_random(e); fm1_output_mix(e); }
        fm1_master_fx_set_type(FM1_MFX_OFF);
        for (int blk = 0; blk < 3; ++blk) { int32_t e[FM1_BLOCK_WORDS]; fill_random(e); fm1_output_mix(e); }
        mfx_reset(); /* equal output gain on both sides again */
        ref_gain = output_gain;
        diffs = 0;
        for (int blk = 0; blk < 20; ++blk) {
            int32_t engine[FM1_BLOCK_WORDS], a[FM1_BLOCK_WORDS], b[FM1_BLOCK_WORDS];
            fill_random(engine); memcpy(a, engine, sizeof a); memcpy(b, engine, sizeof b);
            fm1_output_mix(a);
            CHECK(memcmp(tap, engine, sizeof tap) == 0, "tap not the bare engine block after OFF");
            ref_mix(b);
            diffs += memcmp(a, b, sizeof a) != 0;
        }
        CHECK(diffs == 0, "cycle %d: output differs after returning to OFF", cycle);
    }

    /* Zero cost when off: the ISR state does not move at all. */
    fresh();
    fm1_master_fx_set(FM1_MFX_OFF, 5000.0f, 90); /* settings change, type stays OFF */
    fm1_master_fx_set_resonance(10);
    unsigned char before[sizeof mfx];
    int32_t b[FM1_BLOCK_WORDS];
    fill_random(b);
    fm1_output_mix(b);
    memcpy(before, &mfx, sizeof mfx);
    for (int blk = 0; blk < 10; ++blk) { fill_random(b); fm1_output_mix(b); }
    /* only the seen/want snapshot may have changed, which the first block took */
    CHECK(memcmp(before, &mfx, sizeof mfx) == 0, "ISR state moved while OFF");
}

/* ---- 2. coefficient derivation (float, control side) against double ---- */
static void test_coefficients(void)
{
    seed(12u * 2654435761u);
    printf("published coefficients match the double-precision derivation\n");
    double worst[4] = {0, 0, 0, 0}, worstq = 0;
    for (unsigned res = 0; res <= 127; res += (res < 8 || res > 120) ? 1 : 7)
        for (int i = 0; i <= 60; ++i) {
            double hz = 20.0 * pow(1000.0, i / 60.0);
            fm1_master_fx_set(FM1_MFX_LP, (float)hz, res);
            const struct mfx_coef *c = &mfx_slot[mfx_pub & 1U];
            double hzf = (double)fm1_master_fx_get_cutoff_hz();
            double g = tan(PI * hzf / FS), k = k_eff(hzf, res), s = g * (g + k), a1 = 1.0 / (1.0 + s);
            double want[4] = {2 * g * a1 * 1073741824.0, 2 * g * g * a1 * 1073741824.0,
                              2 * s * a1 * 1073741824.0, k * 536870912.0};
            int32_t got[4] = {c->ca, c->cb, c->cd, c->ck};
            for (int j = 0; j < 4; ++j) {
                CHECK(got[j] >= 0 && got[j] < 2147483647, "coefficient %d out of range at %.1f Hz res %u", j, hz, res);
                /* half an LSB of quantisation plus float rounding of the derivation */
                double e = fmax(fabs(got[j] - want[j]) - 0.5, 0.0) / want[j];
                if (e > worst[j]) worst[j] = e;
            }
            double q = fabs(k - (double)got[3] / 536870912.0) / k; if (q > worstq) worstq = q;
        }
    printf("  worst relative error beyond quantisation: ca %.2e cb %.2e cd %.2e ck %.2e\n", worst[0], worst[1], worst[2], worst[3]);
    for (int j = 0; j < 4; ++j) CHECK(worst[j] < 5e-6, "coefficient %d relative error %.2e", j, worst[j]);

    printf("api clamping\n");
    fm1_master_fx_set(9, 1.0f, 500);
    CHECK(fm1_master_fx_get_type() == FM1_MFX_OFF, "invalid type is OFF");
    CHECK(fm1_master_fx_get_cutoff_hz() == FM1_MFX_CUTOFF_MIN_HZ, "cutoff clamps low");
    CHECK(fm1_master_fx_get_resonance() == FM1_MFX_RESONANCE_MAX, "resonance clamps high");
    fm1_master_fx_set_cutoff_hz(1e9f);
    CHECK(fm1_master_fx_get_cutoff_hz() == FM1_MFX_CUTOFF_MAX_HZ, "cutoff clamps high");
    fm1_master_fx_set_cutoff_hz(NAN);
    CHECK(fm1_master_fx_get_cutoff_hz() == FM1_MFX_CUTOFF_MIN_HZ, "NaN cutoff clamps to the minimum");
    fm1_master_fx_set(FM1_MFX_BP, 1234.5f, 77);
    CHECK(fm1_master_fx_get_type() == FM1_MFX_BP && fm1_master_fx_get_cutoff_hz() == 1234.5f &&
          fm1_master_fx_get_resonance() == 77, "set/get round trip");
    fm1_master_fx_set_type(FM1_MFX_HP); fm1_master_fx_set_resonance(5); fm1_master_fx_set_cutoff_hz(99.0f);
    CHECK(fm1_master_fx_get_type() == FM1_MFX_HP && fm1_master_fx_get_resonance() == 5 &&
          fm1_master_fx_get_cutoff_hz() == 99.0f, "individual setters keep the other fields");
}

/* ---- 3. measured response against the analytic curve ---- */
#define NTONE 25
#define NFFT 32768
static struct { double ideal[NTONE], l[NTONE], r[NTONE], f[NTONE]; int n; double peak_out; } resp;

static double worst_db_all;
static void measure(unsigned type, double fc_req, unsigned res)
{
    fresh();
    fm1_master_fx_set(type, (float)fc_req, res);
    double fc = (double)fm1_master_fx_get_cutoff_hz();
    int bins[NTONE], n = 0;
    for (int i = 0; i < NTONE - 1; ++i) {
        int b = (int)floor(20.0 * pow(1000.0, i / (double)(NTONE - 2)) * NFFT / FS + 0.5);
        if (b < 1) b = 1;
        if (n && b <= bins[n - 1]) b = bins[n - 1] + 1;
        bins[n++] = b;
    }
    int bf = (int)floor(fc * NFFT / FS + 0.5);
    int dup = 0;
    for (int i = 0; i < n; ++i) if (bins[i] == bf) dup = 1;
    if (!dup) bins[n++] = bf;
    resp.n = n;
    double hmax = 1.0;
    for (int i = 0; i < n; ++i) {
        resp.f[i] = bins[i] * FS / NFFT;
        resp.ideal[i] = ideal_mag(type, fc, res, resp.f[i]);
        if (resp.ideal[i] > hmax) hmax = resp.ideal[i];
    }
    /* Several tones at once keep the run short; Schroeder phases keep the crest low. */
    static double ctab[NFFT], stab[NFFT];
    if (!ctab[0]) for (int j = 0; j < NFFT; ++j) { ctab[j] = cos(2.0 * PI * j / NFFT); stab[j] = sin(2.0 * PI * j / NFFT); }
    double a = 0.6 * FULL / (n * 0.5 + 4.0) / hmax;
    double tau = (1.0 / k_eff(fc, res)) / (PI * fc) * FS;
    size_t settle = ((size_t)(16.0 * tau) + 4096 + 127) / 128 * 128;
    size_t frames = settle + NFFT;
    int32_t *in = malloc(frames * 2 * sizeof *in), *out = malloc(frames * 2 * sizeof *out);
    double cp[NTONE], sp[NTONE], cq[NTONE], sq[NTONE];
    for (int i = 0; i < n; ++i) {
        double p0 = PI * i * i / n;
        cp[i] = cos(p0); sp[i] = sin(p0); cq[i] = cos(1.0 - p0); sq[i] = sin(1.0 - p0);
    }
    for (size_t t = 0; t < frames; ++t) {
        double sl = 0, sr = 0;
        for (int i = 0; i < n; ++i) {
            size_t j = ((size_t)bins[i] * (t % NFFT)) % NFFT;
            sl += stab[j] * cp[i] + ctab[j] * sp[i];   /* sin(ph + p0) */
            sr += stab[j] * cq[i] + ctab[j] * sq[i];   /* sin(ph - p0 + 1) */
        }
        in[2 * t] = (int32_t)floor(a * sl + 0.5);
        in[2 * t + 1] = (int32_t)floor(a * 0.8 * sr + 0.5);
    }
    process(in, out, NULL, frames, NULL, NULL);
    resp.peak_out = 0;
    for (size_t t = settle; t < frames; ++t) {
        double v = fabs((double)out[2 * t]); if (v > resp.peak_out) resp.peak_out = v;
        v = fabs((double)out[2 * t + 1]); if (v > resp.peak_out) resp.peak_out = v;
    }
    for (int i = 0; i < n; ++i) {
        double cl = 0, sl = 0, cr = 0, sr = 0;
        for (size_t t = settle; t < frames; ++t) {
            size_t j = ((size_t)bins[i] * (t % NFFT)) % NFFT;
            cl += out[2 * t] * ctab[j]; sl += out[2 * t] * stab[j];
            cr += out[2 * t + 1] * ctab[j]; sr += out[2 * t + 1] * stab[j];
        }
        resp.l[i] = 2.0 / NFFT * sqrt(cl * cl + sl * sl) / a;
        resp.r[i] = 2.0 / NFFT * sqrt(cr * cr + sr * sr) / (a * 0.8);
    }
    free(in); free(out);
}

static void test_response(void)
{
    printf("measured response matches the analytic curve (LP/HP/BP, 7 cutoffs, 5 resonances)\n");
    static const double fcs[] = {20, 100, 440, 1000, 5000, 12000, 20000};
    static const unsigned ress[] = {0, 32, 64, 96, 127};
    double worst_top = 0, worst_mid = 0, worst_low = 0;
    for (unsigned type = FM1_MFX_LP; type <= FM1_MFX_BP; ++type)
        for (unsigned fi = 0; fi < 7; ++fi)
            for (unsigned ri = 0; ri < 5; ++ri) {
                measure(type, fcs[fi], ress[ri]);
                double wt = 0;
                CHECK(resp.peak_out < FULL - 2, "%s %.0f Hz res %u: output clipped (%.0f)", type_name(type), fcs[fi], ress[ri], resp.peak_out);
                for (int i = 0; i < resp.n; ++i) {
                    for (int ch = 0; ch < 2; ++ch) {
                        double m = ch ? resp.r[i] : resp.l[i], id = resp.ideal[i];
                        if (id >= 1e-3) {
                            double e = fabs(20.0 * log10(m / id));
                            if (id >= 1e-2) { if (e > worst_top) worst_top = e; } else if (e > worst_mid) worst_mid = e;
                            if (e > wt) wt = e;
                            CHECK(e <= (id >= 1e-2 ? 0.005 : 0.05), "%s fc %.0f res %u f %.1f ch %d: ideal %.2f dB, measured %.2f dB",
                                  type_name(type), fcs[fi], ress[ri], resp.f[i], ch, 20 * log10(id), 20 * log10(m));
                        } else {
                            /* below -60 dB: no more than 1 dB over, and floor-limited */
                            double lim = id * 1.12 + 3e-5;
                            if (m > lim && m / (id + 1e-9) > worst_low) worst_low = m / (id + 1e-9);
                            CHECK(m <= lim, "%s fc %.0f res %u f %.1f ch %d: stopband ideal %.1f dB, measured %.1f dB",
                                  type_name(type), fcs[fi], ress[ri], resp.f[i], ch, 20 * log10(id), 20 * log10(m + 1e-12));
                        }
                    }
                }
                if (wt > worst_db_all) worst_db_all = wt;
            }
    printf("  worst error: %.4f dB where the response is above -40 dB, %.4f dB between -60 and -40 dB\n", worst_top, worst_mid);
}

/* ---- 4. extremes: full scale, wild settings, stability ---- */
static double global_max_state;
static void run_extreme(unsigned type, double fc, unsigned res, const int32_t *in, size_t frames, const char *what)
{
    fresh();
    fm1_master_fx_set(type, (float)fc, res);
    int32_t b[FM1_BLOCK_WORDS];
    int bad = 0;
    double mx = 0;
    for (size_t f = 0; f < frames; f += FM1_BLOCK_FRAMES) {
        memcpy(b, in + 2 * f, sizeof b);
        fm1_output_mix(b);
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) if (tap[i] < -8388608 || tap[i] > 8388607) bad = 1;
        double s = max_state_fs(); if (s > mx) mx = s;
    }
    CHECK(!bad, "%s %s %.0f Hz res %u: tap outside 24 bits", what, type_name(type), fc, res);
    CHECK(mfx.guard == 0, "%s %s %.0f Hz res %u: state guard hit %u times", what, type_name(type), fc, res, mfx.guard);
    if (mx > global_max_state) global_max_state = mx;
}
static void test_extremes(void)
{
    seed(13u * 2654435761u);
    printf("stable and overflow-free at extreme settings with full-scale input\n");
    const size_t frames = 44100 / 128 * 128;
    int32_t *in = malloc(frames * 2 * sizeof *in);
    static const double fcs[] = {20, 63, 1000, 9000, 20000};
    static const unsigned ress[] = {0, 100, 127};
    for (unsigned type = FM1_MFX_LP; type <= FM1_MFX_BP; ++type)
        for (unsigned fi = 0; fi < 5; ++fi)
            for (unsigned ri = 0; ri < 3; ++ri) {
                double fc = fcs[fi];
                for (int kind = 0; kind < 7; ++kind) {
                    for (size_t t = 0; t < frames; ++t) {
                        int32_t l, r;
                        double ph = 2.0 * PI * fc * t / FS;
                        switch (kind) {
                        case 0: l = rnd_q23(); r = rnd_q23(); break;                       /* white noise */
                        case 1: l = sin(ph) >= 0 ? 8388607 : -8388608; r = -l - 1; break;   /* square at the cutoff */
                        case 2: l = r = 8388607; break;                                    /* DC */
                        case 3: l = r = (t & 1) ? 8388607 : -8388608; break;               /* Nyquist */
                        case 4: l = (t % 700 == 0) ? 8388607 : 0; r = (t % 700 == 0) ? -8388608 : 0; break;
                        case 5: l = (int32_t)floor(8388607.0 * sin(2.0 * PI * (20.0 * t + 10000.0 * (double)t * t / frames) / FS)); r = -l; break; /* chirp */
                        default: l = (int32_t)floor(8388607.0 * sin(ph * 1.0001)); r = l; break; /* full-scale sine on the cutoff */
                        }
                        in[2 * t] = l; in[2 * t + 1] = r;
                    }
                    run_extreme(type, fc, ress[ri], in, frames, "static");
                }
            }
    printf("  largest integrator state seen: %.1f full scales (worst case by L1 norm 30.5, guard at 64)\n", global_max_state);
    CHECK(global_max_state < 40.0, "state margin too thin: %.1f full scales", global_max_state);

    /* Parameters thrown around every block (and every few) under full-scale noise and tones. */
    printf("random parameter storms under full-scale input\n");
    for (int round = 0; round < 12; ++round) {
        fresh();
        double mx = 0;
        int32_t b[FM1_BLOCK_WORDS];
        for (int blk = 0; blk < 3000; ++blk) {
            if (blk % (1 + round % 4) == 0)
                fm1_master_fx_set(rnd() % 4, (float)(20.0 * pow(1000.0, rnd_unit())), rnd() % 128);
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i)
                b[i] = (round & 1) ? (int32_t)((rnd() & 1) ? 8388607 : -8388608) : rnd_q23();
            fm1_output_mix(b);
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
                if (tap[i] < -8388608 || tap[i] > 8388607) CHECK(0, "storm: tap outside 24 bits");
                if (b[i] < -8388608 || b[i] > 8388607) CHECK(0, "storm: output outside 24 bits");
            }
            double s = max_state_fs(); if (s > mx) mx = s;
        }
        CHECK(mfx.guard == 0, "storm round %d: state guard hit", round);
        if (mx > global_max_state) global_max_state = mx;
    }
    printf("  largest state including the storms: %.1f full scales\n", global_max_state);
    CHECK(global_max_state < 48.0, "state margin too thin after the storms: %.1f full scales", global_max_state);

    /* The filter rings out to silence: no limit cycle, no DC. */
    printf("silence in gives silence out\n");
    static const struct { unsigned type; double fc; unsigned res; } idle[] = {
        {FM1_MFX_LP, 20, 127}, {FM1_MFX_HP, 20, 127}, {FM1_MFX_BP, 20, 127}, {FM1_MFX_LP, 1000, 127},
        {FM1_MFX_HP, 1000, 0}, {FM1_MFX_BP, 15000, 90}, {FM1_MFX_LP, 20000, 127}, {FM1_MFX_HP, 20000, 127}};
    for (unsigned i = 0; i < sizeof idle / sizeof idle[0]; ++i) {
        fresh();
        fm1_master_fx_set(idle[i].type, (float)idle[i].fc, idle[i].res);
        int32_t b[FM1_BLOCK_WORDS];
        for (int blk = 0; blk < 200; ++blk) { fill_random(b); fm1_output_mix(b); }
        size_t quiet = 0;
        int nonzero_after = 0;
        for (int blk = 0; blk < 9000; ++blk) { /* 26 s */
            memset(b, 0, sizeof b); fm1_output_mix(b);
            int nz = 0;
            for (unsigned j = 0; j < FM1_BLOCK_WORDS; ++j) nz |= tap[j] != 0;
            if (!nz) ++quiet; else { quiet = 0; nonzero_after = blk; }
        }
        CHECK(quiet >= 2000, "%s %.0f Hz res %u: not silent, last non-zero block %d",
              type_name(idle[i].type), idle[i].fc, idle[i].res, nonzero_after);
        printf("  %s %5.0f Hz res %3u: exactly zero from block %d (%.1f s)\n", type_name(idle[i].type),
               idle[i].fc, idle[i].res, nonzero_after + 1, (nonzero_after + 1) * 128 / FS);
    }
    free(in);
}

/* ---- 4b. no DC offset from rounding ---- */
static void test_dc(void)
{
    seed(14u * 2654435761u);
    printf("rounding adds no DC offset to a quiet zero-mean signal\n");
    static const unsigned types[] = {FM1_MFX_LP, FM1_MFX_HP, FM1_MFX_BP};
    for (int t = 0; t < 3; ++t)
        for (int ci = 0; ci < 3; ++ci) {
            static const double fcs[] = {40.0, 700.0, 9000.0};
            fresh();
            fm1_master_fx_set(types[t], (float)fcs[ci], 30);
            double sum = 0;
            long n = 0;
            for (int blk = 0; blk < 1300; ++blk) {
                int32_t b[FM1_BLOCK_WORDS];
                for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) b[i] = (int32_t)(rnd() % 7U) - 3; /* -3..3 LSB */
                fm1_output_mix(b);
                if (blk < 300) continue;
                for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) { sum += tap[i]; ++n; }
            }
            CHECK(fabs(sum / n) < 0.1, "%s %.0f Hz: mean output %.3f LSB for a zero-mean input", type_name(types[t]), fcs[ci], sum / n);
        }
}

/* ---- 5. tap independent of volume, no loopback ---- */
static void test_tap(void)
{
    seed(15u * 2654435761u);
    printf("USB out ignores master volume and carries no USB or Bluetooth in\n");
    static const unsigned types[] = {FM1_MFX_OFF, FM1_MFX_LP, FM1_MFX_HP, FM1_MFX_BP};
    for (unsigned ti = 0; ti < 4; ++ti) {
        int32_t engine[40][FM1_BLOCK_WORDS];
        int32_t taps[3][40][FM1_BLOCK_WORDS], outs[3][40][FM1_BLOCK_WORDS];
        for (int b = 0; b < 40; ++b) for (int i = 0; i < FM1_BLOCK_WORDS; ++i)
            engine[b][i] = (int32_t)floor(3e6 * sin(0.05 * (b * 128 + i / 2) * (1 + (i & 1) * 0.3)) + rnd_q23() * 0.05);
        for (int variant = 0; variant < 3; ++variant) {
            fresh();
            fm1_master_fx_set(types[ti], 900.0f, 80);
            for (int b = 0; b < 40; ++b) {
                int32_t blk[FM1_BLOCK_WORDS];
                memcpy(blk, engine[b], sizeof blk);
                mock_level = variant == 0 ? 0 : 127;               /* knob down or up */
                mock_usb_frames = variant == 2 ? FM1_BLOCK_FRAMES : 0;
                mock_bt_frames = variant == 2 ? FM1_BLOCK_FRAMES : 0;
                for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
                    mock_usb_pcm[i] = 8388607 - (int32_t)(i * 1000); mock_bt_pcm[i] = (int16_t)(32767 - (int)i * 10);
                }
                fm1_output_mix(blk);
                memcpy(taps[variant][b], tap, sizeof tap); memcpy(outs[variant][b], blk, sizeof blk);
            }
        }
        CHECK(memcmp(taps[0], taps[1], sizeof taps[0]) == 0, "%s: tap differs between volume 0 and volume max", type_name(types[ti]));
        CHECK(memcmp(taps[1], taps[2], sizeof taps[1]) == 0, "%s: tap differs with USB and Bluetooth input present (loopback)", type_name(types[ti]));
        CHECK(memcmp(outs[1], outs[2], sizeof outs[1]) != 0, "%s: output ignored USB/Bluetooth in", type_name(types[ti]));
        long long e0 = 0, e1 = 0;
        for (int b = 20; b < 40; ++b) for (int i = 0; i < FM1_BLOCK_WORDS; ++i) { e0 += llabs(outs[0][b][i]); e1 += llabs(outs[1][b][i]); }
        CHECK(e0 == 0, "%s: output at master level 0 is not silent (%lld)", type_name(types[ti]), e0);
        CHECK(e1 > 0, "%s: output at master level 127 is silent", type_name(types[ti]));
    }
    /* Engine silent, USB and Bluetooth loud: the tap stays digital zero. */
    for (unsigned ti = 0; ti < 4; ++ti) {
        fresh();
        fm1_master_fx_set(types[ti], 3000.0f, 40);
        mock_usb_frames = FM1_BLOCK_FRAMES; mock_bt_frames = FM1_BLOCK_FRAMES;
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) { mock_usb_pcm[i] = (i & 2) ? 8388607 : -8388608; mock_bt_pcm[i] = (i & 4) ? 32767 : -32768; }
        int nz = 0, outnz = 0;
        for (int b = 0; b < 30; ++b) {
            int32_t blk[FM1_BLOCK_WORDS] = {0};
            fm1_output_mix(blk);
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) { nz |= tap[i] != 0; outnz |= blk[i] != 0; }
        }
        CHECK(!nz, "%s: USB/Bluetooth input leaked into the tap", type_name(types[ti]));
        CHECK(outnz, "%s: USB/Bluetooth input missing from the output", type_name(types[ti]));
    }
}

/* ---- 6. click-free parameter changes ---- */
static double max_d2(const int32_t *x, size_t from, size_t frames, int ch)
{
    double m = 0;
    for (size_t t = from + 2; t < frames; ++t) {
        double d = fabs((double)x[2 * t + ch] - 2.0 * x[2 * (t - 1) + ch] + x[2 * (t - 2) + ch]);
        if (d > m) m = d;
    }
    return m;
}
struct sweep_ctx { unsigned type; double f0, f1, r0, r1; size_t blocks; unsigned every; unsigned mode; };
static void sweep_cb(size_t blk, void *p)
{
    struct sweep_ctx *c = p;
    if (blk % c->every) return;
    double x = (double)blk / c->blocks;
    if (c->mode == 1) { /* random jumps incl. type changes and OFF */
        static unsigned t;
        if (blk == 0) t = c->type;
        t = (blk / c->every) % 7 == 0 ? rnd() % 4 : (rnd() % 3 == 0 ? rnd() % 4 : t);
        fm1_master_fx_set(t, (float)(30.0 * pow(500.0, rnd_unit())), rnd() % 128);
    } else if (c->mode == 3) { /* type and OFF only, cutoff and resonance stay */
        fm1_master_fx_set(rnd() % 4, (float)c->f0, (unsigned)c->r0);
    } else if (c->mode == 2) { /* only jump cutoff and resonance, same type */
        fm1_master_fx_set(c->type, (float)(30.0 * pow(500.0, rnd_unit())), rnd() % 128);
    } else {
        fm1_master_fx_set(c->type, (float)(c->f0 * pow(c->f1 / c->f0, x)), (unsigned)(c->r0 + (c->r1 - c->r0) * x + 0.5));
    }
}
static void make_input(int32_t *in, size_t frames, double a, double f1, double f2)
{
    for (size_t t = 0; t < frames; ++t) {
        in[2 * t] = (int32_t)floor(a * FULL * (sin(2 * PI * f1 * t / FS) + sin(2 * PI * f2 * t / FS)) / 2 + 0.5);
        in[2 * t + 1] = (int32_t)floor(a * FULL * (sin(2 * PI * f1 * t / FS + 1) + sin(2 * PI * f2 * t / FS + 2)) / 2 + 0.5);
    }
}
/* Largest movement of the output within one block of frames. */
static double block_excursion(const int32_t *x, size_t from, size_t frames, int ch)
{
    double m = 0;
    for (size_t t = from + 128; t < frames; ++t) {
        double d = fabs((double)x[2 * t + ch] - x[2 * (t - 128) + ch]);
        if (d > m) m = d;
    }
    return m;
}
static int32_t *click_ref;
static double steady_d2(unsigned type, const int32_t *in, size_t frames, double fc, unsigned res, double *wetdiff)
{
    fresh();
    fm1_master_fx_set(type, (float)fc, res);
    process(in, click_ref, NULL, frames, NULL, NULL);
    for (size_t t = 1200; t < frames; ++t)
        for (int ch = 0; ch < 2; ++ch) {
            double d = fabs((double)click_ref[2 * t + ch] - in[2 * t + ch]);
            if (d > *wetdiff) *wetdiff = d;
        }
    return fmax(max_d2(click_ref, 1200, frames, 0), max_d2(click_ref, 1200, frames, 1));
}
/* The worst steady-state second difference over cutoffs (the tones' own frequencies
 * included, where the resonance peak sits) and resonances. */
static double steady_bound(unsigned type, const int32_t *in, size_t frames, double f1, double f2, double *wetdiff)
{
    double m = 0;
    for (int i = 0; i < 32; ++i) {
        double fc = i == 30 ? f1 : i == 31 ? f2 : 20.0 * pow(1000.0, i / 29.0);
        for (unsigned res = 0; res <= 127; res += 64) m = fmax(m, steady_d2(type, in, frames, fc, res, wetdiff));
    }
    return m;
}
static void test_clicks(void)
{
    seed(16u * 2654435761u);
    printf("parameter changes are click-free\n");
    const size_t frames = 128 * 420;
    int32_t *in = malloc(frames * 2 * sizeof *in), *out = malloc(frames * 2 * sizeof *out);
    click_ref = malloc(frames * 2 * sizeof *click_ref);
    static const struct { const char *name; double a, f1, f2; } sets[] = {
        {"low tones 31 Hz + 83 Hz at 0.04 full scale", 0.04, 31.0, 83.0},
        {"mid tones 733 Hz + 3.1 kHz at 0.03 full scale", 0.03, 733.0, 3100.0}};
    static const unsigned types[] = {FM1_MFX_LP, FM1_MFX_HP, FM1_MFX_BP};
    for (int si = 0; si < 2; ++si) {
        make_input(in, frames, sets[si].a, sets[si].f1, sets[si].f2);
        double d2_in = fmax(max_d2(in, 0, frames, 0), max_d2(in, 0, frames, 1));
        printf(" input: %s, peak d2 %.0f\n", sets[si].name, d2_in);
        double base[3], wetdiff = 0, base_all = 0;
        for (int t = 0; t < 3; ++t) { base[t] = steady_bound(types[t], in, frames, sets[si].f1, sets[si].f2, &wetdiff); base_all = fmax(base_all, base[t]); }
        printf("  steady-state peak d2 over 96 settings: LP %.0f HP %.0f BP %.0f\n", base[0], base[1], base[2]);

        if (si == 0) {
            /* How a hard switch would look, for scale: dry up to the middle, HP after. */
            fresh(); fm1_master_fx_set(FM1_MFX_HP, 300.0f, 20);
            process(in, click_ref, NULL, frames, NULL, NULL);
            int32_t *hard = malloc(frames * 2 * sizeof *hard);
            memcpy(hard, in, frames * 2 * sizeof *hard);
            memcpy(hard + 2 * 40000, click_ref + 2 * 40000, (frames - 40000) * 2 * sizeof *hard);
            double d = max_d2(hard, 2, frames, 0);
            printf("  (a hard dry -> HP switch would show d2 %.0f)\n", d);
            CHECK(d > 20.0 * (2.5 * base_all + 1.5 * block_excursion(hard, 1200, frames, 0) / 128.0 + 8),
                  "the click metric cannot see a hard switch (%.0f vs %.0f)", d, base_all);
            free(hard);
        }
        for (int t = 0; t < 3; ++t) {
            /* Slow sweeps, updated every block and every fifth block. */
            for (int every = 1; every <= 5; every += 4) {
                struct sweep_ctx c = {types[t], 25.0, 18000.0, 20, 100, frames / 128, (unsigned)every, 0};
                fresh(); fm1_master_fx_set(types[t], 25.0f, 20);
                process(in, out, NULL, frames, sweep_cb, &c);
                double d = fmax(max_d2(out, 1200, frames, 0), max_d2(out, 1200, frames, 1));
                printf("  %s sweep 25 Hz -> 18 kHz, resonance 20 -> 100, every %d block%s: peak d2 %.0f (steady %.0f)\n",
                       type_name(types[t]), every, every > 1 ? "s" : "", d, base[t]);
                CHECK(d <= 2.5 * base[t] + 8, "%s sweep clicks: %.0f vs steady %.0f (%s)", type_name(types[t]), d, base[t], sets[si].name);
            }
            /* Cutoff and resonance jumps within one type are spread over one block by the
             * in-block ramp. A retuned resonance releases the energy it stored and rings at its
             * new frequency, which no second-difference bound can tell from a click, so this is
             * reported; the model test below holds the arithmetic to the smooth ideal. */
            struct sweep_ctx c = {types[t], 0, 0, 0, 0, frames / 128, 9, 2};
            fresh(); fm1_master_fx_set(types[t], 500.0f, 64);
            process(in, out, NULL, frames, sweep_cb, &c);
            double d = fmax(max_d2(out, 1200, frames, 0), max_d2(out, 1200, frames, 1));
            printf("  %s random cutoff/resonance jumps every 9 blocks: peak d2 %.0f (steady %.0f; reported only)\n",
                   type_name(types[t]), d, base[t]);
        }
        /* Type changes and OFF <-> ON at a fixed cutoff and resonance ride a one-block
         * crossfade: its only slope change is (wet - dry) / 128 at the ends of the ramp. */
        double dmax = 0, worst_margin = 0, corner = 0;
        for (int r = 0; r < 12; ++r) {
            struct sweep_ctx c = {1 + (unsigned)r % 3, 20.0 * pow(900.0, rnd_unit()), 0, (double)(rnd() % 128), 0, frames / 128, 3, 3};
            fresh();
            fm1_master_fx_set(c.type, (float)c.f0, (unsigned)c.r0);
            process(in, out, NULL, frames, sweep_cb, &c);
            double d = fmax(max_d2(out, 1200, frames, 0), max_d2(out, 1200, frames, 1));
            /* the corner is (wet - dry) / 128: the largest wet - dry this very run reached */
            double wd = 0;
            for (size_t t = 1200; t < frames; ++t)
                for (int ch = 0; ch < 2; ++ch) wd = fmax(wd, fabs((double)out[2 * t + ch] - in[2 * t + ch]));
            /* two corners can meet (fade-out straight into fade-in): twice (wet - dry) / 128, and
             * the wet - dry of an off-grid setting can exceed the grid's, hence the margin */
            double bound = 2.5 * base_all + 4.0 * fmax(wd, wetdiff) / 128.0 + 8;
            dmax = fmax(dmax, d); corner = fmax(corner, wd / 128.0);
            worst_margin = fmax(worst_margin, d / bound);
            /* the mid-tone set's bound is mostly its own resonance peaks: the low-tone set judges */
            if (si == 0) CHECK(d <= bound, "type changes click: %.0f vs bound %.0f (%s)", d, bound, sets[si].name);
        }
        printf("  random type/OFF changes every 3 blocks at fixed cutoff/resonance, 12 runs: peak d2 %.0f (steady %.0f, crossfade corner up to %.0f), worst run at %.0f%% of its bound\n",
               dmax, base_all, corner, worst_margin * 100.0);
    }
    free(in); free(out); free(click_ref);
}

/* ---- 7. fixed point against a double-precision model of the intended behaviour ---- */
/* The model follows the written design: coefficients and crossfade weight move
 * across each block (exactly linear, or by the integer step the stage uses), a type
 * change fades the old filter out and the new one in, a start takes the low-pass
 * state from the first input frame. */
struct model { double s1[2], s2[2], c[4]; unsigned cur; double wet; };
static void model_block(struct model *m, const struct mfx_coef *want, const int32_t *in, double *out, int exact)
{
    double wc[4] = {want->ca / 1073741824.0, want->cb / 1073741824.0, want->cd / 1073741824.0, want->ck / 536870912.0};
    unsigned wt = want->type;
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) out[i] = in[i];
    if (!m->cur && !wt) return;
    if (m->cur != wt && m->wet == 0.0) {
        if (!m->cur) {
            for (int ch = 0; ch < 2; ++ch) { m->s1[ch] = 0; m->s2[ch] = in[ch]; }
            for (int j = 0; j < 4; ++j) m->c[j] = wc[j];
        }
        m->cur = wt;
        if (!wt) return;
    }
    double w0 = m->wet, w1 = m->cur == wt ? 1.0 : 0.0;
    for (int f = 0; f < FM1_BLOCK_FRAMES; ++f) {
        double a = (f + 1) / (double)FM1_BLOCK_FRAMES, c[4], w = w0 + (w1 - w0) * a;
        for (int j = 0; j < 4; ++j) {
            if (exact) { c[j] = m->c[j] + (wc[j] - m->c[j]) * a; continue; }
            /* the stage steps each coefficient by (target - current + 64) >> 7 per frame */
            double sc = j == 3 ? 536870912.0 : 1073741824.0;
            long cq = (long)llround(m->c[j] * sc), wq = (long)llround(wc[j] * sc);
            c[j] = (double)(cq + ((wq - cq + 64) >> 7) * (f + 1)) / sc;
        }
        for (int ch = 0; ch < 2; ++ch) {
            double x = in[2 * f + ch], v3 = x - m->s2[ch];
            double i1 = c[0] * v3 - c[2] * m->s1[ch], i2 = c[0] * m->s1[ch] + c[1] * v3;
            double v1 = m->s1[ch] + i1 / 2, v2 = m->s2[ch] + i2 / 2;
            m->s1[ch] += i1; m->s2[ch] += i2;
            double bp = c[3] * v1, y = m->cur == FM1_MFX_LP ? v2 : m->cur == FM1_MFX_BP ? bp : x - bp - v2;
            y = fmin(fmax(y, -8388608.0), 8388607.0);
            out[2 * f + ch] = x + (y - x) * w;
        }
    }
    for (int j = 0; j < 4; ++j) m->c[j] = wc[j];
    m->wet = w1;
}
static void test_model(void)
{
    seed(17u * 2654435761u);
    printf("fixed point follows the double-precision model through parameter storms\n");
    static const unsigned res_cap[] = {127, 100, 70, 40};
    double worst_all = 0, worst_exact = 0;
    for (int r = 0; r < 48; ++r) {
        int exact = r >= 24, round = r % 24;
        fresh();
        struct model m;
        memset(&m, 0, sizeof m);
        double worst = 0, sum = 0;
        long n = 0;
        int every = 1 + round % 4;
        for (int blk = 0; blk < 1500; ++blk) {
            if (blk % every == 0)
                fm1_master_fx_set(rnd() % 4, (float)(20.0 * pow(1000.0, rnd_unit())), rnd() % (res_cap[round / 6] + 1));
            int32_t in[FM1_BLOCK_WORDS], b[FM1_BLOCK_WORDS];
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
                double tone = 0.25 * FULL * sin(0.02 * (blk * 128 + i / 2) * (1 + (i & 1) * 0.37)) + 0.1 * FULL * sin(0.9 * (blk * 128 + i / 2));
                in[i] = (int32_t)floor(tone + 0.2 * rnd_q23() * (round & 1) + 0.5) | 1; /* never digital silence */
            }
            memcpy(b, in, sizeof b);
            double want[FM1_BLOCK_WORDS];
            model_block(&m, &mfx_slot[mfx_pub & 1U], in, want, exact);
            fm1_output_mix(b);
            if (blk < 3) continue;
            for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
                double e = fabs(tap[i] - want[i]);
                if (e > worst) worst = e;
                sum += e; ++n;
            }
        }
        printf("  %s ramp, round %2d (update every %d block%s, resonance up to %3u): max error %.1f LSB, mean %.3f LSB\n",
               exact ? "exact  " : "integer", round, every, every > 1 ? "s" : "", res_cap[round / 6], worst, sum / n);
        /* integer ramp: rounding only; exact ramp: plus up to 64 LSB (Q30) at the ramp end */
        CHECK(worst < (exact ? 160.0 : 8.0), "round %d: fixed point strays %.1f LSB from the model", round, worst);
        if (exact) { if (worst > worst_exact) worst_exact = worst; } else if (worst > worst_all) worst_all = worst;
    }
    printf("  worst: %.1f LSB against the stage's own ramp, %.1f LSB against an exactly linear one\n", worst_all, worst_exact);
}

/* ---- 8. publication: the ISR never sees a half-written update ---- */
static void test_publication(void)
{
    seed(18u * 2654435761u);
    printf("publication to the ISR is all-or-nothing\n");
    fresh();
    fm1_master_fx_set(FM1_MFX_LP, 800.0f, 50);
    int32_t a[FM1_BLOCK_WORDS], in[FM1_BLOCK_WORDS];
    fill_random(in);
    for (int i = 0; i < 4; ++i) { memcpy(a, in, sizeof a); fm1_output_mix(a); }
    struct mfx_coef settled = mfx.want;
    /* The control task is interrupted half way through filling the other slot. */
    struct mfx_coef *other = &mfx_slot[(mfx_pub + 1U) & 1U];
    other->ca = 123456789; other->cb = 7; other->cd = 5; other->type = FM1_MFX_HP; /* ck not written yet */
    memcpy(a, in, sizeof a); fm1_output_mix(a);
    CHECK(mfx.want.ca == settled.ca && mfx.want.cb == settled.cb && mfx.want.cd == settled.cd &&
          mfx.want.ck == settled.ck && mfx.want.type == settled.type, "ISR consumed a slot that was still being written");
    /* Publishing completes the write; the next block takes it whole. */
    other->ck = 99; mfx_pub = mfx_pub + 1U;
    memcpy(a, in, sizeof a); fm1_output_mix(a);
    CHECK(mfx.want.ca == 123456789 && mfx.want.ck == 99 && mfx.want.type == FM1_MFX_HP, "ISR did not take the published slot");
    CHECK(mfx.seen == mfx_pub, "seen tracks pub");
    /* Reset keeps the settings and restarts from the live input. */
    fm1_master_fx_set(FM1_MFX_BP, 2000.0f, 10);
    memcpy(a, in, sizeof a); fm1_output_mix(a);
    fm1_output_reset();
    CHECK(mfx.cur == FM1_MFX_OFF && mfx.wet == 0, "reset leaves the filter running");
    memcpy(a, in, sizeof a); fm1_output_mix(a);
    CHECK(mfx.cur == FM1_MFX_BP, "filter did not restart after reset");
    CHECK(fm1_master_fx_get_type() == FM1_MFX_BP && fm1_master_fx_get_cutoff_hz() == 2000.0f, "reset lost the settings");
}

int main(void)
{
    if (getenv("FILTER_TEST_SEED")) seed_base = (uint32_t)strtoul(getenv("FILTER_TEST_SEED"), NULL, 10);
    test_bypass();
    test_coefficients();
    test_response();
    test_extremes();
    test_dc();
    test_tap();
    test_clicks();
    test_model();
    test_publication();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
