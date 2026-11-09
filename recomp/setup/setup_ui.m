// setup_ui.m: the first-run experience of a drag-and-drop recomp app.
//
// The app ships the runtime and launcher but no game. Until the game has been built on this Mac, this window asks for the
// player's own game folder: dropped on the app's icon, dropped on this window, chosen in a panel, or put in
// ~/Games/<Game>/Put Game Files Here. Once a folder checks out it offers setup, runs setup.py with the bundled tools, and
// restarts into the normal launcher. An update that changes the game code runs the same setup again on its own.
#import <Cocoa/Cocoa.h>
#import <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static NSString *sha256_of(NSString *path)
{
    NSFileHandle *f = [NSFileHandle fileHandleForReadingAtPath:path];
    if (!f) return nil;
    CC_SHA256_CTX c; CC_SHA256_Init(&c);
    for (;;) { @autoreleasepool {
        NSData *d = [f readDataOfLength:1 << 20];
        if (!d.length) break;
        CC_SHA256_Update(&c, d.bytes, (CC_LONG)d.length);
    } }
    unsigned char out[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final(out, &c);
    NSMutableString *s = [NSMutableString string];
    for (int k = 0; k < CC_SHA256_DIGEST_LENGTH; k++) [s appendFormat:@"%02x", out[k]];
    return s;
}
/* a path under `folder`, matching each name without regard to case, as Windows would */
static NSString *find_ci(NSString *folder, NSString *relative)
{
    NSString *here = folder;
    for (NSString *part in [relative componentsSeparatedByString:@"/"]) {
        NSString *hit = nil;
        for (NSString *n in [NSFileManager.defaultManager contentsOfDirectoryAtPath:here error:nil])
            if ([n caseInsensitiveCompare:part] == NSOrderedSame) { hit = n; break; }
        if (!hit) return nil;
        here = [here stringByAppendingPathComponent:hit];
    }
    return here;
}
static NSDictionary *game_config(void)
{
    NSString *p = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"setup/game.json"];
    NSData *d = [NSData dataWithContentsOfFile:p];
    return d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
}
static NSString *game_root(NSDictionary *cfg)
{
    return [[NSHomeDirectory() stringByAppendingPathComponent:@"Games"] stringByAppendingPathComponent:cfg[@"folder"]];
}
/* the folder at or under `path` (3 levels at most) holding the game's executable */
static NSString *locate_game(NSString *path, NSDictionary *cfg)
{
    NSMutableArray *queue = [NSMutableArray arrayWithObject:@[path, @0]];
    NSFileManager *fm = NSFileManager.defaultManager;
    while (queue.count) {
        NSString *here = queue[0][0]; int depth = [queue[0][1] intValue]; [queue removeObjectAtIndex:0];
        if (find_ci(here, cfg[@"exe"])) return here;
        if (depth >= 3) continue;
        for (NSString *n in [[fm contentsOfDirectoryAtPath:here error:nil] sortedArrayUsingSelector:@selector(compare:)]) {
            NSString *sub = [here stringByAppendingPathComponent:n]; BOOL dir = NO;
            if ([fm fileExistsAtPath:sub isDirectory:&dir] && dir) [queue addObject:@[sub, @(depth + 1)]];
        }
    }
    return nil;
}
/* nil when `folder` is the right game and complete; otherwise a sentence for the player */
static NSString *check_game(NSString *folder, NSDictionary *cfg)
{
    NSString *exe = find_ci(folder, cfg[@"exe"]);
    if (!exe) return [NSString stringWithFormat:@"That folder doesn't have %@ in it.", cfg[@"exe"]];
    if (!cfg[@"exe_sha256"][sha256_of(exe) ?: @""])
        return [NSString stringWithFormat:@"This isn't the version of %@ this app works with. It needs %@.", cfg[@"name"], cfg[@"version"]];
    for (NSString *r in cfg[@"required"]) if (!find_ci(folder, r))
        return [NSString stringWithFormat:@"That copy of %@ is incomplete: %@ is missing.", cfg[@"name"], r];
    return nil;
}
static BOOL game_is_built(NSDictionary *cfg)
{
    NSString *rc = [game_root(cfg) stringByAppendingPathComponent:@"recomp"];
    NSData *d = [NSData dataWithContentsOfFile:[rc stringByAppendingPathComponent:@"ready.json"]];
    NSDictionary *r = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
    return r && [r[@"code"] isEqual:cfg[@"code"]] && [NSFileManager.defaultManager fileExistsAtPath:[rc stringByAppendingPathComponent:@"libgame.dylib"]];
}
static void restart_app(NSDictionary *cfg)
{
    NSString *rc = [game_root(cfg) stringByAppendingPathComponent:@"recomp"];
    setenv("DYLD_LIBRARY_PATH", rc.fileSystemRepresentation, 1); setenv("DS_EASY_READY", "1", 1);
    const char *self = NSBundle.mainBundle.executablePath.fileSystemRepresentation;
    char *const av[] = {(char *)self, NULL};
    execv(self, av);
}

@interface DropZone : NSView
@property (copy) void (^onDrop)(NSString *path);
@property (copy) NSString *line;
@property BOOL hot, enabled;
@end
@implementation DropZone
- (instancetype)initWithFrame:(NSRect)r { if ((self = [super initWithFrame:r])) { [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]]; _enabled = YES; } return self; }
- (void)drawRect:(NSRect)d
{
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 3, 3) xRadius:14 yRadius:14];
    [[NSColor colorWithWhite:1 alpha:self.hot ? 0.13 : 0.05] setFill]; [p fill];
    CGFloat dash[2] = {9, 7}; [p setLineDash:dash count:2 phase:0]; p.lineWidth = 2;
    [[NSColor colorWithWhite:1 alpha:self.hot ? 0.9 : 0.35] setStroke]; [p stroke];
    NSMutableParagraphStyle *st = [NSMutableParagraphStyle new]; st.alignment = NSTextAlignmentCenter;
    NSDictionary *at = @{NSFontAttributeName: [NSFont systemFontOfSize:17 weight:NSFontWeightMedium], NSForegroundColorAttributeName: [NSColor colorWithWhite:0.92 alpha:1], NSParagraphStyleAttributeName: st};
    NSRect b = self.bounds; CGFloat h = [self.line boundingRectWithSize:NSMakeSize(b.size.width - 60, 200) options:NSStringDrawingUsesLineFragmentOrigin attributes:at].size.height;
    [self.line drawInRect:NSMakeRect(30, (b.size.height - h) / 2, b.size.width - 60, h + 2) withAttributes:at];
}
- (NSString *)pathIn:(id<NSDraggingInfo>)info
{
    NSArray *urls = [info.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    return ((NSURL *)urls.firstObject).path;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)i { if (!self.enabled || ![self pathIn:i]) return NSDragOperationNone; self.hot = YES; self.needsDisplay = YES; return NSDragOperationCopy; }
- (void)draggingExited:(id<NSDraggingInfo>)i { self.hot = NO; self.needsDisplay = YES; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)i { self.hot = NO; self.needsDisplay = YES; NSString *p = [self pathIn:i]; if (p && self.onDrop) self.onDrop(p); return p != nil; }
@end

@interface SetupController : NSObject <NSApplicationDelegate>
@end
@implementation SetupController {
    NSDictionary *_cfg; NSString *_root, *_inbox, *_found, *_declined;
    NSWindow *_win; NSTextField *_title, *_body, *_note; DropZone *_zone; NSProgressIndicator *_bar;
    NSButton *_primary, *_secondary, *_tertiary; NSTask *_task; NSTimer *_scan; BOOL _busy, _updating, _cancelled;
    NSDate *_began; void (^_onPrimary)(void), (^_onSecondary)(void), (^_onTertiary)(void);
}
- (NSTextField *)label:(NSRect)r size:(CGFloat)s weight:(NSFontWeight)w white:(CGFloat)c
{
    NSTextField *t = [NSTextField wrappingLabelWithString:@""]; t.frame = r; t.font = [NSFont systemFontOfSize:s weight:w];
    t.textColor = [NSColor colorWithWhite:c alpha:1]; t.alignment = NSTextAlignmentCenter; t.selectable = NO; return t;
}
- (NSButton *)button:(NSRect)r action:(SEL)a
{
    NSButton *b = [NSButton buttonWithTitle:@"" target:self action:a]; b.frame = r; b.controlSize = NSControlSizeLarge; b.bezelStyle = NSBezelStyleRounded; return b;
}
- (void)buildWindow
{
    _win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 620, 460) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    _win.title = _cfg[@"name"]; _win.backgroundColor = [NSColor colorWithWhite:0.11 alpha:1]; _win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _win.releasedWhenClosed = NO; [_win center];
    NSView *v = _win.contentView; v.wantsLayer = YES; v.layer.backgroundColor = [NSColor colorWithWhite:0.11 alpha:1].CGColor;
    _title = [self label:NSMakeRect(40, 382, 540, 44) size:28 weight:NSFontWeightBold white:1]; [v addSubview:_title];
    _body = [self label:NSMakeRect(50, 308, 520, 66) size:14 weight:NSFontWeightRegular white:0.72]; [v addSubview:_body];
    _zone = [[DropZone alloc] initWithFrame:NSMakeRect(50, 112, 520, 186)]; [v addSubview:_zone];
    __weak SetupController *me = self; _zone.onDrop = ^(NSString *p) { [me tryPath:p]; };
    _bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(60, 214, 500, 20)]; _bar.indeterminate = NO; _bar.minValue = 0; _bar.maxValue = 100; _bar.hidden = YES; [v addSubview:_bar];
    _note = [self label:NSMakeRect(50, 66, 520, 38) size:13 weight:NSFontWeightRegular white:0.72]; [v addSubview:_note];
    _primary = [self button:NSMakeRect(390, 20, 180, 36) action:@selector(primary:)]; _primary.keyEquivalent = @"\r"; [v addSubview:_primary];
    _secondary = [self button:NSMakeRect(220, 20, 165, 36) action:@selector(secondary:)]; [v addSubview:_secondary];
    _tertiary = [self button:NSMakeRect(50, 20, 165, 36) action:@selector(tertiary:)]; [v addSubview:_tertiary];
}
- (void)primary:(id)s { if (_onPrimary) _onPrimary(); }
- (void)secondary:(id)s { if (_onSecondary) _onSecondary(); }
- (void)tertiary:(id)s { if (_onTertiary) _onTertiary(); }
- (void)button:(NSButton *)b title:(NSString *)t { b.title = t ?: @""; b.hidden = t == nil; }
- (void)showWaiting:(NSString *)problem
{
    _busy = NO; _found = nil; _zone.hidden = NO; _zone.enabled = YES; _bar.hidden = YES;
    _title.stringValue = [NSString stringWithFormat:@"Set Up %@", _cfg[@"name"]];
    _body.stringValue = [NSString stringWithFormat:@"This app uses your own copy of the game. Nothing else to install.\n%@", _cfg[@"where"]];
    _zone.line = [NSString stringWithFormat:@"Drop your %@ folder here\nor on the app's icon", _cfg[@"name"]]; _zone.needsDisplay = YES;
    _note.stringValue = problem ?: [NSString stringWithFormat:@"You can also put it in ~/Games/%@/Put Game Files Here.", _cfg[@"folder"]];
    _note.textColor = problem ? [NSColor colorWithRed:1 green:0.55 blue:0.5 alpha:1] : [NSColor colorWithWhite:0.6 alpha:1];
    __weak SetupController *me = self;
    [self button:_primary title:@"Choose Folder..."]; _onPrimary = ^{ [me choose]; };
    [self button:_secondary title:@"Open That Folder"]; _onSecondary = ^{ [me openInbox]; };
    [self button:_tertiary title:@"Quit"]; _onTertiary = ^{ [NSApp terminate:nil]; };
    if (problem) [self shot:@"problem"];
}
- (void)openInbox
{
    [NSFileManager.defaultManager createDirectoryAtPath:_inbox withIntermediateDirectories:YES attributes:nil error:nil];
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:_inbox]];
}
- (void)choose
{
    NSOpenPanel *p = [NSOpenPanel openPanel]; p.canChooseDirectories = YES; p.canChooseFiles = NO; p.prompt = @"Use This Folder";
    p.message = [NSString stringWithFormat:@"Choose your %@ folder", _cfg[@"name"]];
    [p beginSheetModalForWindow:_win completionHandler:^(NSModalResponse r) { if (r == NSModalResponseOK) [self tryPath:p.URL.path]; }];
}
- (void)tryPath:(NSString *)path
{
    if (_busy) return;
    BOOL dir = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&dir]) return;
    if (!dir) path = path.stringByDeletingLastPathComponent;
    _busy = YES; _note.stringValue = @"Checking that folder..."; _note.textColor = [NSColor colorWithWhite:0.6 alpha:1];
    NSDictionary *cfg = _cfg;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *game = locate_game(path, cfg);
        NSString *problem = game ? check_game(game, cfg) : [NSString stringWithFormat:@"That folder doesn't have %@ in it.", cfg[@"exe"]];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_busy = NO;
            if (problem) [self showWaiting:problem]; else [self showFound:game];
        });
    });
}
- (void)scanInbox
{
    if (_busy || _found) return;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSArray *items = [[fm contentsOfDirectoryAtPath:_inbox error:nil] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"NOT SELF BEGINSWITH '.'"]];
    if (!items.count) { _declined = nil; return; }
    if (_declined) return;
    NSDictionary *cfg = _cfg; NSString *inbox = _inbox; _busy = YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSString *game = locate_game(inbox, cfg);
        BOOL ok = game && !check_game(game, cfg);                  /* still being copied in: not yet, and no complaint */
        dispatch_async(dispatch_get_main_queue(), ^{ self->_busy = NO; if (ok && !self->_found) [self showFound:game]; });
    });
}
- (void)showFound:(NSString *)game
{
    _found = game; _zone.hidden = YES; _bar.hidden = YES;
    _title.stringValue = [NSString stringWithFormat:@"Found %@ %@", _cfg[@"name"], _cfg[@"version"]];
    _body.stringValue = [NSString stringWithFormat:@"Set it up now? This builds the game for your Mac and takes about %@ minutes.\nYour files aren't changed.", _cfg[@"minutes"]];
    _note.stringValue = game; _note.textColor = [NSColor colorWithWhite:0.5 alpha:1];
    __weak SetupController *me = self; NSString *inbox = _inbox;
    [self button:_primary title:@"Set Up"]; _onPrimary = ^{ [me runSetup:game]; };
    [self button:_secondary title:@"Not Now"]; _onSecondary = ^{ SetupController *s = me; if ([game hasPrefix:inbox]) s->_declined = game; [s showWaiting:nil]; };
    [self button:_tertiary title:nil]; _onTertiary = nil;
    [self autoPress:@"found"];
}
/* tests: DS_SETUP_SHOTS=<folder> saves the window as each screen appears; DS_SETUP_PRESS presses the main button on the Found and All Set screens */
- (void)shot:(NSString *)name
{
    const char *dir = getenv("DS_SETUP_SHOTS"); if (!dir) return;
    NSView *v = _win.contentView; [v displayIfNeeded];
    NSBitmapImageRep *r = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:r];
    [[r representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[[@(dir) stringByAppendingPathComponent:name] stringByAppendingPathExtension:@"png"] atomically:YES];
}
- (void)autoPress:(NSString *)name
{
    [self shot:name];
    if (getenv("DS_SETUP_PRESS")) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), ^{ if (self->_onPrimary) self->_onPrimary(); });
}
- (void)fail:(NSString *)why
{
    _busy = NO; _found = nil; _bar.hidden = YES; _zone.hidden = YES;
    _title.stringValue = @"Setup Didn't Finish"; _body.stringValue = why; _note.stringValue = @"You can try again. It picks up where it stopped.";
    _note.textColor = [NSColor colorWithWhite:0.6 alpha:1];
    __weak SetupController *me = self;
    [self button:_primary title:@"Start Over"]; _onPrimary = ^{ [me showWaiting:nil]; };
    [self button:_secondary title:nil]; _onSecondary = nil; [self button:_tertiary title:@"Quit"]; _onTertiary = ^{ [NSApp terminate:nil]; };
    [self shot:@"failed"]; if (getenv("DS_SETUP_PRESS")) { fprintf(stderr, "setup failed: %s\n", why.UTF8String); exit(3); }
}
- (void)runSetup:(NSString *)game
{
    _busy = YES; _cancelled = NO; _zone.hidden = YES; _bar.hidden = NO; _bar.doubleValue = 0; _began = [NSDate date];
    _title.stringValue = _updating ? [NSString stringWithFormat:@"Updating %@", _cfg[@"name"]] : [NSString stringWithFormat:@"Setting Up %@", _cfg[@"name"]];
    _body.stringValue = @"Your Mac is building the game. You can keep using it while this runs."; _note.stringValue = @"Getting ready..."; _note.textColor = [NSColor colorWithWhite:0.72 alpha:1];
    __weak SetupController *me = self;
    [self button:_primary title:nil]; _onPrimary = nil; [self button:_secondary title:nil]; _onSecondary = nil;
    [self button:_tertiary title:@"Cancel"]; _onTertiary = ^{ SetupController *s = me; s->_cancelled = YES; if (s->_task.running) kill(-s->_task.processIdentifier, SIGTERM); };
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"setup"], *root = _root, *inbox = _inbox;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *tools = [root stringByAppendingPathComponent:@".setup/tools"], *mark = [tools stringByAppendingPathComponent:@".ok"];
        NSFileManager *fm = NSFileManager.defaultManager;
        if (![fm fileExistsAtPath:mark]) {        /* unpacked here rather than run from inside the app */
            [fm removeItemAtPath:tools error:nil]; [fm createDirectoryAtPath:tools withIntermediateDirectories:YES attributes:nil error:nil];
            NSTask *x = [NSTask new]; x.executableURL = [NSURL fileURLWithPath:@"/usr/bin/tar"];
            x.arguments = @[@"-xf", [res stringByAppendingPathComponent:@"toolkit.tar.xz"], @"-C", tools];
            NSError *e = nil; [x launchAndReturnError:&e]; [x waitUntilExit];
            if (e || x.terminationStatus) { dispatch_async(dispatch_get_main_queue(), ^{ [self fail:@"Couldn't unpack the setup tools. Is your disk full?"]; }); return; }
            NSTask *q = [NSTask new]; q.executableURL = [NSURL fileURLWithPath:@"/usr/bin/xattr"];   /* tar carries the download's quarantine over: the app's own tools must run without a prompt each */
            q.arguments = @[@"-r", @"-d", @"com.apple.quarantine", tools]; q.standardError = [NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
            [q launchAndReturnError:nil]; [q waitUntilExit];
            [@"" writeToFile:mark atomically:NO encoding:NSUTF8StringEncoding error:nil];
        }
        NSTask *t = [NSTask new]; NSPipe *pipe = [NSPipe pipe];
        t.executableURL = [NSURL fileURLWithPath:[tools stringByAppendingPathComponent:@"python/bin/python3"]];
        t.arguments = @[[res stringByAppendingPathComponent:@"setup.py"], @"--game", game ?: @"", @"--root", root, @"--setup", res, @"--tools", tools];
        t.environment = @{@"PATH": @"/usr/bin:/bin", @"HOME": NSHomeDirectory(), @"LANG": @"en_US.UTF-8"};
        t.standardOutput = pipe; t.standardError = [NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
        self->_task = t;
        __block NSMutableData *buf = [NSMutableData data]; __block NSString *lastError = nil; __block BOOL done = NO;
        pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
            NSData *d = h.availableData; if (!d.length) return;
            [buf appendData:d];
            for (;;) {
                NSRange r = [buf rangeOfData:[NSData dataWithBytes:"\n" length:1] options:0 range:NSMakeRange(0, buf.length)];
                if (r.location == NSNotFound) break;
                NSData *line = [buf subdataWithRange:NSMakeRange(0, r.location)]; [buf replaceBytesInRange:NSMakeRange(0, r.location + 1) withBytes:NULL length:0];
                NSDictionary *m = [NSJSONSerialization JSONObjectWithData:line options:0 error:nil]; if (![m isKindOfClass:NSDictionary.class]) continue;
                if ([m[@"stage"] isEqual:@"error"]) { lastError = m[@"text"]; continue; }
                if ([m[@"stage"] isEqual:@"done"]) done = YES;
                dispatch_async(dispatch_get_main_queue(), ^{ [self progress:[m[@"pct"] doubleValue] text:m[@"text"]]; });
            }
        };
        NSError *e = nil; [t launchAndReturnError:&e];
        if (!e) [t waitUntilExit];
        pipe.fileHandleForReading.readabilityHandler = nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (e) [self fail:@"Couldn't start the setup tools."];
            else if (self->_cancelled) [self showWaiting:nil];
            else if (done) [self finishedFrom:game inbox:inbox];
            else [self fail:lastError ?: @"Setup stopped unexpectedly."];
        });
    });
}
- (void)progress:(double)pct text:(NSString *)text
{
    _bar.doubleValue = pct;
    static BOOL once; if (pct > 30 && !once) { once = YES; [self shot:@"progress"]; }
    NSTimeInterval t = -_began.timeIntervalSinceNow;
    NSString *left = @"";
    if (pct > 8 && t > 8) { int m = (int)ceil(t * (100 - pct) / pct / 60); left = m <= 1 ? @"  ·  about a minute left" : [NSString stringWithFormat:@"  ·  about %d minutes left", m]; }
    _note.stringValue = [text stringByAppendingString:left];
}
/* the files that were dropped stay put unless the player says otherwise; what came through the app's own folder is removed */
- (void)finishedFrom:(NSString *)game inbox:(NSString *)inbox
{
    NSDictionary *cfg = _cfg; NSFileManager *fm = NSFileManager.defaultManager;
    if (game.length && [game hasPrefix:inbox]) { for (NSString *n in [fm contentsOfDirectoryAtPath:inbox error:nil]) [fm removeItemAtPath:[inbox stringByAppendingPathComponent:n] error:nil]; restart_app(cfg); return; }
    if (!game.length) { restart_app(cfg); return; }
    _busy = YES; _bar.hidden = YES; _title.stringValue = @"All Set";
    _body.stringValue = @"The app has its own copy of the game now.\nWould you like to remove the folder you dropped, to save space?"; _note.stringValue = game; _note.textColor = [NSColor colorWithWhite:0.5 alpha:1];
    [self button:_primary title:@"Keep It and Play"]; _onPrimary = ^{ restart_app(cfg); };
    [self button:_secondary title:@"Move to Trash"];
    _onSecondary = ^{ [NSWorkspace.sharedWorkspace recycleURLs:@[[NSURL fileURLWithPath:game]] completionHandler:^(NSDictionary *d, NSError *e) { restart_app(cfg); }]; };
    [self button:_tertiary title:nil]; _onTertiary = nil;
    [self autoPress:@"done"];
}
- (void)start:(NSDictionary *)cfg
{
    _cfg = cfg; _root = game_root(cfg); _inbox = [_root stringByAppendingPathComponent:@"Put Game Files Here"];
    [NSFileManager.defaultManager createDirectoryAtPath:_inbox withIntermediateDirectories:YES attributes:nil error:nil];
    [self buildWindow];
    NSMenu *bar = [NSMenu new]; NSMenuItem *it = [NSMenuItem new]; [bar addItem:it]; NSMenu *m = [NSMenu new];
    [m addItemWithTitle:[@"Quit " stringByAppendingString:cfg[@"name"]] action:@selector(terminate:) keyEquivalent:@"q"]; it.submenu = m; NSApp.mainMenu = bar;
}
- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    NSString *installed = [_root stringByAppendingPathComponent:@"game-files"];
    [_win makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    if ([NSFileManager.defaultManager fileExistsAtPath:[installed stringByAppendingPathComponent:_cfg[@"exe"]]]) { _updating = YES; [self runSetup:@""]; return; }   /* the game is here: an update changed its code */
    if (!_found && !_busy) [self showWaiting:nil];
    [self shot:@"waiting"];
    if (getenv("DS_SETUP_AUTO")) [self tryPath:@(getenv("DS_SETUP_AUTO"))];   /* tests: what a drop does */
    _scan = [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer *t) { [self scanInbox]; }];
}
- (void)application:(NSApplication *)app openFiles:(NSArray<NSString *> *)names
{
    [app replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
    if (!_busy && names.count) { [self showWaiting:nil]; [self tryPath:names[0]]; }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n { if (_task.running) kill(-_task.processIdentifier, SIGTERM); }
@end

/* Called from main() when the app is opened normally. Returns 1 with the three paths set once the game is ready; otherwise
 * it runs the setup window and never comes back (the app restarts into the launcher when setup is done). -1: not this kind
 * of build. */
int ds_easy_install(const char **exe, const char **game, const char **data)
{
    NSDictionary *cfg = game_config();
    if (!cfg) return -1;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *root = game_root(cfg), *rc = [root stringByAppendingPathComponent:@"recomp"];
    if (game_is_built(cfg)) {
        if (!getenv("DS_EASY_READY")) restart_app(cfg);                   /* binds the game at load: see make_release.sh */
        if (!dlsym(RTLD_DEFAULT, "rt_fntab")) unsetenv("DS_EASY_READY");
        else {
            [fm createDirectoryAtPath:[root stringByAppendingPathComponent:@"drive_c/Users/player/Documents"] withIntermediateDirectories:YES attributes:nil error:nil];
            NSString *log = [root stringByAppendingPathComponent:[cfg[@"log"] stringByAppendingString:@".log"]];
            if ([[fm attributesOfItemAtPath:log error:nil] fileSize] > 5000000) { NSString *old = [[log stringByDeletingPathExtension] stringByAppendingString:@".old.log"]; [fm removeItemAtPath:old error:nil]; [fm moveItemAtPath:log toPath:old error:nil]; }
            freopen(log.fileSystemRepresentation, "a", stderr);
            setenv("DS_LOG_FILE", log.fileSystemRepresentation, 1);
            NSString *upd = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"update"];
            if ([fm isExecutableFileAtPath:upd]) { setenv("DS_UPDATER", upd.fileSystemRepresentation, 1); setenv("DS_UPDATE_BRANCH", "release", 1); setenv("DS_BUILT_COMMIT", [cfg[@"release"] ?: @"" UTF8String], 1); }
            *exe = strdup([rc stringByAppendingPathComponent:cfg[@"exe"]].fileSystemRepresentation);
            *game = strdup([root stringByAppendingPathComponent:@"game-files"].fileSystemRepresentation);
            *data = strdup(root.fileSystemRepresentation);
            return 1;
        }
    }
    [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    SetupController *c = [SetupController new]; [c start:cfg]; NSApp.delegate = c;
    [NSApp run];
    exit(0);
}
