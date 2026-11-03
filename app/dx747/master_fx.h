/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 master-bus filter (state-variable, stereo, after the engine block and
 * before the USB recording tap and master volume). See master_fx.inc.
 *
 * Control task only, never from the audio ISR. Every call publishes a fresh
 * coefficient set that the ISR picks up at the next block boundary and ramps
 * towards inside that block, so changes are click-free; type changes and
 * OFF <-> ON fade the filtered signal in or out over one block (2.9 ms).
 * OFF is the default and costs a few instructions per block. */
#ifndef DX747_MASTER_FX_H
#define DX747_MASTER_FX_H
#include "fm1.h"

enum fm1_master_fx_type {
    FM1_MFX_OFF, /* bypass, bit-exact */
    FM1_MFX_LP,  /* 12 dB/oct low-pass, resonance peak of Q at the cutoff */
    FM1_MFX_HP,  /* 12 dB/oct high-pass, resonance peak of Q at the cutoff */
    FM1_MFX_BP,  /* band-pass, unity gain at the cutoff at any resonance */
    FM1_MFX_TYPES
};
#define FM1_MFX_CUTOFF_MIN_HZ 20.0f
#define FM1_MFX_CUTOFF_MAX_HZ 20000.0f
#define FM1_MFX_CUTOFF_DEFAULT_HZ 1000.0f
/* Resonance 0..127 (MIDI CC71 range) maps exponentially to damping
 * k = 1/Q = 2^(0.5 - 4.5 * resonance / 127): 0 is Butterworth (Q 0.7071, flat),
 * 127 is Q 16. Near Nyquist the internal state grows with Q / cos(pi fc / fs),
 * so Q is capped at 24 cos(pi fc / fs): no effect below 12 kHz, Q 11.6 at
 * 15 kHz, 6.9 at 18 kHz, 3.5 at 20 kHz. */
#define FM1_MFX_RESONANCE_MAX 127U

/* Values outside the range are clamped (NaN cutoff becomes the minimum); a type
 * of FM1_MFX_TYPES or more selects OFF. The three-argument form publishes once,
 * for scene recall. Rates assume FM1_SAMPLE_RATE (44,100 Hz). */
void fm1_master_fx_set(unsigned type, float cutoff_hz, unsigned resonance);
void fm1_master_fx_set_type(unsigned type);
unsigned fm1_master_fx_get_type(void);
void fm1_master_fx_set_cutoff_hz(float cutoff_hz);
float fm1_master_fx_get_cutoff_hz(void);
void fm1_master_fx_set_resonance(unsigned resonance);
unsigned fm1_master_fx_get_resonance(void);
#endif
