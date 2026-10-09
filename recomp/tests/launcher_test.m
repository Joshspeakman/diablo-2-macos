// The launch window: its settings reach the runtime (D2_RENDERER, D2_PERSPECTIVE, D2_RESOLUTION, D2_TEXTURES, D2_FPS, D2_WIDTH, DSR_FILTER,
// DS_NOTCH) and are remembered in
// launcher.plist; it draws with the game's art from <data>/art (or without it) at the expected size.
//   clang -fobjc-arc -framework AppKit -framework Metal -framework ImageIO -lz recomp/tests/launcher_test.m -o /tmp/launcher_test && /tmp/launcher_test [art folder]
#include "../host/launcher.m"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static NSString *env(const char *k) { const char *v = getenv(k); return v ? @(v) : nil; }

int main(int argc, char **argv)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID UUID].UUIDString];
        [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        /* no saved settings: nothing is applied (the runtime's defaults: DirectDraw, sharp) */
        unsetenv("D2_RENDERER"); unsetenv("D2_PERSPECTIVE"); unsetenv("DSR_FILTER"); unsetenv("DS_NOTCH"); setenv("DS_NO_LAUNCHER", "1", 1);
        CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 1);
        CHECK(env("D2_RENDERER") == nil && env("D2_PERSPECTIVE") == nil && env("DSR_FILTER") == nil);
        /* saved settings reach the runtime as environment */
        [@{@"video": @"gdi", @"scaling": @"crt", @"notch": @"around"} writeToFile:[dir stringByAppendingPathComponent:@"launcher.plist"] atomically:YES];
        CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 1);
        CHECK([env("D2_RENDERER") isEqualToString:@"gdi"]); CHECK([env("DSR_FILTER") isEqualToString:@"crt"]); CHECK([env("DS_NOTCH") isEqualToString:@"around"]);
        CHECK(env("D2_PERSPECTIVE") == nil);                                   /* no view saved: the game's choice */
        /* Direct3D with each view: Isometric and Perspective are set at start, the game's choice leaves its option alone */
        for (NSString *view in @[@"off", @"on", @"game"]) {
            [@{@"video": @"d3d", @"view": view, @"scaling": @"sharp", @"notch": @"hide"} writeToFile:[dir stringByAppendingPathComponent:@"launcher.plist"] atomically:YES];
            CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 1);
            CHECK([env("D2_RENDERER") isEqualToString:@"d3d"]);
            CHECK([view isEqualToString:@"game"] ? env("D2_PERSPECTIVE") == nil : [env("D2_PERSPECTIVE") isEqualToString:view]);
        }
        /* Direct3D's picture when none of it was saved: the most of each (the display's resolution and refresh rate, smooth
         * textures, the display's width, zoom out to 2x) */
        CHECK([env("D2_RESOLUTION") isEqualToString:@"display"] && [env("D2_TEXTURES") isEqualToString:@"smooth"] && [env("D2_FPS") isEqualToString:@"display"]
              && [env("D2_WIDTH") isEqualToString:@"display"] && [env("D2_ZOOM_MAX") isEqualToString:@"2"]);
        [@{@"video": @"d3d", @"view": @"on", @"resolution": @"display", @"textures": @"smooth", @"fps": @"display", @"width": @"display", @"scaling": @"sharp", @"notch": @"hide"}
            writeToFile:[dir stringByAppendingPathComponent:@"launcher.plist"] atomically:YES];
        CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 1);
        CHECK([env("D2_RESOLUTION") isEqualToString:@"display"] && [env("D2_TEXTURES") isEqualToString:@"smooth"] && [env("D2_FPS") isEqualToString:@"display"] && [env("D2_WIDTH") isEqualToString:@"display"]);
        /* Direct3D's settings are left out when it isn't the renderer (the game's own view setting kept) */
        [@{@"video": @"ddraw", @"view": @"on", @"resolution": @"display", @"textures": @"smooth", @"fps": @"120", @"width": @"display", @"scaling": @"sharp", @"notch": @"hide"}
            writeToFile:[dir stringByAppendingPathComponent:@"launcher.plist"] atomically:YES];
        CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 1);
        CHECK([env("D2_RENDERER") isEqualToString:@"ddraw"] && env("D2_PERSPECTIVE") == nil && env("D2_RESOLUTION") == nil && env("D2_TEXTURES") == nil && env("D2_FPS") == nil && env("D2_WIDTH") == nil);
        /* an unknown saved value falls back to the first choice */
        [@{@"video": @"glide", @"scaling": @"hq4x", @"notch": @"hide"} writeToFile:[dir stringByAppendingPathComponent:@"launcher.plist"] atomically:YES];
        CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 1);
        CHECK([env("D2_RENDERER") isEqualToString:@"ddraw"]); CHECK([env("DSR_FILTER") isEqualToString:@"sharp"]);
        /* the window drawn: without art, and with the game's art when a folder is given */
        unsetenv("DS_NO_LAUNCHER");
        for (int pass = 0; pass < (argc > 1 ? 2 : 1); pass++) {
            if (pass) { NSString *art = [dir stringByAppendingPathComponent:@"art"]; [NSFileManager.defaultManager removeItemAtPath:art error:nil];
                        CHECK([NSFileManager.defaultManager copyItemAtPath:@(argv[1]) toPath:art error:nil]); }
            for (int page = -1; page <= 0; page++) {
                NSString *shot = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"launcher-%d-%d.png", pass, page]];
                setenv("DS_LAUNCHER_SHOT", shot.UTF8String, 1);
                if (page >= 0) setenv("DS_LAUNCHER_PAGE", "0", 1); else unsetenv("DS_LAUNCHER_PAGE");
                CHECK(ds_launcher_run("/nonexistent", dir.UTF8String) == 0);
                NSImage *img = [[NSImage alloc] initWithContentsOfFile:shot];
                CHECK(img != nil); CHECK(img.size.width == 780 && img.size.height == 620);
                if (pass && page < 0) {   /* the art is drawn: the window is not the plain fallback colour at the top centre (the logo) */
                    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithData:[NSData dataWithContentsOfFile:shot]];
                    CGFloat px = rep.pixelsWide / 780.0, sum = 0; int n = 0;      /* the logo's area (its flames), averaged */
                    for (int y = 30; y < 160; y += 4) for (int x = 230; x < 550; x += 4) {
                        CGFloat r = 0, g = 0, b = 0, a = 0;
                        [[[rep colorAtX:(NSInteger)(x * px) y:(NSInteger)(y * px)] colorUsingColorSpace:NSColorSpace.sRGBColorSpace] getRed:&r green:&g blue:&b alpha:&a];
                        sum += r + g + b; n++;
                    }
                    CHECK(sum / n > 0.25);
                }
                if (getenv("KEEP_SHOTS")) printf("%s\n", shot.UTF8String);
            }
        }
        if (!getenv("KEEP_SHOTS")) [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
        printf("%s: launcher settings and window\n", fails ? "FAILED" : "ok");
    }
    return fails != 0;
}
