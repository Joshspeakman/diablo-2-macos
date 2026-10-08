// d2_icon <logo.png> <backdrop.png> <out.png> [--nightly] [--ii]: the app's icon (install.sh), drawn from the art
// d2art.py decodes from the player's own archives at install time: the main menu's backdrop, darkened, on a rounded
// tile, and the Diablo II logo over it (--ii: only its flaming "II", close up). The game's own icon is a 32x32,
// 16-colour Windows icon, too small for the Dock. --nightly adds a "NIGHTLY" band, as tools/nightly_icon.m does.
// Build: clang -fobjc-arc -framework AppKit
#import <AppKit/AppKit.h>

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 4) { fprintf(stderr, "usage: d2_icon <logo.png> <backdrop.png> <out.png> [--nightly] [--ii]\n"); return 2; }
        BOOL nightly = NO, whole = YES;
        for (int k = 4; k < argc; k++) { if (!strcmp(argv[k], "--nightly")) nightly = YES; else if (!strcmp(argv[k], "--ii")) whole = NO; }
        NSImage *logo = [[NSImage alloc] initWithContentsOfFile:@(argv[1])], *back = [[NSImage alloc] initWithContentsOfFile:@(argv[2])];
        if (!logo || logo.size.width < 100) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
        const CGFloat S = 1024;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)S pixelsHigh:(NSInteger)S
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;

        // the tile: macOS app-icon proportions (824 of 1024, corner radius ~185)
        NSRect tile = NSMakeRect(100, 100, 824, 824);
        NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:tile xRadius:185 yRadius:185];
        NSShadow *drop = [NSShadow new]; drop.shadowColor = [NSColor colorWithWhite:0 alpha:0.45]; drop.shadowOffset = NSMakeSize(0, -10); drop.shadowBlurRadius = 24;
        [NSGraphicsContext saveGraphicsState]; [drop set]; [[NSColor blackColor] setFill]; [shape fill]; [NSGraphicsContext restoreGraphicsState];
        [NSGraphicsContext saveGraphicsState];
        [shape addClip];
        [[NSColor colorWithSRGBRed:0.05 green:0.02 blue:0.02 alpha:1] setFill]; NSRectFill(tile);
        if (back.size.width > 100) {   // the backdrop's lower half (the town; its top holds the menu's own logo), dimmed
            CGFloat side = back.size.height * 0.58;
            [back drawInRect:tile fromRect:NSMakeRect((back.size.width - side) / 2, 0, side, side)
                   operation:NSCompositingOperationSourceOver fraction:0.55];
        }
        NSGradient *shade = [[NSGradient alloc] initWithColors:@[[NSColor colorWithWhite:0 alpha:0.15], [NSColor colorWithWhite:0 alpha:0.75]]];
        [shade drawInRect:tile relativeCenterPosition:NSMakePoint(0, 0.1)];
        NSGradient *ember = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:0.95 green:0.35 blue:0.05 alpha:0.45]
                                                          endingColor:[NSColor colorWithSRGBRed:0.60 green:0.08 blue:0.02 alpha:0]];
        CGFloat top = nightly ? 285 : 100;   // the art sits above the band
        [ember drawInRect:NSMakeRect(160, top + 40, 704, 704) relativeCenterPosition:NSMakePoint(0, -0.2)];

        NSImage *art = logo; NSRect from = NSZeroRect;
        if (!whole) {   // the "II": the middle of the logo, a square as tall as the logo (its sides faded into the tile below)
            CGFloat h = logo.size.height, cx = logo.size.width * 0.505;
            from = NSMakeRect(cx - h / 2, 0, h, h);
        }
        NSSize fs = whole ? logo.size : from.size;
        CGFloat avail = 824 - (top - 100) - 60, scale = MIN(720 / fs.width, avail / fs.height);
        NSRect to = NSMakeRect(512 - fs.width * scale / 2, top + (824 - (top - 100) - fs.height * scale) / 2, fs.width * scale, fs.height * scale);
        // drawn into its own layer so the sides can be faded out (the neighbouring letters of the logo)
        NSImage *layer = [[NSImage alloc] initWithSize:to.size];
        [layer lockFocus];
        NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;
        [art drawInRect:NSMakeRect(0, 0, to.size.width, to.size.height) fromRect:from operation:NSCompositingOperationCopy fraction:1];
        if (!whole) {
            NSGradient *fade = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:1], 0.0, [NSColor colorWithWhite:0 alpha:0], 0.16,
                                                                             [NSColor colorWithWhite:0 alpha:0], 0.84, [NSColor colorWithWhite:0 alpha:1], 1.0, nil];
            [NSGraphicsContext.currentContext setCompositingOperation:NSCompositingOperationDestinationOut];
            [fade drawInRect:NSMakeRect(0, 0, to.size.width, to.size.height) angle:0];
        }
        [layer unlockFocus];
        NSShadow *glow = [NSShadow new]; glow.shadowColor = [NSColor colorWithSRGBRed:1 green:0.45 blue:0.05 alpha:0.55]; glow.shadowOffset = NSZeroSize; glow.shadowBlurRadius = 40;
        [NSGraphicsContext saveGraphicsState]; [glow set];
        [layer drawInRect:to fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
        [NSGraphicsContext restoreGraphicsState];

        if (nightly) {
            NSRect band = NSMakeRect(100, 100, 824, 185);
            NSGradient *bandFill = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:0.36 green:0.20 blue:0.78 alpha:1]
                                                                 endingColor:[NSColor colorWithSRGBRed:0.20 green:0.10 blue:0.52 alpha:1]];
            [bandFill drawInRect:band angle:-90];
            NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.alignment = NSTextAlignmentCenter;
            NSShadow *ts = [NSShadow new]; ts.shadowColor = [NSColor colorWithWhite:0 alpha:0.6]; ts.shadowOffset = NSMakeSize(0, -3); ts.shadowBlurRadius = 6;
            NSDictionary *attrs = @{NSFontAttributeName: [NSFont systemFontOfSize:104 weight:NSFontWeightHeavy], NSForegroundColorAttributeName: NSColor.whiteColor,
                                    NSParagraphStyleAttributeName: ps, NSKernAttributeName: @8, NSShadowAttributeName: ts};
            NSString *label = @"NIGHTLY"; NSSize ls = [label sizeWithAttributes:attrs];
            [label drawInRect:NSMakeRect(100, NSMidY(band) + 12 - ls.height / 2, 824, ls.height) withAttributes:attrs];
        }
        [NSGraphicsContext restoreGraphicsState];
        // a thin rim so the tile reads on dark backgrounds too
        [(nightly ? [NSColor colorWithSRGBRed:0.55 green:0.50 blue:1.0 alpha:0.35] : [NSColor colorWithSRGBRed:0.85 green:0.45 blue:0.15 alpha:0.35]) setStroke];
        shape.lineWidth = 6; [shape stroke];
        [NSGraphicsContext restoreGraphicsState];
        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![png writeToFile:@(argv[3]) atomically:YES]) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    }
    return 0;
}
