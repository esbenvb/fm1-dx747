/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_AUDIO_OUTPUT_H
#define FM1_AUDIO_OUTPUT_H
#include "fm1.h"
#include "dx747/master_fx.h"
/* Reset before start or while the audio ISR is excluded. */
void fm1_output_reset(void);
/* Finish one synthesized block: master filter, USB recording tap (the filtered
 * engine block before master gain, so no USB/Bluetooth input), master gain,
 * then USB and Bluetooth input. Audio ISR only; output holds FM1_BLOCK_WORDS
 * Q23 samples. Master filter settings: dx747/master_fx.h. */
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output);
#endif
