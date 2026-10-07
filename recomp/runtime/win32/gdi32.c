/* GDI32: device contexts, memory bitmaps and TrueType text through Core Text (the game rasterises font glyphs
 * with ExtTextOutA and reads them back with GetPixel/GetDIBits to build font textures). */
#include "w32.h"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

extern int w32_screen_w, w32_screen_h;
enum { G_DC = 1, G_BITMAP, G_FONT, G_BRUSH, G_PEN, G_STOCK, G_PALETTE };
typedef struct GObj {
    int type;
    /* bitmap */ int bw, bh; uint32_t *px;            /* 0x00RRGGBB per pixel, top-down */
    /* font */   CTFontRef font; int height, weight, italic; char face[64]; int ascent, descent, internal_leading, avg_w, max_w;
    /* dc */     uint32_t bitmap, fontsel, text_color, bk_color; int bk_mode;
    uint32_t color;
    /* palette */ uint32_t pal[256]; int npal;            /* PALETTEENTRY (r, g, b, flags) as stored */
    /* DIB section (bitmap) */ uint32_t dib, dib_pitch; int dib_bpp, dib_topdown; uint32_t ctab[256];   /* bits in guest memory; RGBQUAD = 0x00RRGGBB */
    /* dc */     uint32_t palsel;
} GObj;
#define MAXG 1024
#define GBASE 0x00b00000u
static GObj *gtab[MAXG]; static pthread_mutex_t glock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_new(GObj *o) { pthread_mutex_lock(&glock); for (int k = 1; k < MAXG; k++) if (!gtab[k]) { gtab[k] = o; pthread_mutex_unlock(&glock); return GBASE + 4 * (uint32_t)k; } pthread_mutex_unlock(&glock); return 0; }
static GObj *G(uint32_t h, int type) { uint32_t k = (h - GBASE) / 4; GObj *o = (h >= GBASE && k < MAXG) ? gtab[k] : 0; return o && (!type || o->type == type) ? o : 0; }
static uint32_t screen_dc;
static GObj *dc_of(uint32_t h)
{
    if (h == 0x20004) {                                   /* GetDC(window): a screen-compatible DC */
        if (!screen_dc) { GObj *d = calloc(1, sizeof *d); d->type = G_DC; d->text_color = 0; d->bk_color = 0xffffff; d->bk_mode = 2; screen_dc = g_new(d); }
        return G(screen_dc, G_DC);
    }
    return G(h, G_DC);
}

IMPL(gdi32, GetStockObject) { RET(0x00b80000u + (ARG(0) & 0xff), 1); }
IMPL(gdi32, CreateHatchBrush) { GObj *o = calloc(1, sizeof *o); o->type = G_BRUSH; o->color = ARG(1); RET(g_new(o), 2); }
IMPL(gdi32, CreateCompatibleDC)
{
    GObj *o = calloc(1, sizeof *o); o->type = G_DC; o->text_color = 0; o->bk_color = 0xffffff; o->bk_mode = 2;
    RET(g_new(o), 1);
}
IMPL(gdi32, DeleteDC) { uint32_t k = (ARG(0) - GBASE) / 4; GObj *o = G(ARG(0), G_DC); if (o) { free(o); gtab[k] = 0; } RET(o != 0, 1); }
IMPL(gdi32, CreateCompatibleBitmap)
{
    int w = (int)ARG(1), h = (int)ARG(2); if (w <= 0 || h <= 0 || w > 8192 || h > 8192) RET(0, 3);
    GObj *o = calloc(1, sizeof *o); o->type = G_BITMAP; o->bw = w; o->bh = h; o->px = calloc((size_t)w * h, 4);
    RET(g_new(o), 3);
}
IMPL(gdi32, DeleteObject)
{
    uint32_t h = ARG(0), k = (h - GBASE) / 4; GObj *o = G(h, 0);
    if (!o) RET(1, 1);
    if (o->type == G_BITMAP) { free(o->px); if (o->dib) heap_free(w32_process_heap, o->dib); }
    if (o->type == G_FONT && o->font) CFRelease(o->font);
    free(o); gtab[k] = 0; RET(1, 1);
}
IMPL(gdi32, SelectObject)
{
    GObj *d = dc_of(ARG(0)); uint32_t h = ARG(1), old = 0; GObj *o = G(h, 0);
    if (!d) RET(0, 2);
    if (o && o->type == G_BITMAP) { old = d->bitmap; d->bitmap = h; }
    else if (o && o->type == G_FONT) { old = d->fontsel; d->fontsel = h; }
    else old = 0x00b80000u;                                 /* brushes, pens, stock objects: tracked loosely */
    RET(old ? old : 0x00b80000u, 2);
}
IMPL(gdi32, SetBkMode) { GObj *d = dc_of(ARG(0)); int o = d ? d->bk_mode : 0; if (d) d->bk_mode = (int)ARG(1); RET((uint32_t)o, 2); }
IMPL(gdi32, SetTextColor) { GObj *d = dc_of(ARG(0)); uint32_t o = d ? d->text_color : 0; if (d) d->text_color = ARG(1); RET(o, 2); }
IMPL(gdi32, SetBkColor) { GObj *d = dc_of(ARG(0)); uint32_t o = d ? d->bk_color : 0; if (d) d->bk_color = ARG(1); RET(o, 2); }
IMPL(gdi32, GetDeviceCaps)
{
    switch (ARG(1)) {
    case 8: RET((uint32_t)w32_screen_w, 2); case 10: RET((uint32_t)w32_screen_h, 2);
    case 12: RET(32, 2); case 14: RET(1, 2); case 24: RET(0xffffffffu, 2);
    case 88: case 90: RET(96, 2); case 116: RET(60, 2); case 38: RET(0x7e99, 2);
    case 4: RET(320, 2); case 6: RET(240, 2);
    }
    RET(0, 2);
}
IMPL(gdi32, DPtoLP) { RET(1, 3); }

/* ---- fonts ---- */
static CFStringRef mac_face(const char *face)
{
    static const char *map[][2] = {{"ms sans serif", "Arial"}, {"ms serif", "Times New Roman"}, {"system", "Arial"},
        {"fixedsys", "Courier New"}, {"terminal", "Courier New"}, {"small fonts", "Arial"}, {"ms shell dlg", "Arial"}, {0, 0}};
    for (int k = 0; map[k][0]; k++) if (!strcasecmp(face, map[k][0])) face = map[k][1];
    if (!*face) face = "Arial";
    CFStringRef r = CFStringCreateWithCString(0, face, kCFStringEncodingWindowsLatin1);
    return r ? r : CFStringCreateWithCString(0, "Arial", kCFStringEncodingASCII);   /* a name that doesn't convert */
}
static uint32_t make_font(int height, int weight, int italic, const char *face)
{
    GObj *o = calloc(1, sizeof *o); o->type = G_FONT; o->height = height; o->weight = weight; o->italic = italic;
    snprintf(o->face, sizeof o->face, "%s", face);
    CFStringRef name = mac_face(face);
    CTFontRef f = CTFontCreateWithName(name, 100, 0); CFRelease(name);
    CTFontSymbolicTraits tr = (weight >= 600 ? kCTFontBoldTrait : 0) | (italic ? kCTFontItalicTrait : 0);
    if (tr) { CTFontRef g = CTFontCreateCopyWithSymbolicTraits(f, 100, 0, tr, tr); if (g) { CFRelease(f); f = g; } }
    double asc = CTFontGetAscent(f), desc = CTFontGetDescent(f);
    double size = height < 0 ? -height : height > 0 ? height * 100.0 / (asc + desc) : 12;   /* Windows: <0 em height, >0 cell height */
    o->font = CTFontCreateCopyWithAttributes(f, size, 0, 0); CFRelease(f);
    o->ascent = (int)ceil(CTFontGetAscent(o->font)); o->descent = (int)ceil(CTFontGetDescent(o->font));
    o->internal_leading = o->ascent + o->descent - (int)lround(size);
    if (o->internal_leading < 0) o->internal_leading = 0;
    UniChar ch[2] = {'x', 'W'}; CGGlyph gl[2]; CGSize adv[2];
    CTFontGetGlyphsForCharacters(o->font, ch, gl, 2); CTFontGetAdvancesForGlyphs(o->font, kCTFontOrientationHorizontal, gl, adv, 2);
    o->avg_w = (int)lround(adv[0].width); o->max_w = (int)lround(adv[1].width);
    return g_new(o);
}
IMPL(gdi32, CreateFontA) { RET(make_font((int)ARG(0), (int)ARG(4), (int)ARG(5), ARG(13) ? GS(ARG(13)) : ""), 14); }
IMPL(gdi32, CreateFontIndirectA)
{
    uint32_t lf = ARG(0); char face[33]; memcpy(face, GP(lf + 28), 32); face[32] = 0;
    RET(make_font((int)rt_r32(G_MEM, lf), (int)rt_r32(G_MEM, lf + 16), rt_r8(G_MEM, lf + 20), face), 1);
}
static GObj *dc_font(GObj *d) { GObj *f = d ? G(d->fontsel, G_FONT) : 0; if (!f && d) { d->fontsel = make_font(-13, 400, 0, "Arial"); f = G(d->fontsel, G_FONT); } return f; }
IMPL(gdi32, GetTextMetricsA)
{
    GObj *f = dc_font(dc_of(ARG(0))); uint32_t t = ARG(1); if (!f) RET(0, 2);
    memset(GP(t), 0, 56);
    uint32_t v[11] = {(uint32_t)(f->ascent + f->descent), (uint32_t)f->ascent, (uint32_t)f->descent, (uint32_t)f->internal_leading, 0,
                      (uint32_t)f->avg_w, (uint32_t)f->max_w, (uint32_t)f->weight, 0, 96, 96};
    memcpy(GP(t), v, sizeof v);
    rt_w8(G_MEM, t + 44, 32); rt_w8(G_MEM, t + 45, 255); rt_w8(G_MEM, t + 46, '?'); rt_w8(G_MEM, t + 47, ' ');
    rt_w8(G_MEM, t + 48, (uint8_t)f->italic); rt_w8(G_MEM, t + 51, 0x06);   /* TMPF_VECTOR | TMPF_TRUETYPE */
    RET(1, 2);
}
static double advance(GObj *f, uint16_t ch)
{
    UniChar u = ch; CGGlyph g; CGSize a = {0, 0};
    if (CTFontGetGlyphsForCharacters(f->font, &u, &g, 1)) CTFontGetAdvancesForGlyphs(f->font, kCTFontOrientationHorizontal, &g, &a, 1);
    return a.width;
}
IMPL(gdi32, GetCharWidthA)
{
    GObj *f = dc_font(dc_of(ARG(0))); uint32_t first = ARG(1), last = ARG(2), out = ARG(3); if (!f) RET(0, 4);
    for (uint32_t ch = first; ch <= last; ch++) {
        uint16_t w; uint8_t b = (uint8_t)ch; w32_mb_to_wide(1252, &b, 1, &w, 1);
        rt_w32(G_MEM, out + 4 * (ch - first), (uint32_t)lround(advance(f, w)));
    }
    RET(1, 4);
}
/* draw UTF-16 text at (x, y) = top-left of the text cell, in the DC's text colour (and background when OPAQUE) */
static void draw_text(GObj *d, int x, int y, const uint16_t *s, int n)
{
    GObj *bm = G(d->bitmap, G_BITMAP), *f = dc_font(d);
    if (!bm || !bm->px || !f || n <= 0) return;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cg = CGBitmapContextCreate(bm->px, (size_t)bm->bw, (size_t)bm->bh, 8, (size_t)bm->bw * 4, cs, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    CGContextTranslateCTM(cg, 0, bm->bh); CGContextScaleCTM(cg, 1, -1);                 /* top-down like GDI */
    CGContextSetShouldSmoothFonts(cg, false);
    uint32_t tc = d->text_color;
    CGFloat comps[4] = {(tc & 0xff) / 255.0, ((tc >> 8) & 0xff) / 255.0, ((tc >> 16) & 0xff) / 255.0, 1};
    CFStringRef str = CFStringCreateWithCharacters(0, s, n);
    CGColorRef col = CGColorCreateGenericRGB(comps[0], comps[1], comps[2], 1);
    const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName}; const void *vals[] = {f->font, col};
    CFDictionaryRef attrs = CFDictionaryCreate(0, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(0, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    if (d->bk_mode == 2) {                                                               /* OPAQUE */
        uint32_t bc = d->bk_color; double w = CTLineGetTypographicBounds(line, 0, 0, 0);
        CGContextSetRGBFillColor(cg, (bc & 0xff) / 255.0, ((bc >> 8) & 0xff) / 255.0, ((bc >> 16) & 0xff) / 255.0, 1);
        CGContextFillRect(cg, CGRectMake(x, y, w, f->ascent + f->descent));
    }
    CGContextSetTextMatrix(cg, CGAffineTransformMakeScale(1, -1));
    CGContextSetTextPosition(cg, x, y + f->ascent);
    CTLineDraw(line, cg);
    CFRelease(line); CFRelease(as); CFRelease(attrs); CGColorRelease(col); CFRelease(str); CGContextRelease(cg);
}
IMPL(gdi32, ExtTextOutA)
{
    GObj *d = dc_of(ARG(0)); uint32_t s = ARG(5); int n = (int)ARG(6);
    if (d && s && n > 0) { uint16_t w[1024]; if (n > 1024) n = 1024; w32_mb_to_wide(1252, (const uint8_t *)GP(s), n, w, n); draw_text(d, (int)ARG(1), (int)ARG(2), w, n); }
    RET(1, 8);
}
IMPL(gdi32, TextOutW)
{
    GObj *d = dc_of(ARG(0)); int n = (int)ARG(4);
    if (d && n > 0) draw_text(d, (int)ARG(1), (int)ARG(2), GW(ARG(3)), n);
    RET(1, 5);
}
IMPL(gdi32, GetPixel)
{
    GObj *d = dc_of(ARG(0)); GObj *bm = d ? G(d->bitmap, G_BITMAP) : 0; int x = (int)ARG(1), y = (int)ARG(2);
    if (!bm || !bm->px || x < 0 || y < 0 || x >= bm->bw || y >= bm->bh) RET(0xffffffffu, 3);
    uint32_t p = bm->px[(size_t)y * bm->bw + x];                                         /* BGRX in memory */
    RET(((p >> 16) & 0xff) | (p & 0xff00) | ((p & 0xff) << 16), 3);                      /* COLORREF 0x00BBGGRR */
}
IMPL(gdi32, GetDIBits)
{
    GObj *bm = G(ARG(1), G_BITMAP); uint32_t start = ARG(2), lines = ARG(3), bits = ARG(4), bi = ARG(5);
    if (!bm || !bm->px) RET(0, 7);
    int32_t h = (int32_t)rt_r32(G_MEM, bi + 8); uint16_t bpp = (uint16_t)rt_r16(G_MEM, bi + 14);
    if (!bits) {                                                                          /* fill in the header */
        rt_w32(G_MEM, bi + 4, (uint32_t)bm->bw); rt_w32(G_MEM, bi + 8, (uint32_t)bm->bh); rt_w16(G_MEM, bi + 12, 1);
        rt_w16(G_MEM, bi + 14, 32); rt_w32(G_MEM, bi + 16, 0); rt_w32(G_MEM, bi + 20, (uint32_t)(bm->bw * bm->bh * 4));
        RET((uint32_t)bm->bh, 7);
    }
    if (bpp != 32 && bpp != 24) { fprintf(stderr, "w32: GetDIBits %u bpp not supported\n", bpp); RET(0, 7); }
    int stride = ((bm->bw * (bpp / 8)) + 3) & ~3, topdown = h < 0;
    for (uint32_t k = 0; k < lines && start + k < (uint32_t)bm->bh; k++) {
        int row = topdown ? (int)(start + k) : bm->bh - 1 - (int)(start + k);
        uint8_t *dst = (uint8_t *)GP(bits + k * (uint32_t)stride); const uint32_t *src = bm->px + (size_t)row * bm->bw;
        for (int x = 0; x < bm->bw; x++) {
            if (bpp == 32) memcpy(dst + 4 * x, &src[x], 4);
            else { dst[3 * x] = (uint8_t)src[x]; dst[3 * x + 1] = (uint8_t)(src[x] >> 8); dst[3 * x + 2] = (uint8_t)(src[x] >> 16); }
        }
    }
    RET(lines, 7);
}

/* ---- palettes and the rest Diablo II and Smacker import (the game draws through DirectDraw; these keep its GDI
 * set-up calls working: a logical palette is a list of entries, realising it changes nothing on this display) ---- */
IMPL(gdi32, CreatePalette)                             /* (LOGPALETTE *: u16 version, u16 count, entries[count]) */
{
    uint32_t lp = ARG(0); if (!lp) RET(0, 1);
    GObj *o = calloc(1, sizeof *o); o->type = G_PALETTE; o->npal = rt_r16(G_MEM, lp + 2); if (o->npal > 256) o->npal = 256;
    for (int k = 0; k < o->npal; k++) o->pal[k] = rt_r32(G_MEM, lp + 4 + 4 * (uint32_t)k);
    RET(g_new(o), 1);
}
IMPL(gdi32, SetPaletteEntries)                         /* (pal, start, count, entries) */
{
    GObj *o = G(ARG(0), G_PALETTE); uint32_t st = ARG(1), n = ARG(2), e = ARG(3);
    if (!o || st >= 256) RET(0, 4);
    if (st + n > 256) n = 256 - st;
    for (uint32_t k = 0; k < n; k++) o->pal[st + k] = rt_r32(G_MEM, e + 4 * k);
    if ((int)(st + n) > o->npal) o->npal = (int)(st + n);
    RET(n, 4);
}
IMPL(gdi32, SelectPalette)                             /* (dc, pal, background) -> the previous one */
{
    GObj *d = dc_of(ARG(0)); if (!d || !G(ARG(1), G_PALETTE)) RET(0, 3);
    uint32_t old = d->palsel ? d->palsel : 0x00b8000fu; d->palsel = ARG(1); RET(old, 3);
}
IMPL(gdi32, RealizePalette) { RET(0, 1); }             /* no entries change on a true-colour display */
IMPL(gdi32, GetSystemPaletteUse) { RET(1, 1); }        /* SYSPAL_STATIC */
IMPL(gdi32, GetSystemPaletteEntries)                   /* (dc, start, count, out): the 20 static colours, black elsewhere */
{
    static const uint32_t stat[20] = {0x000000, 0x000080, 0x008000, 0x008080, 0x800000, 0x800080, 0x808000, 0xc0c0c0, 0xc0dcc0, 0xf0caa6,
                                      0xf0fbff, 0xa4a0a0, 0x808080, 0x0000ff, 0x00ff00, 0x00ffff, 0xff0000, 0xff00ff, 0xffff00, 0xffffff};
    uint32_t st = ARG(1), n = ARG(2), out = ARG(3);
    if (!out) RET(256, 4);
    for (uint32_t k = 0; k < n && st + k < 256; k++) {
        uint32_t i = st + k, v = i < 10 ? stat[i] : i >= 246 ? stat[i - 236] : 0;       /* 0x00BBGGRR = PALETTEENTRY r, g, b */
        rt_w32(G_MEM, out + 4 * k, v);
    }
    RET(n, 4);
}
IMPL(gdi32, CreateDCA) { GObj *o = calloc(1, sizeof *o); o->type = G_DC; o->bk_color = 0xffffff; o->bk_mode = 2; RET(g_new(o), 4); }   /* ("DISPLAY", ...) */
IMPL(gdi32, CreateBitmap)                              /* (w, h, planes, bpp, bits): a monochrome mask or colour bitmap; contents not kept */
{
    int w = (int)ARG(0), h = (int)ARG(1); if (w <= 0 || h <= 0 || w > 8192 || h > 8192) RET(0, 5);
    GObj *o = calloc(1, sizeof *o); o->type = G_BITMAP; o->bw = w; o->bh = h; o->px = calloc((size_t)w * h, 4);
    RET(g_new(o), 5);
}
IMPL(gdi32, SetDIBColorTable)                          /* (dc, start, count, RGBQUAD colours): the DIB selected in dc */
{
    GObj *d = dc_of(ARG(0)), *bm = d ? G(d->bitmap, G_BITMAP) : 0; uint32_t st = ARG(1), n = ARG(2), in = ARG(3);
    if (!bm || !bm->dib || bm->dib_bpp > 8 || st >= 256) RET(0, 4);
    if (st + n > 256) n = 256 - st;
    for (uint32_t k = 0; k < n; k++) bm->ctab[st + k] = rt_r32(G_MEM, in + 4 * k) & 0xffffff;
    RET(n, 4);
}
IMPL(gdi32, GdiFlush) { RET(1, 0); }
IMPL(gdi32, GdiSetBatchLimit) { RET(20, 1); }
IMPL(gdi32, PatBlt) { RET(1, 6); }
IMPL(gdi32, StretchDIBits)                             /* Smacker's GDI output; the game shows its videos through DirectDraw */
{
    static int once; if (!once++) fprintf(stderr, "w32: gdi32!StretchDIBits: not shown (GDI output)\n");
    RET(ARG(8), 13);                                   /* the number of scan lines */
}

/* ---- DIB sections: Diablo II's windowed mode draws its 8-bit frames into one and copies it to the window ---- */
IMPL(gdi32, CreateDIBSection)                          /* (dc, BITMAPINFO *, usage, void **bits, section, offset) */
{
    uint32_t bi = ARG(1), usage = ARG(2), outp = ARG(3);
    int32_t w = (int32_t)rt_r32(G_MEM, bi + 4), h = (int32_t)rt_r32(G_MEM, bi + 8);
    int bpp = rt_r16(G_MEM, bi + 14); uint32_t comp = rt_r32(G_MEM, bi + 16), hsz = rt_r32(G_MEM, bi), used = rt_r32(G_MEM, bi + 32);
    if (w <= 0 || !h || w > 8192 || (h < 0 ? -h : h) > 8192 || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) || (comp != 0 && comp != 3)) {
        fprintf(stderr, "w32: CreateDIBSection %dx%d %d bpp (compression %u) not supported\n", w, h, bpp, comp);
        if (outp) rt_w32(G_MEM, outp, 0); RET(0, 6);
    }
    GObj *o = calloc(1, sizeof *o); o->type = G_BITMAP; o->bw = w; o->bh = h < 0 ? -h : h;
    o->dib_bpp = bpp; o->dib_topdown = h < 0; o->dib_pitch = ((uint32_t)w * (uint32_t)bpp / 8 + 3) & ~3u;
    o->dib = heap_alloc(w32_process_heap, 8, o->dib_pitch * (uint32_t)o->bh + 16);
    if (bpp == 8) {                                    /* the colour table after the header: RGBQUADs, or palette indices */
        uint32_t n = used && used <= 256 ? used : 256;
        for (uint32_t k = 0; k < n; k++) o->ctab[k] = usage == 0 ? rt_r32(G_MEM, bi + hsz + 4 * k) & 0xffffff : k * 0x010101u;
    }
    if (outp) rt_w32(G_MEM, outp, o->dib);
    RET(g_new(o), 6);
}
/* a DIB's pixels x, y, w, h as BGRA (8-bit through its colour table), into dst (pitch w * 4) */
static void dib_to_bgra(const GObj *bm, int x, int y, int w, int h, uint32_t *dst)
{
    uint32_t lut[256];
    if (bm->dib_bpp == 8) { for (int k = 0; k < 256; k++) lut[k] = 0xff000000u | bm->ctab[k]; void dsr_frame_palette(const uint32_t *); dsr_frame_palette(lut); }
    for (int r = 0; r < h; r++) {
        int sy = y + r, row = bm->dib_topdown ? sy : bm->bh - 1 - sy;
        const uint8_t *src = G_MEM + bm->dib + (uint32_t)row * bm->dib_pitch; uint32_t *o = dst + (size_t)r * (size_t)w;
        if (row < 0 || row >= bm->bh) { memset(o, 0, (size_t)w * 4); continue; }
        switch (bm->dib_bpp) {
        case 8: for (int c = 0; c < w; c++) o[c] = lut[src[x + c]]; break;
        case 16: for (int c = 0; c < w; c++) { uint32_t v = src[2 * (x + c)] | src[2 * (x + c) + 1] << 8;   /* 5-5-5 */
                     o[c] = 0xff000000u | ((v >> 10 & 31) * 255 / 31) << 16 | ((v >> 5 & 31) * 255 / 31) << 8 | (v & 31) * 255 / 31; } break;
        case 24: for (int c = 0; c < w; c++) { const uint8_t *q = src + 3 * (x + c); o[c] = 0xff000000u | q[2] << 16 | q[1] << 8 | q[0]; } break;
        default: for (int c = 0; c < w; c++) { uint32_t v; memcpy(&v, src + 4 * (x + c), 4); o[c] = v | 0xff000000u; } break;
        }
    }
}
uint32_t *dsr_frame_begin(uint32_t w, uint32_t h); void dsr_frame_end(Ctx *c);
uint32_t gdi_frames;                                   /* pictures the game copied to its window */
/* a copy from a memory DC's DIB to the window: what the player sees (the source rectangle; the renderer scales it to
 * the window) */
static int blit(Ctx *c, uint32_t ddc, int dx, int dy, int dw, int dh, uint32_t sdc, int sx, int sy, int sw, int sh)
{
    GObj *s = dc_of(sdc), *bm = s ? G(s->bitmap, G_BITMAP) : 0;
    (void)dx; (void)dy; (void)dw; (void)dh;
    if (ddc != 0x20004 && (!dc_of(ddc) || dc_of(ddc) != G(screen_dc, G_DC))) {   /* between memory DCs */
        GObj *d = dc_of(ddc), *db = d ? G(d->bitmap, G_BITMAP) : 0;
        if (!bm || !db || !bm->dib || !db->dib || bm->dib_bpp != db->dib_bpp || sw != dw || sh != dh) return 1;
        uint32_t bp = (uint32_t)bm->dib_bpp / 8;
        for (int r = 0; r < sh; r++) {
            int srow = bm->dib_topdown ? sy + r : bm->bh - 1 - (sy + r), drow = db->dib_topdown ? dy + r : db->bh - 1 - (dy + r);
            if (srow < 0 || srow >= bm->bh || drow < 0 || drow >= db->bh) continue;
            memmove(G_MEM + db->dib + (uint32_t)drow * db->dib_pitch + (uint32_t)dx * bp, G_MEM + bm->dib + (uint32_t)srow * bm->dib_pitch + (uint32_t)sx * bp, (size_t)sw * bp);
        }
        return 1;
    }
    if (!bm || !bm->dib || sw <= 0 || sh <= 0) return 1;
    if (sx < 0) sx = 0; if (sy < 0) sy = 0;
    if (sx + sw > bm->bw) sw = bm->bw - sx; if (sy + sh > bm->bh) sh = bm->bh - sy;
    if (sw <= 0 || sh <= 0) return 1;
    uint32_t *px = dsr_frame_begin((uint32_t)sw, (uint32_t)sh);
    dib_to_bgra(bm, sx, sy, sw, sh, px);
    gdi_frames++;
    dsr_frame_end(c);
    return 1;
}
IMPL(gdi32, StretchBlt)                                /* (dst dc, x, y, w, h, src dc, x, y, w, h, rop) */
{
    RET(blit(c, ARG(0), (int)ARG(1), (int)ARG(2), (int)ARG(3), (int)ARG(4), ARG(5), (int)ARG(6), (int)ARG(7), (int)ARG(8), (int)ARG(9)), 11);
}
IMPL(gdi32, BitBlt)                                    /* (dst dc, x, y, w, h, src dc, x, y, rop) */
{
    RET(blit(c, ARG(0), (int)ARG(1), (int)ARG(2), (int)ARG(3), (int)ARG(4), ARG(5), (int)ARG(6), (int)ARG(7), (int)ARG(3), (int)ARG(4)), 9);
}
