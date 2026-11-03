/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 master filter: fixed-point trapezoidal (TPT) state-variable filter on the
 * summed engine block, included by app/audio_output.c and by tools/dx747/host-test/filter.
 *
 * Audio ISR side (FM1_AUDIO_CODE or always_inline, SRAM data only, no calls, no
 * libm, no int64 multiply except 32x32 -> 64 widening, constant shifts only):
 *   Zavalishin/Cytomic form, per channel and sample, with coefficients that
 *   are all below 2 and therefore fit int32 in Q30:
 *     v3 = x - ic2;  v1 = a1 ic1 + a2 v3;  v2 = ic2 + a2 ic1 + a3 v3
 *     ic1 += 2 (a2 v3 - (1 - a1) ic1);  ic2 += 2 (a2 ic1 + a3 v3)
 *     LP = v2, BP = k v1 (unity peak gain), HP = x - k v1 - v2
 *   The integrators are int64 in Q55 (a Q23 sample in the high word) carrying
 *   a +0.5 LSB bias, so the high word is the rounded sample. Accumulating the
 *   increments at full precision keeps the filter exact down to 20 Hz, where
 *   a3 is about 2e-6. Headroom is 256 full scales. The trapezoidal memories
 *   grow as Q / cos(pi fc / fs) near Nyquist (2.3 times the Q of the output
 *   at 18 kHz, 6.8 times at 20 kHz), so the control side caps Q at
 *   24 cos(pi fc / fs): worst-case memory 30 full scales (impulse response
 *   L1 norm), guarded at 64. Rounding the feedback views can leave a limit
 *   cycle of a few LSB at high Q; it is snapped to zero when the input block
 *   is digital silence.
 *   Coefficient interpolation within the block, and a one-block crossfade for
 *   type changes and OFF <-> ON, keep parameter changes free of clicks.
 * Control side: float coefficient math (own sin/exp2 polynomials, no libm so the
 * image does not grow) published through a two-slot sequence counter. */
#include "master_fx.h"

#define MFX_PI 3.14159265f
#define MFX_FULL 65536                           /* crossfade weight, wet only */
#define MFX_STEP (MFX_FULL / FM1_BLOCK_FRAMES)   /* weight change per frame */
#define MFX_BIAS ((int64_t)1 << 31)              /* rounds the Q55 high word */
#define MFX_GUARD ((int64_t)1 << 61)             /* 64 full scales */
#define MFX_IDLE ((int64_t)1 << 36)              /* 16 LSB: silent-input snap */

struct mfx_coef { int32_t ca, cb, cd, ck; uint32_t type; };

/* Control task -> ISR. The ISR copies slot[pub & 1] and re-checks pub; the
 * writer only ever fills the other slot, then bumps pub. */
static struct mfx_coef mfx_slot[2];
static volatile uint32_t mfx_pub;

/* ISR private. cur is the type being run, OFF when idle (then wet is 0). */
static struct {
    int64_t s1[2], s2[2];   /* band, low integrators per channel (Q55 + bias) */
    int32_t ca, cb, cd, ck; /* 2 a2 (Q30), 2 a3 (Q30), 2 (1 - a1) (Q30), k (Q29) */
    uint32_t seen;
    int32_t wet;            /* 0 or MFX_FULL between blocks */
    uint32_t cur;
    uint32_t guard;         /* clamps of runaway state, expected to stay 0 */
    struct mfx_coef want;
} mfx;

static inline __attribute__((always_inline)) int32_t mfx_clip24(int32_t v)
{
    if (((uint32_t)v + 8388608U) >> 24)
        return v < 0 ? -8388608 : 8388607;
    return v;
}

static inline __attribute__((always_inline)) int32_t mfx_tick(
    int64_t *s1, int64_t *s2, int32_t x,
    int32_t ca, int32_t cb, int32_t cd, int32_t ck, unsigned mode)
{
    int32_t w1 = (int32_t)(*s1 >> 32), w2 = (int32_t)(*s2 >> 32);
    int32_t v3 = x - w2;
    /* Half the Q55 step: 2 (a2 v3 - ...) is (Q53 sum) * 4, the output uses half. */
    int64_t t1 = ((int64_t)ca * v3 - (int64_t)cd * w1) * 2;
    int64_t t2 = ((int64_t)ca * w1 + (int64_t)cb * v3) * 2;
    int64_t v1 = *s1 + t1, v2 = *s2 + t2;
    int32_t lp = (int32_t)(v2 >> 32);
    *s1 = v1 + t1;
    *s2 = v2 + t2;
    if (mode == FM1_MFX_LP) return mfx_clip24(lp);
    int32_t bp = (int32_t)(((int64_t)(int32_t)(v1 >> 32) * ck + ((int32_t)1 << 28)) >> 29);
    if (mode == FM1_MFX_BP) return mfx_clip24(bp);
    return mfx_clip24(x - bp - lp);
}

/* One channel of one block of one filter type. slow: coefficients and/or the
 * crossfade move. One channel per pass keeps the state in registers. */
static inline __attribute__((always_inline)) int32_t mfx_block(
    int32_t *blk, unsigned ch, unsigned mode, unsigned slow, int32_t w,
    int32_t dw, const int32_t d[4])
{
    int32_t any = 0;
    int64_t s1 = mfx.s1[ch], s2 = mfx.s2[ch];
    int32_t ca = mfx.ca, cb = mfx.cb, cd = mfx.cd, ck = mfx.ck;
    for (unsigned f = ch; f < FM1_BLOCK_WORDS; f += 2) {
        if (slow) {
            ca += d[0]; cb += d[1]; cd += d[2]; ck += d[3]; w += dw;
        }
        int32_t x = blk[f];
        any |= x;
        int32_t y = mfx_tick(&s1, &s2, x, ca, cb, cd, ck, mode);
        if (w != MFX_FULL)
            y = x + (int32_t)(((int64_t)(y - x) * w) >> 16);
        blk[f] = y;
    }
    mfx.s1[ch] = s1; mfx.s2[ch] = s2;
    return any;
}

/* One loop per type, shared by both channels (a call each, so no code twice). */
static FM1_AUDIO_CODE int32_t mfx_lp(int32_t *blk, unsigned ch, unsigned slow, int32_t w, int32_t dw, const int32_t d[4])
{
    return mfx_block(blk, ch, FM1_MFX_LP, slow, w, dw, d);
}
static FM1_AUDIO_CODE int32_t mfx_bp(int32_t *blk, unsigned ch, unsigned slow, int32_t w, int32_t dw, const int32_t d[4])
{
    return mfx_block(blk, ch, FM1_MFX_BP, slow, w, dw, d);
}
static FM1_AUDIO_CODE int32_t mfx_hp(int32_t *blk, unsigned ch, unsigned slow, int32_t w, int32_t dw, const int32_t d[4])
{
    return mfx_block(blk, ch, FM1_MFX_HP, slow, w, dw, d);
}

static inline __attribute__((always_inline)) int mfx_small(int64_t s)
{
    return (uint64_t)(s - MFX_BIAS + MFX_IDLE) < (uint64_t)MFX_IDLE * 2U;
}

static inline __attribute__((always_inline)) int64_t mfx_guard(int64_t v)
{
    if (v > MFX_GUARD) { ++mfx.guard; return MFX_GUARD; }
    if (v < -MFX_GUARD) { ++mfx.guard; return -MFX_GUARD; }
    return v;
}

/* Audio ISR, in place, once per block, before anything reads the block. */
static FM1_AUDIO_CODE void mfx_run(int32_t *blk)
{
    uint32_t pub = mfx_pub;
    if (pub != mfx.seen) {
        const struct mfx_coef *s = &mfx_slot[pub & 1U];
        struct mfx_coef c;
        c.ca = s->ca; c.cb = s->cb; c.cd = s->cd; c.ck = s->ck; c.type = s->type;
        __asm__ volatile("" ::: "memory");
        if (mfx_pub == pub) {
            mfx.want.ca = c.ca; mfx.want.cb = c.cb; mfx.want.cd = c.cd;
            mfx.want.ck = c.ck; mfx.want.type = c.type;
            mfx.seen = pub;
        }
    }
    uint32_t want = mfx.want.type;
    if (!mfx.cur && !want) return; /* bypass: the block is left untouched */

    if (mfx.cur != want && !mfx.wet) { /* idle or fully faded out: switch now */
        if (!mfx.cur) { /* start: low-pass state follows the input, no step */
            mfx.s1[0] = MFX_BIAS; mfx.s1[1] = MFX_BIAS;
            mfx.s2[0] = (int64_t)blk[0] * 4294967296LL + MFX_BIAS;
            mfx.s2[1] = (int64_t)blk[1] * 4294967296LL + MFX_BIAS;
            mfx.ca = mfx.want.ca; mfx.cb = mfx.want.cb;
            mfx.cd = mfx.want.cd; mfx.ck = mfx.want.ck;
        }
        mfx.cur = want;
        if (!want) return;
    }
    int32_t w = mfx.wet, dw = 0;
    if (mfx.cur == want) { if (w != MFX_FULL) dw = MFX_STEP; }
    else if (w) dw = -MFX_STEP;
    int32_t d[4];
    d[0] = (mfx.want.ca - mfx.ca + 64) >> 7; d[1] = (mfx.want.cb - mfx.cb + 64) >> 7;
    d[2] = (mfx.want.cd - mfx.cd + 64) >> 7; d[3] = (mfx.want.ck - mfx.ck + 64) >> 7;
    unsigned slow = (d[0] | d[1] | d[2] | d[3] | dw) != 0;
    int32_t any;
    switch (mfx.cur) {
    case FM1_MFX_LP: any = mfx_lp(blk, 0, slow, w, dw, d) | mfx_lp(blk, 1, slow, w, dw, d); break;
    case FM1_MFX_BP: any = mfx_bp(blk, 0, slow, w, dw, d) | mfx_bp(blk, 1, slow, w, dw, d); break;
    default: any = mfx_hp(blk, 0, slow, w, dw, d) | mfx_hp(blk, 1, slow, w, dw, d); break;
    }
    mfx.ca = mfx.want.ca; mfx.cb = mfx.want.cb; mfx.cd = mfx.want.cd; mfx.ck = mfx.want.ck;
    mfx.wet = w + dw * (int32_t)FM1_BLOCK_FRAMES;
    int idle = !any;
    for (unsigned c = 0; c < 2; ++c) {
        mfx.s1[c] = mfx_guard(mfx.s1[c]);
        mfx.s2[c] = mfx_guard(mfx.s2[c]);
        idle = idle && mfx_small(mfx.s1[c]) && mfx_small(mfx.s2[c]);
    }
    if (idle) { /* digital silence in and only rounding residue left: stop the ring */
        mfx.s1[0] = mfx.s1[1] = mfx.s2[0] = mfx.s2[1] = MFX_BIAS;
    }
}

/* Reset before start or while the audio ISR is excluded; settings are kept. */
static void mfx_reset(void)
{
    mfx.cur = FM1_MFX_OFF;
    mfx.wet = 0;
}

/* ---- control task ---- */

/* sin on [-pi/2, pi/2], odd Taylor series to x^13: error below 1e-8. */
static float mfx_sin(float x)
{
    float q = x * x;
    return x * (1.0f + q * (-1.0f / 6.0f + q * (1.0f / 120.0f + q * (-1.0f / 5040.0f +
           q * (1.0f / 362880.0f + q * (-1.0f / 39916800.0f + q * (1.0f / 6227020800.0f)))))));
}

/* 2^e for e in [-8, 8): integer part through the exponent bits, fraction on
 * [-0.5, 0.5] with a degree 6 Taylor series (error 1.2e-7). */
static float mfx_exp2(float e)
{
    int n = (int)(e + 8.5f) - 8;
    float a = (e - (float)n) * 0.693147181f;
    float p = 1.0f + a * (1.0f + a * (0.5f + a * (1.0f / 6.0f + a * (1.0f / 24.0f +
              a * (1.0f / 120.0f + a * (1.0f / 720.0f))))));
    union { uint32_t u; float f; } s;
    s.u = (uint32_t)(n + 127) << 23;
    return p * s.f;
}

/* v in [0, 2): Q30 (or Q29 with scale 2^29). */
static int32_t mfx_q(float v, float scale)
{
    if (v > 1.9999f) v = 1.9999f;
    return (int32_t)(v * scale + 0.5f);
}

static struct { uint32_t type, res; float hz; } mfx_cfg = {
    FM1_MFX_OFF, 0, FM1_MFX_CUTOFF_DEFAULT_HZ
};

static void mfx_publish(void)
{
    float x = MFX_PI * mfx_cfg.hz / (float)FM1_SAMPLE_RATE; /* 0.0014 .. 1.4247 */
    float cx = mfx_sin(1.57079633f - x);                    /* cos x, 1 .. 0.146 */
    float g = mfx_sin(x) / cx;                              /* tan, prewarped */
    float k = mfx_exp2(0.5f - (4.5f / 127.0f) * (float)mfx_cfg.res);
    if (k * 24.0f * cx < 1.0f) k = 1.0f / (24.0f * cx);     /* Q <= 24 cos x, see above */
    float s = g * (g + k), a1 = 1.0f / (1.0f + s);
    struct mfx_coef *c = &mfx_slot[(mfx_pub + 1U) & 1U];
    c->ca = mfx_q(2.0f * g * a1, 1073741824.0f);
    c->cb = mfx_q(2.0f * g * g * a1, 1073741824.0f);
    c->cd = mfx_q(2.0f * s * a1, 1073741824.0f);
    c->ck = mfx_q(k, 536870912.0f);
    c->type = mfx_cfg.type;
    __asm__ volatile("" ::: "memory");
    mfx_pub = mfx_pub + 1U;
}

static void mfx_store(unsigned type, float hz, unsigned res)
{
    mfx_cfg.type = type < FM1_MFX_TYPES ? type : FM1_MFX_OFF;
    if (!(hz >= FM1_MFX_CUTOFF_MIN_HZ)) hz = FM1_MFX_CUTOFF_MIN_HZ; /* also NaN */
    if (hz > FM1_MFX_CUTOFF_MAX_HZ) hz = FM1_MFX_CUTOFF_MAX_HZ;
    mfx_cfg.hz = hz;
    mfx_cfg.res = res > FM1_MFX_RESONANCE_MAX ? FM1_MFX_RESONANCE_MAX : res;
    mfx_publish();
}

void fm1_master_fx_set(unsigned type, float cutoff_hz, unsigned resonance)
{
    mfx_store(type, cutoff_hz, resonance);
}
void fm1_master_fx_set_type(unsigned type) { mfx_store(type, mfx_cfg.hz, mfx_cfg.res); }
unsigned fm1_master_fx_get_type(void) { return mfx_cfg.type; }
void fm1_master_fx_set_cutoff_hz(float hz) { mfx_store(mfx_cfg.type, hz, mfx_cfg.res); }
float fm1_master_fx_get_cutoff_hz(void) { return mfx_cfg.hz; }
void fm1_master_fx_set_resonance(unsigned res) { mfx_store(mfx_cfg.type, mfx_cfg.hz, res); }
unsigned fm1_master_fx_get_resonance(void) { return mfx_cfg.res; }
