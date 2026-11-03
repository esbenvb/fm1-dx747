/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host tests for the DX747 scene store (app/dx747/scene.inc). The user flash is
 * modelled byte for byte: writes may only clear bits, erases clear a sector, and
 * a power cut can strike any single erase or write with a torn result. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "fm1.h"
#include "../../../../app/dx747/route.inc"

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- fake flash --------------------------------------------------------- */
#define FLASH_BYTES 536576U
#define SECTOR 4096U
#define LIBRARY_OFFSET 516096U /* GROOVE_LIBRARY_OFFSET: four DX7 voice banks */
#define TUNING_OFFSET 532480U
static uint8_t flash[FLASH_BYTES];
static unsigned op_count, cut_at, tear_mode, dead, violations;
static unsigned pause_depth, pause_max, pauses, resumes;

static int allowed(uint32_t o, size_t n)
{
    uint32_t end = o + (uint32_t)n;
    if (o >= 65536U && end <= 65536U + SECTOR) return 1;                       /* staging */
    return o >= 69632U && end <= 69632U + 33U * SECTOR;                        /* scenes and settings */
}
int fm1_flash_read(uint32_t o, void *d, size_t n)
{
    if (dead) return FM1_FLASH_IO;
    if ((uint64_t)o + n > FLASH_BYTES) return FM1_FLASH_INVALID;
    memcpy(d, flash + o, n);
    return 0;
}
static size_t torn(size_t n)
{
    switch (tear_mode) { case 0: return 0; case 1: return 1; case 2: return n / 2; case 3: return n > 1 ? n - 1 : 0; default: return n; }
}
int fm1_flash_write(uint32_t o, const void *d, size_t n)
{
    if (dead) return FM1_FLASH_IO;
    if ((uint64_t)o + n > FLASH_BYTES) return FM1_FLASH_INVALID;
    if (!allowed(o, n)) ++violations;
    ++op_count;
    const uint8_t *p = d;
    for (size_t i = 0; i < n; ++i) if (p[i] & ~flash[o + i]) return FM1_FLASH_NEEDS_ERASE;
    size_t done = n;
    if (cut_at && op_count == cut_at) { done = torn(n); dead = 1; }
    for (size_t i = 0; i < done; ++i) flash[o + i] &= p[i];
    return dead ? FM1_FLASH_IO : 0;
}
int fm1_flash_erase(uint32_t o)
{
    if (dead) return FM1_FLASH_IO;
    if (o >= FLASH_BYTES) return FM1_FLASH_INVALID;
    uint32_t base = o & ~(SECTOR - 1U);
    if (!allowed(base, SECTOR)) ++violations;
    ++op_count;
    if (cut_at && op_count == cut_at) {
        size_t done = torn(SECTOR);
        memset(flash + base, 0xff, done);
        for (size_t i = done; i < SECTOR; ++i) flash[base + i] = (uint8_t)(flash[base + i] & (i * 31U + 7U)); /* half-erased garbage */
        dead = 1;
        return FM1_FLASH_IO;
    }
    memset(flash + base, 0xff, SECTOR);
    return 0;
}
uint32_t fm1_flash_size(void) { return FLASH_BYTES; }
void fm1_engine_pause(void) { ++pauses; if (++pause_depth > pause_max) pause_max = pause_depth; }
void fm1_engine_resume(void) { ++resumes; if (pause_depth) --pause_depth; }

/* Memory failures on demand. */
static int fail_malloc;
static void *test_malloc(size_t n) { return fail_malloc ? NULL : malloc(n); }
#define malloc(n) test_malloc(n)

/* The staging owner: finish a foreign committed record (here: just erase it), then ours. */
static unsigned prepare_calls, foreign_completed;
#define FOREIGN_MAGIC 0x4b424d46U /* a library bank record, say */
int groove_storage_prepare(void);

#include "../../../../app/dx747/scene.inc"

int groove_storage_prepare(void)
{
    ++prepare_calls;
    uint32_t magic, commit;
    memcpy(&magic, flash + DX_STAGE, 4); memcpy(&commit, flash + DX_STAGE + 16, 4);
    if (magic == FOREIGN_MAGIC && commit == 0) { ++foreign_completed; int r = fm1_flash_erase(DX_STAGE); if (r) return r; }
    return dx_scene_recover();
}

/* ---- helpers ------------------------------------------------------------ */
static void fresh_flash(void)
{
    memset(flash, 0xff, sizeof flash);
    for (unsigned i = LIBRARY_OFFSET; i < FLASH_BYTES; ++i) flash[i] = (uint8_t)(i * 13U + 5U); /* library + tuning data that must never change */
    op_count = cut_at = tear_mode = dead = violations = 0;
    pause_depth = pause_max = pauses = resumes = 0;
    prepare_calls = foreign_completed = 0; fail_malloc = 0;
    memset(&dx_catalog, 0, sizeof dx_catalog);
}
static int tail_intact(void)
{
    for (unsigned i = LIBRARY_OFFSET; i < FLASH_BYTES; ++i) if (flash[i] != (uint8_t)(i * 13U + 5U)) return 0;
    return 1;
}
static void make_scene(struct dx_scene *s, unsigned seed, const char *name)
{
    memset(s, 0, sizeof *s);
    memset(s->name, ' ', sizeof s->name);
    memcpy(s->name, name, strlen(name) < 12 ? strlen(name) : 12);
    s->kbd_ch = (uint8_t)(seed % 16); s->pool = (uint8_t)(8 + seed % 20);
    for (unsigned i = 0; i < 4; ++i) s->effects[i] = (uint8_t)(seed * 7 + i * 31);
    s->filter_type = (uint8_t)(seed % 4); s->filter_res = (uint8_t)(seed % 100); s->filter_hz = (uint16_t)(100 + seed * 53 % 19000);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        struct dx_scene_engine *x = &s->e[e];
        for (unsigned i = 0; i < DX_PRESET_BYTES; ++i) x->preset[i] = (uint8_t)(seed * 131 + e * 17 + i * 3);
        x->rx = (uint16_t)(1U << ((seed + e) % 16)); x->lo = (uint8_t)(e * 3); x->hi = (uint8_t)(100 + e);
        x->shift = (int8_t)(((int)((seed + e) % 7) - 3) * 12); x->fold = (uint8_t)(e % 3); x->ctl = (uint8_t)(e + 9 > 16 ? 0 : e + 9);
        x->poly = (uint8_t)(1 + (seed + e) % 8); x->mute = (uint8_t)((seed + e) % 5 == 0);
        x->pan = (int8_t)(((int)((seed * 3 + e) % 101)) - 50); x->expr = (uint8_t)(100 + e); x->bright = (int8_t)(((int)((seed + e * 5) % 101)) - 50);
        x->flags = 1;
    }
}
static int same(const struct dx_scene *a, const struct dx_scene *b)
{
    uint8_t x[DX_SCENE_BYTES], y[DX_SCENE_BYTES];
    dx_scene_pack(a, x); dx_scene_pack(b, y);
    return !memcmp(x, y, sizeof x);
}
static int reboot(void) /* power returns: the RAM state is gone, flash stays */
{
    dead = 0; op_count = 0; cut_at = 0; pause_depth = 0;
    memset(&dx_catalog, 0, sizeof dx_catalog);
    return dx_scene_recover();
}

/* ---- tests -------------------------------------------------------------- */
static void test_pack(void)
{
    struct dx_scene a, b;
    uint8_t image[DX_SCENE_BYTES];
    make_scene(&a, 3, "BAND");
    CHECK(dx_scene_valid(&a));
    dx_scene_pack(&a, image);
    CHECK(dx_scene_unpack(image, &b) == 0 && same(&a, &b) && !memcmp(a.name, b.name, 12) && b.filter_hz == a.filter_hz);
    CHECK(image[SO_ENGINES] != 0);
    memset(image, 0xff, sizeof image);
    CHECK(dx_scene_unpack(image, &b) == DX_SCENE_EMPTY);
    dx_scene_pack(&a, image); image[SO_VERSION] = 9;
    CHECK(dx_scene_unpack(image, &b) == DX_SCENE_INVALID);

    struct dx_scene bad;
    bad = a; bad.kbd_ch = 16; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.pool = 0; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.pool = 65; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.name[2] = 7; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.filter_type = 4; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.filter_hz = 10; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[3].poly = 0; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[3].poly = 9; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[7].lo = 90; bad.e[7].hi = 80; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[1].hi = 128; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[2].shift = 49; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[2].pan = -51; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[2].expr = 128; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[5].fold = 3; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.e[5].ctl = 17; CHECK(!dx_scene_valid(&bad));
    bad = a; bad.filter_res = 127; CHECK(dx_scene_valid(&bad));
    bad = a; bad.filter_res = 128; CHECK(!dx_scene_valid(&bad));
    CHECK(!dx_scene_valid(NULL));
    fresh_flash();
    CHECK(dx_scene_write(0, &bad) == DX_SCENE_INVALID && op_count == 0); /* nothing written for an invalid scene */
}

static void test_write_read_and_layout(void)
{
    fresh_flash();
    struct dx_scene s, r;
    CHECK(dx_scene_store_state() == 0);
    CHECK(dx_scene_read(0, &r) == DX_SCENE_EMPTY);
    make_scene(&s, 1, "ONE");
    CHECK(dx_scene_read(64, &r) == DX_SCENE_INVALID && dx_scene_write(64, &s) == DX_SCENE_INVALID);
    CHECK(dx_scene_write(0, &s) == 0);
    CHECK(dx_scene_read(0, &r) == 0 && same(&s, &r));
    CHECK(dx_scene_read(1, &r) == DX_SCENE_EMPTY); /* its sector mate is untouched */
    struct dx_scene t; make_scene(&t, 2, "TWO");
    CHECK(dx_scene_write(1, &t) == 0);
    CHECK(dx_scene_read(0, &r) == 0 && same(&s, &r) && dx_scene_read(1, &r) == 0 && same(&t, &r));
    uint32_t magic; memcpy(&magic, flash + DX_STORE_BASE, 4);
    CHECK(magic == DX_SEC_MAGIC);
    struct dx_scene u; make_scene(&u, 9, "SECTOR31");
    CHECK(dx_scene_write(63, &u) == 0 && dx_scene_read(63, &r) == 0 && same(&u, &r));
    memcpy(&magic, flash + DX_STORE_BASE + 31 * SECTOR, 4);
    CHECK(magic == DX_SEC_MAGIC); /* scene 63 lives in the last sector */
    struct dx_scene v; make_scene(&v, 4, "MIDDLE");
    CHECK(dx_scene_write(10, &v) == 0 && dx_scene_read(10, &r) == 0 && same(&v, &r));
    memcpy(&magic, flash + DX_STORE_BASE + 5 * SECTOR, 4);
    CHECK(magic == DX_SEC_MAGIC);
    CHECK(violations == 0 && tail_intact());
    CHECK(pause_depth == 0 && pauses == resumes && pause_max == 1);
    CHECK(prepare_calls >= 4 && foreign_completed == 0);
    /* staging is left erased */
    int clean = 1; for (unsigned i = 0; i < SECTOR; ++i) clean &= flash[DX_STAGE + i] == 0xff;
    CHECK(clean);

    /* overwrite and unchanged writes */
    struct dx_scene w; make_scene(&w, 5, "REPLACED");
    unsigned ops = op_count;
    CHECK(dx_scene_write(0, &w) == 0 && op_count > ops);
    CHECK(dx_scene_read(0, &r) == 0 && same(&w, &r) && dx_scene_read(1, &r) == 0 && same(&t, &r));
    ops = op_count;
    CHECK(dx_scene_write(0, &w) == 0 && op_count == ops); /* unchanged content never touches flash */
    CHECK(dx_scene_clear(0) == 0 && dx_scene_read(0, &r) == DX_SCENE_EMPTY && dx_scene_read(1, &r) == 0 && same(&t, &r));
    ops = op_count;
    CHECK(dx_scene_clear(0) == 0 && op_count == ops);
    CHECK(dx_scene_clear(40) == 0 && op_count == ops); /* clearing in a sector that was never written */
}

static void test_catalog(void)
{
    fresh_flash();
    struct dx_scene s;
    make_scene(&s, 1, "ALPHA"); CHECK(dx_scene_write(3, &s) == 0);
    make_scene(&s, 2, "BETA"); s.e[0].rx = 0; s.e[1].mute = 1; s.e[1].rx = 2; CHECK(dx_scene_write(4, &s) == 0);
    memset(&dx_catalog, 0, sizeof dx_catalog);
    CHECK(dx_scene_scan() == 0);
    char name[13]; uint8_t used = 0;
    CHECK(dx_scene_info(3, name, &used) == 0 && !strcmp(name, "ALPHA       "));
    CHECK(dx_scene_info(4, name, &used) == 0 && !strcmp(name, "BETA        "));
    CHECK(!(used & 1) && !(used & 2)); /* engine 0 has no channel, engine 1 is muted */
    CHECK(dx_scene_info(5, name, &used) == DX_SCENE_EMPTY && dx_scene_info(64, name, &used) == DX_SCENE_EMPTY);
    CHECK(dx_scene_info(3, name, NULL) == 0);
    CHECK(dx_scene_clear(3) == 0 && dx_scene_info(3, name, &used) == DX_SCENE_EMPTY);
}

static void test_claim_and_foreign_data(void)
{
    fresh_flash();
    /* A stock sample bank: magic "FM1S" then audio, 40 sectors long (past the store). */
    uint32_t sample_magic = 0x53314d46U;
    memcpy(flash + DX_STORE_BASE, &sample_magic, 4);
    for (unsigned i = 16; i < 40U * SECTOR; ++i) flash[DX_STORE_BASE + i] = (uint8_t)(i * 7U + 1U);
    uint8_t beyond[SECTOR]; memcpy(beyond, flash + DX_STORE_BASE + 33U * SECTOR, SECTOR);
    struct dx_scene s, r;
    make_scene(&s, 1, "FIRST");
    CHECK(dx_scene_store_state() == 1);
    CHECK(dx_scene_write(0, &s) == DX_SCENE_NEEDS_CLAIM && op_count == 0); /* refused before touching anything */
    CHECK(dx_scene_clear(0) == DX_SCENE_NEEDS_CLAIM && dx_scene_write(31, &s) == DX_SCENE_NEEDS_CLAIM);
    CHECK(dx_scene_claim() == 0);
    CHECK(dx_scene_store_state() == 0);
    CHECK(dx_scene_write(0, &s) == 0 && dx_scene_read(0, &r) == 0 && same(&s, &r));
    CHECK(dx_scene_write(63, &s) == 0);
    CHECK(!memcmp(beyond, flash + DX_STORE_BASE + 33U * SECTOR, SECTOR)); /* sample sectors beyond the store survive */
    CHECK(tail_intact() && violations == 0 && pauses == resumes);

    /* Garbage in a single sector deeper in the store blocks only that sector until claimed. */
    fresh_flash();
    flash[DX_STORE_BASE + 7U * SECTOR + 3] = 0x12; /* sector 7 is not erased and not ours */
    CHECK(dx_scene_store_state() == 0);
    CHECK(dx_scene_write(0, &s) == 0);
    CHECK(dx_scene_write(14, &s) == DX_SCENE_NEEDS_CLAIM && dx_scene_write(15, &s) == DX_SCENE_NEEDS_CLAIM);
    CHECK(dx_scene_claim() == 0 && dx_scene_write(14, &s) == 0);
}

static void test_coexistence_with_other_staging_users(void)
{
    fresh_flash();
    struct dx_scene s, r;
    make_scene(&s, 1, "MIXED");
    /* A committed library bank sits in staging; the scene module must not touch it in recover. */
    uint32_t foreign[8] = {FOREIGN_MAGIC, 1, 2, 3, 0 /* commit word at 16 */, 0, 0, 0};
    memset(flash + DX_STAGE, 0xff, SECTOR);
    memcpy(flash + DX_STAGE, foreign, sizeof foreign);
    uint8_t before[64]; memcpy(before, flash + DX_STAGE, sizeof before);
    CHECK(dx_scene_recover() == 0 && !memcmp(before, flash + DX_STAGE, sizeof before));
    /* A write first lets the staging owner finish the foreign record, then uses staging. */
    CHECK(dx_scene_write(2, &s) == 0 && foreign_completed == 1 && prepare_calls == 1);
    CHECK(dx_scene_read(2, &r) == 0 && same(&s, &r));
    CHECK(tail_intact() && violations == 0);
    /* A half-written scene record of ours (no commit) is discarded, a scene read still works. */
    uint8_t half[DX_SEC_HEADER] = {0x44, 0x58, 0x53, 0x31, 1, 0, 0, 0, 1, 0, 0, 0, 0xde, 0xad, 0xbe, 0xef, 0xff, 0xff, 0xff, 0xff};
    memcpy(flash + DX_STAGE, half, sizeof half);
    CHECK(dx_scene_recover() == 0);
    int clean = 1; for (unsigned i = 0; i < SECTOR; ++i) clean &= flash[DX_STAGE + i] == 0xff;
    CHECK(clean);
    CHECK(dx_scene_read(2, &r) == 0 && same(&s, &r));
}

/* A scenario starts from a snapshot; run it with a power cut at every flash
 * operation and every kind of tear. Afterwards each scene must be exactly its
 * old or its new content (never a mixture), the sector mate unchanged, and
 * the store must still accept writes. */
enum { SCENARIO_NEW, SCENARIO_OVERWRITE, SCENARIO_CLEAR, SCENARIO_COUNT };
static uint8_t snapshot[FLASH_BYTES];
static struct dx_scene old4, old5, new5;

static void setup_scenario(unsigned kind)
{
    fresh_flash();
    make_scene(&old4, 40, "NEIGHBOUR");
    make_scene(&old5, 50, "OLD");
    make_scene(&new5, 60, "NEW");
    CHECK(dx_scene_write(8, &old4) == 0); /* scenes 8 and 9 share a sector */
    if (kind != SCENARIO_NEW) CHECK(dx_scene_write(9, &old5) == 0);
    memcpy(snapshot, flash, sizeof flash);
}
static int run_scenario(unsigned kind)
{
    switch (kind) {
    case SCENARIO_NEW: case SCENARIO_OVERWRITE: return dx_scene_write(9, &new5);
    default: return dx_scene_clear(9);
    }
}

static void test_power_cuts(void)
{
    for (unsigned kind = 0; kind < SCENARIO_COUNT; ++kind) {
        setup_scenario(kind);
        op_count = 0;
        CHECK(run_scenario(kind) == 0);
        unsigned total = op_count;
        CHECK(total >= 6 && total <= 12);
        unsigned cuts = 0, got_old = 0, got_new = 0;
        for (unsigned cut = 1; cut <= total; ++cut) for (unsigned mode = 0; mode < 4; ++mode) {
            memcpy(flash, snapshot, sizeof flash);
            op_count = 0; dead = 0; violations = 0; pause_depth = 0; cut_at = cut; tear_mode = mode;
            memset(&dx_catalog, 0, sizeof dx_catalog);
            int r = run_scenario(kind);
            CHECK(r != 0); /* a cut operation must be reported */
            CHECK(pause_depth == 0);
            ++cuts;
            /* reads straight after the cut (before recovery) already agree */
            dead = 0; op_count = 0; cut_at = 0;
            struct dx_scene x;
            int rb = dx_scene_read(9, &x);
            int pre_old = kind == SCENARIO_NEW ? rb == DX_SCENE_EMPTY : rb == 0 && same(&x, &old5);
            int pre_new = kind == SCENARIO_CLEAR ? rb == DX_SCENE_EMPTY : rb == 0 && same(&x, &new5);
            CHECK(pre_old || pre_new);
            CHECK(reboot() == 0);
            rb = dx_scene_read(9, &x);
            int is_old = kind == SCENARIO_NEW ? rb == DX_SCENE_EMPTY : rb == 0 && same(&x, &old5);
            int is_new = kind == SCENARIO_CLEAR ? rb == DX_SCENE_EMPTY : rb == 0 && same(&x, &new5);
            CHECK(is_old || is_new);
            got_old += is_old; got_new += is_new;
            CHECK(dx_scene_read(8, &x) == 0 && same(&x, &old4)); /* the neighbour never changes */
            { uint32_t m, c; memcpy(&m, flash + DX_STAGE, 4); memcpy(&c, flash + DX_STAGE + DX_SEC_COMMIT, 4);
              CHECK(!(m == DX_SEC_MAGIC && c == 0)); } /* recovery leaves no committed record behind (a torn erase may leave junk, the next write erases it) */
            CHECK(tail_intact() && violations == 0);
            /* and the store is still fully usable */
            struct dx_scene again; make_scene(&again, 70, "AFTER");
            CHECK(dx_scene_write(9, &again) == 0 && dx_scene_read(9, &x) == 0 && same(&x, &again));
            CHECK(dx_scene_read(8, &x) == 0 && same(&x, &old4));
        }
        CHECK(cuts == total * 4);
        CHECK(got_old > 0 && got_new > 0); /* both outcomes occur: before and after the staging commit */
    }
}

static void test_claim_power_cuts(void)
{
    fresh_flash();
    uint32_t sample_magic = 0x53314d46U;
    memcpy(flash + DX_STORE_BASE, &sample_magic, 4);
    for (unsigned i = 16; i < 40U * SECTOR; ++i) flash[DX_STORE_BASE + i] = (uint8_t)(i * 7U + 1U);
    memcpy(snapshot, flash, sizeof flash);
    op_count = 0;
    CHECK(dx_scene_claim() == 0);
    unsigned total = op_count;
    CHECK(total == 33); /* 32 scene sectors and the settings sector */
    for (unsigned cut = 1; cut <= total; ++cut) for (unsigned mode = 0; mode < 4; ++mode) {
        memcpy(flash, snapshot, sizeof flash);
        op_count = 0; dead = 0; violations = 0; cut_at = cut; tear_mode = mode; pause_depth = 0;
        CHECK(dx_scene_claim() != 0 && pause_depth == 0);
        dead = 0; op_count = 0; cut_at = 0;
        int state = dx_scene_store_state();
        /* sector 0 is erased last: any earlier cut still reads as foreign, a cut in its erase may read as foreign or empty */
        CHECK(state == 1 || (state == 0 && cut == total));
        CHECK(dx_scene_claim() == 0 && dx_scene_store_state() == 0); /* asking again finishes the job */
        struct dx_scene s, r; make_scene(&s, 3, "AFTER CLAIM");
        CHECK(dx_scene_write(0, &s) == 0 && dx_scene_read(0, &r) == 0 && same(&s, &r));
        CHECK(violations == 0 && tail_intact());
    }
}


static void test_settings(void)
{
    fresh_flash();
    struct dx_settings st, back;
    CHECK(dx_settings_read(&back) == DX_SCENE_EMPTY);
    memset(&st, 0, sizeof st); st.radio_off = 2; st.role = 1; st.backlight = 3; st.last_scene = 12; st.kbd_ch = 4; st.flags = 1;
    CHECK(dx_settings_valid(&st));
    CHECK(dx_settings_write(&st) == 0);
    memset(&back, 0, sizeof back);
    CHECK(dx_settings_read(&back) == 0 && !memcmp(&st, &back, sizeof st));
    unsigned ops = op_count;
    CHECK(dx_settings_write(&st) == 0 && op_count == ops); /* unchanged settings never touch flash */
    struct dx_scene s, r; make_scene(&s, 1, "WITH SETTING");
    CHECK(dx_scene_write(31, &s) == 0 && dx_scene_write(63, &s) == 0);
    CHECK(dx_settings_read(&back) == 0 && !memcmp(&st, &back, sizeof st)); /* scenes and settings do not disturb each other */
    st.backlight = 5; CHECK(dx_settings_write(&st) == 0 && dx_scene_read(63, &r) == 0 && same(&s, &r));
    struct dx_settings bad = st;
    bad.radio_off = 8; CHECK(!dx_settings_valid(&bad));
    bad = st; bad.role = 2; CHECK(!dx_settings_valid(&bad));
    bad = st; bad.backlight = 9; CHECK(!dx_settings_valid(&bad));
    bad = st; bad.last_scene = 64; CHECK(!dx_settings_valid(&bad));
    bad = st; bad.kbd_ch = 16; CHECK(!dx_settings_valid(&bad));
    bad = st; bad.flags = 2; CHECK(!dx_settings_valid(&bad));
    CHECK(dx_settings_write(&bad) == DX_SCENE_INVALID);
    CHECK(tail_intact() && violations == 0);
    /* a power cut during a settings write leaves the old or the new settings */
    struct dx_settings old = st, nu = st; nu.backlight = 1; nu.last_scene = 40;
    memcpy(snapshot, flash, sizeof flash);
    for (unsigned cut = 1; cut <= 7; ++cut) for (unsigned mode = 0; mode < 4; ++mode) {
        memcpy(flash, snapshot, sizeof flash);
        op_count = 0; dead = 0; cut_at = cut; tear_mode = mode;
        int rc = dx_settings_write(&nu);
        dead = 0; op_count = 0; cut_at = 0;
        CHECK(reboot() == 0);
        struct dx_settings x;
        CHECK(dx_settings_read(&x) == 0);
        CHECK(!memcmp(&x, &old, sizeof x) || !memcmp(&x, &nu, sizeof x));
        (void)rc;
    }
}

/* Recovery itself may lose power; running it again must still finish the job. */
static void test_recover_power_cuts(void)
{
    fresh_flash();
    struct dx_scene a, b, r;
    make_scene(&a, 1, "KEPT"); make_scene(&b, 2, "STAGED");
    CHECK(dx_scene_write(6, &a) == 0);
    memcpy(snapshot, flash, sizeof flash);
    /* cut right after the staging commit (before the destination erase): op 3 completes, op 4 is cut */
    unsigned state_found = 0;
    for (unsigned cut = 3; cut <= 5; ++cut) {
        memcpy(flash, snapshot, sizeof flash);
        op_count = 0; dead = 0; cut_at = cut; tear_mode = 2;
        CHECK(dx_scene_write(7, &b) != 0);
        dead = 0; op_count = 0; cut_at = 0;
        uint32_t m, c; memcpy(&m, flash + DX_STAGE, 4); memcpy(&c, flash + DX_STAGE + DX_SEC_COMMIT, 4);
        if (!(m == DX_SEC_MAGIC && c == 0)) continue; /* this cut hit before the commit */
        ++state_found;
        static uint8_t base[FLASH_BYTES]; memcpy(base, flash, sizeof flash);
        op_count = 0; CHECK(dx_scene_recover() == 0);
        unsigned total = op_count;
        CHECK(total >= 3);
        for (unsigned rc = 1; rc <= total; ++rc) for (unsigned mode = 0; mode < 4; ++mode) {
            memcpy(flash, base, sizeof flash);
            op_count = 0; dead = 0; cut_at = rc; tear_mode = mode;
            CHECK(dx_scene_recover() != 0);
            dead = 0; op_count = 0; cut_at = 0;
            CHECK(dx_scene_read(7, &r) == 0 && same(&r, &b));   /* readable from staging even before the second recovery */
            CHECK(dx_scene_read(6, &r) == 0 && same(&r, &a));
            CHECK(reboot() == 0);
            CHECK(dx_scene_read(7, &r) == 0 && same(&r, &b) && dx_scene_read(6, &r) == 0 && same(&r, &a));
            uint32_t m2, c2; memcpy(&m2, flash + DX_STAGE, 4); memcpy(&c2, flash + DX_STAGE + DX_SEC_COMMIT, 4);
            CHECK(!(m2 == DX_SEC_MAGIC && c2 == 0));
            CHECK(tail_intact() && violations == 0);
        }
    }
    CHECK(state_found >= 1);
}

static void test_memory_failure(void)
{
    fresh_flash();
    struct dx_scene s, r; make_scene(&s, 1, "NOMEM");
    fail_malloc = 1;
    CHECK(dx_scene_write(0, &s) == DX_SCENE_MEMORY);
    CHECK(op_count == 0 && pauses == resumes);
    fail_malloc = 0;
    CHECK(dx_scene_write(0, &s) == 0 && dx_scene_read(0, &r) == 0 && same(&s, &r));
}

static void test_torn_sector_reads_as_empty(void)
{
    fresh_flash();
    struct dx_scene s, r; make_scene(&s, 1, "ONE");
    CHECK(dx_scene_write(0, &s) == 0);
    flash[DX_STORE_BASE + DX_SEC_HEADER + 500] ^= 0x10; /* a bit flips in the stored sector, staging is erased */
    CHECK(dx_scene_read(0, &r) == DX_SCENE_EMPTY && dx_scene_read(1, &r) == DX_SCENE_EMPTY);
    CHECK(dx_scene_scan() == 0);
    char name[13];
    CHECK(dx_scene_info(0, name, NULL) == DX_SCENE_EMPTY);
    struct dx_scene t; make_scene(&t, 2, "AGAIN");
    CHECK(dx_scene_write(1, &t) == 0 && dx_scene_read(1, &r) == 0 && same(&t, &r)); /* the sector is rebuilt */
}

/* ---- capture and apply with a recording engine -------------------------- */
static char trace[4096];
static void tr(const char *fmt, int a, int b, int c)
{
    char line[64]; snprintf(line, sizeof line, fmt, a, b, c);
    if (strlen(trace) + strlen(line) + 1 < sizeof trace) strcat(trace, line);
}
static uint8_t eng_preset[DX_ENGINES][DX_PRESET_BYTES];
static unsigned eng_muted;
static int import_fail_for = -1;
static int p_export(unsigned e, uint8_t out[DX_PRESET_BYTES]) { memcpy(out, eng_preset[e], DX_PRESET_BYTES); return 0; }
static int p_import(unsigned e, const uint8_t in[DX_PRESET_BYTES]) { tr("I%d;", (int)e, 0, 0); memcpy(eng_preset[e], in, DX_PRESET_BYTES); return (int)e == import_fail_for ? -8 : 0; }
static int p_muted(unsigned e) { return (eng_muted >> e) & 1U; }
static void p_toggle(unsigned e) { tr("M%d;", (int)e, 0, 0); eng_muted ^= 1U << e; }
static uint8_t fx[4] = {10, 20, 30, 40};
static void p_fx_get(uint8_t v[4]) { memcpy(v, fx, 4); }
static void p_fx_set(const uint8_t v[4]) { tr("F%d;", v[0], 0, 0); memcpy(fx, v, 4); }
static void p_cc(unsigned e, unsigned c, unsigned v) { tr("C%d:%d=%d;", (int)e, (int)c, (int)v); }
static void p_bright(unsigned e, int o) { tr("B%d:%d;", (int)e, o, 0); }
static void p_filter(unsigned t, unsigned hz, unsigned res) { tr("T%d:%d:%d;", (int)t, (int)hz, (int)res); }
static void p_panic(void) { tr("P;", 0, 0, 0); }
static struct dx_cfg published; static unsigned publishes;
static void p_publish(const struct dx_cfg *c) { tr("U;", 0, 0, 0); published = *c; ++publishes; }
static const struct dx_port port = {p_export, p_import, p_muted, p_toggle, p_fx_get, p_fx_set, p_cc, p_bright, p_filter, p_panic, p_publish};

static void test_capture_and_apply(void)
{
    struct dx_cfg cfg; dx_cfg_default(&cfg);
    struct dx_live live; memset(&live, 0, sizeof live);
    for (unsigned e = 0; e < DX_ENGINES; ++e) { memset(eng_preset[e], (int)(e + 1), DX_PRESET_BYTES); live.pan[e] = (int8_t)(e * 5 - 20); live.expr[e] = (uint8_t)(90 + e); live.bright[e] = (int8_t)e; }
    cfg.e[2].rx = 0x0003; cfg.e[2].lo = 36; cfg.e[2].hi = 96; cfg.e[2].shift = -12; cfg.e[2].ctl = 11; cfg.e[2].fold = DX_FOLD_WRAP;
    cfg.e[3].mute = 1; cfg.kbd_ch = 4; cfg.pool = 12; cfg.up = 0xff & ~(1U << 6); /* engine 7 failed to start */
    live.filter_type = DX_FILTER_LP; live.filter_hz = 4200; live.filter_res = 20;
    struct dx_scene s;
    CHECK(dx_scene_capture(&port, &cfg, &live, "LIVE SET    ", &s) == 0);
    CHECK(!memcmp(s.name, "LIVE SET    ", 12) && s.kbd_ch == 4 && s.pool == 12 && !memcmp(s.effects, fx, 4));
    CHECK(s.e[2].rx == 3 && s.e[2].shift == -12 && s.e[2].fold == DX_FOLD_WRAP && s.e[3].mute == 1);
    CHECK(s.e[0].flags == 1 && s.e[6].flags == 0 && s.e[6].preset[0] == 0); /* the missing engine has no preset */
    CHECK(s.e[0].preset[0] == 1 && s.e[5].preset[10] == 6 && s.e[1].pan == -15 && s.e[1].expr == 91 && s.e[4].bright == 4);
    CHECK(s.filter_type == DX_FILTER_LP && s.filter_hz == 4200 && s.filter_res == 20);
    CHECK(dx_scene_valid(&s));

    /* A different live state, then recall the scene. */
    struct dx_cfg cfg2; dx_cfg_default(&cfg2); cfg2.up = 0xff & ~(1U << 6);
    struct dx_live live2; memset(&live2, 0, sizeof live2);
    for (unsigned e = 0; e < DX_ENGINES; ++e) memset(eng_preset[e], 0, DX_PRESET_BYTES);
    eng_muted = 1U << 0; /* engine 0 is muted now, but the scene wants it unmuted; engine 3 vice versa */
    memset(fx, 0, 4); trace[0] = 0; publishes = 0;
    CHECK(dx_scene_apply(&port, &s, &cfg2, &live2) == 0);
    CHECK(trace[0] == 'P' && strstr(trace, "U;") > strstr(trace, "I7;") && strstr(trace, "I0;") < strstr(trace, "U;")); /* panic first, engines, then the router */
    CHECK(strstr(trace, "I6;") == NULL); /* the engine that is not up is skipped */
    CHECK(publishes == 1 && published.e[2].rx == 3 && published.kbd_ch == 4 && published.pool == 12 && published.e[3].mute == 1);
    CHECK(published.up == cfg2.up && published.priv[7] == 9); /* up mask and private channels are the device's, not the scene's */
    CHECK(cfg2.e[2].shift == -12 && cfg2.e[2].fold == DX_FOLD_WRAP);
    CHECK(!memcmp(eng_preset[0], s.e[0].preset, DX_PRESET_BYTES) && eng_preset[6][0] == 0);
    CHECK(eng_muted == (1U << 3)); /* engine 0 unmuted, engine 3 muted, others unchanged */
    CHECK(strstr(trace, "M0;") && strstr(trace, "M3;") && !strstr(trace, "M1;"));
    CHECK(fx[0] == 10 && fx[3] == 40 && strstr(trace, "F10;"));
    CHECK(live2.pan[1] == -15 && live2.expr[1] == 91 && live2.bright[4] == 4 && live2.filter_hz == 4200 && live2.filter_type == DX_FILTER_LP);
    CHECK(strstr(trace, "C0:10=") && strstr(trace, "C1:10=") && strstr(trace, "C1:11=91;")); /* pan then expression for every live engine */
    CHECK(strstr(trace, "T1:4200:20;"));
    /* pan to CC10: centre 64, hard left 1, hard right 127 */
    struct dx_scene p = s; p.e[0].pan = -50; p.e[1].pan = 0; p.e[2].pan = 50;
    trace[0] = 0; CHECK(dx_scene_apply(&port, &p, &cfg2, &live2) == 0);
    CHECK(strstr(trace, "C0:10=1;") && strstr(trace, "C1:10=64;") && strstr(trace, "C2:10=127;"));

    /* The first engine error is returned but the rest still load. */
    import_fail_for = 2; trace[0] = 0;
    CHECK(dx_scene_apply(&port, &s, &cfg2, &live2) == -8);
    CHECK(strstr(trace, "I3;") && strstr(trace, "I7;") && strstr(trace, "U;"));
    import_fail_for = -1;
    CHECK(dx_scene_apply(&port, &s, &cfg2, &live2) == 0);
    struct dx_scene invalid = s; invalid.pool = 0; trace[0] = 0;
    CHECK(dx_scene_apply(&port, &invalid, &cfg2, &live2) == DX_SCENE_INVALID && trace[0] == 0); /* nothing happens for a bad scene */

    /* Through flash and back. */
    fresh_flash();
    CHECK(dx_scene_write(12, &s) == 0);
    struct dx_scene back;
    CHECK(dx_scene_read(12, &back) == 0 && same(&s, &back));
}

int main(void)
{
    test_pack();
    test_write_read_and_layout();
    test_catalog();
    test_claim_and_foreign_data();
    test_coexistence_with_other_staging_users();
    test_power_cuts();
    test_claim_power_cuts();
    test_settings();
    test_recover_power_cuts();
    test_memory_failure();
    test_torn_sector_reads_as_empty();
    test_capture_and_apply();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
