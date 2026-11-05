/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Host tests for the DX747 parts model (app/dx747/parts.h, parts.inc): the default layout, engine allocation, the
 * routing derived from the parts and the edits the screens make. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../../../app/dx747/route.inc"
#include "../../../../app/dx747/parts.inc"

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static struct dx_parts p;
static struct dx_cfg c;

static void fresh(void)
{
    dx_cfg_default(&c);
    dx_parts_default(&p);
    dx_parts_derive(&p, &c);
}
/* Remove every part, as the screens can: all engines free. */
static void clear_all(void)
{
    for (unsigned i = 0; i < DX_PARTS; ++i) while (dx_part_used(&p, i)) dx_parts_remove_layer(&p, &c, i, 0);
}

static void test_default(void)
{
    fresh();
    CHECK(dx_parts_valid(&p));
    struct dx_cfg d;
    dx_cfg_default(&d);
    for (unsigned e = 0; e < DX_ENGINES; ++e) {
        CHECK(c.e[e].rx == d.e[e].rx);                            /* the same routing as a unit before any scene */
        CHECK(c.e[e].ctl == e + 1U);                              /* the only layer of a part is its selected layer */
    }
    CHECK(c.kbd_ch == 0 && p.selected == 0);
    CHECK(dx_parts_mask(&p) == 0xff && dx_parts_free_engines(&p, &c) == 0);
}

static void test_add_and_remove(void)
{
    fresh();
    clear_all();
    CHECK(dx_parts_mask(&p) == 0 && dx_parts_free_engines(&p, &c) == 0xff);
    CHECK(dx_parts_valid(&p));
    for (unsigned e = 0; e < DX_ENGINES; ++e) CHECK(c.e[e].rx == 0 && c.e[e].ctl == 0);
    /* a new part takes the lowest free slot, the lowest free channel and the lowest free engine */
    int a = dx_parts_add_part(&p, &c, DX_PARTS);
    CHECK(a == 0 && p.part[0].channel == 1 && p.layer[0].owner == 0 && c.e[0].rx == 1U && c.e[0].ctl == 1);
    CHECK(p.selected == 0 && c.kbd_ch == 0);                       /* the first part is the one the keys play */
    CHECK(c.e[0].lo == 0 && c.e[0].hi == 127 && c.e[0].vlo == 1 && c.e[0].vhi == 127 && c.e[0].shift == 0);
    int b = dx_parts_add_part(&p, &c, DX_PARTS);
    CHECK(b == 1 && p.part[1].channel == 2 && p.layer[1].owner == 1 && p.selected == 0);   /* the selection stays */
    /* layers: engine order, selected layer holds the control channel */
    int e = dx_parts_add_layer(&p, &c, 0);
    CHECK(e == 2 && p.layer[2].owner == 0 && c.e[2].rx == 1U);
    uint8_t list[DX_ENGINES];
    CHECK(dx_part_layers(&p, 0, list) == 2 && list[0] == 0 && list[1] == 2);
    CHECK(c.e[0].ctl == 1 && c.e[2].ctl == 0);                     /* SysEx target is layer 1 */
    p.part[0].sel = 1;
    dx_parts_derive(&p, &c);
    CHECK(c.e[0].ctl == 0 && c.e[2].ctl == 1 && dx_part_sel_engine(&p, 0) == 2);
    CHECK(dx_parts_valid(&p));
    /* removing the selected layer moves the selection to the last remaining one */
    CHECK(dx_parts_remove_layer(&p, &c, 0, 1) == 2);
    CHECK(p.part[0].sel == 0 && p.layer[2].owner == DX_NO_PART && c.e[2].rx == 0 && c.e[0].ctl == 1);
    /* removing the last layer removes the part and hands the selection on */
    CHECK(p.selected == 0);
    CHECK(dx_parts_remove_layer(&p, &c, 0, 0) == 0);
    CHECK(!dx_part_used(&p, 0) && p.part[0].channel == 0 && p.selected == 1);
    CHECK(dx_parts_valid(&p));
    CHECK(dx_parts_remove_layer(&p, &c, 0, 0) == DX_PART_REFUSED);  /* nothing there */
    CHECK(dx_parts_add_layer(&p, &c, 0) == DX_PART_REFUSED);        /* a layer needs a part */
    /* the freed slot, engine and channel are the ones the next part takes */
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == 0 && p.part[0].channel == 1 && p.layer[0].owner == 0);
}

static void test_preferred_slot(void)
{
    fresh();
    clear_all();
    CHECK(dx_parts_add_part(&p, &c, 5) == 5 && p.part[5].channel == 1 && p.selected == 5);   /* the empty slot the screen shows */
    CHECK(dx_parts_add_part(&p, &c, 5) == 0 && p.part[0].channel == 2);                       /* taken: the lowest unused slot instead */
    CHECK(dx_parts_add_part(&p, &c, 9) == 1 && p.selected == 5);                              /* no such slot: likewise */
    CHECK(dx_parts_valid(&p));
}

static void test_limits(void)
{
    fresh();
    /* every engine is a layer already: no part and no layer can be added */
    CHECK(dx_parts_add_layer(&p, &c, 0) == DX_PART_NO_ENGINE && dx_parts_valid(&p));
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == DX_PART_NO_SLOT);
    /* free the part slots but keep the engines busy: eight layers on one part */
    clear_all();
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == 0);
    for (unsigned i = 1; i < DX_ENGINES; ++i) CHECK(dx_parts_add_layer(&p, &c, 0) == (int)i);
    CHECK(dx_parts_add_layer(&p, &c, 0) == DX_PART_NO_ENGINE);
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == DX_PART_NO_ENGINE && !dx_part_used(&p, 1));   /* a failed add leaves no half part */
    CHECK(dx_parts_valid(&p));
    uint8_t list[DX_ENGINES];
    CHECK(dx_part_layers(&p, 0, list) == 8);
    /* engines that failed to start are never given out */
    clear_all();
    c.up = 0x0f;
    for (unsigned i = 0; i < 4; ++i) CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == (int)i);
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == DX_PART_NO_ENGINE);
    /* the scene channel is never given to a part */
    clear_all();
    c.up = 0xff; c.scene_ch = 1;
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == 0 && p.part[0].channel == 2);
    CHECK(!dx_parts_set_channel(&p, &c, 0, 1));
    CHECK(dx_parts_set_channel(&p, &c, 0, 5) && p.part[0].channel == 5 && c.e[0].rx == 1U << 4);
    /* a new part also keeps off the master control channel (a part may still be moved there by hand) */
    clear_all();
    c.scene_ch = 0; c.master_ch = 1;
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == 0 && p.part[0].channel == 2);
    c.scene_ch = 2;
    CHECK(dx_parts_add_part(&p, &c, DX_PARTS) == 1 && p.part[1].channel == 3);
    CHECK(dx_parts_set_channel(&p, &c, 1, 1) && p.part[1].channel == 1);
}

static void test_channels(void)
{
    fresh();
    CHECK(!dx_parts_set_channel(&p, &c, 0, 2));                    /* part 2 has channel 2 */
    CHECK(dx_parts_set_channel(&p, &c, 0, 1));                     /* its own channel again is fine */
    CHECK(!dx_parts_set_channel(&p, &c, 0, 0) && !dx_parts_set_channel(&p, &c, 0, 17));
    CHECK(dx_parts_set_channel(&p, &c, 0, 12) && c.e[0].rx == 1U << 11 && dx_part_on_channel(&p, 12) == 0);
    CHECK(dx_part_on_channel(&p, 1) == DX_NO_PART && dx_parts_valid(&p));
    CHECK(!dx_parts_set_channel(&p, &c, 9, 3));                    /* no such part */
    /* the keys play the selected part's channel */
    dx_parts_select(&p, 0);
    dx_parts_derive(&p, &c);
    CHECK(c.kbd_ch == 11);
    dx_parts_select(&p, 3);
    dx_parts_derive(&p, &c);
    CHECK(c.kbd_ch == 3);
    dx_parts_select(&p, 99);
    CHECK(p.selected == 3);
    /* an empty selected part: the keys play nothing */
    clear_all();
    CHECK(p.selected < DX_PARTS && !dx_part_used(&p, p.selected) && c.kbd_ch == 16);
    CHECK(dx_parts_add_part(&p, &c, 4) == 4 && c.kbd_ch == 0 && p.selected == 4);
}

static void test_collapse(void)
{
    fresh();
    clear_all();
    dx_parts_add_part(&p, &c, DX_PARTS);
    dx_parts_add_layer(&p, &c, 0); dx_parts_add_layer(&p, &c, 0);
    c.e[0].lo = 36; c.e[0].hi = 60; c.e[0].shift = 12; c.e[0].vlo = 10; c.e[0].vhi = 90; c.e[0].poly = 3; c.e[0].mute = 1;
    p.layer[0].lvl = 40; p.part[0].sel = 2;
    CHECK(dx_parts_collapse(&p, &c, 0) == ((1U << 1) | (1U << 2)));
    CHECK(dx_part_used(&p, 0) && p.layer[0].owner == 0 && p.layer[1].owner == DX_NO_PART && p.layer[2].owner == DX_NO_PART);
    CHECK(c.e[0].lo == 0 && c.e[0].hi == 127 && c.e[0].shift == 0 && c.e[0].vlo == 1 && c.e[0].vhi == 127);
    CHECK(c.e[0].poly == 3 && c.e[0].mute == 1 && p.layer[0].lvl == 40 && p.part[0].sel == 0);   /* its own level stays */
    CHECK(c.e[1].rx == 0 && c.e[2].rx == 0 && c.e[0].ctl == 1 && dx_parts_valid(&p));
    CHECK(dx_parts_collapse(&p, &c, 0) == 0);                       /* one layer: nothing to do */
}

static void test_valid(void)
{
    struct dx_parts q;
    fresh();
    q = p; q.part[3].channel = 2; CHECK(!dx_parts_valid(&q));      /* two parts on one channel */
    q = p; q.part[3].channel = 17; CHECK(!dx_parts_valid(&q));
    q = p; q.layer[2].owner = 9; CHECK(!dx_parts_valid(&q));
    q = p; q.layer[2].owner = DX_NO_PART; CHECK(!dx_parts_valid(&q));   /* part 3 now has no layer */
    q = p; q.part[1].sel = 1; CHECK(!dx_parts_valid(&q));          /* selected layer that does not exist */
    q = p; q.selected = 8; CHECK(!dx_parts_valid(&q));
    q = p; q.part[0].level = 128; CHECK(!dx_parts_valid(&q));
    q = p; q.layer[0].lvl = 128; CHECK(!dx_parts_valid(&q));
    q = p; q.part[0].name[0] = 1; CHECK(!dx_parts_valid(&q));
    q = p; q.part[7].channel = 0; CHECK(!dx_parts_valid(&q));      /* its engine would belong to an unused part */
    q = p; q.part[7].channel = 0; q.layer[7].owner = DX_NO_PART; CHECK(dx_parts_valid(&q));
    q = p; q.part[7].channel = 0; q.layer[7].owner = DX_NO_PART; q.part[7].sel = 1; CHECK(!dx_parts_valid(&q));
    CHECK(dx_parts_valid(&p));
}

static void test_volume(void)
{
    CHECK(dx_layer_volume(127, 127) == 127 && dx_layer_volume(0, 127) == 0 && dx_layer_volume(127, 0) == 0);
    CHECK(dx_layer_volume(64, 100) == 50 && dx_layer_volume(100, 64) == 50);
    for (unsigned a = 0; a < 128; ++a) for (unsigned b = 0; b < 128; ++b) {
        unsigned v = dx_layer_volume(a, b);
        if (v > 127 || v > a || v > b) { CHECK(0); return; }
    }
    CHECK(1);
}

int main(void)
{
    test_default();
    test_add_and_remove();
    test_preferred_slot();
    test_limits();
    test_channels();
    test_collapse();
    test_valid();
    test_volume();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
