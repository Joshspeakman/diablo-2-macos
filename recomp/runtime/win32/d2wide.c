/* Diablo II wider than 4:3 (D2_WIDTH=<pixels>, the launch window's Width; runtime/win32/hooks.c calls in here from hooks
 * tools/lift.py places): the game's 800x600 mode reports a wider screen (still 600 high) to its graphics layer and to
 * the client, so the world is drawn over the whole width. */
#include "w32.h"

static int wide_w = -1;
int d2wide_width(void)                                               /* the width, or 0 for the game's own 800 */
{
    if (wide_w < 0) { const char *e = getenv("D2_WIDTH"); int w = e ? atoi(e) : 0; wide_w = w > 800 && w <= 2400 ? (w + 1) & ~1 : 0; }
    return wide_w;
}

/* Interface pieces the game draws at fixed 800x600 positions on the right (not tied to anything clicked): the
 * stretches of code that draw them are marked by hooks (start, end); draws made inside one that start in the right half
 * (x >= 390, 800x600 terms) move out by the extra width (ddraw_pal.c). */
static const uint32_t marks[][2] = {
    {0x499497, 0x49949c},                                             /* the right-hand panels' frame */
};
static int inside;
int d2wide_mark(uint32_t addr)                                        /* a hook at a mark's start or end: 1 if it was one */
{
    for (unsigned k = 0; k < sizeof marks / sizeof marks[0]; k++) {
        if (addr == marks[k][0]) { inside++; return 1; }
        if (addr == marks[k][1]) { if (inside > 0) inside--; return 1; }
    }
    return 0;
}
int d2wide_shift(float x0)                                            /* how far a draw starting at x0 moves */
{
    return inside > 0 && x0 >= 390 && d2wide_width() ? d2wide_width() - 800 : 0;
}
/* Out of a game (menus, character screens, loading) the game lays everything out for 800x600: drawn in the middle of
 * the wider screen, and the cursor (relative motion; the game draws it) kept to those 800 pixels */
static int menus;
int d2wide_menu_shift(void) { return menus && d2wide_width() ? (d2wide_width() - 800) / 2 : 0; }
extern int w32_cursor_limit_w;                                          /* user32.c */
void d2wide_frame(void)                                               /* each presented frame */
{
    inside = 0;                                                       /* no mark left open */
    if (!d2wide_width()) return;
    menus = rt_r32(G_MEM, 0x7a6a70) == 0;                              /* no player unit: not in a game */
    w32_cursor_limit_w = menus ? 800 : 0;
}

/* The Inventory table (*0x96d4f4, 0xf0-byte rows; rows 16-31 the 800x600 layouts): the game already draws a right-hand
 * panel's background (with its grid painted in) out by the extra width, but takes everything placed on it from here at
 * 800x600 places: the panel's rectangle (+0: left, right, top, bottom), the grid's (+0x14: left, right) where items sit
 * and are clicked, and the ten equipment slots (+0x28, 20 bytes each: left, right, top, bottom). Their x move out once,
 * for the rows whose panel is on the right (x >= 400), so pictures, items and clicks all agree. */
static void shift_body_locations(void)
{
    static int done; uint32_t base = rt_r32(G_MEM, 0x96d4f4), rows = rt_r32(G_MEM, 0x96d4f0);
    if (done || !base || rows < 32) return;
    int dx = d2wide_width() - 800;
    for (uint32_t r = 16; r < 32; r++) {
        uint32_t row = base + r * 0xf0;
        if ((int32_t)rt_r32(G_MEM, row) < 400) continue;              /* a left-hand panel (stash, cube, trade, hireling) */
        static const uint32_t xs[] = {0x00, 0x04, 0x14, 0x18};          /* panel left, right; grid left, right */
        for (unsigned k = 0; k < 4; k++) rt_w32(G_MEM, row + xs[k], rt_r32(G_MEM, row + xs[k]) + (uint32_t)dx);
        for (uint32_t k = 0; k < 10; k++) {
            uint32_t loc = row + 0x28 + 20 * k;
            if (!rt_r32(G_MEM, loc + 4)) continue;                       /* unused slot */
            rt_w32(G_MEM, loc, rt_r32(G_MEM, loc) + (uint32_t)dx); rt_w32(G_MEM, loc + 4, rt_r32(G_MEM, loc + 4) + (uint32_t)dx);
        }
    }
    done = 1;
}
/* The Belts table (*0x96d4f8, 0x108-byte rows; rows 7-13 the 800x600 layouts): the belt's boxes (16 at +8: left,
 * right, top, bottom) sit in the bottom bar, which the game centres on the wider screen: its potions and the clicks on
 * them move by half the extra width, once. */
static void shift_belts(void)
{
    static int done; uint32_t base = rt_r32(G_MEM, 0x96d4f8);
    if (done || !base) return;
    int dx = (d2wide_width() - 800) / 2;
    for (uint32_t r = 7; r < 14; r++)
        for (uint32_t k = 0; k < 16; k++) {
            uint32_t box = base + r * 0x108 + 8 + 16 * k;
            if (!rt_r32(G_MEM, box + 4)) continue;
            rt_w32(G_MEM, box, rt_r32(G_MEM, box) + (uint32_t)dx); rt_w32(G_MEM, box + 4, rt_r32(G_MEM, box + 4) + (uint32_t)dx);
        }
    done = 1;
}
void d2wide_screen_set(void) { if (d2wide_width()) { shift_body_locations(); shift_belts(); } }   /* the client takes its screen size */

/* A side panel is 400 pixels wide at 800x600, and the game works it out as twice a quarter of the screen's width (where
 * the world ends beside it, where the view slides, where a unit or missile is still shown, ...): each place that
 * takes that quarter gets 200 instead (the register that holds it, just after it is computed). */
static const struct { uint32_t addr; int reg; } quarters[] = {   /* reg: 0 eax, 1 ebx, 2 ecx, 3 edx, 4 esi, 5 edi */
    {0x452dd5, 0},
    {0x452de5, 0},
    {0x454fc8, 0},
    {0x454fd8, 0},
    {0x45615f, 0},
    {0x45622b, 0},
    {0x459484, 1},
    {0x459494, 0},
    {0x45976b, 5},
    {0x459797, 0},
    {0x46e3a1, 0},
    {0x46e3c0, 0},
    {0x470f53, 0},
    {0x470f63, 0},
    {0x471587, 0},
    {0x47159f, 0},
    {0x4717ce, 0},
    {0x4717de, 0},
    {0x471b29, 0},
    {0x471b41, 0},
    {0x471c40, 0},
    {0x471c50, 0},
    {0x4720e2, 0},
    {0x4720f2, 0},
    {0x4734bd, 0},
    {0x4734d2, 0},
    {0x4736f3, 4},
    {0x473705, 0},
    {0x473abc, 0},
    {0x473ad4, 0},
    {0x4db6ce, 0},
    {0x4db6e1, 0},
    {0x4ddfeb, 0},
    {0x4ddffb, 0},
    {0x4de0a3, 0},
    {0x4de0b3, 0},
    {0x4de1ba, 0},
    {0x4de1ca, 0},
    {0x4de29c, 0},
    {0x4de2ac, 0},
    {0x50c72d, 0},
    {0x5132f8, 0},
    {0x513308, 0},
    {0x6b8071, 0},
    {0x6c9608, 0},
    {0x6c9618, 0},
};
int d2wide_quarter(Ctx *c, uint32_t addr)
{
    for (unsigned k = 0; k < sizeof quarters / sizeof quarters[0]; k++) {
        if (quarters[k].addr != addr) continue;
        if (d2wide_width()) {
            uint32_t *r[6] = {&c->eax, &c->ebx, &c->ecx, &c->edx, &c->esi, &c->edi};
            *r[quarters[k].reg] = 200;
        }
        return 1;
    }
    return 0;
}
