/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* DX747 scene store and recall (see scene.h). Included once by app/project.c
 * on the device and by tools/dx747/host-test/scene.
 *
 * Flash format. A sector holds one scene (or the device settings): a 20 byte header
 * (magic, version, sector, generation, CRC, commit word) and the image. A write never
 * touches the destination first: the new sector image is committed in the
 * shared staging sector, then the destination is erased and programmed, then
 * staging is erased. At any power cut either the old or the new image is
 * complete and CRC-valid somewhere, and dx_scene_recover finishes the job.
 * Reads take the newest valid copy (destination or a committed staging copy).
 * The CRC covers the header and the image, so a torn sector reads as empty
 * rather than as a damaged scene. Scene i is sector i; the settings are the sector
 * after the last scene. */
#include <stdlib.h>
#include <string.h>
#include "fm1.h"
#include "fm1_engine_control.h"
#include "scene.h"
#include "parts.inc"

/* Provided by the staging owner (app/project.c): finish whatever another writer
 * left committed in the staging sector before this module reuses it. */
extern int groove_storage_prepare(void);

enum { DX_SEC_MAGIC = 0x31535844U /* "DXS1" */, DX_SEC_VERSION = 2, DX_SEC_HEADER = 20,
       DX_SEC_IMAGE = DX_SEC_HEADER + DX_SCENE_BYTES, DX_SEC_COMMIT = 16 };
struct dx_sec { uint32_t magic; uint16_t version, sector; uint32_t generation, crc, commit; };
_Static_assert(sizeof(struct dx_sec) == DX_SEC_HEADER, "sector header layout");
_Static_assert(DX_SEC_IMAGE <= DX_SECTOR_BYTES, "a scene fits its sector");
_Static_assert(DX_SCENES == DX_SCENE_SECTORS, "one scene per sector");
_Static_assert(DX_STORE_SECTORS == DX_SCENE_SECTORS + 1, "scenes then settings");

/* Packed scene image offsets. */
enum { SO_MAGIC = 0, SO_VERSION = 2, SO_USED = 3, SO_SEL = 4, SO_POOL = 5, SO_PARTS = 6, SO_NAME = 8,
       SO_EFFECTS = 20, SO_FTYPE = 24, SO_FRES = 25, SO_FHZ = 26, SO_PART0 = 28, SP_BYTES = 16,
       SP_CHANNEL = 0, SP_LEVEL = 1, SP_SEL = 2, SP_NAME = 4,
       SO_ENGINE0 = SO_PART0 + DX_PARTS * SP_BYTES, SE_BYTES = 237,
       SE_OWNER = 223, SE_LVL = 224, SE_LO = 225, SE_HI = 226, SE_VLO = 227, SE_VHI = 228, SE_SHIFT = 229,
       SE_FOLD = 230, SE_POLY = 231, SE_MUTE = 232, SE_PAN = 233, SE_EXPR = 234, SE_BRIGHT = 235, SE_FLAGS = 236 };
_Static_assert(SO_ENGINE0 + DX_ENGINES * SE_BYTES == DX_SCENE_BYTES, "scene image size");

static uint32_t dx_crc32(uint32_t crc, const void *data, unsigned bytes)
{
    static const uint32_t table[16] = {
        0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU, 0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
        0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU, 0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU
    };
    const uint8_t *p = data;
    while (bytes--) { crc ^= *p++; crc = (crc >> 4) ^ table[crc & 15U]; crc = (crc >> 4) ^ table[crc & 15U]; }
    return crc;
}

/* ---- packing ----------------------------------------------------------- */

/* The parts of a scene as the struct the rest of the code checks and edits. */
static void dx_scene_parts(const struct dx_scene *s, struct dx_parts *p)
{
    memset(p, 0, sizeof *p);
    p->selected = s->sel;
    memcpy(p->part, s->part, sizeof p->part);
    for (unsigned e = 0; e < DX_ENGINES; ++e) { p->layer[e].owner = s->e[e].owner; p->layer[e].lvl = s->e[e].lvl; }
}

int dx_scene_valid(const struct dx_scene *s)
{
    if (!s || s->sel >= DX_PARTS || s->pool < 1 || s->pool > DX_HELD_MAX || s->filter_type > DX_FILTER_BP ||
        s->filter_res > 127 || (s->filter_hz && (s->filter_hz < 20 || s->filter_hz > 20000))) return 0;
    for (unsigned i = 0; i < sizeof s->name; ++i) if (s->name[i] < 32 || s->name[i] > 126) return 0;
    struct dx_parts p;
    dx_scene_parts(s, &p);
    if (!dx_parts_valid(&p)) return 0;
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        const struct dx_scene_engine *x = &s->e[e];
        if (x->fold > DX_FOLD_WRAP || x->poly < 1 || x->poly > 8 || x->mute > 1 ||
            x->shift < -48 || x->shift > 48 || x->pan < -50 || x->pan > 50 || x->expr > 127 ||
            x->bright < -50 || x->bright > 50 || x->lo > x->hi || x->hi > 127 ||
            x->vlo < 1 || x->vhi > 127 || x->vlo > x->vhi) return 0;
    }
    return 1;
}

void dx_scene_pack(const struct dx_scene *s, uint8_t out[DX_SCENE_BYTES])
{
    memset(out, 0, DX_SCENE_BYTES);
    out[SO_MAGIC] = 'S'; out[SO_MAGIC + 1] = 'C'; out[SO_VERSION] = 2; out[SO_USED] = 1;
    out[SO_SEL] = s->sel; out[SO_POOL] = s->pool;
    for (unsigned i = 0; i < DX_PARTS; ++i) if (s->part[i].channel) out[SO_PARTS] |= (uint8_t)(1U << i);
    memcpy(out + SO_NAME, s->name, 12);
    memcpy(out + SO_EFFECTS, s->effects, 4);
    out[SO_FTYPE] = s->filter_type; out[SO_FRES] = s->filter_res;
    out[SO_FHZ] = (uint8_t)s->filter_hz; out[SO_FHZ + 1] = (uint8_t)(s->filter_hz >> 8);
    for (unsigned i = 0; i < DX_PARTS; ++i) {
        uint8_t *p = out + SO_PART0 + i * SP_BYTES;
        p[SP_CHANNEL] = s->part[i].channel; p[SP_LEVEL] = s->part[i].level; p[SP_SEL] = s->part[i].sel;
        memcpy(p + SP_NAME, s->part[i].name, sizeof s->part[i].name);
    }
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        uint8_t *p = out + SO_ENGINE0 + e * SE_BYTES;
        const struct dx_scene_engine *x = &s->e[e];
        memcpy(p, x->preset, DX_PRESET_BYTES);
        p[SE_OWNER] = x->owner; p[SE_LVL] = x->lvl; p[SE_LO] = x->lo; p[SE_HI] = x->hi; p[SE_VLO] = x->vlo; p[SE_VHI] = x->vhi;
        p[SE_SHIFT] = (uint8_t)x->shift; p[SE_FOLD] = x->fold; p[SE_POLY] = x->poly; p[SE_MUTE] = x->mute;
        p[SE_PAN] = (uint8_t)x->pan; p[SE_EXPR] = x->expr; p[SE_BRIGHT] = (uint8_t)x->bright; p[SE_FLAGS] = x->flags;
    }
}

/* 0 on success, DX_SCENE_EMPTY for an erased sector, DX_SCENE_INVALID otherwise. */
int dx_scene_unpack(const uint8_t in[DX_SCENE_BYTES], struct dx_scene *s)
{
    if (in[SO_MAGIC] == 0xff && in[SO_USED] == 0xff) return DX_SCENE_EMPTY;
    if (in[SO_MAGIC] != 'S' || in[SO_MAGIC + 1] != 'C' || in[SO_VERSION] != 2 || in[SO_USED] != 1) return DX_SCENE_INVALID;
    memset(s, 0, sizeof *s);
    s->sel = in[SO_SEL]; s->pool = in[SO_POOL];
    memcpy(s->name, in + SO_NAME, 12);
    memcpy(s->effects, in + SO_EFFECTS, 4);
    s->filter_type = in[SO_FTYPE]; s->filter_res = in[SO_FRES];
    s->filter_hz = (uint16_t)(in[SO_FHZ] | in[SO_FHZ + 1] << 8);
    for (unsigned i = 0; i < DX_PARTS; ++i) {
        const uint8_t *p = in + SO_PART0 + i * SP_BYTES;
        s->part[i].channel = p[SP_CHANNEL]; s->part[i].level = p[SP_LEVEL]; s->part[i].sel = p[SP_SEL];
        memcpy(s->part[i].name, p + SP_NAME, sizeof s->part[i].name);
    }
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        const uint8_t *p = in + SO_ENGINE0 + e * SE_BYTES;
        struct dx_scene_engine *x = &s->e[e];
        memcpy(x->preset, p, DX_PRESET_BYTES);
        x->owner = p[SE_OWNER]; x->lvl = p[SE_LVL]; x->lo = p[SE_LO]; x->hi = p[SE_HI]; x->vlo = p[SE_VLO]; x->vhi = p[SE_VHI];
        x->shift = (int8_t)p[SE_SHIFT]; x->fold = p[SE_FOLD]; x->poly = p[SE_POLY]; x->mute = p[SE_MUTE];
        x->pan = (int8_t)p[SE_PAN]; x->expr = p[SE_EXPR]; x->bright = (int8_t)p[SE_BRIGHT]; x->flags = p[SE_FLAGS];
    }
    return dx_scene_valid(s) ? 0 : DX_SCENE_INVALID;
}

/* ---- sector access ----------------------------------------------------- */

static uint32_t dx_sector_addr(unsigned sector) { return DX_STORE_BASE + sector * DX_SECTOR_BYTES; }

static uint32_t dx_sec_crc_image(const uint8_t *img)
{
    uint32_t crc = dx_crc32(~0U, img, 12);
    return ~dx_crc32(crc, img + DX_SEC_HEADER, DX_SEC_IMAGE - DX_SEC_HEADER);
}

/* CRC of the stored image at addr, without a RAM copy. */
static int dx_sec_crc_flash(uint32_t addr, uint32_t *crc_out)
{
    uint8_t head[DX_SEC_HEADER], chunk[256];
    int r = fm1_flash_read(addr, head, sizeof head);
    if (r) return r;
    uint32_t crc = dx_crc32(~0U, head, 12);
    for (unsigned at = DX_SEC_HEADER; at < DX_SEC_IMAGE;) {
        unsigned n = DX_SEC_IMAGE - at < sizeof chunk ? DX_SEC_IMAGE - at : (unsigned)sizeof chunk;
        r = fm1_flash_read(addr + at, chunk, n);
        if (r) return r;
        crc = dx_crc32(crc, chunk, n);
        at += n;
    }
    *crc_out = ~crc;
    return 0;
}

/* 0 when a committed, CRC-valid image for this sector is stored at addr. */
static int dx_sec_valid_at(uint32_t addr, unsigned sector, struct dx_sec *h)
{
    int r = fm1_flash_read(addr, h, sizeof *h);
    if (r) return r;
    if (h->magic != DX_SEC_MAGIC || h->version != DX_SEC_VERSION || h->sector != sector || h->commit) return DX_SCENE_EMPTY;
    uint32_t crc;
    r = dx_sec_crc_flash(addr, &crc);
    if (r) return r;
    return crc == h->crc ? 0 : DX_SCENE_EMPTY;
}

static int dx_sector_is_sample_bank(unsigned sector);
static int dx_store_foreign_besides(unsigned except);

/* Newest valid copy of a sector: the destination, or staging if it is newer. A staged copy of a sector
 * that now holds the stock sample bank is stale (stock firmware ran in between) and does not count. */
static int dx_locate(unsigned sector, uint32_t *addr, struct dx_sec *h)
{
    struct dx_sec d, s;
    uint32_t dest = dx_sector_addr(sector);
    int rd = dx_sec_valid_at(dest, sector, &d);
    if (rd < 0) return rd;
    int rs = dx_sec_valid_at(DX_STAGE, sector, &s);
    if (rs < 0) return rs;
    if (!rd && (rs || (int32_t)(d.generation - s.generation) >= 0)) { *addr = dest; *h = d; return 0; }
    if (!rs) {
        int bank = dx_sector_is_sample_bank(sector);
        if (bank < 0) return bank;
        if (bank) return DX_SCENE_EMPTY;
        /* The same test recovery applies before it finishes a staged image, so that reads and recovery agree: with
         * foreign data in another store sector the staged copy is stale, not a save waiting to be finished. */
        int foreign = dx_store_foreign_besides(sector);
        if (foreign) return foreign < 0 ? foreign : DX_SCENE_EMPTY;
        *addr = DX_STAGE; *h = s; return 0;
    }
    return DX_SCENE_EMPTY;
}

/* The stock sample bank starts with this magic ("FM1S", app/samples.c in stock). */
enum { DX_SAMPLE_MAGIC = 0x53314d46 };

/* A sector holds ours or nothing: it starts with our magic, or its first 256 bytes
 * are erased. Samples and other data never look like that, and a sector that lost
 * power in the middle of an erase usually does not either (see dx_sector_free). */
static int dx_sector_ours_or_blank(unsigned sector)
{
    uint8_t head[256];
    int r = fm1_flash_read(dx_sector_addr(sector), head, sizeof head);
    if (r) return r;
    uint32_t magic;
    memcpy(&magic, head, 4);
    if (magic == DX_SEC_MAGIC) return 0;
    for (unsigned i = 0; i < sizeof head; ++i) if (head[i] != 0xff) return DX_SCENE_NEEDS_CLAIM;
    return 0;
}

/* True when the sector starts with the stock sample bank's header. */
static int dx_sector_is_sample_bank(unsigned sector)
{
    uint32_t magic;
    int r = fm1_flash_read(dx_sector_addr(sector), &magic, sizeof magic);
    return r < 0 ? r : magic == DX_SAMPLE_MAGIC;
}

/* A sector is free for scenes when it holds ours or is erased, or when it was torn by a power
 * cut in the middle of our own write and the committed copy that finishes it waits in staging.
 * Never when it plainly holds the stock sample bank, whatever waits in staging. */
static int dx_sector_free(unsigned sector)
{
    int r = dx_sector_ours_or_blank(sector);
    if (r != DX_SCENE_NEEDS_CLAIM) return r;
    r = dx_sector_is_sample_bank(sector);
    if (r) return r < 0 ? r : DX_SCENE_NEEDS_CLAIM;
    struct dx_sec staged;
    r = dx_sec_valid_at(DX_STAGE, sector, &staged);
    if (r < 0) return r;
    return r ? DX_SCENE_NEEDS_CLAIM : 0;
}

/* The stock firmware's project and settings records (magic "FMP6", "FMS6", the v5 and the
 * legacy ones), laid out {magic, generation, target sector, crc, commit}, are staged in the same
 * sector. A committed one whose target sector has not caught up is a stock save that a power cut
 * interrupted: staging holds the only complete copy, and stock would finish it at its next write. */
static int dx_stock_magic(uint32_t magic)
{
    return magic == 0x36504d46U || magic == 0x36534d46U || magic == 0x35504d46U ||
           magic == 0x35534d46U || magic == 0x34504d46U || magic == 0x34534d46U;
}
/* 1 when staging holds such an unfinished stock save, 0 when not, negative on a flash error. */
static int dx_stock_stranded(void)
{
    uint32_t w[5], d[5];
    int r = fm1_flash_read(DX_STAGE, w, sizeof w);
    if (r) return r;
    if (!dx_stock_magic(w[0]) || w[4] != 0 || w[2] >= DX_STAGE / DX_SECTOR_BYTES) return 0;
    r = fm1_flash_read(w[2] * DX_SECTOR_BYTES, d, sizeof d);
    if (r) return r;
    if (dx_stock_magic(d[0]) && d[4] == 0) {
        uint32_t diff = d[1] - w[1];
        if ((!diff && d[3] == w[3]) || (diff && diff < 0x80000000U)) return 0; /* the target caught up: staging is a duplicate */
    }
    return 1;
}

/* 1 when a store sector other than `except` holds someone else's data, negative on a flash error. */
static int dx_store_foreign_besides(unsigned except)
{
    for (unsigned s = 0; s < DX_STORE_SECTORS; ++s) {
        if (s == except) continue;
        int r = dx_sector_ours_or_blank(s);
        if (r) return r == DX_SCENE_NEEDS_CLAIM ? 1 : r;
    }
    return 0;
}

/* 0 when the whole store is ours or erased and staging holds nothing of someone else's that
 * only the user may give up; 1 when the user has to agree to dx_scene_claim first. */
int dx_scene_store_state(void)
{
    if (fm1_flash_size() < DX_STORE_BASE + (uint32_t)DX_STORE_SECTORS * DX_SECTOR_BYTES) return FM1_FLASH_UNAVAILABLE;
    for (unsigned s = 0; s < DX_STORE_SECTORS; ++s) {
        int r = dx_sector_free(s);
        if (r) return r == DX_SCENE_NEEDS_CLAIM ? 1 : r;
    }
    return dx_stock_stranded();
}

/* The user agreed to give up what stands in the way: the stock sample bank (or any other foreign data)
 * in the store, an unfinished stock save, or a stale scene image of ours in staging. Only that is
 * erased: sectors that already hold our scenes or nothing are left alone, so scenes survive a claim
 * whose only reason was one foreign sector or a stock save in staging. A voice-library record in
 * staging is left for its own owner to finish. An interrupted claim leaves foreign sectors behind, so
 * the store still reads as foreign and the user is asked again. */
int dx_scene_claim(void)
{
    if (fm1_flash_size() < DX_STORE_BASE + (uint32_t)DX_STORE_SECTORS * DX_SECTOR_BYTES) return FM1_FLASH_UNAVAILABLE;
    uint32_t magic;
    int r = fm1_flash_read(DX_STAGE, &magic, sizeof magic);
    if (r) return r;
    fm1_engine_pause();
    if (magic == DX_SEC_MAGIC || dx_stock_magic(magic)) r = fm1_flash_erase(DX_STAGE);
    for (unsigned i = 0; !r && i < DX_STORE_SECTORS; ++i) {
        int f = dx_sector_ours_or_blank(i);
        if (f == DX_SCENE_NEEDS_CLAIM) r = fm1_flash_erase(dx_sector_addr(i)); /* the torn sector of our own write counts as foreign here: its staged copy was just discarded */
        else r = f;
    }
    fm1_engine_resume();
    return r;
}

/* ---- staging protocol -------------------------------------------------- */

static int dx_verify_image(uint32_t addr, uint32_t expected)
{
    uint32_t crc;
    int r = dx_sec_crc_flash(addr, &crc);
    return r ? r : crc == expected ? 0 : FM1_FLASH_IO;
}

static int dx_commit_word(uint32_t addr)
{
    uint32_t zero = 0;
    return fm1_flash_write(addr + DX_SEC_COMMIT, &zero, sizeof zero);
}

/* img is a complete image with its header filled in and commit = ~0. */
static int dx_program(unsigned sector, const uint8_t *img, uint32_t crc)
{
    uint32_t dest = dx_sector_addr(sector);
    int r = fm1_flash_erase(DX_STAGE);
    if (!r) r = fm1_flash_write(DX_STAGE, img, DX_SEC_IMAGE);
    if (!r) r = dx_verify_image(DX_STAGE, crc);
    if (!r) r = dx_commit_word(DX_STAGE);
    if (!r) r = fm1_flash_erase(dest);
    if (!r) r = fm1_flash_write(dest, img, DX_SEC_IMAGE);
    if (!r) r = dx_verify_image(dest, crc);
    if (!r) r = dx_commit_word(dest);
    if (!r) r = fm1_flash_erase(DX_STAGE);
    return r;
}

/* Finish a committed staged image, or discard a half-written one. Anything in
 * staging that is not ours (a library bank, a project) is left alone, and the one
 * thing of someone else's that the next write would destroy for good, an unfinished
 * stock save, makes recovery refuse until the user has agreed to dx_scene_claim. */
int dx_scene_recover(void)
{
    struct dx_sec st;
    int r = fm1_flash_read(DX_STAGE, &st, sizeof st);
    if (r) return r;
    if (st.magic != DX_SEC_MAGIC) {
        r = dx_stock_stranded();
        return r < 0 ? r : r ? DX_SCENE_NEEDS_CLAIM : 0;
    }
    if (st.commit || st.version != DX_SEC_VERSION || st.sector >= DX_STORE_SECTORS) return fm1_flash_erase(DX_STAGE);
    r = dx_sec_valid_at(DX_STAGE, st.sector, &st);
    if (r < 0) return r;
    if (r) return fm1_flash_erase(DX_STAGE); /* committed but not intact: cannot have been committed by us */
    struct dx_sec d;
    uint32_t dest = dx_sector_addr(st.sector);
    r = dx_sec_valid_at(dest, st.sector, &d);
    if (r < 0) return r;
    if (!r && (int32_t)(d.generation - st.generation) >= 0) return fm1_flash_erase(DX_STAGE);
    /* A committed image can be stale: stranded by a power cut, then stock firmware ran and used the
     * flash for its samples. Our own interrupted write leaves every other store sector ours or
     * erased, and its destination torn or ours; if the destination plainly holds the stock sample bank,
     * or another sector holds someone else's data, this image no longer belongs here. */
    r = dx_sector_is_sample_bank(st.sector);
    if (r < 0) return r;
    if (r) return fm1_flash_erase(DX_STAGE);
    r = dx_store_foreign_besides(st.sector);
    if (r < 0) return r;
    if (r) return fm1_flash_erase(DX_STAGE);
    fm1_engine_pause();
    r = fm1_flash_erase(dest);
    for (unsigned at = 0; !r && at < DX_SEC_IMAGE;) {
        uint8_t chunk[256];
        unsigned n = DX_SEC_IMAGE - at < sizeof chunk ? DX_SEC_IMAGE - at : (unsigned)sizeof chunk;
        r = fm1_flash_read(DX_STAGE + at, chunk, n);
        for (unsigned i = DX_SEC_COMMIT; !r && i < DX_SEC_COMMIT + 4; ++i) if (i >= at && i < at + n) chunk[i - at] = 0xff;
        if (!r) r = fm1_flash_write(dest + at, chunk, n);
        at += n;
    }
    if (!r) r = dx_verify_image(dest, st.crc);
    if (!r) r = dx_commit_word(dest);
    if (!r) r = fm1_flash_erase(DX_STAGE);
    fm1_engine_resume();
    return r;
}

/* ---- scene access ------------------------------------------------------ */

/* Replace the image of one sector with payload (n bytes, the rest of the image erased), or clear it when payload is
 * NULL. */
static unsigned scene_revision;                        /* counts scene writes, so a screen that lists the scenes knows when to read them again */
unsigned dx_scene_revision(void) { return scene_revision; }
static int dx_sector_put(unsigned sector, const uint8_t *payload, unsigned n)
{
    if (sector >= DX_STORE_SECTORS || n > DX_SCENE_BYTES) return DX_SCENE_INVALID;
    /* Recovery first: it finishes the write that a power cut left half done, which a sector check
     * would otherwise mistake for someone else's data. Then nothing is written until the whole
     * store is ours or erased. */
    int r = groove_storage_prepare();
    if (!r) r = dx_scene_store_state();
    if (r) return r == 1 ? DX_SCENE_NEEDS_CLAIM : r;
    uint8_t *img = malloc(DX_SEC_IMAGE);
    if (!img) return DX_SCENE_MEMORY;
    uint32_t addr, generation = 0;
    struct dx_sec h;
    r = dx_locate(sector, &addr, &h);
    if (!r) { r = fm1_flash_read(addr, img, DX_SEC_IMAGE); generation = h.generation; }
    else if (r == DX_SCENE_EMPTY) { memset(img, 0xff, DX_SEC_IMAGE); r = 0; }
    if (r) { free(img); return r; }
    uint8_t *body = img + DX_SEC_HEADER;
    static uint8_t fresh[DX_SCENE_BYTES]; /* control task only; off the 8 KB stack */
    memset(fresh, 0xff, sizeof fresh);
    if (payload) memcpy(fresh, payload, n);
    if (!payload && !generation) { free(img); return 0; } /* clearing a sector that does not exist */
    if (generation && !memcmp(body, fresh, DX_SCENE_BYTES)) { free(img); return 0; } /* unchanged: no erase */
    memcpy(body, fresh, DX_SCENE_BYTES);
    struct dx_sec nh = {DX_SEC_MAGIC, DX_SEC_VERSION, (uint16_t)sector, generation + 1U, 0, ~0U};
    memcpy(img, &nh, DX_SEC_HEADER);
    nh.crc = dx_sec_crc_image(img);
    memcpy(img + 12, &nh.crc, 4);
    fm1_engine_pause();
    r = dx_program(sector, img, nh.crc);
    fm1_engine_resume();
    free(img);
    if (!r && sector < DX_SCENE_SECTORS) (void)dx_scene_scan();
    return r;
}

int dx_scene_write(unsigned index, const struct dx_scene *s)
{
    if (!dx_scene_valid(s)) return DX_SCENE_INVALID;
    if (index >= DX_SCENES) return DX_SCENE_INVALID;
    static uint8_t image[DX_SCENE_BYTES]; /* control task only; off the 8 KB stack */
    dx_scene_pack(s, image);
    return dx_sector_put(index, image, sizeof image);
}
int dx_scene_clear(unsigned index)
{
    return index >= DX_SCENES ? DX_SCENE_INVALID : dx_sector_put(index, NULL, 0);
}

/* ---- device settings (the sector after the scenes) ------------------------- */

enum { ST_BYTES = 10 };

int dx_settings_valid(const struct dx_settings *s)
{
    return s && s->radio_off <= 7 && s->role <= 1 && s->backlight <= 8 && s->last_scene < DX_SCENES &&
           s->scene_ch <= 16 && !(s->flags & ~1U) && s->master_ch <= 16;
}
int dx_settings_write(const struct dx_settings *s)
{
    if (!dx_settings_valid(s)) return DX_SCENE_INVALID;
    const uint8_t image[ST_BYTES] = {'D', 'S', 2, s->radio_off, s->role, s->backlight, s->last_scene, s->scene_ch, s->flags, s->master_ch};
    return dx_sector_put(DX_SETTINGS_SECTOR, image, sizeof image);
}
int dx_settings_read(struct dx_settings *out)
{
    uint32_t addr;
    struct dx_sec h;
    int r = dx_locate(DX_SETTINGS_SECTOR, &addr, &h);
    if (r) return r;
    uint8_t image[ST_BYTES];
    r = fm1_flash_read(addr + DX_SEC_HEADER, image, sizeof image);
    if (r) return r;
    if (image[0] == 0xff && image[1] == 0xff) return DX_SCENE_EMPTY;
    if (image[0] != 'D' || image[1] != 'S' || image[2] != 2) return DX_SCENE_INVALID;
    out->radio_off = image[3]; out->role = image[4]; out->backlight = image[5];
    out->last_scene = image[6]; out->scene_ch = image[7]; out->flags = image[8]; out->master_ch = image[9];
    return dx_settings_valid(out) ? 0 : DX_SCENE_INVALID;
}

int dx_scene_read(unsigned index, struct dx_scene *out)
{
    if (index >= DX_SCENES) return DX_SCENE_INVALID;
    uint32_t addr;
    struct dx_sec h;
    int r = dx_locate(index, &addr, &h);
    if (r) return r;
    static uint8_t image[DX_SCENE_BYTES]; /* control task only; off the 8 KB stack */
    r = fm1_flash_read(addr + DX_SEC_HEADER, image, sizeof image);
    return r ? r : dx_scene_unpack(image, out);
}

/* Catalogue: names and which parts each scene uses, for the Scenes screen. */
typedef struct { uint64_t used; char name[DX_SCENES][12]; uint8_t parts[DX_SCENES]; } dx_catalog_t;
static dx_catalog_t dx_catalog;

int dx_scene_scan(void)
{
    /* Built aside and published only when every sector was read: a flash error half way must not leave a catalogue
     * that shows real scenes as free slots. */
    static dx_catalog_t fresh; /* control task only */
    memset(&fresh, 0, sizeof fresh);
    for (unsigned sector = 0; sector < DX_SCENE_SECTORS; ++sector) {
        uint32_t addr;
        struct dx_sec h;
        int r = dx_locate(sector, &addr, &h);
        if (r == DX_SCENE_EMPTY) continue;
        if (r) return r;
        uint8_t head[SO_EFFECTS];
        r = fm1_flash_read(addr + DX_SEC_HEADER, head, sizeof head);
        if (r) return r;
        if (head[SO_MAGIC] != 'S' || head[SO_MAGIC + 1] != 'C' || head[SO_VERSION] != 2 || head[SO_USED] != 1) continue;
        fresh.used |= (uint64_t)1 << sector;
        memcpy(fresh.name[sector], head + SO_NAME, 12);
        fresh.parts[sector] = head[SO_PARTS];
    }
    dx_catalog = fresh;
    ++scene_revision;                                      /* the boot scan, a claim and every scene write: screens list the scenes again */
    return 0;
}

int dx_scene_info(unsigned index, char name[13], uint8_t *parts_used)
{
    if (index >= DX_SCENES || !((dx_catalog.used >> index) & 1U)) return DX_SCENE_EMPTY;
    memcpy(name, dx_catalog.name[index], 12);
    name[12] = 0;
    if (parts_used) *parts_used = dx_catalog.parts[index];
    return 0;
}

/* ---- live state to and from a scene ------------------------------------ */

int dx_scene_capture(const struct dx_port *port, const struct dx_cfg *cfg, const struct dx_live *live,
                     const char name[12], struct dx_scene *out)
{
    memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < 12; ++i) out->name[i] = name && name[i] >= 32 && name[i] <= 126 ? name[i] : ' ';
    out->sel = live->parts.selected; out->pool = cfg->pool;
    memcpy(out->part, live->parts.part, sizeof out->part);
    port->effects_get(out->effects);
    out->filter_type = live->filter_type; out->filter_res = live->filter_res; out->filter_hz = live->filter_hz;
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        struct dx_scene_engine *x = &out->e[e];
        const struct dx_engine_cfg *c = &cfg->e[e];
        const struct dx_layer *l = &live->parts.layer[e];
        x->owner = l->owner; x->lvl = l->lvl;
        x->lo = c->lo; x->hi = c->hi; x->shift = c->shift; x->fold = c->fold; x->poly = c->poly; x->mute = c->mute;
        x->vlo = c->vlo ? c->vlo : 1; x->vhi = c->vhi ? c->vhi : 127;
        x->pan = live->pan[e]; x->expr = live->expr[e]; x->bright = live->bright[e];
        if ((cfg->up & (1U << e)) && l->owner != DX_NO_PART) { /* a free engine has nothing worth keeping */
            int r = port->export_preset(e, x->preset);
            if (r) return r;
            x->flags |= 1; /* preset present */
        }
    }
    return dx_scene_valid(out) ? 0 : DX_SCENE_INVALID;
}

/* Everything audible is released first, then the engines are loaded, then the
 * router switches to the new routing and the controllers the engine does not
 * persist (pan, expression, level, mute) are restored. The first engine error is
 * returned after the rest have been tried. */
int dx_scene_apply(const struct dx_port *port, const struct dx_scene *s, struct dx_cfg *cfg, struct dx_live *live)
{
    if (!dx_scene_valid(s)) return DX_SCENE_INVALID;
    struct dx_cfg next = *cfg;
    struct dx_parts parts;
    dx_scene_parts(s, &parts);
    next.pool = s->pool;
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        const struct dx_scene_engine *x = &s->e[e];
        struct dx_engine_cfg *c = &next.e[e];
        c->lo = x->lo; c->hi = x->hi; c->shift = x->shift; c->fold = x->fold; c->poly = x->poly; c->mute = x->mute;
        c->vlo = x->vlo; c->vhi = x->vhi;
    }
    dx_parts_derive(&parts, &next);
    int first = 0;
    port->panic();
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        if (!(next.up & (1U << e)) || !(s->e[e].flags & 1) || s->e[e].owner == DX_NO_PART) continue;
        int r = port->import_preset(e, s->e[e].preset);
        if (r && !first) first = r;
    }
    port->publish(&next);
    *cfg = next;
    live->parts = parts;
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        const struct dx_scene_engine *x = &s->e[e];
        live->pan[e] = x->pan; live->expr[e] = x->expr; live->bright[e] = x->bright;
        if (!(next.up & (1U << e)) || x->owner == DX_NO_PART) continue;
        int value = 64 + x->pan * 63 / 50;
        port->cc(e, 10, (unsigned)(value < 1 ? 1 : value > 127 ? 127 : value));
        port->cc(e, 11, x->expr);
        port->cc(e, 7, dx_layer_volume(x->lvl, parts.part[x->owner].level));
        if (!!port->muted(e) != !!x->mute) port->toggle_mute(e);
        port->bright(e, x->bright);
    }
    port->effects_set(s->effects);
    live->filter_type = s->filter_type; live->filter_res = s->filter_res; live->filter_hz = s->filter_hz;
    port->filter(s->filter_type, s->filter_hz, s->filter_res);
    return first;
}
