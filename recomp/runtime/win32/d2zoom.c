/* Diablo II zoomed out (Direct3D; runtime/win32/hooks.c calls in here from hooks tools/lift.py places in the client's
 * in-game draw, 0x44c990): more of the world in the same screen, the interface as it was.
 *
 * The in-game draw takes the world's view rectangle from the view object (*0x7a0640: +4 left, +8 top, +0xc right,
 * +0x10 bottom), centres the camera on the player in it, draws the world, then the interface. For the world pass only,
 * the rectangle and the screen's size (the graphics layer's and the client's, SIZES) are made zoom times larger, so the
 * game lays out and draws that much more of the world around the player; the Direct3D draws it makes then (ddraw_pal.c)
 * are scaled back down into the screen by 1/zoom about the rectangle's corner. Everything is put back before the
 * interface is drawn. */
#include "w32.h"
#include <math.h>

enum { VIEW = 0x7a0640, PLAYER_UNIT = 0x7a6a70 };
/* the screen's size where the world pass reads it: the graphics layer's (width, height), the client's (width, height)
 * and the client's view (width, height less the 40-pixel bottom bar) */
static const uint32_t SIZES[] = {0x7c9138, 0x7c913c, 0x71146c, 0x711470, 0x711474, 0x7a5220, 0x7a521c};
#define NSIZES (sizeof SIZES / sizeof SIZES[0])
static float zoom = 0;                                               /* this frame's zoom (1: the game's own view) */
static volatile float target = 0;                                    /* where the mouse wheel has asked it to go */
static const uint32_t MOUSE_X = 0x7a6ab0, MOUSE_Y = 0x7a6aac;      /* the client's cursor */
static uint32_t saved_mouse[2];
static int active; static uint32_t saved_rect[4], saved_clip[4], saved_size[NSIZES], view; static float ox, oy;
/* The launch window's Zoom (D2_ZOOM_MAX: how far out the mouse wheel goes; Direct3D only), or a fixed zoom for tests
 * (D2_ZOOM) */
float d2zoom_max(void)
{
    static float m = 0;
    if (m <= 0) {
        const char *e = getenv("D2_ZOOM_MAX"), *f = getenv("D2_ZOOM"), *r = getenv("D2_RENDERER");
        m = e ? (float)atof(e) : 1; if (f && atof(f) > m) m = (float)atof(f);
        if (!(m >= 1)) m = 1; if (m > 3) m = 3;
        if (!r || strcmp(r, "d3d")) m = 1;                            /* the zoomed draws are Direct3D's (ddraw_pal.c) */
    }
    return m;
}
static float clampz(float z) { float m = d2zoom_max(); return z < 1 ? 1 : z > m ? m : z; }
float d2zoom_level(void)
{
    if (target <= 0) { const char *e = getenv("D2_ZOOM"); target = clampz(e ? (float)atof(e) : 1); if (zoom <= 0) zoom = target; }
    return zoom;
}
/* the mouse wheel (host): notches up (> 0) zoom in towards the game's own view, down zoom out; 0 when zoom is off */
int d2zoom_wheel(double notches)
{
    if (d2zoom_max() <= 1.001f || !rt_r32(G_MEM, PLAYER_UNIT)) return 0;
    d2zoom_level(); target = clampz(target * (float)pow(1.1, -notches));
    return 1;
}
/* the draw's scale (1 when not zoomed or not in the world pass) and the point it is about */
int d2zoom_draw(float *scale, float *x0, float *y0) { if (!active) return 0; *scale = 1 / zoom; *x0 = ox; *y0 = oy; return 1; }
void d2zoom_world_begin(void)                                        /* the in-game draw, before it reads the view */
{
    if (active || !rt_r32(G_MEM, PLAYER_UNIT)) return;
    d2zoom_level();                                                  /* this frame's zoom: a little closer to the wheel's */
    { float t = target, d = t - zoom; zoom = fabsf(d) < 0.004f ? t : zoom + d * 0.3f; }
    if (zoom <= 1.001f) return;
    view = rt_r32(G_MEM, VIEW); if (!view) return;
    {   /* the view's own rectangle, as the game set it */
        uint32_t cur[4]; for (int k = 0; k < 4; k++) cur[k] = rt_r32(G_MEM, view + 4 + 4 * (uint32_t)k);
        memcpy(saved_rect, cur, sizeof cur);
    }
    int32_t l = (int32_t)saved_rect[0], t = (int32_t)saved_rect[1], r = (int32_t)saved_rect[2], b = (int32_t)saved_rect[3];
    if (r <= l || b <= t) return;
    for (unsigned k = 0; k < NSIZES; k++) saved_size[k] = rt_r32(G_MEM, SIZES[k]);
    rt_w32(G_MEM, view + 0xc, (uint32_t)(l + (int32_t)lroundf((float)(r - l) * zoom)));
    rt_w32(G_MEM, view + 0x10, (uint32_t)(t + (int32_t)lroundf((float)(b - t) * zoom)));
    for (unsigned k = 0; k < NSIZES; k++) rt_w32(G_MEM, SIZES[k], (uint32_t)lroundf((float)saved_size[k] * zoom));
    /* the rectangle the ground's tiles are tested against (+0x14: left, top, right, bottom, a margin around the view):
     * its far sides move out with the view */
    for (int k = 0; k < 4; k++) saved_clip[k] = rt_r32(G_MEM, view + 0x14 + 4 * (uint32_t)k);
    {   int32_t cr = (int32_t)saved_clip[2], cb = (int32_t)saved_clip[3];
        rt_w32(G_MEM, view + 0x1c, (uint32_t)(cr + (int32_t)lroundf((float)(r - l) * (zoom - 1))));
        rt_w32(G_MEM, view + 0x20, (uint32_t)(cb + (int32_t)lroundf((float)(b - t) * (zoom - 1)))); }
    if (getenv("D2_ZOOM_LOG")) { static int n; if (n++ < 1) fprintf(stderr, "d2zoom: ground clip %d,%d-%d,%d\n", saved_clip[0], saved_clip[1], saved_clip[2], saved_clip[3]); }
    rt_w32(G_MEM, view, rt_r32(G_MEM, view) | 1);                      /* the view changed */
    ox = (float)l; oy = (float)t; active = 1;
    /* the cursor, for what the world pass works out from it (the unit under it, where a click goes): in the larger view */
    saved_mouse[0] = rt_r32(G_MEM, MOUSE_X); saved_mouse[1] = rt_r32(G_MEM, MOUSE_Y);
    rt_w32(G_MEM, MOUSE_X, (uint32_t)(l + (int32_t)lroundf((float)((int32_t)saved_mouse[0] - l) * zoom)));
    rt_w32(G_MEM, MOUSE_Y, (uint32_t)(t + (int32_t)lroundf((float)((int32_t)saved_mouse[1] - t) * zoom)));
    static int once; if (!once++ && getenv("W32_DDLOG")) fprintf(stderr, "d2zoom: view %d,%d-%d,%d screen %ux%u, zoom %.2f\n", l, t, r, b, saved_size[0], saved_size[1], zoom);
}
static void remember(void);
void d2zoom_world_end(void)                                          /* after the world, before the interface */
{
    if (!active) return;
    for (int k = 0; k < 4; k++) rt_w32(G_MEM, view + 4 + 4 * (uint32_t)k, saved_rect[k]);
    for (unsigned k = 0; k < NSIZES; k++) rt_w32(G_MEM, SIZES[k], saved_size[k]);
    for (int k = 0; k < 4; k++) rt_w32(G_MEM, view + 0x14 + 4 * (uint32_t)k, saved_clip[k]);
    rt_w32(G_MEM, MOUSE_X, saved_mouse[0]); rt_w32(G_MEM, MOUSE_Y, saved_mouse[1]);
    remember(); active = 0;
}
/* The ground is drawn only for the rooms near the player's (0x4de730: the room's own list from 0x619790, its neighbours);
 * zoomed out, the view reaches past them, so for the world pass the list becomes those rooms' neighbours too (rooms
 * the client has no tiles for yet are skipped by the game's own loop). Hooked just after the list is fetched: the
 * list pointer at [ebp-0xc], its count at [ebp-0x10]. */
#define GET_ROOMS_NEAR 0x619790
void d2zoom_rooms(Ctx *c)
{
    if (!active) return;
    static uint32_t buf; if (!buf) buf = heap_alloc(w32_process_heap, 8, 4 * 256 + 8); if (!buf) return;
    uint32_t list = rt_r32(G_MEM, c->ebp - 0xc), n = rt_r32(G_MEM, c->ebp - 0x10), out = buf + 4 * 256;
    if (!list || n > 64) return;
    uint32_t rooms[256], m = 0, near[64];
    for (uint32_t k = 0; k < n; k++) near[k] = rooms[m++] = rt_r32(G_MEM, list + 4 * k);
    uint32_t eax = c->eax, ecx = c->ecx, edx = c->edx; LFs f = c->f;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t a[3] = {near[k], out, out + 4}; rt_w32(G_MEM, out, 0); rt_w32(G_MEM, out + 4, 0);
        w32_callback(c, GET_ROOMS_NEAR, 3, a);
        uint32_t l2 = rt_r32(G_MEM, out), n2 = rt_r32(G_MEM, out + 4);
        for (uint32_t j = 0; l2 && j < n2 && j < 64 && m < 256; j++) {
            uint32_t r = rt_r32(G_MEM, l2 + 4 * j), seen = 0;
            for (uint32_t i = 0; i < m && !seen; i++) seen = rooms[i] == r;
            if (r && !seen) rooms[m++] = r;
        }
    }
    c->eax = eax; c->ecx = ecx; c->edx = edx; c->f = f;
    for (uint32_t k = 0; k < m; k++) rt_w32(G_MEM, buf + 4 * k, rooms[k]);
    rt_w32(G_MEM, c->ebp - 0xc, buf); rt_w32(G_MEM, c->ebp - 0x10, m);
    static int once; if (!once++ && getenv("D2_ZOOM_LOG")) fprintf(stderr, "d2zoom: ground for %u rooms (%u near)\n", m, n);
}

/* Outside the world pass the cursor is where it is on the screen, but the camera the pass left is the larger view's:
 * a click in the world (0x462d00: ecx the button, edx x, [esp+4] y; all six callers pass the cursor) goes to the point
 * under the cursor in the larger view */
static int zoomed;                                                   /* the last world pass was zoomed */
static float last_x0, last_y0, last_zoom = 1;
static void remember(void) { zoomed = active; if (active) { last_x0 = ox; last_y0 = oy; last_zoom = zoom; } }
void d2zoom_click(Ctx *c)
{
    if (!zoomed || !rt_r32(G_MEM, PLAYER_UNIT)) return;
    int32_t x = (int32_t)c->edx, y = (int32_t)rt_r32(G_MEM, c->esp + 4);
    if (!x && !y) return;                                            /* (the game's "no position") */
    c->edx = (uint32_t)lroundf(last_x0 + ((float)x - last_x0) * last_zoom);
    rt_w32(G_MEM, c->esp + 4, (uint32_t)lroundf(last_y0 + ((float)y - last_y0) * last_zoom));
}
/* The box of the unit under the cursor (0x467430 records it at 0x7a6a7c: left, top, right, bottom), when recorded while
 * the world is drawn, is in the larger view: back into the screen, where the cursor is tested against it */
void d2zoom_hover_box(void)
{
    if (!active) return;
    for (uint32_t k = 0; k < 4; k++) {
        float o = (k & 1) ? oy : ox; int32_t v = (int32_t)rt_r32(G_MEM, 0x7a6a7c + 4 * k);
        rt_w32(G_MEM, 0x7a6a7c + 4 * k, (uint32_t)lroundf(o + ((float)v - o) / zoom));
    }
}
/* The unit under the cursor is looked for in the game's tick (0x467ac0 -> 0x467130): the cursor's point (esi x, ebx y,
 * the caller's locals) is tested against the screen's bottom bar and side panels, then (at 0x46723c) adjusted for the
 * view (0x4670a0) and used to test units placed with the camera the larger view left. So there it goes into that view;
 * nothing else uses it. */
void d2zoom_hover_find(Ctx *c)
{
    if (!zoomed || active || !rt_r32(G_MEM, PLAYER_UNIT)) return;
    uint32_t px = c->esi, py = c->ebx; if (!px || !py) return;
    int32_t x = (int32_t)rt_r32(G_MEM, px), y = (int32_t)rt_r32(G_MEM, py);
    rt_w32(G_MEM, px, (uint32_t)lroundf(last_x0 + ((float)x - last_x0) * last_zoom));
    rt_w32(G_MEM, py, (uint32_t)lroundf(last_y0 + ((float)y - last_y0) * last_zoom));
}
