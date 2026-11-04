/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The real engine behind struct dx_port. Included by app/project.c after
 * instrument.h, dx747/route.h and dx747/scene.h. Control task only. */
#include "master_fx.h"

static int port_export(unsigned e, uint8_t out[DX_PRESET_BYTES])
{
    int r = groove_timbre_export(e, out, DX_PRESET_BYTES);
    /* the scene stores the voice as it is and the BRIGHT offset next to it */
    if (!r) dx_bright_export_fix(&ctl, e, out + offsetof(struct groove_preset, data));
    return r;
}
static int port_import(unsigned e, const uint8_t in[DX_PRESET_BYTES])
{ return groove_timbre_import(e, in, DX_PRESET_BYTES); }
static int port_muted(unsigned e)
{
    struct groove_view v;
    groove_get_view(e, 0, &v);
    return v.muted;
}
static void port_toggle(unsigned e) { groove_mix_toggle(e, 0); }
static void port_fx_get(uint8_t v[4]) { groove_effects_get(v); }
static void port_fx_set(const uint8_t v[4])
{
    fm1_engine_pause(); /* groove_effects_restore wants the audio gate held */
    groove_effects_restore(v);
    fm1_engine_resume();
}
/* A controller on the engine's private channel (pan, expression, ...). */
static void port_cc(unsigned e, unsigned controller, unsigned value)
{
    const struct dx_cfg *c = dx_route_cfg();
    uint8_t m[3] = {(uint8_t)(0xb0U | c->priv[e]), (uint8_t)controller, (uint8_t)value};
    (void)fm1_midi_post(m, 3);
}
/* The preset was just imported, so the voice is new: start from it. */
static void port_bright(unsigned e, int offset)
{
    if (ctl.port->refresh(e)) ctl.refresh |= (uint8_t)(1U << e); /* the engine panicked and loaded a voice: let its controller state apply */
    dx_bright_apply(&ctl, e, offset, 1);
}
static void port_filter(unsigned type, unsigned hz, unsigned resonance)
{ fm1_master_fx_set(type, hz ? (float)hz : FM1_MFX_CUTOFF_DEFAULT_HZ, resonance); }
static void port_panic(void) { dx_route_panic(); }
static void port_publish(const struct dx_cfg *c) { dx_route_publish(c); }

static const struct dx_port dx_device_port = {
    port_export, port_import, port_muted, port_toggle, port_fx_get, port_fx_set,
    port_cc, port_bright, port_filter, port_panic, port_publish
};
