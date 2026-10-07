/* DirectDraw as Diablo II uses it: DirectDrawCreate (an IDirectDraw that is asked for later versions), 8-bit
 * palettized surfaces the game draws into itself, palettes, a clipper, colour fills and copies. All pixels are the
 * game's own (it renders in software); the visible frame is the primary surface's pixels through its palette, sent
 * to the Metal renderer as one picture per Flip (full screen) or per copy onto the primary (in a window).
 *
 * COM objects are small guest blocks {vtable, host object index}; each interface version has its own vtable of
 * thunks (methods shared, argument counts and structure sizes per version). Surface pixels live in guest memory
 * because the game writes through the pointers Lock returns. */
#include "w32.h"
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "../../../src/dsr/dsr_proto.h"

uint32_t *cmd_begin(uint32_t op, uint32_t payload);   /* ddraw.c: the command ring to the renderer */
void cmd_end(void);
void dsr_present(Ctx *c, uint32_t id);
uint32_t next_object_id(void);
void w32_set_window_pos(Ctx *c, uint32_t hwnd, int x, int y, int cx, int cy, uint32_t fl);
extern int w32_screen_w, w32_screen_h;

#define DD_OK 0u
#define E_NOINTERFACE 0x80004002u
#define DDERR_INVALIDPARAMS 0x80070057u
#define DDERR_UNSUPPORTED 0x80004001u
#define DDERR_GENERIC 0x80004005u
#define DDH(n) (0x88760000u | (n))
#define DDERR_NOTFOUND DDH(255)
#define DDERR_NOPALETTEATTACHED DDH(572)
#define DDERR_NOCLIPPERATTACHED DDH(568)
#define DDERR_NOTLOCKED DDH(584)
#define DDERR_CANTCREATEDC DDH(581)
#define DDERR_NOBLTHW DDH(575)
#define DDERR_NOCOLORKEY DDH(215)

enum { DDSD_CAPS = 1, DDSD_HEIGHT = 2, DDSD_WIDTH = 4, DDSD_PITCH = 8, DDSD_BACKBUFFERCOUNT = 0x20, DDSD_REFRESHRATE = 0x40000,
       DDSD_LPSURFACE = 0x800, DDSD_PIXELFORMAT = 0x1000, DDSD_CKSRCBLT = 0x10000 };
enum { DDSCAPS_BACKBUFFER = 4, DDSCAPS_COMPLEX = 8, DDSCAPS_FLIP = 0x10, DDSCAPS_FRONTBUFFER = 0x20, DDSCAPS_OFFSCREENPLAIN = 0x40,
       DDSCAPS_PALETTE = 0x100, DDSCAPS_PRIMARYSURFACE = 0x200, DDSCAPS_SYSTEMMEMORY = 0x800, DDSCAPS_VIDEOMEMORY = 0x4000,
       DDSCAPS_VISIBLE = 0x8000, DDSCAPS_LOCALVIDMEM = 0x10000000 };
enum { DDPF_PALETTEINDEXED8 = 0x20, DDPF_RGB = 0x40 };
enum { DDBLT_COLORFILL = 0x400, DDBLT_KEYSRC = 0x8000, DDBLT_KEYSRCOVERRIDE = 0x10000, DDBLTFAST_SRCCOLORKEY = 1 };
enum { DDCKEY_SRCBLT = 8 };
enum { DDSCL_FULLSCREEN = 1, DDSCL_EXCLUSIVE = 0x10 };
/* DDSURFACEDESC (108 bytes, versions 1-3) and DDSURFACEDESC2 (124, versions 4 and 7) share these offsets */
enum { SD_SIZE = 0, SD_FLAGS = 4, SD_HEIGHT = 8, SD_WIDTH = 12, SD_PITCH = 16, SD_BACKBUFFERS = 20, SD_REFRESH = 24,
       SD_SURFACE = 36, SD_CKSRCBLT = 64, SD_PF = 72, SD_CAPS = 104 };
enum { PF_SIZE = 0, PF_FLAGS = 4, PF_FOURCC = 8, PF_BITS = 12, PF_R = 16, PF_G = 20, PF_B = 24, PF_A = 28 };

static int dd_log = -1;
static void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void dlog(const char *fmt, ...)
{
    if (dd_log < 0) dd_log = getenv("DSR_LOG") != 0 || getenv("W32_DDLOG") != 0;
    if (!dd_log) return;
    va_list a; va_start(a, fmt); fprintf(stderr, "ddraw: "); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a);
}

/* ---- objects ---- */
typedef struct PSurf PSurf;
typedef struct PPal { int ref; uint32_t g; uint32_t entries[256]; uint32_t caps; } PPal;     /* entries: PALETTEENTRY r, g, b, flags */
typedef struct PClip { int ref; uint32_t g, hwnd; } PClip;
typedef struct PDraw { int ref; uint32_t g[4]; uint32_t hwnd, coop, mode_w, mode_h, mode_bpp; PSurf *primary; } PDraw;
struct PSurf {
    int ref; uint32_t g[4], g_gamma; PDraw *dd;
    uint32_t w, h, bpp, caps, pitch, mem;            /* mem: guest pixels */
    uint32_t pf[8];                                  /* DDPIXELFORMAT */
    PSurf *back; PSurf *front;                       /* flip chain: primary -> back buffer (and back to the primary) */
    PPal *pal; PClip *clip;
    uint32_t ck_lo, ck_hi; int has_ck;
    int locked;
};
enum { T_DD = 1, T_SURF, T_PAL, T_CLIP, T_GAMMA };
typedef struct { int type; void *p; } Obj;
#define MAXO 8192
static Obj objs[MAXO]; static uint32_t nobjs = 1;
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static uint32_t vt_dd[4], vt_surf[4], vt_pal, vt_clip, vt_gamma;   /* [version index]: 0 v1, 1 v2, 2 v4, 3 v7 (surfaces: v1/v2+v3, v4, v7 share a layout) */

static uint32_t iface(uint32_t vtable, int type, void *p)
{
    pthread_mutex_lock(&lk);
    uint32_t k = nobjs < MAXO ? nobjs++ : 0;
    if (k) { objs[k].type = type; objs[k].p = p; }
    pthread_mutex_unlock(&lk);
    uint32_t g = heap_alloc(w32_process_heap, 8, 8);
    rt_w32(G_MEM, g, vtable); rt_w32(G_MEM, g + 4, k);
    return g;
}
static void *the(uint32_t g, int type)
{
    if (!g) return 0;
    uint32_t k = rt_r32(G_MEM, g + 4);
    return k && k < MAXO && objs[k].type == type ? objs[k].p : 0;
}
#define M(name, n) static void name(Ctx *c) { enum { nargs_ = n };
#define END }
#define ME(T, ty) T *me = the(ARG(0), ty); if (!me) { fprintf(stderr, "ddraw: bad " #T " object %08x\n", ARG(0)); RET(DDERR_INVALIDPARAMS, nargs_); }
#define OK_M(name, n) M(name, n) RET(DD_OK, n); END
#define NOT_M(name, n) M(name, n) { static int once; if (!once++) fprintf(stderr, "ddraw: %s not supported\n", #name); } RET(DDERR_UNSUPPORTED, n); END

/* ---- pixel formats ---- */
static void pf_for(uint32_t *pf, uint32_t bpp)
{
    memset(pf, 0, 32); pf[0] = 32; pf[3] = bpp;
    if (bpp == 8) pf[1] = DDPF_RGB | DDPF_PALETTEINDEXED8;
    else if (bpp == 16) { pf[1] = DDPF_RGB; pf[4] = 0xf800; pf[5] = 0x07e0; pf[6] = 0x001f; }
    else { pf[1] = DDPF_RGB; pf[4] = 0xff0000; pf[5] = 0x00ff00; pf[6] = 0x0000ff; }
}

/* ---- the picture: the primary's pixels through its palette, to the renderer ---- */
static uint32_t screen_id, screen_w, screen_h;
static uint32_t last_pal_rgb[256];                   /* the palette last shown (tests read it: DS_PEEKFILE "palette") */
uint32_t ddraw_pal_frames;                           /* pictures sent */
static void to_bgra(const PSurf *s, const uint8_t *src, uint32_t *dst, uint32_t w, uint32_t h, uint32_t pitch)
{
    if (s->bpp == 8) {
        uint32_t lut[256];
        for (int k = 0; k < 256; k++) {
            uint32_t e = s->pal ? s->pal->entries[k] : (uint32_t)(k * 0x010101);              /* r | g << 8 | b << 16 */
            lut[k] = 0xff000000u | (e & 0xff) << 16 | (e & 0xff00) | (e >> 16 & 0xff);       /* BGRA in memory = 0xAARRGGBB */
        }
        memcpy(last_pal_rgb, lut, sizeof lut);
        for (uint32_t y = 0; y < h; y++) { const uint8_t *r = src + y * pitch; uint32_t *o = dst + y * w; for (uint32_t x = 0; x < w; x++) o[x] = lut[r[x]]; }
    } else if (s->bpp == 16) {
        for (uint32_t y = 0; y < h; y++) {
            const uint16_t *r = (const uint16_t *)(src + y * pitch); uint32_t *o = dst + y * w;
            for (uint32_t x = 0; x < w; x++) { uint32_t v = r[x], R = (v >> 11) * 255 / 31, G = (v >> 5 & 63) * 255 / 63, B = (v & 31) * 255 / 31; o[x] = 0xff000000u | R << 16 | G << 8 | B; }
        }
    } else {
        for (uint32_t y = 0; y < h; y++) { const uint32_t *r = (const uint32_t *)(src + y * pitch); uint32_t *o = dst + y * w; for (uint32_t x = 0; x < w; x++) o[x] = r[x] | 0xff000000u; }
    }
}
/* A picture for the screen, w x h BGRA pixels written by the caller straight into the command ring (DirectDraw's
 * primary here, the GDI window in windowed mode: gdi32.c): frame_begin returns where the rows go (pitch w * 4),
 * frame_end sends it and presents. The picture's size is the game's coordinate space for the mouse. */
extern uint32_t dsr_d3d9_w, dsr_d3d9_h;              /* ddraw.c: the size dsr_mode_size reports */
uint32_t *dsr_frame_begin(uint32_t w, uint32_t h)
{
    if (!screen_id || screen_w != w || screen_h != h) {
        if (!screen_id) screen_id = next_object_id();
        else { uint32_t *p = cmd_begin(DSR_SURFACE_DESTROY, 4); p[0] = screen_id; cmd_end(); }
        uint32_t *p = cmd_begin(DSR_SURFACE_CREATE, 40); memset(p, 0, 40);
        p[0] = screen_id; p[1] = w; p[2] = h; p[3] = 4; p[4] = 2; cmd_end();          /* offscreen, XRGB */
        uint32_t *m = cmd_begin(DSR_MODE, 8); m[0] = w; m[1] = h; cmd_end();
        screen_w = w; screen_h = h; dsr_d3d9_w = w; dsr_d3d9_h = h;
    }
    uint32_t *p = cmd_begin(DSR_SURFACE_UPLOAD, 24 + w * h * 4);
    p[0] = screen_id; p[1] = 0; p[2] = 0; p[3] = w; p[4] = h; p[5] = w * 4;
    return p + 6;
}
void dsr_frame_end(Ctx *c)
{
    cmd_end();
    ddraw_pal_frames++;
    dsr_present(c, screen_id);
}
void dsr_frame_palette(const uint32_t *bgra)         /* (tests: the colours of the last 8-bit picture) */
{
    memcpy(last_pal_rgb, bgra, sizeof last_pal_rgb);
}
/* the rectangle x, y, w, h of surface s becomes the picture on screen */
static void show(Ctx *c, const PSurf *s, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h || x + w > s->w || y + h > s->h) return;
    uint32_t *px = dsr_frame_begin(w, h);
    to_bgra(s, G_MEM + s->mem + y * s->pitch + x * (s->bpp / 8), px, w, h, s->pitch);
    dsr_frame_end(c);
}
/* DS_PEEKFILE / tests: the last frame's palette as 0xRRGGBB */
uint32_t ddraw_pal_color(int k) { return k >= 0 && k < 256 ? last_pal_rgb[k] & 0xffffff : 0; }
void ddraw_pal_screen(uint32_t *w, uint32_t *h) { *w = screen_w; *h = screen_h; }

/* ---- surfaces ---- */
static void surf_attach_ifaces(PSurf *s)
{
    for (int v = 0; v < 4; v++) s->g[v] = iface(vt_surf[v], T_SURF, s);
    s->g_gamma = iface(vt_gamma, T_GAMMA, s);
}
static PSurf *surf_new(PDraw *dd, uint32_t w, uint32_t h, uint32_t bpp, uint32_t caps)
{
    PSurf *s = calloc(1, sizeof *s); s->ref = 1; s->dd = dd; s->w = w; s->h = h; s->bpp = bpp; s->caps = caps;
    s->pitch = (w * (bpp / 8) + 3) & ~3u;
    s->mem = heap_alloc(w32_process_heap, 8, s->pitch * h + 16);
    pf_for(s->pf, bpp);
    surf_attach_ifaces(s);
    return s;
}
static void surf_free(PSurf *s)
{
    if (s->dd && s->dd->primary == s) s->dd->primary = 0;
    for (int v = 0; v < 4; v++) { uint32_t k = rt_r32(G_MEM, s->g[v] + 4); if (k < MAXO) objs[k].type = 0; }
    { uint32_t k = rt_r32(G_MEM, s->g_gamma + 4); if (k < MAXO) objs[k].type = 0; }
    if (s->mem) heap_free(w32_process_heap, s->mem);
    free(s);
}
static uint32_t surf_release(PSurf *s)
{
    int r = __atomic_sub_fetch(&s->ref, 1, __ATOMIC_SEQ_CST);
    if (r == 0) {
        PSurf *b = s->back;
        if (b && b->front == s) { b->front = 0; surf_free(b); }      /* the flip chain goes with its primary */
        surf_free(s);
    }
    return (uint32_t)(r < 0 ? 0 : r);
}
static void desc_out(const PSurf *s, uint32_t d, int lock_ptr)       /* fill the game's DDSURFACEDESC(2) */
{
    uint32_t size = rt_r32(G_MEM, d); if (size != 108 && size != 124) size = 108;
    memset(GP(d), 0, size); rt_w32(G_MEM, d, size);
    rt_w32(G_MEM, d + SD_FLAGS, DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | (lock_ptr ? DDSD_LPSURFACE : 0) | (s->back && s->front == 0 ? DDSD_BACKBUFFERCOUNT : 0));
    rt_w32(G_MEM, d + SD_HEIGHT, s->h); rt_w32(G_MEM, d + SD_WIDTH, s->w); rt_w32(G_MEM, d + SD_PITCH, s->pitch);
    if (s->back && !s->front) rt_w32(G_MEM, d + SD_BACKBUFFERS, 1);
    if (lock_ptr) rt_w32(G_MEM, d + SD_SURFACE, s->mem);
    for (int k = 0; k < 8; k++) rt_w32(G_MEM, d + SD_PF + 4 * (uint32_t)k, s->pf[k]);
    rt_w32(G_MEM, d + SD_CAPS, s->caps);
    if (s->has_ck) { rt_w32(G_MEM, d + SD_CKSRCBLT, s->ck_lo); rt_w32(G_MEM, d + SD_CKSRCBLT + 4, s->ck_hi); rt_w32(G_MEM, d + SD_FLAGS, rt_r32(G_MEM, d + SD_FLAGS) | DDSD_CKSRCBLT); }
}
static int vidx_of_surf_iid(uint32_t iid)            /* IDirectDrawSurface 1/2/3 -> 0..1, 4 -> 2, 7 -> 3; -1 other */
{
    switch (iid) { case 0x6c14db81u: return 0; case 0x57805885u: case 0xda044e00u: return 1; case 0x0b2b8630u: return 2; case 0x06675a80u: return 3; }
    return -1;
}
M(s_QueryInterface, 3) ME(PSurf, T_SURF)
    uint32_t iid = rt_r32(G_MEM, ARG(1)), out = ARG(2); int v = vidx_of_surf_iid(iid);
    if (v >= 0 || iid == 0) { rt_w32(G_MEM, out, me->g[v < 0 ? 0 : v]); __atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST); RET(DD_OK, 3); }
    if (iid == 0x69c11c3eu) { rt_w32(G_MEM, out, me->g_gamma); __atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST); RET(DD_OK, 3); }
    dlog("surface QI: interface %08x not available", iid); rt_w32(G_MEM, out, 0); RET(E_NOINTERFACE, 3);
END
M(s_AddRef, 1) ME(PSurf, T_SURF) RET(__atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST), 1); END
M(s_Release, 1) ME(PSurf, T_SURF) RET(surf_release(me), 1); END
/* copies and fills */
static void rect_in(uint32_t r, const PSurf *s, int32_t *o)
{
    if (r) for (int k = 0; k < 4; k++) o[k] = (int32_t)rt_r32(G_MEM, r + 4 * (uint32_t)k);
    else { o[0] = o[1] = 0; o[2] = (int32_t)s->w; o[3] = (int32_t)s->h; }
}
static int clip_to(int32_t *r, const PSurf *s)
{
    if (r[0] < 0) r[0] = 0; if (r[1] < 0) r[1] = 0;
    if (r[2] > (int32_t)s->w) r[2] = (int32_t)s->w; if (r[3] > (int32_t)s->h) r[3] = (int32_t)s->h;
    return r[2] > r[0] && r[3] > r[1];
}
static uint32_t px_get(const PSurf *s, uint32_t x, uint32_t y)
{
    const uint8_t *p = G_MEM + s->mem + y * s->pitch + x * (s->bpp / 8);
    return s->bpp == 8 ? p[0] : s->bpp == 16 ? (uint32_t)(p[0] | p[1] << 8) : (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
}
static void px_put(const PSurf *s, uint32_t x, uint32_t y, uint32_t v)
{
    uint8_t *p = G_MEM + s->mem + y * s->pitch + x * (s->bpp / 8);
    if (s->bpp == 8) p[0] = (uint8_t)v; else if (s->bpp == 16) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); } else memcpy(p, &v, 4);
}
static uint32_t convert(const PSurf *from, const PSurf *to, uint32_t v)   /* a pixel between formats (8-bit through the palette) */
{
    if (from->bpp == to->bpp) return v;
    uint32_t r, g, b;
    if (from->bpp == 8) { uint32_t e = from->pal ? from->pal->entries[v & 0xff] : v * 0x010101u; r = e & 0xff; g = e >> 8 & 0xff; b = e >> 16 & 0xff; }
    else if (from->bpp == 16) { r = (v >> 11) * 255 / 31; g = (v >> 5 & 63) * 255 / 63; b = (v & 31) * 255 / 31; }
    else { r = v >> 16 & 0xff; g = v >> 8 & 0xff; b = v & 0xff; }
    if (to->bpp == 16) return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
    if (to->bpp == 32) return r << 16 | g << 8 | b;
    return 0;                                                       /* (to 8 bits: not done) */
}
static void copy_rect(PSurf *d, const int32_t *dr, PSurf *s, const int32_t *sr, int use_key, uint32_t klo, uint32_t khi)
{
    int32_t dw = dr[2] - dr[0], dh = dr[3] - dr[1], sw = sr[2] - sr[0], sh = sr[3] - sr[1];
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (!use_key && d->bpp == s->bpp && dw == sw && dh == sh) {        /* the common case: rows of bytes */
        uint32_t bp = d->bpp / 8, row = (uint32_t)dw * bp;
        for (int32_t y = 0; y < dh; y++)
            memmove(G_MEM + d->mem + (uint32_t)(dr[1] + y) * d->pitch + (uint32_t)dr[0] * bp, G_MEM + s->mem + (uint32_t)(sr[1] + y) * s->pitch + (uint32_t)sr[0] * bp, row);
        return;
    }
    for (int32_t y = 0; y < dh; y++) {
        uint32_t sy = (uint32_t)(sr[1] + (int64_t)y * sh / dh);
        for (int32_t x = 0; x < dw; x++) {
            uint32_t sx = (uint32_t)(sr[0] + (int64_t)x * sw / dw), v = px_get(s, sx, sy);
            if (use_key && v >= klo && v <= khi) continue;
            px_put(d, (uint32_t)(dr[0] + x), (uint32_t)(dr[1] + y), convert(s, d, v));
        }
    }
}
static void fill_rect(PSurf *d, const int32_t *r, uint32_t v)
{
    for (int32_t y = r[1]; y < r[3]; y++) {
        if (d->bpp == 8) memset(G_MEM + d->mem + (uint32_t)y * d->pitch + (uint32_t)r[0], (int)(v & 0xff), (size_t)(r[2] - r[0]));
        else for (int32_t x = r[0]; x < r[2]; x++) px_put(d, (uint32_t)x, (uint32_t)y, v);
    }
}
static PSurf *visible_of(PSurf *s) { return (s->caps & DDSCAPS_PRIMARYSURFACE) ? s : 0; }
static void shown_after_copy(Ctx *c, PSurf *d, const int32_t *dr)
{
    /* a copy onto a primary without a flip chain is what the player sees now (in a window, the area copied); full
     * screen with a back buffer, the picture comes at Flip instead */
    if (!visible_of(d) || d->back) return;
    show(c, d, (uint32_t)dr[0], (uint32_t)dr[1], (uint32_t)(dr[2] - dr[0]), (uint32_t)(dr[3] - dr[1]));
}
M(s_Blt, 6) ME(PSurf, T_SURF)                                       /* (dst rect, src, src rect, flags, DDBLTFX *) */
    PSurf *src = the(ARG(2), T_SURF); uint32_t flags = ARG(4), fx = ARG(5);
    int32_t dr[4], sr[4]; rect_in(ARG(1), me, dr);
    if (flags & DDBLT_COLORFILL) {
        if (!clip_to(dr, me)) RET(DD_OK, 6);
        fill_rect(me, dr, fx ? rt_r32(G_MEM, fx + 80) : 0);
        shown_after_copy(c, me, dr); RET(DD_OK, 6);
    }
    if (!src) { dlog("Blt flags %x without a source", flags); RET(DD_OK, 6); }
    rect_in(ARG(3), src, sr);
    int key = 0; uint32_t klo = 0, khi = 0;
    if ((flags & DDBLT_KEYSRCOVERRIDE) && fx) { key = 1; klo = rt_r32(G_MEM, fx + 92); khi = rt_r32(G_MEM, fx + 96); }
    else if ((flags & DDBLT_KEYSRC) && src->has_ck) { key = 1; klo = src->ck_lo; khi = src->ck_hi; }
    if (dr[2] - dr[0] == sr[2] - sr[0] && dr[3] - dr[1] == sr[3] - sr[1]) {   /* unscaled: clip both rectangles together */
        int32_t ox = sr[0] - dr[0], oy = sr[1] - dr[1];
        if (!clip_to(dr, me)) RET(DD_OK, 6);
        sr[0] = dr[0] + ox; sr[1] = dr[1] + oy; sr[2] = dr[2] + ox; sr[3] = dr[3] + oy;
        if (!clip_to(sr, src)) RET(DD_OK, 6);
        dr[0] = sr[0] - ox; dr[1] = sr[1] - oy; dr[2] = sr[2] - ox; dr[3] = sr[3] - oy;
    } else if (!clip_to(dr, me) || !clip_to(sr, src)) RET(DD_OK, 6);
    copy_rect(me, dr, src, sr, key, klo, khi);
    shown_after_copy(c, me, dr); RET(DD_OK, 6);
END
M(s_BltFast, 6) ME(PSurf, T_SURF)                                   /* (x, y, src, src rect, trans) */
    PSurf *src = the(ARG(3), T_SURF); if (!src) RET(DDERR_INVALIDPARAMS, 6);
    int32_t sr[4]; rect_in(ARG(4), src, sr);
    int32_t x = (int32_t)ARG(1), y = (int32_t)ARG(2);
    int32_t dr[4] = {x, y, x + sr[2] - sr[0], y + sr[3] - sr[1]}, ox = sr[0] - dr[0], oy = sr[1] - dr[1];
    if (!clip_to(dr, me)) RET(DD_OK, 6);
    sr[0] = dr[0] + ox; sr[1] = dr[1] + oy; sr[2] = dr[2] + ox; sr[3] = dr[3] + oy;
    if (!clip_to(sr, src)) RET(DD_OK, 6);
    dr[0] = sr[0] - ox; dr[1] = sr[1] - oy; dr[2] = sr[2] - ox; dr[3] = sr[3] - oy;
    int key = (ARG(5) & DDBLTFAST_SRCCOLORKEY) && src->has_ck;
    copy_rect(me, dr, src, sr, key, src->ck_lo, src->ck_hi);
    shown_after_copy(c, me, dr); RET(DD_OK, 6);
END
M(s_Flip, 3) ME(PSurf, T_SURF)                                      /* (target override, flags): the back buffer becomes visible */
    PSurf *b = the(ARG(1), T_SURF); if (!b) b = me->back;
    if (!b) RET(DD_OK, 3);
    uint32_t t = me->mem; me->mem = b->mem; b->mem = t;              /* the surfaces keep their identities; their pixels swap */
    show(c, me, 0, 0, me->w, me->h);
    RET(DD_OK, 3);
END
M(s_GetAttachedSurface, 3) ME(PSurf, T_SURF)                        /* (DDSCAPS(2) *, out) */
    uint32_t want = rt_r32(G_MEM, ARG(1)), out = ARG(2);
    PSurf *a = (want & DDSCAPS_BACKBUFFER) || (want & DDSCAPS_FLIP) ? me->back : (want & DDSCAPS_FRONTBUFFER) ? me->front : me->back;
    if (!a) { rt_w32(G_MEM, out, 0); RET(DDERR_NOTFOUND, 3); }
    int v = 0; uint32_t mine = rt_r32(G_MEM, ARG(0)); for (int k = 0; k < 4; k++) if (vt_surf[k] == mine) v = k;
    __atomic_add_fetch(&a->ref, 1, __ATOMIC_SEQ_CST); rt_w32(G_MEM, out, a->g[v]); RET(DD_OK, 3);
END
M(s_GetCaps, 2) ME(PSurf, T_SURF) rt_w32(G_MEM, ARG(1), me->caps); RET(DD_OK, 2); END
M(s_GetSurfaceDesc, 2) ME(PSurf, T_SURF) desc_out(me, ARG(1), 0); RET(DD_OK, 2); END
M(s_GetPixelFormat, 2) ME(PSurf, T_SURF) for (int k = 0; k < 8; k++) rt_w32(G_MEM, ARG(1) + 4 * (uint32_t)k, me->pf[k]); RET(DD_OK, 2); END
M(s_Lock, 5) ME(PSurf, T_SURF)                                      /* (rect, desc, flags, event) */
    uint32_t d = ARG(2); desc_out(me, d, 1);
    if (ARG(1)) { int32_t r[4]; rect_in(ARG(1), me, r); rt_w32(G_MEM, d + SD_SURFACE, me->mem + (uint32_t)r[1] * me->pitch + (uint32_t)r[0] * (me->bpp / 8)); }
    me->locked++; RET(DD_OK, 5);
END
M(s_Unlock, 2) ME(PSurf, T_SURF)
    if (me->locked) me->locked--;
    if (visible_of(me) && !me->back) show(c, me, 0, 0, me->w, me->h);   /* drawn straight onto a single primary */
    RET(DD_OK, 2);
END
M(s_IsLost, 1) RET(DD_OK, 1); END
M(s_Restore, 1) RET(DD_OK, 1); END
M(s_SetPalette, 2) ME(PSurf, T_SURF)
    PPal *p = the(ARG(1), T_PAL);
    if (p) __atomic_add_fetch(&p->ref, 1, __ATOMIC_SEQ_CST);
    me->pal = p; if (me->back) me->back->pal = p;
    RET(DD_OK, 2);
END
M(s_GetPalette, 2) ME(PSurf, T_SURF)
    if (!me->pal) { rt_w32(G_MEM, ARG(1), 0); RET(DDERR_NOPALETTEATTACHED, 2); }
    __atomic_add_fetch(&me->pal->ref, 1, __ATOMIC_SEQ_CST); rt_w32(G_MEM, ARG(1), me->pal->g); RET(DD_OK, 2);
END
M(s_SetClipper, 2) ME(PSurf, T_SURF) me->clip = the(ARG(1), T_CLIP); RET(DD_OK, 2); END
M(s_GetClipper, 2) ME(PSurf, T_SURF)
    if (!me->clip) { rt_w32(G_MEM, ARG(1), 0); RET(DDERR_NOCLIPPERATTACHED, 2); }
    rt_w32(G_MEM, ARG(1), me->clip->g); RET(DD_OK, 2);
END
M(s_SetColorKey, 3) ME(PSurf, T_SURF)                               /* (flags, DDCOLORKEY *) */
    if (ARG(1) & DDCKEY_SRCBLT) {
        if (ARG(2)) { me->ck_lo = rt_r32(G_MEM, ARG(2)); me->ck_hi = rt_r32(G_MEM, ARG(2) + 4); me->has_ck = 1; } else me->has_ck = 0;
    }
    RET(DD_OK, 3);
END
M(s_GetColorKey, 3) ME(PSurf, T_SURF)
    if (!(ARG(1) & DDCKEY_SRCBLT) || !me->has_ck) RET(DDERR_NOCOLORKEY, 3);
    rt_w32(G_MEM, ARG(2), me->ck_lo); rt_w32(G_MEM, ARG(2) + 4, me->ck_hi); RET(DD_OK, 3);
END
M(s_GetDDInterface, 2) ME(PSurf, T_SURF) rt_w32(G_MEM, ARG(1), me->dd ? me->dd->g[0] : 0); RET(DD_OK, 2); END
M(s_GetBltStatus, 2) RET(DD_OK, 2); END
M(s_GetFlipStatus, 2) RET(DD_OK, 2); END
M(s_GetUniquenessValue, 2) rt_w32(G_MEM, ARG(1), 1); RET(DD_OK, 2); END
NOT_M(s_AddAttachedSurface, 2) NOT_M(s_AddOverlayDirtyRect, 2) NOT_M(s_BltBatch, 4) NOT_M(s_DeleteAttachedSurface, 3)
NOT_M(s_EnumAttachedSurfaces, 3) NOT_M(s_EnumOverlayZOrders, 4) NOT_M(s_GetDC, 2) NOT_M(s_ReleaseDC, 2)
NOT_M(s_GetOverlayPosition, 3) NOT_M(s_Initialize, 3) NOT_M(s_SetOverlayPosition, 3) NOT_M(s_UpdateOverlay, 6)
NOT_M(s_UpdateOverlayDisplay, 2) NOT_M(s_UpdateOverlayZOrder, 3) OK_M(s_PageLock, 2) OK_M(s_PageUnlock, 2) NOT_M(s_SetSurfaceDesc, 3)
NOT_M(s_SetPrivateData, 5) NOT_M(s_GetPrivateData, 4) NOT_M(s_FreePrivateData, 2) OK_M(s_ChangeUniquenessValue, 1)
NOT_M(s_SetPriority, 2) NOT_M(s_GetPriority, 2) NOT_M(s_SetLOD, 2) NOT_M(s_GetLOD, 2)

/* gamma: the game's brightness setting as the renderer's ramp */
static uint16_t gamma_ramp[3][256]; static int gamma_set;
M(g_QueryInterface, 3) ME(PSurf, T_GAMMA)
    uint32_t iid = rt_r32(G_MEM, ARG(1)), out = ARG(2);
    if (iid == 0x69c11c3eu) { rt_w32(G_MEM, out, me->g_gamma); __atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST); RET(DD_OK, 3); }
    int v = vidx_of_surf_iid(iid); if (v >= 0) { rt_w32(G_MEM, out, me->g[v]); __atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST); RET(DD_OK, 3); }
    rt_w32(G_MEM, out, 0); RET(E_NOINTERFACE, 3);
END
M(g_AddRef, 1) ME(PSurf, T_GAMMA) RET(__atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST), 1); END
M(g_Release, 1) ME(PSurf, T_GAMMA) RET(surf_release(me), 1); END
M(g_GetGammaRamp, 3)
    if (!gamma_set) for (int k = 0; k < 256; k++) gamma_ramp[0][k] = gamma_ramp[1][k] = gamma_ramp[2][k] = (uint16_t)(k * 0x101);
    memcpy(GP(ARG(2)), gamma_ramp, 1536); RET(DD_OK, 3);
END
M(g_SetGammaRamp, 3)
    memcpy(gamma_ramp, GP(ARG(2)), 1536); gamma_set = 1;
    uint32_t *p = cmd_begin(DSR_GAMMA, 1536); memcpy(p, gamma_ramp, 1536); cmd_end(); RET(DD_OK, 3);
END

/* ---- palettes ---- */
M(p_QueryInterface, 3) ME(PPal, T_PAL)
    uint32_t iid = rt_r32(G_MEM, ARG(1));
    if (iid == 0x6c14db84u || iid == 0) { rt_w32(G_MEM, ARG(2), me->g); me->ref++; RET(DD_OK, 3); }
    rt_w32(G_MEM, ARG(2), 0); RET(E_NOINTERFACE, 3);
END
M(p_AddRef, 1) ME(PPal, T_PAL) RET(__atomic_add_fetch(&me->ref, 1, __ATOMIC_SEQ_CST), 1); END
M(p_Release, 1) ME(PPal, T_PAL) int r = __atomic_sub_fetch(&me->ref, 1, __ATOMIC_SEQ_CST); RET(r < 0 ? 0 : r, 1); END   /* (kept: surfaces may still point at it) */
M(p_GetCaps, 2) ME(PPal, T_PAL) rt_w32(G_MEM, ARG(1), me->caps); RET(DD_OK, 2); END
M(p_GetEntries, 5) ME(PPal, T_PAL)                                  /* (flags, base, count, out) */
    uint32_t b = ARG(2), n = ARG(3), out = ARG(4);
    for (uint32_t k = 0; k < n && b + k < 256; k++) rt_w32(G_MEM, out + 4 * k, me->entries[b + k]);
    RET(DD_OK, 5);
END
M(p_SetEntries, 5) ME(PPal, T_PAL)                                  /* (flags, start, count, entries) */
    uint32_t s = ARG(2), n = ARG(3), in = ARG(4);
    for (uint32_t k = 0; k < n && s + k < 256; k++) me->entries[s + k] = rt_r32(G_MEM, in + 4 * k);
    RET(DD_OK, 5);
END
OK_M(p_Initialize, 4)

/* ---- clipper ---- */
M(cl_QueryInterface, 3) ME(PClip, T_CLIP) rt_w32(G_MEM, ARG(2), me->g); me->ref++; RET(DD_OK, 3); END
M(cl_AddRef, 1) ME(PClip, T_CLIP) RET((uint32_t)++me->ref, 1); END
M(cl_Release, 1) ME(PClip, T_CLIP) RET((uint32_t)(me->ref > 0 ? --me->ref : 0), 1); END
NOT_M(cl_GetClipList, 4)
M(cl_GetHWnd, 2) ME(PClip, T_CLIP) rt_w32(G_MEM, ARG(1), me->hwnd); RET(DD_OK, 2); END
OK_M(cl_Initialize, 3)
M(cl_IsClipListChanged, 2) rt_w32(G_MEM, ARG(1), 0); RET(DD_OK, 2); END
OK_M(cl_SetClipList, 3)
M(cl_SetHWnd, 3) ME(PClip, T_CLIP) me->hwnd = ARG(2); RET(DD_OK, 3); END

/* ---- IDirectDraw (1, 2, 4, 7) ---- */
static int vidx_of_dd_iid(uint32_t iid)
{
    switch (iid) { case 0x6c14db80u: return 0; case 0xb3a6f3e0u: return 1; case 0x9c59509au: return 2; case 0x15e65ec0u: return 3; }
    return -1;
}
M(dd_QueryInterface, 3) ME(PDraw, T_DD)
    uint32_t iid = rt_r32(G_MEM, ARG(1)), out = ARG(2); int v = vidx_of_dd_iid(iid);
    dlog("IDirectDraw QI %08x -> %s", iid, v >= 0 || iid == 0 ? "ok" : "no");
    if (v >= 0 || iid == 0) { rt_w32(G_MEM, out, me->g[v < 0 ? 0 : v]); me->ref++; RET(DD_OK, 3); }
    rt_w32(G_MEM, out, 0); RET(E_NOINTERFACE, 3);                   /* (Direct3D: not offered; the game then uses DirectDraw) */
END
M(dd_AddRef, 1) ME(PDraw, T_DD) RET((uint32_t)++me->ref, 1); END
M(dd_Release, 1) ME(PDraw, T_DD) RET((uint32_t)(me->ref > 0 ? --me->ref : 0), 1); END
OK_M(dd_Compact, 1)
M(dd_CreateClipper, 4) ME(PDraw, T_DD)
    PClip *k = calloc(1, sizeof *k); k->ref = 1; k->g = iface(vt_clip, T_CLIP, k);
    rt_w32(G_MEM, ARG(2), k->g); RET(DD_OK, 4);
END
M(dd_CreatePalette, 5) ME(PDraw, T_DD)                              /* (flags, entries, out, outer) */
    PPal *p = calloc(1, sizeof *p); p->ref = 1; p->caps = ARG(1); p->g = iface(vt_pal, T_PAL, p);
    if (ARG(2)) for (int k = 0; k < 256; k++) p->entries[k] = rt_r32(G_MEM, ARG(2) + 4 * (uint32_t)k);
    rt_w32(G_MEM, ARG(3), p->g); RET(DD_OK, 5);
END
static int vidx_of_this(Ctx *c, const uint32_t *tab) { uint32_t mine = rt_r32(G_MEM, ARG(0)); for (int k = 0; k < 4; k++) if (tab[k] == mine) return k; return 0; }
M(dd_CreateSurface, 4) ME(PDraw, T_DD)                              /* (desc, out, outer) */
    uint32_t d = ARG(1), fl = rt_r32(G_MEM, d + SD_FLAGS), caps = (fl & DDSD_CAPS) ? rt_r32(G_MEM, d + SD_CAPS) : 0;
    uint32_t w = (fl & DDSD_WIDTH) ? rt_r32(G_MEM, d + SD_WIDTH) : 0, h = (fl & DDSD_HEIGHT) ? rt_r32(G_MEM, d + SD_HEIGHT) : 0;
    uint32_t bpp = (fl & DDSD_PIXELFORMAT) ? rt_r32(G_MEM, d + SD_PF + PF_BITS) : me->mode_bpp;
    int v = vidx_of_this(c, vt_dd); int sv = v >= 2 ? v : (v == 1 ? 1 : 0);
    if (caps & DDSCAPS_PRIMARYSURFACE) { w = me->mode_w; h = me->mode_h; bpp = me->mode_bpp; }
    if (!w || !h || (bpp != 8 && bpp != 16 && bpp != 32)) { dlog("CreateSurface %ux%ux%u caps %x refused", w, h, bpp, caps); RET(DDERR_INVALIDPARAMS, 4); }
    uint32_t sc = caps | ((caps & DDSCAPS_SYSTEMMEMORY) ? 0 : DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM);
    if (caps & DDSCAPS_PRIMARYSURFACE) sc |= DDSCAPS_VISIBLE | ((caps & DDSCAPS_FLIP) ? DDSCAPS_FRONTBUFFER : 0);
    PSurf *s = surf_new(me, w, h, bpp, sc);
    if ((caps & DDSCAPS_FLIP) && (fl & DDSD_BACKBUFFERCOUNT) && rt_r32(G_MEM, d + SD_BACKBUFFERS)) {
        PSurf *b = surf_new(me, w, h, bpp, (sc & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER | DDSCAPS_VISIBLE)) | DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_COMPLEX);
        s->back = b; b->front = s; b->back = s;
    }
    if (caps & DDSCAPS_PRIMARYSURFACE) me->primary = s;
    dlog("CreateSurface %ux%ux%u caps %x%s", w, h, bpp, caps, s->back ? " (with a back buffer)" : "");
    rt_w32(G_MEM, ARG(2), s->g[sv]); RET(DD_OK, 4);
END
NOT_M(dd_DuplicateSurface, 3)
static const uint16_t modes[][2] = {{640, 480}, {800, 600}, {1024, 768}};
M(dd_EnumDisplayModes, 5) ME(PDraw, T_DD)                           /* (flags, desc filter, ctx, callback) */
    uint32_t filter = ARG(2), ctx = ARG(3), cb = ARG(4), v = (uint32_t)vidx_of_this(c, vt_dd), size = v >= 2 ? 124 : 108;
    uint32_t d = heap_alloc(w32_process_heap, 8, 124);
    static const uint32_t depths[] = {8, 16, 32};
    for (int di = 0; di < 3; di++) for (size_t k = 0; k < sizeof modes / sizeof *modes; k++) {
        uint32_t w = modes[k][0], h = modes[k][1], bpp = depths[di];
        if (filter) { uint32_t ff = rt_r32(G_MEM, filter + SD_FLAGS);
            if ((ff & DDSD_WIDTH) && rt_r32(G_MEM, filter + SD_WIDTH) != w) continue;
            if ((ff & DDSD_HEIGHT) && rt_r32(G_MEM, filter + SD_HEIGHT) != h) continue;
            if ((ff & DDSD_PIXELFORMAT) && rt_r32(G_MEM, filter + SD_PF + PF_BITS) != bpp) continue; }
        memset(GP(d), 0, 124); rt_w32(G_MEM, d, size);
        rt_w32(G_MEM, d + SD_FLAGS, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE);
        rt_w32(G_MEM, d + SD_WIDTH, w); rt_w32(G_MEM, d + SD_HEIGHT, h); rt_w32(G_MEM, d + SD_PITCH, w * bpp / 8); rt_w32(G_MEM, d + SD_REFRESH, 60);
        uint32_t pf[8]; pf_for(pf, bpp); for (int j = 0; j < 8; j++) rt_w32(G_MEM, d + SD_PF + 4 * (uint32_t)j, pf[j]);
        uint32_t a[2] = {d, ctx};
        if (w32_callback(c, cb, 2, a) == 0) goto done;
    }
done:
    heap_free(w32_process_heap, d); RET(DD_OK, 5);
END
NOT_M(dd_EnumSurfaces, 5)
OK_M(dd_FlipToGDISurface, 1)
M(dd_GetCaps, 3)                                                    /* (driver caps, emulation caps): DDCAPS, as much as the caller's size takes */
    for (int k = 1; k <= 2; k++) {
        uint32_t p = ARG(k); if (!p) continue;
        uint32_t sz = rt_r32(G_MEM, p); if (sz < 68 || sz > 512) continue;
        memset(GP(p + 4), 0, sz - 4);
        rt_w32(G_MEM, p + 4, 0x40 | 0x200 | 0x8000 | 0x400000 | 0x4000000 | 0x80000000u);   /* BLT, BLTSTRETCH, PALETTE, COLORKEY, BLTCOLORFILL, CANBLTSYSMEM */
        rt_w32(G_MEM, p + 12, 0x200);                               /* DDCKEYCAPS_SRCBLT */
        rt_w32(G_MEM, p + 24, 4 | 0x10 | 0x400);                    /* DDPCAPS_8BIT, PRIMARYSURFACE, ALLOW256 */
        rt_w32(G_MEM, p + 60, 256u << 20); rt_w32(G_MEM, p + 64, 240u << 20);
    }
    RET(DD_OK, 3);
END
M(dd_GetDisplayMode, 2) ME(PDraw, T_DD)
    uint32_t d = ARG(1), size = rt_r32(G_MEM, d); if (size != 124) size = 108;
    memset(GP(d), 0, size); rt_w32(G_MEM, d, size);
    rt_w32(G_MEM, d + SD_FLAGS, DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE);
    rt_w32(G_MEM, d + SD_WIDTH, me->mode_w); rt_w32(G_MEM, d + SD_HEIGHT, me->mode_h); rt_w32(G_MEM, d + SD_PITCH, me->mode_w * me->mode_bpp / 8);
    rt_w32(G_MEM, d + SD_REFRESH, 60);
    uint32_t pf[8]; pf_for(pf, me->mode_bpp); for (int j = 0; j < 8; j++) rt_w32(G_MEM, d + SD_PF + 4 * (uint32_t)j, pf[j]);
    RET(DD_OK, 2);
END
M(dd_GetFourCCCodes, 3) if (ARG(1)) rt_w32(G_MEM, ARG(1), 0); RET(DD_OK, 3); END
M(dd_GetGDISurface, 2) ME(PDraw, T_DD)
    if (!me->primary) { rt_w32(G_MEM, ARG(1), 0); RET(DDERR_NOTFOUND, 2); }
    __atomic_add_fetch(&me->primary->ref, 1, __ATOMIC_SEQ_CST); rt_w32(G_MEM, ARG(1), me->primary->g[0]); RET(DD_OK, 2);
END
M(dd_GetMonitorFrequency, 2) rt_w32(G_MEM, ARG(1), 60); RET(DD_OK, 2); END
M(dd_GetScanLine, 2) rt_w32(G_MEM, ARG(1), 0); RET(DD_OK, 2); END
M(dd_GetVerticalBlankStatus, 2) rt_w32(G_MEM, ARG(1), 1); RET(DD_OK, 2); END
OK_M(dd_Initialize, 2)
M(dd_RestoreDisplayMode, 1) ME(PDraw, T_DD) me->mode_w = (uint32_t)w32_screen_w; me->mode_h = (uint32_t)w32_screen_h; me->mode_bpp = 32; RET(DD_OK, 1); END
M(dd_SetCooperativeLevel, 3) ME(PDraw, T_DD)
    dlog("SetCooperativeLevel hwnd %08x flags %08x", ARG(1), ARG(2));
    if (ARG(1)) me->hwnd = ARG(1);
    me->coop = ARG(2); RET(DD_OK, 3);
END
static uint32_t set_mode(Ctx *c, PDraw *me, uint32_t w, uint32_t h, uint32_t bpp)
{
    dlog("SetDisplayMode %ux%ux%u", w, h, bpp);
    if (bpp != 8 && bpp != 16 && bpp != 32) return DDERR_UNSUPPORTED;
    me->mode_w = w; me->mode_h = h; me->mode_bpp = bpp;
    if (me->hwnd && (me->coop & DDSCL_EXCLUSIVE)) w32_set_window_pos(c, me->hwnd, 0, 0, (int)w, (int)h, 0x40 | 0x10);
    uint32_t *p = cmd_begin(DSR_MODE, 8); p[0] = w; p[1] = h; cmd_end();
    return DD_OK;
}
M(dd_SetDisplayMode1, 4) ME(PDraw, T_DD) RET(set_mode(c, me, ARG(1), ARG(2), ARG(3)), 4); END   /* IDirectDraw: (w, h, bpp) */
M(dd_SetDisplayMode, 6) ME(PDraw, T_DD) RET(set_mode(c, me, ARG(1), ARG(2), ARG(3)), 6); END    /* later: (w, h, bpp, refresh, flags) */
OK_M(dd_WaitForVerticalBlank, 3)
M(dd_GetAvailableVidMem, 4) if (ARG(2)) rt_w32(G_MEM, ARG(2), 256u << 20); if (ARG(3)) rt_w32(G_MEM, ARG(3), 240u << 20); RET(DD_OK, 4); END
NOT_M(dd_GetSurfaceFromDC, 3) OK_M(dd_RestoreAllSurfaces, 1) OK_M(dd_TestCooperativeLevel, 1)
M(dd_GetDeviceIdentifier, 3)
    uint32_t p = ARG(1); memset(GP(p), 0, 1060);
    strcpy((char *)GP(p), "display"); strcpy((char *)GP(p + 512), "Apple Metal (Diablo II native)");
    RET(DD_OK, 3);
END
NOT_M(dd_StartModeTest, 4) NOT_M(dd_EvaluateMode, 3)

/* ---- vtables ---- */
typedef void (*Fn)(Ctx *);
static uint32_t vtable(const char *name, const Fn *fns, int n)
{
    uint32_t t = heap_alloc(w32_process_heap, 8, 4u * (uint32_t)n);
    for (int k = 0; k < n; k++) { char nm[96]; snprintf(nm, sizeof nm, "ddraw!%s.%d", name, k); rt_w32(G_MEM, t + 4u * (uint32_t)k, w32_thunk_register(nm, fns[k])); }
    return t;
}
static void init_vtables(void)
{
    static int done; if (done) return; done = 1;
    Fn dd[30] = { dd_QueryInterface, dd_AddRef, dd_Release, dd_Compact, dd_CreateClipper, dd_CreatePalette, dd_CreateSurface,
        dd_DuplicateSurface, dd_EnumDisplayModes, dd_EnumSurfaces, dd_FlipToGDISurface, dd_GetCaps, dd_GetDisplayMode,
        dd_GetFourCCCodes, dd_GetGDISurface, dd_GetMonitorFrequency, dd_GetScanLine, dd_GetVerticalBlankStatus,
        dd_Initialize, dd_RestoreDisplayMode, dd_SetCooperativeLevel, dd_SetDisplayMode1, dd_WaitForVerticalBlank,
        dd_GetAvailableVidMem, dd_GetSurfaceFromDC, dd_RestoreAllSurfaces, dd_TestCooperativeLevel, dd_GetDeviceIdentifier,
        dd_StartModeTest, dd_EvaluateMode };
    vt_dd[0] = vtable("IDirectDraw", dd, 23);
    dd[21] = dd_SetDisplayMode;
    vt_dd[1] = vtable("IDirectDraw2", dd, 24); vt_dd[2] = vtable("IDirectDraw4", dd, 28); vt_dd[3] = vtable("IDirectDraw7", dd, 30);
    static const Fn surf[49] = { s_QueryInterface, s_AddRef, s_Release, s_AddAttachedSurface, s_AddOverlayDirtyRect, s_Blt, s_BltBatch, s_BltFast,
        s_DeleteAttachedSurface, s_EnumAttachedSurfaces, s_EnumOverlayZOrders, s_Flip, s_GetAttachedSurface, s_GetBltStatus,
        s_GetCaps, s_GetClipper, s_GetColorKey, s_GetDC, s_GetFlipStatus, s_GetOverlayPosition, s_GetPalette, s_GetPixelFormat,
        s_GetSurfaceDesc, s_Initialize, s_IsLost, s_Lock, s_ReleaseDC, s_Restore, s_SetClipper, s_SetColorKey,
        s_SetOverlayPosition, s_SetPalette, s_Unlock, s_UpdateOverlay, s_UpdateOverlayDisplay, s_UpdateOverlayZOrder,
        s_GetDDInterface, s_PageLock, s_PageUnlock, s_SetSurfaceDesc, s_SetPrivateData, s_GetPrivateData, s_FreePrivateData,
        s_GetUniquenessValue, s_ChangeUniquenessValue, s_SetPriority, s_GetPriority, s_SetLOD, s_GetLOD };
    vt_surf[0] = vtable("IDirectDrawSurface", surf, 36); vt_surf[1] = vtable("IDirectDrawSurface3", surf, 40);
    vt_surf[2] = vtable("IDirectDrawSurface4", surf, 45); vt_surf[3] = vtable("IDirectDrawSurface7", surf, 49);
    static const Fn pal[] = { p_QueryInterface, p_AddRef, p_Release, p_GetCaps, p_GetEntries, p_Initialize, p_SetEntries };
    vt_pal = vtable("IDirectDrawPalette", pal, 7);
    static const Fn clip[] = { cl_QueryInterface, cl_AddRef, cl_Release, cl_GetClipList, cl_GetHWnd, cl_Initialize, cl_IsClipListChanged, cl_SetClipList, cl_SetHWnd };
    vt_clip = vtable("IDirectDrawClipper", clip, 9);
    static const Fn gam[] = { g_QueryInterface, g_AddRef, g_Release, g_GetGammaRamp, g_SetGammaRamp };
    vt_gamma = vtable("IDirectDrawGammaControl", gam, 5);
}

/* ---- exports ---- */
IMPL(ddraw, DirectDrawCreate)                                       /* (GUID *, out, outer) */
{
    init_vtables();
    PDraw *dd = calloc(1, sizeof *dd); dd->ref = 1;
    dd->mode_w = (uint32_t)w32_screen_w; dd->mode_h = (uint32_t)w32_screen_h; dd->mode_bpp = 32;
    for (int v = 0; v < 4; v++) dd->g[v] = iface(vt_dd[v], T_DD, dd);
    dlog("DirectDrawCreate");
    rt_w32(G_MEM, ARG(1), dd->g[0]); RET(DD_OK, 3);
}
IMPL(ddraw, DirectDrawEnumerateA)                                   /* (callback(guid, description, name, ctx), ctx): the display */
{
    uint32_t buf = heap_alloc(w32_process_heap, 8, 64);
    strcpy((char *)GP(buf), "Primary Display Driver"); strcpy((char *)GP(buf + 32), "display");
    uint32_t a[4] = {0, buf, buf + 32, ARG(1)};
    w32_callback(c, ARG(0), 4, a);
    heap_free(w32_process_heap, buf); RET(DD_OK, 2);
}
