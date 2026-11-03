/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX747_ROUTE_H
#define DX747_ROUTE_H
/* DX747 router core: MIDI channel masks, layering, key splits, octave shift,
 * control channels, held-note bookkeeping, voice budget and paced posting.
 *
 * Portable C with no engine headers, so the same file is included by
 * app/midi_router.c on the device and by tools/dx747/host-test/router.
 * The engine owns one input channel per track and cannot split or transpose,
 * so every engine keeps its private channel and this code rewrites messages
 * onto it.
 *
 * Threading. A struct dx_router has two sides. The PRODUCER (dx_router_event,
 * dx_router_key, dx_router_release_all) owns the held-note table and all
 * routing state; call it from one task only. Messages go straight to the
 * engine while the pacing budget allows, otherwise into a pending ring. The
 * CONSUMER (dx_router_poll, control loop) drains that ring under its own
 * budget. The two sides share only the ring indices and a few counters, all
 * word-sized, and never write each other's state. Build one router for
 * external MIDI (producer: routing task) and one for the built-in keys
 * (producer: control task). */
#include <stdint.h>
#include <string.h>

#ifdef FM1_HOST_TEST
#include <stdatomic.h>
#define DX_A(t) _Atomic(t)
#define DX_BARRIER() atomic_thread_fence(memory_order_seq_cst)
#else
#define DX_A(t) volatile t
#define DX_BARRIER() __asm__ volatile("csync" ::: "memory")
#endif
#define DX_FN static __attribute__((unused))

/* The pending ring holds DX_PEND_MAX messages. Past DX_PEND_SOFT only releases
 * are accepted (note off, sustain up, all notes off): a note on that cannot be
 * queued is never started, so a note can be refused but never left hanging. */
enum { DX_ENGINES = 8, DX_HELD_MAX = 64, DX_PEND_MAX = 128, DX_PEND_SOFT = 64, DX_LOG_MAX = 12 };
enum { DX_SRC_DIN, DX_SRC_USB, DX_SRC_BLE, DX_SRC_KEYS, DX_SRC_COUNT };
/* What a note does when shift pushes it outside 0..127. */
enum { DX_FOLD_DROP, DX_FOLD_CLIP, DX_FOLD_WRAP };
/* The engine drains its input ring (about 64 entries) once per 2.9 ms block and
 * resets every voice on overflow. Four budgets can be alive at once (external
 * direct, external drain, keys direct, keys drain); each may burst DX_TOKENS
 * messages and then sustains DX_RATE per millisecond, so the worst case in any
 * block is 4 * (DX_TOKENS + DX_RATE * 2.9) = 59, below the 63 usable slots. */
enum { DX_TOKENS = 6, DX_RATE = 3 };

struct dx_engine_cfg {
    uint16_t rx;      /* bit c: listens on MIDI channel c + 1 */
    uint8_t lo, hi;   /* note window that is played, inclusive */
    int8_t shift;     /* semitones added to every played note */
    uint8_t fold;     /* DX_FOLD_* */
    uint8_t ctl;      /* control channel 1..16 for CC/PC/bend, 0 = none */
    uint8_t poly;     /* voice limit for this engine, 1..8 */
    uint8_t mute;     /* muted engines are not sent notes */
};
struct dx_cfg {
    struct dx_engine_cfg e[DX_ENGINES];
    uint8_t priv[DX_ENGINES]; /* the engine's own input channel 0..15 */
    uint8_t up;               /* bit e: engine e exists (memory permitting) */
    uint8_t kbd_ch;           /* channel the built-in keys play on, 0..15 */
    uint8_t pool;             /* voices in use across all engines before stealing */
};
#ifdef FM1_HOST_TEST
typedef _Atomic(const struct dx_cfg *) dx_cfgp;
#else
typedef const struct dx_cfg *volatile dx_cfgp; /* the pointer is volatile, not the configuration */
#endif

typedef int (*dx_post_fn)(const uint8_t *message, unsigned bytes);

struct dx_note {
    uint8_t key;    /* source << 4 | channel */
    uint8_t note;   /* note as played */
    uint8_t eng, out; /* engine and the note it was sent as */
    uint8_t sust;   /* released but held by the sustain pedal */
    uint32_t seq;
};
/* A pending message is one word (status | d1 << 8 | d2 << 16 | bytes << 24) so a
 * coalescing update and the consumer's read can never tear. */
DX_FN uint32_t dx_pack(const uint8_t *m, unsigned n) { return (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)(n > 2 ? m[2] : 0) << 16 | (uint32_t)n << 24; }
struct dx_log { uint32_t ms; uint8_t src, ch, status, d1, d2; };

struct dx_router {
    const dx_cfgp *cfg;
    const DX_A(uint32_t) *panic_epoch;   /* bumped to cut everything (any task) */
    dx_post_fn post;
    const struct dx_router *peer;        /* other router, read for the voice budget only */

    /* producer side */
    struct dx_note held[DX_HELD_MAX];
    DX_A(unsigned) nheld;
    uint32_t seq;
    uint16_t pedal[DX_SRC_COUNT];
    uint8_t pedal_eng[DX_SRC_COUNT][16]; /* engines that were sent this pedal-down */
    unsigned tokens, started;
    uint32_t last_ms;
    uint32_t panic_seen_p, drop_seen;
    uint32_t dropped, steals;
    uint16_t act_seen;
    uint32_t act_ms[16];
    uint8_t act_src[16];
    struct dx_log log[DX_LOG_MAX];
    unsigned log_n;
    DX_A(unsigned) pw;                   /* pending ring write index */
    DX_A(uint32_t) need_seq, fail_p;
    DX_A(uint32_t) pend[DX_PEND_MAX];

    /* consumer side */
    DX_A(unsigned) pr;                   /* pending ring read index */
    unsigned ctok, cstarted, rec_next;
    uint32_t clast_ms, panic_seen_c, need_done, rec_mark;
    DX_A(uint32_t) drop_seq, drop_mark, fail_c;
};


/* ---- router API implemented in app/midi_router.c --------------------------
 * Control task unless noted. The UI and the scene store use only these. */
void dx_route_init(void);
const struct dx_cfg *dx_route_cfg(void);           /* active configuration, read only */
void dx_route_publish(const struct dx_cfg *c);     /* copy and activate; affects new notes only */
void dx_route_key(unsigned note, unsigned on);     /* built-in keyboard */
void dx_route_panic(void);                         /* cut every engine; any task */
int dx_route_active(unsigned channel);             /* notes seen on 0..15 in the last 1.2 s */
unsigned dx_route_source(unsigned channel);        /* DX_SRC_* that last used it */
unsigned dx_route_log(struct dx_log *out, unsigned max); /* newest first, both routers merged */
unsigned dx_route_voices(void);                    /* notes the routers believe are sounding */
uint32_t dx_route_dropped(void);                   /* refused or lost messages, both routers */

#endif
