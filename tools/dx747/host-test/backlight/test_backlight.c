/* Host test for the DX747 panel LED brightness (app/board/fm1_input.h) and the
 * power-on chords, safe boot and the ROM update (app/board/panel.c).
 *
 * The real scan interrupt, LED latch and panel.c run against a model of the
 * hardware: mocked GPIO registers, the scan shift register (what the DMA'd word
 * selects is the column that is physically live) and the 11 x 6 button matrix.
 * After every tick the lit LED lines are compared with an independent model of
 * the API calls, so a LED latched in the wrong column, phase or level fails.
 *
 * Hardware-unverified: absolute brightness and visible flicker need the device. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_hw.h"
#include "build/panel_real.c" /* the real panel.c, includes fm1_input.h */

static unsigned failures, checks;
#define CHECK(cond, ...) do { \
    ++checks; \
    if (!(cond)) { \
        if (++failures <= 25) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    } \
} while (0)

/* ---- Hardware model ---------------------------------------------------- */

static uint8_t contact[FM1_NCOL];            /* bit r: contact (column, row r) closed */
static unsigned hw_col;                      /* column the scan shift register has selected */
static unsigned scan_frame;                  /* completed 11-column scans */
static unsigned total_ticks, release_at;     /* release_at: tick at which all contacts open */
static unsigned recovery_calls, recovery_frame;
static int timer_running = 1;

struct tick_info {
    unsigned col, phase, frame, lit;
    uint32_t pa0, ph0, pa1, ph1;
};
static void (*observer)(const struct tick_info *);

/* The API model, kept independently of the implementation's tables. */
static int model_own[FM1_NKEY];              /* -1 follows idle, else 0..8 */
static unsigned model_idle;

static unsigned key_col(unsigned key) { return fm1_key_pin[key] >> 4; }
static unsigned key_row(unsigned key) { return fm1_key_pin[key] & 15u; }
static unsigned model_level(unsigned key) { return model_own[key] < 0 ? model_idle : (unsigned)model_own[key]; }
/* Frame p lights a level-L LED once per floor((p+1)L/8)-floor(pL/8). */
static unsigned duty_on(unsigned level, unsigned phase) { return ((phase + 1u) * level) / 8u - (phase * level) / 8u; }

static unsigned expected_rows(unsigned col, unsigned phase)
{
    unsigned rows = 0;
    for (unsigned key = 0; key < FM1_NKEY; key++)
        if (key_col(key) == col && duty_on(model_level(key), phase)) rows |= 1u << key_row(key);
    return rows;
}

/* LED row lines as wired: rows 1,2 on PA9,PA10; rows 3,4 on PH6,PH9. */
static unsigned lit_rows(void)
{
    uint32_t pa = FM1_PR(FM1_PA, FM1_OUT), ph = FM1_PR(FM1_PH, FM1_OUT);
    return (((pa >> 9) & 1u) << 1) | (((pa >> 10) & 1u) << 2) | (((ph >> 6) & 1u) << 3) | (((ph >> 9) & 1u) << 4);
}

/* Pulled-up row inputs of the live column: PA0 row 0, PA5..PA8 rows 1..4, PB7 row 5. */
static void drive_inputs(void)
{
    unsigned rows = contact[hw_col];
    uint32_t a = 1u | (15u << 5), b = 1u << 7;
    if (rows & 1u) a &= ~1u;
    for (unsigned r = 1; r <= 4; r++) if (rows & (1u << r)) a &= ~(1u << (r + 4));
    if (rows & 32u) b &= ~(1u << 7);
    FM1_PR(FM1_PA, FM1_IN) = a;
    FM1_PR(FM1_PB, FM1_IN) = b;
}

/* One 10 kHz timer interrupt. The word DMA'd on the previous tick is strobed
 * into the shift register by this tick's latch, selecting the new column. */
static void hw_tick(void)
{
    unsigned word = ((unsigned)fm1_scan_bytes[0] << 8) | fm1_scan_bytes[1];
    unsigned selected = ~word & 0x7ffu, incoming = selected ? (unsigned)__builtin_ctz(selected) : 0;
    unsigned phase = scan_frame % FM1_LED_PHASES;
    if (release_at && total_ticks == release_at) memset(contact, 0, sizeof contact);
    ++total_ticks;
    drive_inputs();
    struct tick_info info = {0};
    info.pa0 = FM1_PR(FM1_PA, FM1_OUT);
    info.ph0 = FM1_PR(FM1_PH, FM1_OUT);
    unsigned frame_before = fm1_in.frame;
    fm1_input_tick();
    if (host_dma_busy) return; /* stalled: nothing was latched */
    hw_col = incoming;
    info.col = incoming;
    info.phase = phase;
    info.frame = scan_frame;
    info.lit = lit_rows();
    info.pa1 = FM1_PR(FM1_PA, FM1_OUT);
    info.ph1 = FM1_PR(FM1_PH, FM1_OUT);
    CHECK(frame_before == phase, "column %u latched in phase %u, expected %u", incoming, frame_before, phase);
    CHECK(fm1_in.column == incoming, "software column %u but the shift register selected %u", fm1_in.column, incoming);
    CHECK(info.lit == expected_rows(incoming, phase),
          "frame %u phase %u column %u: lit rows 0x%x, expected 0x%x", scan_frame, phase, incoming, info.lit,
          expected_rows(incoming, phase));
    CHECK(!(info.pa1 & 2u), "latch strobe PA1 left high");
    if (observer) observer(&info);
    if (incoming == FM1_NCOL - 1) ++scan_frame;
}

static void host_poll(void) { if (timer_running) hw_tick(); }

static void run_until_frame(unsigned frame) { while (scan_frame < frame) hw_tick(); }
static void run_frames(unsigned frames) { run_until_frame(scan_frame + frames); }

/* ---- Fixtures ---------------------------------------------------------- */

static void host_reset_model(void)
{
    memset(host_gpio, 0, sizeof host_gpio);
    memset(host_scan, 0, sizeof host_scan);
    host_dma_busy = 0;
    memset(contact, 0, sizeof contact);
    for (unsigned key = 0; key < FM1_NKEY; key++) model_own[key] = -1;
    model_idle = 0;
    hw_col = scan_frame = total_ticks = release_at = recovery_calls = recovery_frame = 0;
    timer_running = 1;
    observer = NULL;
    controls_started = 0;
    safe_boot = 0;
    fm1_led_idle_level(0); /* all LEDs back to the stock "off" */
    for (unsigned key = 0; key < FM1_NKEY; key++) fm1_led_set(key, 0);
}

static void host_reset(void)
{
    host_reset_model();
    fm1_input_init();
    hw_col = 0; /* init latched column 0 */
    CHECK(fm1_in.column == 0 && fm1_in.queued == 1 && fm1_in.frame == 0, "init state");
}

static void api_set(unsigned key, int on)
{
    fm1_led_set(key, on);
    if (key < FM1_NKEY) model_own[key] = on ? 8 : -1;
}
static void api_level(unsigned key, unsigned level)
{
    fm1_led_level(key, level);
    if (key < FM1_NKEY) model_own[key] = (int)(level > 8 ? 8 : level);
}
static void api_idle(unsigned level)
{
    fm1_led_idle_level(level);
    model_idle = level > 8 ? 8 : level;
}

static void press_key(unsigned key) { contact[key_col(key)] |= (uint8_t)(1u << key_row(key)); }

static uint32_t rng_state = 12345;
static unsigned rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

/* Which LED sits on a given (column, row), for the observers. */
static int key_at[FM1_NCOL][16];

/* Per-key frames lit during their own column tick. */
static unsigned lit_count[FM1_NKEY];
static void count_obs(const struct tick_info *t)
{
    for (unsigned row = 0; row < 16; row++)
        if ((t->lit >> row & 1u) && key_at[t->col][row] >= 0) ++lit_count[key_at[t->col][row]];
}
/* Frame 0 misses its column 0 tick (init latched it), so count from frame 1. */
static void reset_counts(void)
{
    observer = NULL;
    if (scan_frame == 0) run_until_frame(1);
    memset(lit_count, 0, sizeof lit_count);
    observer = count_obs;
}

/* ---- Tests ------------------------------------------------------------- */

static void test_wiring_model(void)
{
    memset(key_at, -1, sizeof key_at);
    for (unsigned key = 0; key < FM1_NKEY; key++) {
        CHECK(key_col(key) < FM1_NCOL && key_row(key) >= 1 && key_row(key) <= 4, "key %u has no LED row", key);
        CHECK(key_at[key_col(key)][key_row(key)] < 0, "key %u shares its (column,row)", key);
        key_at[key_col(key)][key_row(key)] = (int)key;
    }
}

static void test_default_state(void)
{
    /* Before any call the table is dark, as stock. */
    for (unsigned p = 0; p < FM1_LED_PHASES; p++)
        for (unsigned c = 0; c < FM1_NCOL; c++) CHECK(fm1_led[p][c] == 0, "default LED table not dark");
    CHECK(sizeof fm1_led == FM1_LED_PHASES * FM1_NCOL, "LED table size");
}

static void test_duty_table(void)
{
    for (unsigned level = 0; level <= FM1_LED_MAX; level++) {
        unsigned bits = 0, count = 0;
        for (unsigned p = 0; p < FM1_LED_PHASES; p++) bits |= duty_on(level, p) << p;
        for (unsigned p = 0; p < FM1_LED_PHASES; p++) count += (fm1_led_duty[level] >> p) & 1u;
        CHECK(fm1_led_duty[level] == bits, "duty table level %u is 0x%02x, formula says 0x%02x", level, fm1_led_duty[level], bits);
        CHECK(count == level, "level %u lights %u of 8 frames", level, count);
    }
}

/* Stock behaviour at 4c7496d: on/off per LED, latched every frame. */
static uint8_t stock_leds[FM1_NCOL];
static void stock_led_key(unsigned key, int on)
{
    unsigned pin = fm1_key_pin[key], column = pin >> 4, mask = 1u << (pin & 15);
    uint8_t value = stock_leds[column];
    stock_leds[column] = (uint8_t)(on ? value | mask : value & ~mask);
}
static void stock_latch(uint32_t *pa, uint32_t *ph, unsigned leds)
{
    const uint32_t a_mask = (1u << 9) | (1u << 10), h_mask = (1u << 6) | (1u << 9);
    *pa &= ~a_mask; *ph &= ~h_mask;
    *pa |= 1u << 1;
    *pa &= ~(1u << 1);
    *pa |= (leds & 6u) << 8;
    *ph |= ((leds & 8u) << 3) | ((leds & 16u) << 5);
}
static unsigned stock_ticks;
static void stock_obs(const struct tick_info *t)
{
    uint32_t pa = t->pa0, ph = t->ph0;
    stock_latch(&pa, &ph, stock_leds[t->col]);
    CHECK(pa == t->pa1 && ph == t->ph1,
          "frame %u column %u: PA/PH 0x%08x/0x%08x differ from stock 0x%08x/0x%08x", t->frame, t->col,
          (unsigned)t->pa1, (unsigned)t->ph1, (unsigned)pa, (unsigned)ph);
    ++stock_ticks;
}

static void test_stock_equivalence(void)
{
    host_reset();
    memset(stock_leds, 0, sizeof stock_leds);
    stock_ticks = 0;
    observer = stock_obs;
    for (unsigned trial = 0; trial < 160; trial++) {
        for (unsigned key = 0; key < FM1_NKEY; key++) {
            int on = trial == 0 ? 0 : trial == 1 ? 1 : trial < 2 + FM1_NKEY ? key == trial - 2 : (int)(rnd() & 1u);
            api_set(key, on);
            stock_led_key(key, on);
            if (rnd() % 7 == 0) { api_set(key, on); stock_led_key(key, on); } /* idempotent */
        }
        run_frames(8 + rnd() % 9);
    }
    CHECK(stock_ticks > 160 * 8 * FM1_NCOL, "stock comparison ran only %u ticks", stock_ticks);
    /* Every phase of the table holds the stock mask at level 8. */
    for (unsigned p = 0; p < FM1_LED_PHASES; p++)
        for (unsigned c = 0; c < FM1_NCOL; c++) CHECK(fm1_led[p][c] == stock_leds[c], "phase %u column %u mask", p, c);
}

static void test_level_zero_is_dark(void)
{
    /* Explicit 0 stays dark even above a bright idle level. */
    host_reset();
    api_idle(8);
    for (unsigned key = 0; key < FM1_NKEY; key++) api_level(key, 0);
    reset_counts();
    run_frames(24);
    for (unsigned key = 0; key < FM1_NKEY; key++) CHECK(lit_count[key] == 0, "key %u lit at level 0", key);
    /* Stock off with idle 0 is dark, and never-set LEDs stay dark. */
    host_reset();
    api_set(5, 1);
    api_set(5, 0);
    reset_counts();
    run_frames(24);
    for (unsigned key = 0; key < FM1_NKEY; key++) CHECK(lit_count[key] == 0, "key %u lit when off", key);
}

#define SEQ_FRAMES 32
static uint8_t rec_seq[SEQ_FRAMES];
static unsigned rec_col, rec_row;
static void rec_obs(const struct tick_info *t)
{
    if (t->col == rec_col && t->frame >= 1 && t->frame <= SEQ_FRAMES) rec_seq[t->frame - 1] = (t->lit >> rec_row) & 1u;
}

static void check_spread(unsigned key, unsigned level)
{
    unsigned count = 0;
    for (unsigned f = 0; f < SEQ_FRAMES; f++) {
        count += rec_seq[f];
        CHECK(rec_seq[f] == rec_seq[f % 8], "key %u level %u: frame %u breaks the 8-frame period", key, level, f);
    }
    CHECK(count == level * (SEQ_FRAMES / 8), "key %u level %u lit %u of %u frames", key, level, count, SEQ_FRAMES);
    if (level == 0) return;
    /* Gaps between lit frames, cyclic over the 8-frame cycle, differ by at most one. */
    unsigned first = 8, last = 0, prev = 0, lo = level == 8 ? 1 : 8 / level, hi = (8 + level - 1) / level;
    for (unsigned p = 0; p < 8; p++) {
        if (!rec_seq[p]) continue;
        if (first == 8) first = p; else CHECK(p - prev >= lo && p - prev <= hi, "key %u level %u: gap %u", key, level, p - prev);
        prev = last = p;
    }
    CHECK(8 + first - last >= lo && 8 + first - last <= hi, "key %u level %u: wrap gap %u", key, level, 8 + first - last);
    /* Any window of k frames holds k*level/8 lit frames, rounded either way:
     * no stretch long enough to read as a slow beat. */
    for (unsigned k = 1; k <= 16; k++)
        for (unsigned start = 0; start < 8; start++) {
            unsigned in_window = 0;
            for (unsigned i = 0; i < k; i++) in_window += rec_seq[(start + i) % 8];
            int err = (int)(8 * in_window) - (int)(k * level);
            CHECK(err > -8 && err < 8, "key %u level %u: window %u at %u holds %u", key, level, k, start, in_window);
        }
}

static void test_duty_per_level(void)
{
    unsigned totals[FM1_LED_MAX + 1] = {0};
    for (unsigned key = 0; key < FM1_NKEY; key++)
        for (unsigned level = 0; level <= FM1_LED_MAX; level++) {
            host_reset();
            api_level(key, level);
            memset(rec_seq, 0, sizeof rec_seq);
            rec_col = key_col(key);
            rec_row = key_row(key);
            observer = rec_obs;
            run_until_frame(SEQ_FRAMES + 1);
            check_spread(key, level);
            for (unsigned f = 0; f < SEQ_FRAMES; f++) totals[level] += rec_seq[f];
        }
    for (unsigned level = 0; level < FM1_LED_MAX; level++)
        CHECK(totals[level] < totals[level + 1], "duty not monotonic: level %u has %u, level %u has %u", level,
              totals[level], level + 1, totals[level + 1]);
}

static void test_idle_level(void)
{
    for (unsigned idle = 0; idle <= FM1_LED_MAX; idle++) {
        host_reset();
        api_idle(idle);
        reset_counts();
        run_frames(16);
        for (unsigned key = 0; key < FM1_NKEY; key++) CHECK(lit_count[key] == 2 * idle, "idle %u: key %u lit %u/16", idle, key, lit_count[key]);
    }
    /* Explicit on, explicit level and explicit dark are not touched by the idle level. */
    host_reset();
    api_idle(2);
    api_set(0, 1);
    api_level(1, 3);
    api_level(2, 0);
    api_set(3, 1);
    api_set(3, 0);                /* back to following idle */
    api_idle(6);
    reset_counts();
    run_frames(16);
    CHECK(lit_count[0] == 16, "on LED lit %u/16 under idle 6", lit_count[0]);
    CHECK(lit_count[1] == 6, "level 3 LED lit %u/16", lit_count[1]);
    CHECK(lit_count[2] == 0, "explicit dark LED lit %u/16", lit_count[2]);
    CHECK(lit_count[3] == 12, "off LED lit %u/16 under idle 6", lit_count[3]);
    CHECK(lit_count[40] == 12, "untouched LED lit %u/16 under idle 6", lit_count[40]);
    /* Back to idle 0: only the explicit ones remain. */
    api_idle(0);
    reset_counts();
    run_frames(16);
    CHECK(lit_count[0] == 16 && lit_count[1] == 6 && lit_count[2] == 0 && lit_count[3] == 0 && lit_count[40] == 0,
          "idle 0 left %u %u %u %u %u", lit_count[0], lit_count[1], lit_count[2], lit_count[3], lit_count[40]);
}

static void test_bad_arguments(void)
{
    host_reset();
    api_level(41, 8);
    api_level(0xffffffffu, 8);
    api_set(41, 1);
    api_set(0xffffffffu, 1);
    api_level(7, 9);
    api_level(8, 0xffffffffu);
    api_idle(0xffffffffu);
    CHECK(model_idle == 8 && fm1_led_idle == 8, "idle did not clamp to 8");
    CHECK(fm1_led_own[7] == 9 && fm1_led_own[8] == 9, "explicit level did not clamp to 8");
    reset_counts();
    run_frames(8);
    for (unsigned key = 0; key < FM1_NKEY; key++) CHECK(lit_count[key] == 8, "key %u lit %u/8 after clamping", key, lit_count[key]);
}

static void test_random_calls(void)
{
    host_reset();
    for (unsigned i = 0; i < 20000; i++) {
        unsigned key = rnd() % 48;
        switch (rnd() % 5) {
        case 0: api_set(key, (int)(rnd() & 1u)); break;
        case 1: api_level(key, rnd() % 12); break;
        case 2: api_idle(rnd() % 11); break;
        default: break;
        }
        for (unsigned n = rnd() % 25; n; n--) hw_tick(); /* mid-frame changes included */
    }
}

static void test_inputs_unaffected(void)
{
    for (unsigned key = 0; key < FM1_NKEY; key++) {
        host_reset();
        api_idle(3);
        for (unsigned led = 0; led < FM1_NKEY; led++) api_level(led, rnd() % 9);
        press_key(key);
        run_frames(4);
        uint32_t buttons = key < 14 ? 1u << key : 0, notes = key < 14 ? 0 : 1u << (key - 14);
        CHECK(fm1_in.buttons == buttons && fm1_in.notes == notes, "key %u pressed: buttons 0x%x notes 0x%x", key,
              (unsigned)fm1_in.buttons, (unsigned)fm1_in.notes);
        memset(contact, 0, sizeof contact);
        run_frames(10);
        CHECK(fm1_in.buttons == 0 && fm1_in.notes == 0, "key %u not released", key);
    }
    host_reset();
    for (unsigned key = 0; key < FM1_NKEY; key++) press_key(key);
    run_frames(4);
    CHECK(fm1_in.buttons == 0x3fffu && fm1_in.notes == (1u << 27) - 1, "all keys: buttons 0x%x notes 0x%x", (unsigned)fm1_in.buttons,
          (unsigned)fm1_in.notes);
}

static void test_dma_stall(void)
{
    host_reset();
    api_set(3, 1);
    run_frames(2);
    unsigned column = fm1_in.column, queued = fm1_in.queued, frame = fm1_in.frame, saved_frame = scan_frame;
    uint32_t pa = FM1_PR(FM1_PA, FM1_OUT), ph = FM1_PR(FM1_PH, FM1_OUT);
    host_dma_busy = 1;
    for (unsigned i = 0; i < 25; i++) {
        hw_tick();
        CHECK(fm1_in.column == column && fm1_in.queued == queued && fm1_in.frame == frame, "stall moved the scan at tick %u", i);
        CHECK(FM1_PR(FM1_PA, FM1_OUT) == pa && FM1_PR(FM1_PH, FM1_OUT) == ph, "stall touched the LED lines");
    }
    host_dma_busy = 0;
    run_frames(20);
    CHECK(scan_frame == saved_frame + 20, "scan did not resume after a stall");
}

/* ---- Power-on chords (panel.c) ----------------------------------------- */

void fm1_device_start(void (*scan)(void))
{
    CHECK(scan == fm1_input_tick, "the panel scan was not attached");
    hw_col = 0; /* fm1_input_init latched column 0 */
}
uint32_t fm1_device_irq_save(void) { return 0; }
void fm1_device_irq_restore(uint32_t state) { (void)state; }
int fm1_device_adc(unsigned channel) { (void)channel; return 0; }
void fm1_device_recovery(void) { ++recovery_calls; recovery_frame = scan_frame; }

#define BTN(n) (1u << (n))
#define OCT_DOWN BTN(0)
#define OCT_UP BTN(1)
#define ENV BTN(4)
#define LFO BTN(5)
#define ALL_BUTTONS 0x3fffu

/* Power on with these buttons down; contacts open at tick `release` (0 = never). */
static void chord_run(uint32_t buttons, int timer, unsigned release)
{
    host_reset_model();
    for (unsigned b = 0; b < 14; b++) if (buttons >> b & 1u) press_key(b);
    timer_running = timer;
    release_at = release;
    fm1_controls_init();
}
static unsigned safe_of(uint32_t buttons, unsigned release)
{
    chord_run(buttons, 1, release);
    return fm1_panel_safe_boot();
}
static unsigned rom_of(uint32_t buttons, unsigned release)
{
    chord_run(buttons, 1, release);
    return recovery_calls;
}

static unsigned flicker_period;
static void flicker_obs(const struct tick_info *t)
{
    if ((t->frame / flicker_period) & 1u) memset(contact, 0, sizeof contact);
    else { press_key(4); press_key(5); press_key(0); press_key(1); }
}

static void late_key_obs(const struct tick_info *t)
{
    if (t->frame == 100) press_key(20);
}

static void test_boot_chords(void)
{
    /* Nothing held: neither, and a normal power-on pays only the settle wait. */
    chord_run(0, 1, 0);
    CHECK(!fm1_panel_safe_boot() && !recovery_calls, "chord without any button");
    CHECK(total_ticks < 100, "a power-on without a chord took %u scan ticks", total_ticks);
    CHECK(controls_started == 1, "init did not finish");

    /* Safe boot: ENV + LFO, any other buttons allowed. */
    CHECK(safe_of(ENV | LFO, 0) == 1 && recovery_calls == 0, "safe boot with ENV + LFO");
    CHECK(fm1_in.buttons == (ENV | LFO), "chord not debounced into the button state");
    CHECK(safe_of(ENV, 0) == 0, "safe boot with ENV only");
    CHECK(safe_of(LFO, 0) == 0, "safe boot with LFO only");
    for (unsigned b = 0; b < 14; b++) {
        if (b == 4 || b == 5) continue;
        CHECK(safe_of(ENV | LFO | BTN(b), 0) == 1, "safe boot with ENV + LFO + button %u", b);
        CHECK(safe_of(ENV | BTN(b), 0) == 0 && safe_of(LFO | BTN(b), 0) == 0, "safe boot from ENV or LFO plus button %u", b);
    }
    /* every other pair of buttons does nothing at all */
    for (unsigned a = 0; a < 14; a++)
        for (unsigned b = a + 1; b < 14; b++) {
            uint32_t pair = BTN(a) | BTN(b);
            if (pair == (ENV | LFO) || pair == (OCT_DOWN | OCT_UP)) continue;
            chord_run(pair, 1, 0);
            CHECK(!fm1_panel_safe_boot() && recovery_calls == 0, "buttons %u+%u started something", a, b);
        }

    /* The ROM updater: OCT- + OCT+ and nothing else, held for about a second. */
    CHECK(rom_of(OCT_DOWN | OCT_UP, 0) == 1 && !fm1_panel_safe_boot(), "ROM update with OCT- + OCT+");
    CHECK(recovery_frame >= FM1_RECOVERY_FRAMES, "ROM update after only %u scan frames", recovery_frame);
    CHECK(rom_of(OCT_DOWN, 0) == 0 && rom_of(OCT_UP, 0) == 0, "ROM update from one OCT button");
    for (unsigned b = 2; b < 14; b++)
        if (b != 4 && b != 5) CHECK(rom_of(OCT_DOWN | OCT_UP | BTN(b), 0) == 0, "ROM update with OCT- + OCT+ + button %u", b);
    CHECK(rom_of(ALL_BUTTONS, 0) == 0 && fm1_panel_safe_boot() == 1, "all buttons: safe boot wins, no ROM update");
    CHECK(rom_of(OCT_DOWN | OCT_UP | ENV | LFO, 0) == 0 && fm1_panel_safe_boot() == 1, "both chords: safe boot wins");
    CHECK(rom_of(ALL_BUTTONS & ~OCT_DOWN, 0) == 0 && rom_of(ALL_BUTTONS & ~OCT_UP, 0) == 0, "ROM update without an OCT button");

    /* The ROM chord wants no note key: a stuck row of the matrix reads as buttons and keys together. */
    host_reset_model();
    press_key(0); press_key(1); press_key(20);
    fm1_controls_init();
    CHECK(recovery_calls == 0, "ROM update with OCT- + OCT+ and a note key down");
    for (unsigned row = 0; row < 8; row++) {
        host_reset_model();
        for (unsigned col = 0; col < FM1_NCOL; col++) contact[col] |= (uint8_t)(1u << row);
        fm1_controls_init();
        CHECK(recovery_calls == 0, "ROM update from a stuck row %u", row);
    }
    /* a note key pressed after the chord has begun ends it too */
    host_reset_model();
    press_key(0); press_key(1);
    observer = late_key_obs;
    fm1_controls_init();
    CHECK(recovery_calls == 0, "ROM update although a key went down during the hold");

    /* Note keys never count. */
    host_reset_model();
    for (unsigned key = 14; key < FM1_NKEY; key++) press_key(key);
    fm1_controls_init();
    CHECK(recovery_calls == 0 && !fm1_panel_safe_boot(), "a chord from note keys");

    /* A dead scan timer: bounded wait, no guess, init completes. */
    chord_run(ENV | LFO, 0, 0);
    CHECK(!fm1_panel_safe_boot() && recovery_calls == 0 && controls_started == 1, "chord without a running scan");
    chord_run(OCT_DOWN | OCT_UP, 0, 0);
    CHECK(!fm1_panel_safe_boot() && recovery_calls == 0 && controls_started == 1, "ROM update without a running scan");

    /* Contact noise and early release are not a chord: it has to last the whole window. */
    CHECK(safe_of(ENV | LFO, 12) == 0, "safe boot from a one-frame glitch");
    CHECK(safe_of(ENV | LFO, 11 * (FM1_SAFE_FRAMES - 8)) == 0, "safe boot released inside the window");
    CHECK(safe_of(ENV | LFO, 11 * (FM1_SAFE_FRAMES + 12)) == 1, "safe boot held past the window");
    CHECK(rom_of(OCT_DOWN | OCT_UP, 11 * 200) == 0, "ROM update released after 0.2 s");
    CHECK(rom_of(OCT_DOWN | OCT_UP, 11 * (FM1_RECOVERY_FRAMES - 20)) == 0, "ROM update released just short of the window");
    CHECK(rom_of(OCT_DOWN | OCT_UP, 11 * (FM1_RECOVERY_FRAMES + 40)) == 1, "ROM update held past the window");
    /* A chord that flickers (down and up every `period` frames) is not held. The debounce keeps a released
     * button "down" for 8 more scans, so periods up to about FM1_SAFE_FRAMES - 8 can never pass. */
    static const unsigned periods[] = {1, 2, 5, 13, 31, 50};
    for (unsigned i = 0; i < sizeof periods / sizeof periods[0]; i++) {
        host_reset_model();
        flicker_period = periods[i];
        observer = flicker_obs;
        fm1_controls_init();
        CHECK(!fm1_panel_safe_boot() && recovery_calls == 0, "chord that flickers every %u frames acted", periods[i]);
    }

    /* Held through the settle wait: the debounced state decided; the flag stays after release. */
    CHECK(safe_of(ENV | LFO, 0) == 1, "setup");
    struct fm1_controls state;
    fm1_controls_read(&state);
    CHECK(state.buttons == (ENV | LFO) && state.keys == 0, "controls_read after init: 0x%x", (unsigned)state.buttons);
    memset(contact, 0, sizeof contact);
    run_frames(40);
    CHECK(fm1_panel_safe_boot() == 1, "safe boot forgot itself after the buttons were released");
    fm1_controls_init();
    CHECK(fm1_panel_safe_boot() == 1, "safe boot cleared by a repeated init");

    /* Only at init: pressing a chord later, or calling init again, does nothing. */
    chord_run(0, 1, 0);
    press_key(0); press_key(1); press_key(4); press_key(5);
    run_frames(1200);
    CHECK(recovery_calls == 0 && !fm1_panel_safe_boot() && fm1_in.buttons == (OCT_DOWN | OCT_UP | ENV | LFO), "a chord after init acted");
    fm1_controls_init();
    CHECK(recovery_calls == 0 && !fm1_panel_safe_boot(), "a repeated init acted");
}

int main(void)
{
    test_wiring_model();
    host_reset_model();
    test_default_state();
    test_duty_table();
    test_stock_equivalence();
    test_level_zero_is_dark();
    test_duty_per_level();
    test_idle_level();
    test_bad_arguments();
    test_random_calls();
    test_inputs_unaffected();
    test_dma_stall();
    test_boot_chords();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
