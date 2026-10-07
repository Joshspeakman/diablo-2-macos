/* present_shader_test: the Metal renderer's present pass (src/renderer: renderer.m + shaders.metal) drawing a known
 * picture into an offscreen drawable, checked pixel by pixel for each scaling filter and the gamma ramp.
 *   clang -fobjc-arc -framework Metal -framework Foundation -I src/renderer recomp/tests/present_shader_test.m src/renderer/renderer.m -o /tmp/pst
 *   (run from src/renderer, or with shaders.metal in the current folder) */
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <math.h>
#include "renderer.h"
#include "../dsr/dsr_proto.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
enum { SW = 4, SH = 3 };
static const uint32_t pic[SH][SW] = {                      /* BGRA in memory: 0xAARRGGBB */
    {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff},
    {0xff000000, 0xff808080, 0xffffff00, 0xff00ffff},
    {0xffff00ff, 0xff102030, 0xffc08040, 0xff406080},
};
static void exec(DSRRenderer *r, uint32_t op, const void *p, uint32_t n) { dsr_renderer_exec(r, op, p, n); }
static void setup(DSRRenderer *r)
{
    uint32_t cr[10] = {1, SW, SH, 4, 2, 0, 0, 0, 0, 0}; exec(r, DSR_SURFACE_CREATE, cr, sizeof cr);
    uint32_t up[6 + SW * SH] = {1, 0, 0, SW, SH, SW * 4}; memcpy(up + 6, pic, sizeof pic); exec(r, DSR_SURFACE_UPLOAD, up, sizeof up);
    uint32_t pr[1] = {1}; exec(r, DSR_PRESENT, pr, sizeof pr);
}
/* the present pass into a w x h drawable; pixels back as 0xAARRGGBB */
static uint32_t *draw(id<MTLDevice> dev, DSRRenderer *r, int w, int h)
{
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; d.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d];
    (void)dsr_renderer_present_to(r, t); dsr_renderer_flush(r, 1);       /* the renderer commits; wait for the GPU */
    uint32_t *px = malloc((size_t)w * h * 4);
    [t getBytes:px bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    return px;
}
static int ch(uint32_t c, int k) { return (int)(c >> (8 * k)) & 0xff; }
static int near(uint32_t a, uint32_t b, int tol) { for (int k = 0; k < 3; k++) if (abs(ch(a, k) - ch(b, k)) > tol) return 0; return 1; }
static int is_source(uint32_t c) { for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) if (near(c, pic[y][x], 1)) return 1; return 0; }

int main(void)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); if (!dev) { printf("SKIP: no Metal device\n"); return 0; }
        DSRRenderer *r = dsr_renderer_create(dev); setup(r);
        /* 1. pixel, whole-number scale (x4): every drawable pixel is its source texel exactly */
        dsr_renderer_set_filter(r, 0);
        uint32_t *p = draw(dev, r, SW * 4, SH * 4);
        for (int y = 0; y < SH * 4; y++) for (int x = 0; x < SW * 4; x++)
            CHECK(near(p[y * SW * 4 + x], pic[y / 4][x / 4], 1), "pixel x4 at %d,%d: %08x, expected %08x", x, y, p[y * SW * 4 + x], pic[y / 4][x / 4]);
        free(p);
        /* 2. sharp at a whole-number scale is the same as pixel */
        dsr_renderer_set_filter(r, 2);
        p = draw(dev, r, SW * 4, SH * 4);
        for (int y = 0; y < SH * 4; y++) for (int x = 0; x < SW * 4; x++)
            CHECK(near(p[y * SW * 4 + x], pic[y / 4][x / 4], 1), "sharp x4 at %d,%d: %08x, expected %08x", x, y, p[y * SW * 4 + x], pic[y / 4][x / 4]);
        free(p);
        /* 3. sharp at x4.5 (18 x 13.5 -> 18 x 14 drawable): texel centres exact; blended pixels only at texel edges, at
         *    most one per edge per row, so every column is a source colour except those few */
        int W = 18, H = 14; p = draw(dev, r, W, H);
        int blends = 0, rows_checked = 0;
        for (int y = 0; y < H; y++) {
            int sy = (int)((y + 0.5) * SH / H); double fy = (y + 0.5) * SH / H - sy; if (fy < 0.25 || fy > 0.75) continue;   /* a row away from texel edges */
            rows_checked++; int row_blends = 0;
            for (int x = 0; x < W; x++) {
                int sx = (int)((x + 0.5) * SW / W); double fx = (x + 0.5) * SW / W - sx;
                if (fx > 0.3 && fx < 0.7) CHECK(near(p[y * W + x], pic[sy][sx], 2), "sharp x4.5 centre %d,%d: %08x vs %08x", x, y, p[y * W + x], pic[sy][sx]);
                if (!is_source(p[y * W + x])) {
                    /* the blend band is 1/4 texel (1.125 px here) centred on a texel edge: a blended pixel's centre
                     * lies within about one pixel of an inner edge, never inside a texel */
                    double ex = (x + 0.5) * SW / W, d = fabs(ex - lround(ex)) * W / SW;
                    CHECK(lround(ex) > 0 && lround(ex) < SW && d <= 1.1, "sharp x4.5: blended pixel %d,%d is %.2f px from a texel edge", x, y, d);
                    row_blends++;
                }
            }
            CHECK(row_blends <= 2 * (SW - 1), "sharp x4.5 row %d has %d blended pixels (at most two per inner edge)", y, row_blends);
            blends += row_blends;
        }
        CHECK(rows_checked > 0, "no rows checked");
        free(p);
        /* 4. smooth (bilinear) at x4.5 blends inside texels too: more non-source pixels than sharp */
        dsr_renderer_set_filter(r, 1); p = draw(dev, r, W, H);
        int sblends = 0; for (int i = 0; i < W * H; i++) if (!is_source(p[i])) sblends++;
        CHECK(sblends > blends + W, "smooth: %d blended pixels, sharp %d (bilinear should blend much more)", sblends, blends);
        free(p);
        /* 5. crt (x6, 24 x 18): a texel row's centre line keeps the colour, its edge lines are darker (about 0.62) */
        dsr_renderer_set_filter(r, 3); p = draw(dev, r, SW * 6, SH * 6);
        for (int ty = 0; ty < SH; ty++) {
            uint32_t mid = p[(ty * 6 + 3) * SW * 6 + 1], edge = p[(ty * 6) * SW * 6 + 1]; int src = ch(pic[ty][0], 2) + ch(pic[ty][0], 1) + ch(pic[ty][0], 0);
            if (src < 60) continue;
            int m = ch(mid, 2) + ch(mid, 1) + ch(mid, 0), e = ch(edge, 2) + ch(edge, 1) + ch(edge, 0);
            CHECK(abs(m - src) <= 12, "crt row %d centre %d, source %d", ty, m, src);
            CHECK(e < m * 0.8 && e > m * 0.5, "crt row %d edge %d vs centre %d (expected about 0.62)", ty, e, m);
        }
        free(p);
        /* 6. gamma ramp: an inverting ramp turns each channel v into 255 - v */
        uint16_t ramp[3][256]; for (int k = 0; k < 256; k++) ramp[0][k] = ramp[1][k] = ramp[2][k] = (uint16_t)((255 - k) * 0x101);
        exec(r, DSR_GAMMA, ramp, sizeof ramp); dsr_renderer_set_filter(r, 0);
        p = draw(dev, r, SW, SH);
        for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) {
            uint32_t e = pic[y][x] ^ 0x00ffffff;
            CHECK(near(p[y * SW + x], e, 1), "gamma at %d,%d: %08x, expected %08x", x, y, p[y * SW + x], e);
        }
        free(p);
        printf("%s: present shader (pixel, sharp, smooth, crt, gamma)\n", fails ? "FAILED" : "ok");
    }
    return fails != 0;
}
