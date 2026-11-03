/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 boot recipe. A fresh engine boots as {DX7, 303, PSG, pluck, sample,
 * DX7, 303, 808}, every track NORMAL, each on its stock input channel. DX747
 * wants eight DX7 tracks in the PLAY role, leaves the input channels alone (so
 * groove_midi_input_set, which pauses audio and panics every track, is never
 * called) and never goes through groove_project_apply (peak heap is higher).
 *
 * Needs instrument.h on the device, or a mock declaring the same names in the
 * host tests. Included after route.inc for DX_FN. */
struct dx_boot {
    uint8_t up;                 /* bit t: track t is a live DX7 engine */
    int8_t err[GROOVE_TRACKS];  /* first failing call's result per track, 0 when fine */
};

/* PLAY first (it keeps the current source and clears the mute bit), then one
 * track at a time convert the source to DX7. A failure (usually memory) leaves
 * that engine out; the others still come up. Returns the number of engines up. */
DX_FN unsigned dx_boot_engines(struct dx_boot *b)
{
    unsigned count = 0;
    memset(b, 0, sizeof *b);
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        int r = groove_track_role_set(t, GROOVE_TRACK_PLAY);
        if (!r) r = groove_source_set(t, GROOVE_DX7);
        b->err[t] = (int8_t)(r < -127 ? -127 : r > 127 ? 127 : r);
        if (!r) { b->up |= (uint8_t)(1U << t); ++count; }
    }
    return count;
}
