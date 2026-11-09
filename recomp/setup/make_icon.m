// make_icon <label> <r> <g> <b> <out.png>: a plain app icon. The app can't carry the game's art until the player's own copy is set up.
#import <Cocoa/Cocoa.h>
int main(int c, char **v) { @autoreleasepool {
    CGFloat s = 1024, r = atof(v[2]), g = atof(v[3]), b = atof(v[4]);
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:s pixelsHigh:s bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState]; NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(100, 100, 824, 824) xRadius:186 yRadius:186];
    NSColor *top = [NSColor colorWithCalibratedRed:MIN(1, r * 1.5) green:MIN(1, g * 1.5) blue:MIN(1, b * 1.5) alpha:1], *low = [NSColor colorWithCalibratedRed:r * 0.45 green:g * 0.45 blue:b * 0.45 alpha:1];
    [[[NSGradient alloc] initWithStartingColor:top endingColor:low] drawInBezierPath:p angle:-90];
    NSString *t = @(v[1]); CGFloat size = t.length > 2 ? 330 : 420; NSMutableParagraphStyle *st = [NSMutableParagraphStyle new]; st.alignment = NSTextAlignmentCenter;
    NSDictionary *a = @{NSFontAttributeName: [NSFont systemFontOfSize:size weight:NSFontWeightHeavy], NSForegroundColorAttributeName: NSColor.whiteColor, NSParagraphStyleAttributeName: st};
    CGFloat h = [t sizeWithAttributes:a].height; [t drawInRect:NSMakeRect(100, 512 - h / 2, 824, h + 4) withAttributes:a];
    [NSGraphicsContext restoreGraphicsState];
    return [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(v[5]) atomically:YES] ? 0 : 1;
} }
