/* Native features and fixes added to the recompiled game at fixed x86 addresses. tools/lift.py lists them:
 *   HOOKS:     run before the instruction at the address (guest registers spilled to the context), then the original
 *              continues;
 *   OVERRIDES: at a function's first instruction; return 1 when the whole call (including the return) was done here,
 *              0 to run the original.
 * DS_TRACE_HOOKS=<addr,...> at build time adds trace hooks that log registers and stack arguments (DS_HOOKTRACE=1). */
#include "w32.h"
#include "ext.h"

int native_hook(Ctx *c, uint32_t addr);   /* native.c */
int d2wide_width(void); int d2wide_mark(uint32_t addr); int d2wide_quarter(Ctx *c, uint32_t addr); void d2wide_screen_set(void);   /* d2wide.c */
int d2smooth_on(void); int d2smooth_frame_due(void); void d2smooth_tick(void); void d2smooth_before_draw(void); void d2smooth_after_draw(void);   /* d2smooth.c */
void rt_hook(Ctx *c, uint32_t addr)
{
    if (native_hook(c, addr)) return;
    switch (addr) {
    case 0x004065af: {
        /* The game has read its command line into its video settings (window, 3dfx, opengl, rave, d3d: bytes 8, 10-13
         * of the structure at ebp-0x4d8) and has just set d3d unless windowed: 1.14d always runs Direct3D full screen.
         * D2_RENDERER picks among the game's own renderers instead: "ddraw" (the default here: its DirectDraw renderer,
         * 8-bit software frames, the cinematics through Bink), "gdi" (its windowed mode; no cinematics there), "d3d"
         * (the game's choice: its Direct3D renderer, on Metal through runtime/win32/ddraw_pal.c). */
        const char *r = getenv("D2_RENDERER"); uint32_t cfg = c->ebp - 0x4d8;
        if (r && !strcmp(r, "d3d")) break;
        int gdi = r && !strcmp(r, "gdi");
        G_MEM[cfg + 8] = (uint8_t)gdi; G_MEM[cfg + 10] = 0; G_MEM[cfg + 11] = 0; G_MEM[cfg + 12] = 0; G_MEM[cfg + 13] = 0;
        break;
    }
    /* wider than 4:3 (D2_WIDTH, d2wide.c) */
    case 0x004f55aa: if (d2wide_width()) rt_w32(G_MEM, c->edx, (uint32_t)d2wide_width()); break;   /* the 800x600 modes' size */
    case 0x00509c3f: case 0x0051126f: case 0x006b5037: case 0x006c7b75:        /* the renderers' screen size, as each sets it */
        if (d2wide_width() && rt_r32(G_MEM, 0x7c9138) == 800) rt_w32(G_MEM, 0x7c9138, (uint32_t)d2wide_width());
        break;
    case 0x0044caed: if (d2wide_width() && c->eax == 3) c->eax = 0; break;     /* both side panels open: the world still shows between them */
    case 0x0045aec6: case 0x0045aedd: if (d2wide_width()) c->eax = 200; break;   /* one side panel open: the view slides by half a panel */
    case 0x0045af07: if (d2wide_width()) c->edx = 200; break;                  /* (not a quarter of the wider screen) */
    case 0x0044ba50: if (d2wide_width()) { c->eax = (uint32_t)d2wide_width(); rt_w32(G_MEM, 0x71146c, c->eax); d2wide_screen_set(); } break;   /* the client's screen width */
    /* more frames than ticks (D2_FPS, d2smooth.c) */
    case 0x0044c790: d2smooth_tick(); break;                                   /* the client's game tick */
    case 0x0044f278:                                                           /* in game: draw when a frame is due, not only after a tick */
        if (d2smooth_on() && rt_r32(G_MEM, 0x7a6a70)) rt_w32(G_MEM, 0x7a0704, d2smooth_frame_due() ? 0 : 1);
        break;
    /* zoomed out (d2zoom.c) */
    case 0x0044c9ab: { void d2zoom_world_begin(void); d2zoom_world_begin(); break; }
    case 0x0044cafd: case 0x0044cb4f: { void d2zoom_world_end(void); d2zoom_world_end(); break; }
    case 0x004de860: { void d2zoom_rooms(Ctx *c); d2zoom_rooms(c); break; }
    case 0x00462d00: { void d2zoom_click(Ctx *c); d2zoom_click(c); break; }
    case 0x0046745f: { void d2zoom_hover_box(void); d2zoom_hover_box(); break; }
    case 0x0046723c: { void d2zoom_hover_find(Ctx *c); d2zoom_hover_find(c); break; }
    case 0x0044f28b: d2smooth_before_draw(); break;                            /* the game's draw ... */
    case 0x0044f291: d2smooth_after_draw(); break;                             /* ... and after it */
    default:
        if (d2wide_mark(addr) || d2wide_quarter(c, addr)) break;                                          /* wide: right-hand interface drawn at 800x600 places */
        /* development trace hooks (tools/lift.py DS_TRACE_HOOKS) */
        if (getenv("DS_HOOKTRACE")) fprintf(stderr, "hook %08x: eax %08x ecx %08x edx %08x ebx %08x esi %08x edi %08x | ret %08x args %08x %08x %08x %08x\n",
                                            addr, c->eax, c->ecx, c->edx, c->ebx, c->esi, c->edi, rt_r32(G_MEM, c->esp), rt_r32(G_MEM, c->esp + 4),
                                            rt_r32(G_MEM, c->esp + 8), rt_r32(G_MEM, c->esp + 12), rt_r32(G_MEM, c->esp + 16));
        if (getenv("DS_HOOKTRACE") && atoi(getenv("DS_HOOKTRACE")) >= 2)   /* and 80 bytes at the first two stack arguments */
            for (int k = 1; k <= 2; k++) {
                uint32_t p = rt_r32(G_MEM, c->esp + 4 * (uint32_t)k); if (p < 0x10000) continue;
                fprintf(stderr, "   arg%d:", k); for (uint32_t j = 0; j < 80; j++) fprintf(stderr, " %02x", G_MEM[p + j]); fprintf(stderr, "\n");
            }
    }
}
int native_override(Ctx *c, uint32_t addr);   /* native.c */
int rt_override(Ctx *c, uint32_t addr)
{
    if (native_override(c, addr)) return 1;
    switch (addr) {
    }
    return 0;
}
