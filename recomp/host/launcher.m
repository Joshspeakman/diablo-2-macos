// The launch window: the video mode, the picture's scaling and updates, remembered in <data>/launcher.plist, then Play.
// Its look comes from the game itself: the main menu's backdrop and the Diablo II logo, which install.sh decodes from
// the player's own archives into <data>/art (tools/d2art.py). Nothing from the game is stored in this project; without
// them the window falls back to plain colours and a drawn title.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#include <CommonCrypto/CommonDigest.h>
#include <zlib.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

// ---------------------------------------------------------------- settings
typedef struct { NSString *value, *label, *note; } Choice;
@interface DSRow : NSObject
@property NSString *title; @property NSArray<NSDictionary *> *choices; @property NSInteger index;
@property (copy) void (^activate)(void);     /* a row that opens something instead of cycling (Mods) */
@end
@implementation DSRow @end

static NSColor *gold(void) { return [NSColor colorWithSRGBRed:0.86 green:0.77 blue:0.55 alpha:1]; }
static NSColor *parchment(void) { return [NSColor colorWithSRGBRed:0.94 green:0.91 blue:0.84 alpha:1]; }
static NSColor *dim(void) { return [NSColor colorWithSRGBRed:0.66 green:0.61 blue:0.52 alpha:1]; }
static NSFont *cp(CGFloat size, BOOL bold)
{
    NSFont *f = [NSFont fontWithName:bold ? @"Copperplate-Bold" : @"Copperplate-Light" size:size] ?: [NSFont fontWithName:@"Copperplate" size:size];
    return f ?: [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
}

// ---------------------------------------------------------------- the view
/* The window: the banner, the Game row, and a button for each group of settings (Display, Graphics, Mods, Updates).
 * A group's button opens it as a panel over the window, with Back where Quit was; Mods and Updates open their own. */
@interface DSSection : NSObject
@property NSString *title; @property NSArray<DSRow *> *rows;
@property (copy) void (^activate)(void); @property (copy) NSString *(^summary)(void);
@end
@implementation DSSection @end

@interface DSLaunchView : NSView
@property NSArray<DSRow *> *top; @property NSArray<DSSection *> *sections; @property NSInteger open;   /* the open group, -1 none */
@property NSInteger focus; @property NSInteger hover; @property NSInteger pressed;
@property NSImage *banner, *stone, *logo, *wood, *woodHover, *woodDown; @property NSString *note;
@property (copy) void (^onPlay)(void); @property (copy) void (^onQuit)(void); @property (copy) void (^onChange)(void);
@end

enum { HIT_NONE = -1, HIT_PLAY = 100, HIT_QUIT = 101, HIT_BACK = 102, HIT_SECTION = 200 };   /* rows: 10*row + 0 (left arrow) / 1 (value) / 2 (right arrow) */
enum { BANNER_H = 252, TOP_Y = 270 };

@implementation DSLaunchView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (NSArray<DSRow *> *)rows { return self.open >= 0 ? self.sections[self.open].rows : self.top; }
- (NSInteger)items { return (NSInteger)self.rows.count + (self.open >= 0 ? 0 : (NSInteger)self.sections.count); }   /* what the keyboard moves between */
- (NSRect)flyRect   /* an open group's panel: above Back and Play, taller (reaching up into the banner) when its rows need it */
{
    NSRect b = self.bounds; CGFloat h = b.size.height - 268, need = 80 + 74 * (CGFloat)(self.open >= 0 ? self.sections[self.open].rows.count : 0);
    CGFloat grow = MIN(MAX(need - h, 0), 168 - 16);            /* as far up as its rows need, short of the window's top */
    return NSMakeRect(40, 168 - grow, b.size.width - 80, h + grow);
}
- (NSRect)rowRect:(NSInteger)k { CGFloat y = self.open >= 0 ? self.flyRect.origin.y + 70 : TOP_Y + 14; return NSMakeRect(70, y + 74 * k, self.bounds.size.width - 140, 64); }
- (NSRect)arrow:(NSInteger)k right:(BOOL)right { NSRect r = [self rowRect:k]; return NSMakeRect(right ? NSMaxX(r) - 34 : NSMaxX(r) - 330, r.origin.y + 6, 30, 30); }
- (NSRect)valueRect:(NSInteger)k { NSRect r = [self rowRect:k]; return NSMakeRect(NSMaxX(r) - 300, r.origin.y + 9, 262, 30); }
- (NSRect)sectionRect:(NSInteger)i
{
    NSInteger n = (NSInteger)self.sections.count, per = n == 3 ? 3 : 2, line = i / per, inLine = MIN(per, n - line * per), col = i % per;
    CGFloat w = per == 3 ? 220 : 290, g = per == 3 ? 20 : 28, x0 = (self.bounds.size.width - (inLine * w + (inLine - 1) * g)) / 2;
    return NSMakeRect(x0 + col * (w + g), TOP_Y + 74 * self.top.count + 44 + line * 100, w, 58);
}
- (NSRect)playRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) + 12, b.size.height - 84, 220, 56); }
- (NSRect)quitRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) - 232, b.size.height - 84, 220, 56); }
- (NSInteger)hit:(NSPoint)p
{
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    if (self.open < 0) for (NSInteger i = 0; i < (NSInteger)self.sections.count; i++) if (NSPointInRect(p, [self sectionRect:i])) return HIT_SECTION + i;
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        if (NSPointInRect(p, [self arrow:k right:NO])) return 10 * k;
        if (NSPointInRect(p, [self arrow:k right:YES])) return 10 * k + 2;
        if (NSPointInRect(p, [self valueRect:k])) return 10 * k + 1;
    }
    if (self.open >= 0 && !NSPointInRect(p, self.flyRect)) return HIT_BACK;   /* a click outside the panel closes it */
    return HIT_NONE;
}
- (void)openSection:(NSInteger)i
{
    DSSection *s = self.sections[i]; self.focus = (NSInteger)self.top.count + i;
    if (s.activate) { s.activate(); self.needsDisplay = YES; return; }
    self.open = i; self.focus = 0; self.hover = HIT_NONE; self.needsDisplay = YES;
}
- (void)closeSection { if (self.open < 0) return; self.focus = (NSInteger)self.top.count + self.open; self.open = -1; self.hover = HIT_NONE; self.needsDisplay = YES; }
- (void)updateTrackingAreas
{
    for (NSTrackingArea *t in self.trackingAreas) [self removeTrackingArea:t];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
- (void)mouseMoved:(NSEvent *)e { NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; if (h != self.hover) { self.hover = h; self.needsDisplay = YES; } }
- (void)mouseDown:(NSEvent *)e { self.pressed = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; self.needsDisplay = YES; }
- (void)mouseUp:(NSEvent *)e
{
    NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]], p = self.pressed; self.pressed = HIT_NONE; self.needsDisplay = YES;
    if (h != p || h == HIT_NONE) return;
    if (h == HIT_PLAY) { if (self.onPlay) self.onPlay(); return; }
    if (h == HIT_QUIT) { if (self.onQuit) self.onQuit(); return; }
    if (h == HIT_BACK) { [self closeSection]; return; }
    if (h >= HIT_SECTION) { [self openSection:h - HIT_SECTION]; return; }
    NSInteger k = h / 10; self.focus = k;
    if (self.rows[k].activate) { self.rows[k].activate(); self.needsDisplay = YES; return; }
    [self step:k by:(h % 10 == 0) ? -1 : 1];
}
- (void)step:(NSInteger)k by:(NSInteger)dir
{
    if (k >= (NSInteger)self.rows.count) { NSInteger n = [self items]; self.focus = (self.focus + dir + n) % n; self.needsDisplay = YES; return; }   /* the group buttons */
    DSRow *r = self.rows[k]; NSInteger n = (NSInteger)r.choices.count;
    if (r.activate) { r.activate(); self.needsDisplay = YES; return; }
    r.index = (r.index + dir + n) % n; self.needsDisplay = YES;
    if (self.onChange) self.onChange();
}
- (void)keyDown:(NSEvent *)e
{
    NSInteger n = [self items], rc = (NSInteger)self.rows.count;
    switch (e.keyCode) {
    case 0x24: case 0x4c:                                                /* Return, Enter */
        if (self.focus >= rc) { [self openSection:self.focus - rc]; return; }
        if (self.rows[self.focus].activate) { self.rows[self.focus].activate(); self.needsDisplay = YES; return; }
        if (self.open >= 0) { [self closeSection]; return; }
        if (self.onPlay) self.onPlay(); return;
    case 0x35: if (self.open >= 0) { [self closeSection]; return; }      /* Escape */
               if (self.onQuit) self.onQuit(); return;
    case 0x7e: self.focus = (self.focus + n - 1) % n; self.needsDisplay = YES; return;
    case 0x7d: self.focus = (self.focus + 1) % n; self.needsDisplay = YES; return;
    case 0x7b: [self step:self.focus by:-1]; return;
    case 0x7c: [self step:self.focus by:1]; return;
    }
    [super keyDown:e];
}
static void draw_text_mode(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, NSShadow *sh, NSLineBreakMode mode)
{
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.alignment = al; ps.lineBreakMode = mode;
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: ps} mutableCopy];
    if (sh) a[NSShadowAttributeName] = sh;
    [s drawInRect:r withAttributes:a];
}
static void draw_text(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, NSShadow *sh) { draw_text_mode(s, f, c, r, al, sh, NSLineBreakByTruncatingTail); }
- (void)drawTitle:(NSRect)r
{
    /* "DIABLO II" when the game's logo isn't there: brushed silver letters, a copper rim, a dark drop shadow */
    NSString *t = @"DIABLO II"; NSFont *f = cp(46, YES);
    NSDictionary *at = @{NSFontAttributeName: f, NSKernAttributeName: @3};
    NSSize sz = [t sizeWithAttributes:at]; NSPoint o = NSMakePoint(NSMidX(r) - sz.width / 2, NSMidY(r) - sz.height / 2 - 2);
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.85]; sh.shadowOffset = NSMakeSize(0, -3); sh.shadowBlurRadius = 5;
    NSMutableDictionary *rim = [at mutableCopy]; rim[NSStrokeColorAttributeName] = [NSColor colorWithSRGBRed:0.62 green:0.33 blue:0.16 alpha:1]; rim[NSStrokeWidthAttributeName] = @(9); rim[NSShadowAttributeName] = sh;
    rim[NSForegroundColorAttributeName] = [NSColor colorWithSRGBRed:0.62 green:0.33 blue:0.16 alpha:1];
    [t drawAtPoint:o withAttributes:rim];
    /* silver fill: the glyphs as a clip, a vertical gradient through it */
    NSImage *mask = [NSImage imageWithSize:sz flipped:NO drawingHandler:^BOOL(NSRect dst) {
        NSMutableDictionary *m = [at mutableCopy]; m[NSForegroundColorAttributeName] = NSColor.whiteColor; [t drawAtPoint:NSZeroPoint withAttributes:m];
        NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0.98 alpha:1], 0.0, [NSColor colorWithWhite:0.72 alpha:1], 0.45,
                         [NSColor colorWithWhite:0.42 alpha:1], 0.5, [NSColor colorWithWhite:0.86 alpha:1], 0.8, [NSColor colorWithWhite:0.6 alpha:1], 1.0, nil];
        [[NSGraphicsContext currentContext] setCompositingOperation:NSCompositingOperationSourceIn];
        [g drawInRect:dst angle:90];
        return YES;
    }];
    [mask drawInRect:NSMakeRect(o.x, o.y, sz.width, sz.height) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
}
- (void)drawButton:(NSRect)r title:(NSString *)title code:(NSInteger)code primary:(BOOL)primary
{
    NSImage *img = self.pressed == code ? (self.woodDown ?: self.wood) : self.hover == code ? (self.woodHover ?: self.wood) : self.wood;
    if (img) [img drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    else {   /* the game's menu buttons: dark stone, a thin gold edge, lighter when pointed at, sunk when pressed */
        CGFloat lift = self.pressed == code ? -0.03 : self.hover == code ? 0.05 : 0;
        NSGradient *g = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:0.30 + lift alpha:1] endingColor:[NSColor colorWithWhite:0.13 + lift alpha:1]];
        NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 1, 1) xRadius:3 yRadius:3];
        [g drawInBezierPath:p angle:self.pressed == code ? 90 : -90];
        [[NSColor colorWithSRGBRed:0.62 green:0.5 blue:0.28 alpha:1] setStroke]; p.lineWidth = 1.5; [p stroke];
        NSBezierPath *in = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 4, 4) xRadius:2 yRadius:2]; in.lineWidth = 1;
        [[NSColor colorWithWhite:0 alpha:0.6] setStroke]; [in stroke];
    }
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; sh.shadowOffset = NSMakeSize(0, -1.5); sh.shadowBlurRadius = 2;
    NSColor *c = self.hover == code ? [NSColor colorWithSRGBRed:1 green:0.93 blue:0.72 alpha:1] : primary ? gold() : parchment();
    /* the capitals centred on the button (the line's own box sits them high: it leaves room below for descenders) */
    NSFont *f = cp(primary ? 24 : 20, YES); NSDictionary *at = @{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSShadowAttributeName: sh};
    NSSize sz = [title sizeWithAttributes:at];
    [title drawAtPoint:NSMakePoint(NSMidX(r) - sz.width / 2, round(NSMidY(r) + f.capHeight / 2 - f.ascender)) withAttributes:at];
}
- (void)drawArrow:(NSRect)r right:(BOOL)right lit:(BOOL)lit
{
    NSBezierPath *p = [NSBezierPath bezierPath]; CGFloat x0 = r.origin.x + 7, x1 = NSMaxX(r) - 7, y0 = r.origin.y + 6, y1 = NSMaxY(r) - 6, ym = NSMidY(r);
    if (right) { [p moveToPoint:NSMakePoint(x0, y0)]; [p lineToPoint:NSMakePoint(x1, ym)]; [p lineToPoint:NSMakePoint(x0, y1)]; }
    else { [p moveToPoint:NSMakePoint(x1, y0)]; [p lineToPoint:NSMakePoint(x0, ym)]; [p lineToPoint:NSMakePoint(x1, y1)]; }
    [p closePath];
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:lit ? 1.0 : 0.88 alpha:1] endingColor:[NSColor colorWithWhite:lit ? 0.7 : 0.45 alpha:1]];
    [g drawInBezierPath:p angle:90];
    [[NSColor colorWithSRGBRed:0.55 green:0.3 blue:0.14 alpha:1] setStroke]; p.lineWidth = 1.5; [p stroke];
}
/* a riveted iron band around a plaque, like the frames of the game's menus */
- (void)drawIronFrame:(NSRect)r
{
    NSBezierPath *outer = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 3, 3)];
    outer.lineWidth = 7; [[NSColor colorWithWhite:0.08 alpha:1] setStroke]; [outer stroke];
    outer.lineWidth = 5; [[NSColor colorWithSRGBRed:0.45 green:0.46 blue:0.47 alpha:1] setStroke]; [outer stroke];
    NSBezierPath *hi = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 1.5, 1.5)]; hi.lineWidth = 1; [[NSColor colorWithWhite:0.78 alpha:0.8] setStroke]; [hi stroke];
    NSBezierPath *lo = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 6, 6)]; lo.lineWidth = 1.2; [[NSColor colorWithWhite:0.05 alpha:0.9] setStroke]; [lo stroke];
    void (^rivet)(CGFloat, CGFloat) = ^(CGFloat x, CGFloat y) {
        NSRect d = NSMakeRect(x - 4, y - 4, 8, 8);
        NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithSRGBRed:0.98 green:0.86 blue:0.55 alpha:1], 0.0,
                         [NSColor colorWithSRGBRed:0.62 green:0.42 blue:0.16 alpha:1], 0.6, [NSColor colorWithSRGBRed:0.25 green:0.15 blue:0.05 alpha:1], 1.0, nil];
        [g drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:d] relativeCenterPosition:NSMakePoint(-0.35, 0.35)];
    };
    for (CGFloat x = r.origin.x + 14; x <= NSMaxX(r) - 13; x += (r.size.width - 28) / 8) { rivet(x, r.origin.y + 3.5); rivet(x, NSMaxY(r) - 3.5); }
    rivet(r.origin.x + 3.5, NSMidY(r)); rivet(NSMaxX(r) - 3.5, NSMidY(r));
}
- (void)drawRows:(NSShadow *)sh
{
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        DSRow *row = self.rows[k]; NSRect r = [self rowRect:k]; NSDictionary *ch = row.choices[row.index];
        if (k == self.focus) { [[NSColor colorWithSRGBRed:0.86 green:0.6 blue:0.25 alpha:0.12] setFill]; [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, -10, -2) xRadius:3 yRadius:3] fill]; }
        draw_text(row.title.uppercaseString, cp(19, YES), gold(), NSMakeRect(r.origin.x, r.origin.y + 8, 300, 28), NSTextAlignmentLeft, sh);
        [self drawArrow:[self arrow:k right:NO] right:NO lit:self.hover == 10 * k];
        [self drawArrow:[self arrow:k right:YES] right:YES lit:self.hover == 10 * k + 2];
        draw_text(ch[@"label"], cp(18, NO), self.hover == 10 * k + 1 ? NSColor.whiteColor : parchment(), [self valueRect:k], NSTextAlignmentCenter, sh);
        draw_text(ch[@"note"] ?: @"", cp(11, NO), dim(), NSMakeRect(NSMaxX(r) - 330, r.origin.y + 42, 330, 18), NSTextAlignmentCenter, nil);
        if (k + 1 < (NSInteger)self.rows.count) { [[NSColor colorWithSRGBRed:0.55 green:0.38 blue:0.2 alpha:0.35] setFill]; NSRectFill(NSMakeRect(r.origin.x, NSMaxY(r) + 5, r.size.width, 1)); }
    }
}
- (void)drawPanel:(NSRect)panel
{
    [[NSColor colorWithWhite:0 alpha:0.5] setFill]; [[NSBezierPath bezierPathWithRoundedRect:panel xRadius:4 yRadius:4] fill];
    [[NSColor colorWithSRGBRed:0.55 green:0.38 blue:0.2 alpha:0.9] setStroke]; NSBezierPath *bp = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(panel, 0.5, 0.5) xRadius:4 yRadius:4]; bp.lineWidth = 1.5; [bp stroke];
}
- (void)drawRect:(NSRect)dirty
{
    NSRect b = self.bounds;
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; sh.shadowOffset = NSMakeSize(0, -1); sh.shadowBlurRadius = 2;
    /* the main menu's backdrop, filling the window with its aspect kept (cropped at the sides) */
    if (self.stone) {
        CGFloat sc = MAX(b.size.width / self.stone.size.width, b.size.height / self.stone.size.height), w = self.stone.size.width * sc, h = self.stone.size.height * sc;
        [self.stone drawInRect:NSMakeRect(NSMidX(b) - w / 2, NSMidY(b) - h / 2, w, h) fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    }
    else { [[NSColor colorWithSRGBRed:0.06 green:0.05 blue:0.05 alpha:1] setFill]; NSRectFill(b); }
    NSGradient *vig = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:0.15], 0.0, [NSColor colorWithWhite:0 alpha:0.62], 1.0, nil];
    [vig drawInRect:b relativeCenterPosition:NSMakePoint(0, 0.1)];
    if (self.banner) {
        /* the game's key art across the top (downloaded at install), fading into the wall */
        NSSize is = self.banner.size; CGFloat h = b.size.width * is.height / is.width;
        NSRect br = NSMakeRect(0, 0, b.size.width, BANNER_H), ir = NSMakeRect(0, (BANNER_H - h) / 2, b.size.width, h);
        [NSColor.blackColor setFill]; NSRectFill(br);
        [self.banner drawInRect:ir fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES
                          hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        NSGradient *fade = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:0 alpha:0] endingColor:[NSColor colorWithWhite:0 alpha:0.9]];
        [fade drawInRect:NSMakeRect(0, BANNER_H - 40, b.size.width, 40) angle:90];
        [[NSColor colorWithSRGBRed:0.55 green:0.38 blue:0.2 alpha:0.9] setFill]; NSRectFill(NSMakeRect(0, BANNER_H, b.size.width, 1.5));
        /* the subtitle under the logo, on the art's dark left side (placed on the art, so it follows it) */
        NSFont *sf = cp(14, NO); NSRect sr = NSMakeRect(ir.origin.x + 0.045 * ir.size.width, ir.origin.y + 0.68 * ir.size.height, 0.375 * ir.size.width, 0.1 * ir.size.height);
        NSDictionary *sa = @{NSFontAttributeName: sf, NSForegroundColorAttributeName: gold(), NSShadowAttributeName: sh, NSKernAttributeName: @1};
        NSString *st = @"NATIVE EDITION FOR macOS"; NSSize ss = [st sizeWithAttributes:sa];
        [st drawAtPoint:NSMakePoint(NSMidX(sr) - ss.width / 2, round(NSMidY(sr) + sf.capHeight / 2 - sf.ascender)) withAttributes:sa];
    } else if (self.stone && self.logo) {
        /* the game's main menu: its logo where the game draws it on this backdrop (800x600: x centred, y 5 to 160), the
         * backdrop's own "Expansion Set" lettering under it, and the subtitle below that */
        CGFloat sc = MAX(b.size.width / 800, b.size.height / 600), ox = NSMidX(b) - 400 * sc, oy = NSMidY(b) - 300 * sc;
        CGFloat lw = self.logo.size.width * sc, lh = self.logo.size.height * sc;
        [self.logo drawInRect:NSMakeRect(ox + 400 * sc - lw / 2, oy + 160 * sc - lh, lw, lh) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        draw_text(@"NATIVE EDITION FOR macOS", cp(13, NO), gold(), NSMakeRect(0, oy + 236 * sc, b.size.width, 18), NSTextAlignmentCenter, sh);
    } else {
        /* without it: the main menu's logo (or a title plaque) over the backdrop */
        NSRect pr = NSMakeRect(NSMidX(b) - 290, 40, 580, 168);
        if (self.logo) {
            CGFloat lw = 440, lh = lw * self.logo.size.height / self.logo.size.width; if (lh > 150) { lw *= 150 / lh; lh = 150; }
            [self.logo drawInRect:NSMakeRect(NSMidX(b) - lw / 2, pr.origin.y, lw, lh) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        } else {
            [[NSColor colorWithSRGBRed:0.42 green:0.25 blue:0.15 alpha:1] setFill]; NSRectFill(NSInsetRect(NSMakeRect(pr.origin.x, 60, 580, 128), 6, 6));
            [self drawIronFrame:NSMakeRect(pr.origin.x, 60, 580, 128)];
            [self drawTitle:NSMakeRect(pr.origin.x, 66, pr.size.width, 98)];
        }
        draw_text(@"NATIVE EDITION FOR macOS", cp(13, NO), gold(), NSMakeRect(pr.origin.x, NSMaxY(pr) + 18, pr.size.width, 18), NSTextAlignmentCenter, sh);
    }
    /* the top level: the Game row, then a button for each group with a summary under it */
    NSInteger fly = self.open, foc = self.focus, hov = self.hover; self.open = -1;
    if (fly >= 0) self.focus = self.hover = HIT_NONE;           /* under the panel: nothing lit */
    [self drawPanel:NSMakeRect(54, TOP_Y, b.size.width - 108, 74 * self.top.count + 14)];
    [self drawRows:sh];
    for (NSInteger i = 0; i < (NSInteger)self.sections.count; i++) {
        DSSection *s = self.sections[i]; NSRect r = [self sectionRect:i]; NSInteger code = HIT_SECTION + i;
        if (fly < 0 && self.focus == (NSInteger)self.top.count + i) { [[NSColor colorWithSRGBRed:0.86 green:0.6 blue:0.25 alpha:0.28] setFill]; [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, -6, -5) xRadius:6 yRadius:6] fill]; }
        [self drawButton:r title:s.title.uppercaseString code:code primary:NO];
        NSString *sum = s.summary ? s.summary() : nil;
        if (!sum) { NSMutableArray *l = [NSMutableArray array]; for (DSRow *row in s.rows) [l addObject:row.choices[row.index][@"label"]]; sum = [l componentsJoinedByString:@" · "]; }
        draw_text_mode(sum, cp(11, NO), dim(), NSMakeRect(r.origin.x - 10, NSMaxY(r) + 6, r.size.width + 20, 32), NSTextAlignmentCenter, nil, NSLineBreakByWordWrapping);   /* two lines when it needs them */
    }
    if (self.note.length) draw_text(self.note, cp(11, NO), dim(), NSMakeRect(54, [self sectionRect:(NSInteger)self.sections.count - 1].origin.y + 104, b.size.width - 108, 30), NSTextAlignmentCenter, nil);
    self.open = fly; self.focus = foc; self.hover = hov;
    /* an open group: a panel over the window */
    if (self.open >= 0) {
        [[NSColor colorWithWhite:0 alpha:0.6] setFill]; NSRectFillUsingOperation(b, NSCompositingOperationSourceOver);
        NSRect f = self.flyRect;
        [[NSColor colorWithSRGBRed:0.05 green:0.045 blue:0.04 alpha:1] setFill]; NSRectFill(NSInsetRect(f, 6, 6));   /* the panel: the game's dark stone */
        [[NSColor colorWithWhite:0 alpha:0.35] setFill]; NSRectFillUsingOperation(NSInsetRect(f, 6, 6), NSCompositingOperationSourceOver);
        [self drawIronFrame:f];
        draw_text(self.sections[self.open].title.uppercaseString, cp(24, YES), gold(), NSMakeRect(f.origin.x, f.origin.y + 22, f.size.width, 32), NSTextAlignmentCenter, sh);
        [[NSColor colorWithSRGBRed:0.55 green:0.38 blue:0.2 alpha:0.6] setFill]; NSRectFill(NSMakeRect(f.origin.x + 30, f.origin.y + 60, f.size.width - 60, 1));
        [self drawRows:sh];
    }
    [self drawButton:self.quitRect title:self.open >= 0 ? @"BACK" : @"QUIT" code:self.open >= 0 ? HIT_BACK : HIT_QUIT primary:NO];
    [self drawButton:self.playRect title:@"PLAY" code:HIT_PLAY primary:YES];
}
@end

// ---------------------------------------------------------------- choices, settings file, the window
/* A display with a notch: the game in macOS full screen (with Game Mode either way) below it, with a black band beside
 * the camera, or over the whole display around it, the notch covering a little of the picture's top centre (DS_NOTCH) */
static int notch_height(void) { if (@available(macOS 12.0, *)) return (int)NSScreen.mainScreen.safeAreaInsets.top; return 0; }
static NSArray<NSDictionary *> *notch_choices(void)
{
    return @[@{@"value": @"hide", @"label": @"Hide the Notch", @"note": @"The picture below the camera, a black band beside it"},
             @{@"value": @"around", @"label": @"Around the Notch", @"note": @"The picture on the whole display, the notch over its top"}];
}
/* The game's own renderers (D2_RENDERER, chosen at start-up by runtime/win32/hooks.c): its DirectDraw renderer, which
 * plays the cinematics, its Direct3D renderer (on Metal: runtime/win32/ddraw_pal.c), which has the Perspective view,
 * or its windowed mode. */
static NSArray<NSDictionary *> *video_choices(void)
{
    return @[@{@"value": @"ddraw", @"label": @"DirectDraw", @"note": @"The classic renderer, 800x600"},
             @{@"value": @"d3d", @"label": @"Direct3D", @"note": @"Every option below (recommended)"},
             @{@"value": @"gdi", @"label": @"Windowed Mode", @"note": @"The game's -w mode: the same picture, no cinematics"}];
}
/* Direct3D's view (D2_PERSPECTIVE: the game's Options -> Video -> Perspective, kept in the registry): as last chosen in
 * the game, or set at each start */
static NSArray<NSDictionary *> *view_choices(void)
{
    return @[@{@"value": @"game", @"label": @"Game's Choice", @"note": @"The game's own setting, as it last saved it"},
             @{@"value": @"off", @"label": @"Isometric", @"note": @"The classic view, without depth"},
             @{@"value": @"on", @"label": @"Perspective", @"note": @"True 3D: the ground tilts away toward the horizon"}];
}
/* the picture scaled to the display by the renderer's present shader (DSR_FILTER) */
static NSArray<NSDictionary *> *scaling_choices(void)
{
    return @[@{@"value": @"sharp", @"label": @"Sharp", @"note": @"Crisp square pixels, smooth only at their edges"},
             @{@"value": @"smooth", @"label": @"Smooth", @"note": @"Bilinear: soft, like a flat panel at a lower resolution"},
             @{@"value": @"pixel", @"label": @"Pixel", @"note": @"Nearest pixel: hard edges, uneven sizes"},
             @{@"value": @"crt", @"label": @"CRT", @"note": @"Sharp, with the scanlines of a 2000-era monitor"}];
}
/* Direct3D's picture: the game's 800x600 frame, or drawn at the display's resolution (D2_RESOLUTION, host/main.m) */
static NSArray<NSDictionary *> *resolution_choices(void)
{
    return @[@{@"value": @"original", @"label": @"Original", @"note": @"800x600, scaled to the display like DirectDraw"},
             @{@"value": @"display", @"label": @"Display", @"note": @"Drawn at the display's resolution: crisp edges and text"}];
}
/* Direct3D's textures: as the game samples them, or filtered (D2_TEXTURES, runtime/win32/ddraw_pal.c) */
static NSArray<NSDictionary *> *texture_choices(void)
{
    return @[@{@"value": @"sharp", @"label": @"Sharp", @"note": @"The original pixels, each an even square"},
             @{@"value": @"smooth", @"label": @"Smooth", @"note": @"Filtered: softer sprites and ground, no stair-steps"}];
}
/* Direct3D's frame rate (D2_FPS, runtime/win32/d2smooth.c): the game's 25, or more with movement blended between its ticks */
static NSArray<NSDictionary *> *fps_choices(void)
{
    return @[@{@"value": @"25", @"label": @"Original", @"note": @"25 frames a second, as the game draws"},
             @{@"value": @"60", @"label": @"60", @"note": @"Smooth movement between the game's 25 ticks"},
             @{@"value": @"120", @"label": @"120", @"note": @"Smoother still, for 120 Hz displays"},
             @{@"value": @"display", @"label": @"Display", @"note": @"The display's own refresh rate"}];
}
/* Direct3D's width (D2_WIDTH, runtime/win32/d2wide.c): the game's 4:3, or the display's shape at the same height,
 * showing more of the world to the sides */
static NSArray<NSDictionary *> *width_choices(void)
{
    return @[@{@"value": @"800", @"label": @"Original", @"note": @"4:3, with bars at the sides of a wider display"},
             @{@"value": @"display", @"label": @"Display", @"note": @"A wider view of the world, more than other players see"}];
}
/* Direct3D zoomed out (D2_ZOOM_MAX, runtime/win32/d2zoom.c): the mouse wheel (a trackpad: two-finger scroll or pinch) shows more of the world around you, from
 * the game's own view out to the farthest chosen here */
static NSArray<NSDictionary *> *zoom_choices(void)
{
    return @[@{@"value": @"1", @"label": @"Off", @"note": @"The game's own view; the wheel and pinch do nothing"},
             @{@"value": @"1.5", @"label": @"Wheel or Pinch, 1.5×", @"note": @"Scroll or pinch to see more of the world, up to 1.5×"},
             @{@"value": @"2", @"label": @"Wheel or Pinch, 2×", @"note": @"Up to twice as far; heavier on the GPU"}];
}
static NSInteger index_of(NSArray<NSDictionary *> *c, NSString *v, NSInteger dflt)
{
    for (NSUInteger k = 0; k < c.count; k++) if ([c[k][@"value"] isEqualToString:v ?: @""]) return (NSInteger)k;
    return dflt;
}
/* the chosen settings (row key -> value) as environment for the runtime (read later at start-up); Direct3D's only
 * with Direct3D (the game's own Perspective setting is left alone otherwise) */
static void apply(NSDictionary<NSString *, NSString *> *v)
{
    BOOL d3d = [v[@"video"] isEqualToString:@"d3d"];
    setenv("D2_RENDERER", v[@"video"].UTF8String, 1);
    if (!d3d || [v[@"view"] isEqualToString:@"game"]) unsetenv("D2_PERSPECTIVE"); else setenv("D2_PERSPECTIVE", v[@"view"].UTF8String, 1);
    if (d3d) { setenv("D2_RESOLUTION", v[@"resolution"].UTF8String, 1); setenv("D2_TEXTURES", v[@"textures"].UTF8String, 1); }
    else { unsetenv("D2_RESOLUTION"); unsetenv("D2_TEXTURES"); }
    if (d3d && ![v[@"fps"] isEqualToString:@"25"]) setenv("D2_FPS", v[@"fps"].UTF8String, 1); else unsetenv("D2_FPS");
    if (d3d && ![v[@"width"] isEqualToString:@"800"]) setenv("D2_WIDTH", v[@"width"].UTF8String, 1); else unsetenv("D2_WIDTH");
    if (d3d && v[@"zoom"] && ![v[@"zoom"] isEqualToString:@"1"]) setenv("D2_ZOOM_MAX", v[@"zoom"].UTF8String, 1); else unsetenv("D2_ZOOM_MAX");
    setenv("DSR_FILTER", v[@"scaling"].UTF8String, 1);
    setenv("DS_NOTCH", v[@"notch"].UTF8String, 1);
}
static NSDictionary<NSString *, NSString *> *chosen(NSDictionary<NSString *, DSRow *> *rows)   /* row key -> its value */
{
    NSMutableDictionary *d = [NSMutableDictionary dictionary];
    for (NSString *k in rows) d[k] = rows[k].choices[rows[k].index][@"value"];
    return d;
}
@interface DSBlockTarget : NSObject      /* a button action as a block */
@property (copy) void (^fire)(NSInteger tag);
- (void)clicked:(NSButton *)b;
@end
@implementation DSBlockTarget
- (void)clicked:(NSButton *)b { if (self.fire) self.fire(b.tag); }
@end

// ---------------------------------------------------------------- updates
/* install.sh gives each app an updater (Contents/MacOS/update, passed in DS_UPDATER): it fetches the branch the app
 * follows (main, or nightly for Nightly) from GitHub into a worktree of the checkout the app was installed from and
 * rebuilds the app from it, keeping the game folder, saves and settings. The launcher asks GitHub (in the
 * background, each time it opens) whether that branch moved past the commit the app was built from. */
enum { UPD_NONE, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_ERROR };
static void on_main(dispatch_block_t b) { CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, b); CFRunLoopWakeUp(CFRunLoopGetMain()); }
static NSString *short_commit(NSString *c) { return c.length > 7 ? [c substringToIndex:7] : (c ?: @""); }
static NSString *update_check(NSString *updater)   /* the newest commit of the branch on GitHub; nil if it can't be reached */
{
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater, @"--check"];
    NSPipe *p = [NSPipe pipe]; t.standardOutput = p; t.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![t launchAndReturnError:nil]) return nil;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), dispatch_get_global_queue(0, 0), ^{ if (t.running) [t terminate]; });
    NSData *d = [p.fileHandleForReading readDataToEndOfFile]; [t waitUntilExit];
    NSString *out = [[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    return t.terminationStatus == 0 && out.length >= 40 ? [out substringToIndex:40] : nil;
}
static NSArray<NSDictionary *> *update_choice(int state, NSString *branch, NSString *built, NSString *latest)
{
    NSString *label = @"Not available", *note = @"This copy wasn't installed from the GitHub repository";
    switch (state) {
    case UPD_CHECKING: label = @"Checking..."; note = [NSString stringWithFormat:@"Asking GitHub for the newest %@", branch]; break;
    case UPD_CURRENT: label = @"Up to date"; note = [NSString stringWithFormat:@"%@ %@", branch, short_commit(built)]; break;
    case UPD_AVAILABLE: label = @"Update available"; note = [NSString stringWithFormat:@"%@ -> %@: press to install", short_commit(built), short_commit(latest)]; break;
    case UPD_ERROR: label = @"Couldn't check"; note = @"GitHub unreachable or not signed in: press to retry"; break;
    }
    return @[@{@"value": @"", @"label": label, @"note": note}];
}
/* the phase of install.sh's output, in words */
static NSString *update_phase(NSString *line, NSString *branch)
{
    if ([line hasPrefix:@"== fetching"]) return @"Downloading the update from GitHub";
    if ([line hasPrefix:[NSString stringWithFormat:@"== %@:", branch]]) return [@"Got " stringByAppendingString:[line substringFromIndex:branch.length + 4]];
    if ([line hasPrefix:@"== analysis"]) return @"Rebuilding from your game: analysing its code (1 of 4)";
    if ([line hasPrefix:@"== lifting"]) return @"Rebuilding from your game: translating to native code (2 of 4)";
    if ([line hasPrefix:@"== compiling"]) return @"Rebuilding from your game: compiling (3 of 4)";
    if ([line hasPrefix:@"== installing"]) return @"Installing the new app (4 of 4)";
    return nil;
}
/* runs the updater with a progress window (modal); returns YES when the app was rebuilt */
static BOOL update_run(NSWindow *parent, NSString *updater, NSString *branch, NSString *logPath)
{
    NSPanel *pn = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 170) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    pn.title = @"Updating";
    NSTextField *title = [NSTextField labelWithString:@"Updating to the newest version"]; title.font = [NSFont boldSystemFontOfSize:14]; title.frame = NSMakeRect(20, 128, 480, 22);
    NSTextField *status = [NSTextField wrappingLabelWithString:@"Starting..."]; status.frame = NSMakeRect(20, 64, 480, 56);
    NSProgressIndicator *spin = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 44, 480, 12)]; spin.indeterminate = YES; [spin startAnimation:nil];
    NSTextField *time = [NSTextField labelWithString:@"This takes a few minutes. Your saves and settings are kept."]; time.font = [NSFont systemFontOfSize:11]; time.textColor = NSColor.secondaryLabelColor; time.frame = NSMakeRect(20, 14, 360, 18);
    NSButton *btn = [NSButton buttonWithTitle:@"Close" target:nil action:nil]; btn.frame = NSMakeRect(400, 8, 100, 30); btn.hidden = YES;
    for (NSView *x in @[title, status, spin, time, btn]) [pn.contentView addSubview:x];
    if (parent) [pn setFrameOrigin:NSMakePoint(NSMidX(parent.frame) - 260, NSMidY(parent.frame) - 85)]; else [pn center];
    [[NSFileManager defaultManager] createFileAtPath:logPath contents:nil attributes:nil];
    NSFileHandle *log = [NSFileHandle fileHandleForWritingAtPath:logPath];
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater];
    NSPipe *pipe = [NSPipe pipe]; t.standardOutput = pipe; t.standardError = pipe;
    NSMutableString *pending = [NSMutableString new], *tail = [NSMutableString new];
    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = h.availableData; if (!d.length) return;
        [log writeData:d];
        NSString *chunk = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] ?: @"";
        @synchronized (pending) {
            [pending appendString:chunk];
            NSRange nl;
            while ((nl = [pending rangeOfString:@"\n"]).location != NSNotFound) {
                NSString *line = [pending substringToIndex:nl.location]; [pending deleteCharactersInRange:NSMakeRange(0, nl.location + 1)];
                [tail appendFormat:@"%@\n", line]; if (tail.length > 600) [tail deleteCharactersInRange:NSMakeRange(0, tail.length - 600)];
                NSString *ph = update_phase(line, branch);
                if (ph) on_main(^{ status.stringValue = ph; });
            }
        }
    };
    __block BOOL ok = NO;
    __attribute__((objc_precise_lifetime)) DSBlockTarget *bt = [DSBlockTarget new];   /* a button's target is weak: kept here */
    bt.fire = ^(NSInteger tag) { (void)tag; [NSApp stopModal]; };
    btn.target = bt; btn.action = @selector(clicked:);
    t.terminationHandler = ^(NSTask *task) {
        int code = task.terminationStatus;
        on_main(^{
            pipe.fileHandleForReading.readabilityHandler = nil; [log closeFile];
            [spin stopAnimation:nil]; spin.hidden = YES; btn.hidden = NO;
            if (code == 0) { ok = YES; title.stringValue = @"Updated"; status.stringValue = @"The new version is installed. The app restarts when you press Restart."; btn.title = @"Restart"; }
            else {
                NSString *last; @synchronized (pending) { last = [[tail componentsSeparatedByString:@"\n"] componentsJoinedByString:@" "]; }
                title.stringValue = @"The update didn't finish";
                status.stringValue = [NSString stringWithFormat:@"Nothing was changed if it stopped before installing. Details: %@  (%@)", logPath,
                                      last.length > 160 ? [last substringFromIndex:last.length - 160] : last];
                time.stringValue = @"";
            }
        });
    };
    if (![t launchAndReturnError:nil]) { [log closeFile]; return NO; }
    [pn makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:pn];
    [pn orderOut:nil];
    return ok;
}

/* Shows the launch window (modal) unless DS_NO_LAUNCHER is set; returns 0 to quit, 1 to play.
 * DS_LAUNCHER_SHOT=<png> renders it to an image instead (development; DS_LAUNCHER_PAGE: a group opened). */
int ds_launcher_run(const char *game_dir, const char *data_dir)
{
    (void)game_dir;
    NSString *data = @(data_dir), *plistPath = [data stringByAppendingPathComponent:@"launcher.plist"];
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:plistPath] ?: @{};
    DSRow *video = [DSRow new]; video.title = @"Video"; video.choices = video_choices(); video.index = index_of(video.choices, saved[@"video"] ?: @"d3d", 0);
    DSRow *view = [DSRow new]; view.title = @"View"; view.choices = view_choices(); view.index = index_of(view.choices, saved[@"view"] ?: @"on", 0);
    DSRow *scale = [DSRow new]; scale.title = @"Scaling"; scale.choices = scaling_choices(); scale.index = index_of(scale.choices, saved[@"scaling"], 0);
    DSRow *notch = [DSRow new]; notch.title = @"Notch"; notch.choices = notch_choices(); notch.index = index_of(notch.choices, saved[@"notch"], 0);
    DSRow *res = [DSRow new]; res.title = @"Resolution"; res.choices = resolution_choices(); res.index = index_of(res.choices, saved[@"resolution"] ?: @"display", 0);
    DSRow *texr = [DSRow new]; texr.title = @"Textures"; texr.choices = texture_choices(); texr.index = index_of(texr.choices, saved[@"textures"] ?: @"smooth", 0);
    DSRow *fpsr = [DSRow new]; fpsr.title = @"Frame Rate"; fpsr.choices = fps_choices(); fpsr.index = index_of(fpsr.choices, saved[@"fps"] ?: @"display", 0);
    DSRow *widr = [DSRow new]; widr.title = @"Width"; widr.choices = width_choices(); widr.index = index_of(widr.choices, saved[@"width"] ?: @"display", 0);
    DSRow *zoomr = [DSRow new]; zoomr.title = @"Zoom"; zoomr.choices = zoom_choices(); zoomr.index = index_of(zoomr.choices, saved[@"zoom"] ?: @"2", 0);
    NSDictionary<NSString *, DSRow *> *rows = @{@"video": video, @"view": view, @"resolution": res, @"textures": texr, @"fps": fpsr, @"width": widr, @"zoom": zoomr,
                                                @"scaling": scale, @"notch": notch};
    const char *shot = getenv("DS_LAUNCHER_SHOT");
    if (getenv("DS_NO_LAUNCHER") && !shot) {
        if (saved.count) apply(chosen(rows));
        return 1;
    }

    NSString *updater = getenv("DS_UPDATER") ? @(getenv("DS_UPDATER")) : nil, *branch = getenv("DS_UPDATE_BRANCH") ? @(getenv("DS_UPDATE_BRANCH")) : @"main";
    NSString *built = getenv("DS_BUILT_COMMIT") ? @(getenv("DS_BUILT_COMMIT")) : @"";
    if (![[NSFileManager defaultManager] isExecutableFileAtPath:updater ?: @""]) updater = nil;
    DSRow *upd = [DSRow new]; upd.title = @"Updates";
    __block int ustate = updater ? UPD_CHECKING : UPD_NONE; __block NSString *latest = nil;
    upd.choices = update_choice(ustate, branch, built, nil);
    BOOL shotMode = shot != NULL;
    DSSection *display = [DSSection new], *d3dSec = [DSSection new], *updSec = [DSSection new];
    d3dSec.title = @"Direct3D"; d3dSec.rows = @[view, res, texr, fpsr, widr, zoomr];
    __weak DSRow *wvid = video, *wview = view;
    __weak DSRow *wres = res, *wtex = texr, *wfps = fpsr, *wwid = widr, *wzoom = zoomr;
    d3dSec.summary = ^NSString * {
        if (![wvid.choices[wvid.index][@"value"] isEqualToString:@"d3d"]) return @"Used when Video is Direct3D";
        NSString *f = [NSString stringWithFormat:@"%@%@", wfps.index ? [NSString stringWithFormat:@" · %@ fps", wfps.choices[wfps.index][@"label"]] : @"", wwid.index ? @" · Wide" : @""];
        if (wzoom.index) f = [f stringByAppendingFormat:@" · Zoom %@", [wzoom.choices[wzoom.index][@"value"] stringByAppendingString:@"×"]];
        return [NSString stringWithFormat:@"%@ · %@ · %@%@", wview.choices[wview.index][@"label"], wres.choices[wres.index][@"label"], wtex.choices[wtex.index][@"label"], f];
    };
    display.title = @"Display"; display.rows = notch_height() > 0 ? @[scale, notch] : @[scale];
    updSec.title = @"Updates"; updSec.summary = ^NSString * { NSDictionary *c = upd.choices[upd.index]; return [c[@"note"] length] ? [NSString stringWithFormat:@"%@ · %@", c[@"label"], c[@"note"]] : c[@"label"]; };
    DSLaunchView *v = [[DSLaunchView alloc] initWithFrame:NSMakeRect(0, 0, 780, 620)];
    v.top = @[video]; v.sections = @[display, d3dSec, updSec]; v.open = -1; v.hover = v.pressed = HIT_NONE;
    __weak DSLaunchView *wv = v; __weak DSRow *wu = upd;
    updSec.activate = ^{ if (wu.activate) wu.activate(); };
    void (^check)(void) = ^{
        if (!updater) return;
        ustate = UPD_CHECKING; wu.choices = update_choice(ustate, branch, built, nil); wv.needsDisplay = YES;
        void (^done)(NSString *) = ^(NSString *l) {
            latest = l; ustate = !l ? UPD_ERROR : [l isEqualToString:built] ? UPD_CURRENT : UPD_AVAILABLE;
            wu.choices = update_choice(ustate, branch, built, l); wv.needsDisplay = YES;
        };
        if (shotMode) { done(update_check(updater)); return; }               /* development: the result in the image */
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ NSString *l = update_check(updater); on_main(^{ done(l); }); });
    };
    check();
    upd.activate = ^{
        if (ustate == UPD_CHECKING) return;
        if (ustate != UPD_AVAILABLE) { check(); return; }
        NSAlert *a = [NSAlert new];
        a.messageText = [NSString stringWithFormat:@"Update to the newest %@?", [branch isEqualToString:@"nightly"] ? @"Nightly" : @"version"];
        a.informativeText = [NSString stringWithFormat:@"The %@ branch (%@) is downloaded from GitHub and the app is rebuilt from your own copy of the game, "
                             "which takes a few minutes. Your game folder, saves and settings are kept. The app restarts when it is done.", branch, short_commit(latest)];
        [a addButtonWithTitle:@"Update"]; [a addButtonWithTitle:@"Cancel"];
        if ([a runModal] != NSAlertFirstButtonReturn) return;
        NSString *logPath = [data stringByAppendingPathComponent:@"update.log"];
        if (update_run(wv.window, updater, branch, logPath)) {   /* restart: the new app, then this one ends */
            NSString *app = [[[updater stringByDeletingLastPathComponent] stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
            NSTask *o = [NSTask new]; o.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"]; o.arguments = @[@"-n", app];
            [o launchAndReturnError:nil]; [o waitUntilExit];
            exit(0);
        }
        check();
    };
    /* the game's own art, decoded at install time (tools/d2art.py): the main menu's backdrop and the logo */
    NSString *art = [data stringByAppendingPathComponent:@"art"];
    NSImage *backdrop = [[NSImage alloc] initWithContentsOfFile:[art stringByAppendingPathComponent:@"backdrop.png"]];
    NSImage *logo = [[NSImage alloc] initWithContentsOfFile:[art stringByAppendingPathComponent:@"logo.png"]];
    if (backdrop.size.width > 100) v.stone = backdrop;
    if (logo.size.width > 100) v.logo = logo;
    if (shot) {   /* development: the window as an image (DS_LAUNCHER_PAGE: a group opened) */
        if (getenv("DS_LAUNCHER_PAGE") && *getenv("DS_LAUNCHER_PAGE")) v.open = atoi(getenv("DS_LAUNCHER_PAGE"));
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(shot) atomically:YES];
        return 0;
    }

    NSWindow *w = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO;   /* ARC owns it: AppKit's default would release it again when the close button closes it (a crash on exit) */
    w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.movableByWindowBackground = YES; w.title = @"Diablo II";
    w.backgroundColor = NSColor.blackColor; w.contentView = v; [w center]; [w makeFirstResponder:v];
    __block int result = 0;
    v.onPlay = ^{ result = 1; [NSApp stopModal]; };
    v.onQuit = ^{ result = 0; [NSApp stopModal]; };
    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification object:w queue:nil usingBlock:^(NSNotification *n) { (void)n; [NSApp stopModal]; }];
    [NSApp activateIgnoringOtherApps:YES];
    [w makeKeyAndOrderFront:nil];
    if (getenv("DS_LAUNCHER_AUTOPLAY"))   /* testing: Play after a moment, as if clicked */
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ if (v.onPlay) v.onPlay(); });
    [NSApp runModalForWindow:w];
    [w orderOut:nil];
    if (!result) return 0;
    NSDictionary *values = chosen(rows);
    [values writeToFile:plistPath atomically:YES];
    apply(values);
    return result;
}
