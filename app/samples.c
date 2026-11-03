/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 has no sampler. The flash that held the stock sample bank now holds the
 * scene store and settings (dx747/scene.inc), so none of the stock sample
 * transfer may touch it: every writer below refuses, and the workbench's sample
 * commands (transfer commands 11 to 16) therefore fail without writing. The
 * sealed engine still links against groove_samples_init and _poll, and defines
 * groove_samples_stop itself. */
#include "instrument.h"

void groove_samples_init(void) {}
void groove_samples_poll(void) {}
unsigned groove_samples_status(void) { return 0; } /* empty */
unsigned groove_samples_size(void) { return 0; }
int groove_samples_begin(unsigned bytes) { (void)bytes; return GROOVE_PROJECT_INVALID; }
int groove_samples_write(unsigned offset, const uint8_t *data, unsigned bytes)
{ (void)offset; (void)data; (void)bytes; return GROOVE_PROJECT_INVALID; }
int groove_samples_finish(void) { return GROOVE_PROJECT_INVALID; }
void groove_samples_cancel(void) {}
int groove_samples_read(unsigned offset, uint8_t *data, unsigned bytes)
{ (void)offset; (void)data; (void)bytes; return GROOVE_PROJECT_INVALID; }
