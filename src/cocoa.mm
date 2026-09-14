#if !defined(__APPLE__)
#error cocoa.mm is macOS only
#endif

#import <Cocoa/Cocoa.h>
#include "jptxt.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>

static App g_app;
static NSWindow* g_win;
static NSView* g_view;
static NSFont* g_font;
static int g_font_px = 14;
static NSTrackingArea* g_track;

static NSString* u2ns(const char* s, int n = -1) {
    if (!s) return @"";
    if (n < 0) return [NSString stringWithUTF8String:s];
    return [[NSString alloc] initWithBytes:s length:(NSUInteger)n encoding:NSUTF8StringEncoding];
}

static void metrics_from_font() {
    NSSize sz = [@"M" sizeWithAttributes:@{NSFontAttributeName: g_font}];
    CGFloat asc = g_font.ascender;
    CGFloat h = ceil(g_font.ascender - g_font.descender + g_font.leading);
    app_set_metrics(&g_app, (int)ceil(sz.width), (int)h, (int)ceil(asc));
}

void plat_set_font(int px) {
    g_font_px = px;
    g_app.font_px = px;
    g_font = [NSFont userFixedPitchFontOfSize:(CGFloat)px];
    if (!g_font) g_font = [NSFont fontWithName:@"Menlo" size:(CGFloat)px];
    metrics_from_font();
    app_layout_scroll(&g_app);
    plat_invalidate();
}

static NSColor* nsrgb(uint32_t rgb) {
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 255) / 255.0
                                     green:((rgb >> 8) & 255) / 255.0
                                      blue:(rgb & 255) / 255.0
                                     alpha:1];
}

struct PaintCtx { };
static void d_fill(void*, int x, int y, int w, int h, uint32_t rgb) {
    NSRect r = NSMakeRect(x, y, w, h);
    [nsrgb(rgb) setFill];
    NSRectFill(r);
}
static void d_text(void*, int x, int y, const char* s, int n, uint32_t rgb) {
    if (n <= 0) return;
    NSString* str = u2ns(s, n);
    NSDictionary* attrs = @{
        NSFontAttributeName: g_font,
        NSForegroundColorAttributeName: nsrgb(rgb)
    };
    [str drawAtPoint:NSMakePoint(x, y) withAttributes:attrs];
}

@interface JPView : NSView <NSWindowDelegate>
@end

@implementation JPView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSRect b = self.bounds;
    PaintCtx pc{};
    Draw dr{};
    dr.ctx = &pc;
    dr.cell_w = g_app.cell_w;
    dr.cell_h = g_app.cell_h;
    dr.ascent = g_app.ascent;
    dr.fill = d_fill;
    dr.text = d_text;
    app_paint(&g_app, &dr, (int)b.size.width, (int)b.size.height);
}
- (void)setFrameSize:(NSSize)s {
    [super setFrameSize:s];
    app_resize(&g_app, (int)s.width, (int)s.height);
}
static uint32_t mods_of(NSEvent* e) {
    uint32_t m = 0;
    NSEventModifierFlags f = e.modifierFlags;
    if (f & NSEventModifierFlagShift) m |= JP_SHIFT;
    if (f & NSEventModifierFlagCommand) m |= JP_CTRL; // Cmd as Ctrl for shortcuts
    if (f & NSEventModifierFlagControl) m |= JP_CTRL;
    if (f & NSEventModifierFlagOption) m |= JP_ALT;
    return m;
}
- (void)keyDown:(NSEvent*)e {
    uint32_t mods = mods_of(e);
    NSString* ch = e.charactersIgnoringModifiers;
    unsigned short vk = e.keyCode;
    Key k;
    bool handled = true;
    switch (vk) {
    case 123: k = Key::Left; break;
    case 124: k = Key::Right; break;
    case 125: k = Key::Down; break;
    case 126: k = Key::Up; break;
    case 115: k = Key::Home; break;
    case 119: k = Key::End; break;
    case 116: k = Key::PageUp; break;
    case 121: k = Key::PageDown; break;
    case 51:  k = Key::Backspace; break;
    case 117: k = Key::Del; break;
    case 48:  k = Key::Tab; break;
    case 36:  k = Key::Enter; break;
    case 53:  k = Key::Escape; break;
    case 114: k = Key::Insert; break;
    case 99:  k = Key::F3; break;
    default: handled = false; break;
    }
    if (mods & JP_CTRL) {
        NSString* c = e.charactersIgnoringModifiers.lowercaseString;
        if ([c isEqualTo:@"n"]) { app_cmd(&g_app, CMD_NEW); return; }
        if ([c isEqualTo:@"o"]) { app_cmd(&g_app, CMD_OPEN); return; }
        if ([c isEqualTo:@"s"]) { app_cmd(&g_app, CMD_SAVE); return; }
        if ([c isEqualTo:@"w"]) { app_cmd(&g_app, CMD_CLOSE); return; }
        if ([c isEqualTo:@"z"]) { app_cmd(&g_app, CMD_UNDO); return; }
        if ([c isEqualTo:@"y"]) { app_cmd(&g_app, CMD_REDO); return; }
        if ([c isEqualTo:@"x"]) { app_cmd(&g_app, CMD_CUT); return; }
        if ([c isEqualTo:@"c"]) { app_cmd(&g_app, CMD_COPY); return; }
        if ([c isEqualTo:@"v"]) { app_cmd(&g_app, CMD_PASTE); return; }
        if ([c isEqualTo:@"a"]) { app_cmd(&g_app, CMD_SELALL); return; }
        if ([c isEqualTo:@"f"]) { app_cmd(&g_app, CMD_FIND); return; }
        if ([c isEqualTo:@"h"]) { app_cmd(&g_app, (mods & JP_SHIFT) ? CMD_HEX : CMD_REPLACE); return; }
        if ([c isEqualTo:@"g"]) { app_cmd(&g_app, CMD_GOTO); return; }
        if ([c isEqualTo:@"0"]) { app_cmd(&g_app, CMD_FONT_RST); return; }
        if ([c isEqualTo:@"="] || [c isEqualTo:@"+"]) { app_cmd(&g_app, CMD_FONT_UP); return; }
        if ([c isEqualTo:@"-"]) { app_cmd(&g_app, CMD_FONT_DN); return; }
        if (vk == 48) { app_cmd(&g_app, (mods & JP_SHIFT) ? CMD_PREVTAB : CMD_NEXTTAB); return; }
    }
    if (handled) {
        app_key(&g_app, k, mods);
        if (g_app.quit) [NSApp terminate:nil];
        return;
    }
    NSString* chars = e.characters;
    if (chars.length) {
        unichar u = [chars characterAtIndex:0];
        if (u >= 32) app_char(&g_app, u);
    }
}
- (void)mouseDown:(NSEvent*)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    app_mouse_down(&g_app, (int)p.x, (int)p.y, 1, mods_of(e));
}
- (void)mouseUp:(NSEvent*)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    app_mouse_up(&g_app, (int)p.x, (int)p.y, 1);
}
- (void)mouseDragged:(NSEvent*)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    app_mouse_move(&g_app, (int)p.x, (int)p.y, mods_of(e));
}
- (void)otherMouseDown:(NSEvent*)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    app_mouse_down(&g_app, (int)p.x, (int)p.y, 3, 0);
}
- (void)scrollWheel:(NSEvent*)e {
    int lines = (int)(-e.scrollingDeltaY);
    if (lines == 0) lines = e.scrollingDeltaY < 0 ? 1 : -1;
    uint32_t m = 0;
    if (e.modifierFlags & NSEventModifierFlagCommand) m |= JP_CTRL;
    app_wheel(&g_app, lines, m);
}
- (void)jpCmd:(id)sender {
    app_cmd(&g_app, (int)[sender tag]);
    if (g_app.quit) [NSApp terminate:nil];
}
- (void)windowWillClose:(NSNotification*)n {
    (void)n;
    app_cmd(&g_app, CMD_EXIT);
    if (!g_app.quit) {
        // cancelled: keep window — Cocoa will still close. Re-open is hard; just terminate if quit.
    }
    if (g_app.quit) [NSApp terminate:nil];
}
@end

static void build_menu() {
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenu* app = [[NSMenu alloc] init];
    [app addItemWithTitle:@"About jptxt" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    appItem.submenu = app;
    [bar addItem:appItem];

    auto add = [](NSMenu* m, NSString* t, int cmd, NSString* k) {
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:t action:@selector(jpCmd:) keyEquivalent:k];
        it.tag = cmd;
        [m addItem:it];
    };

    NSMenu* file = [[NSMenu alloc] initWithTitle:@"File"];
    add(file, @"New", CMD_NEW, @"n");
    add(file, @"Open…", CMD_OPEN, @"o");
    add(file, @"Save", CMD_SAVE, @"s");
    add(file, @"Save As…", CMD_SAVEAS, @"S");
    add(file, @"Close", CMD_CLOSE, @"w");
    NSMenuItem* fi = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    fi.submenu = file; [bar addItem:fi];

    NSMenu* ed = [[NSMenu alloc] initWithTitle:@"Edit"];
    add(ed, @"Undo", CMD_UNDO, @"z");
    add(ed, @"Redo", CMD_REDO, @"y");
    [ed addItem:[NSMenuItem separatorItem]];
    add(ed, @"Cut", CMD_CUT, @"x");
    add(ed, @"Copy", CMD_COPY, @"c");
    add(ed, @"Paste", CMD_PASTE, @"v");
    add(ed, @"Select All", CMD_SELALL, @"a");
    NSMenuItem* ei = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    ei.submenu = ed; [bar addItem:ei];

    NSMenu* se = [[NSMenu alloc] initWithTitle:@"Search"];
    add(se, @"Find…", CMD_FIND, @"f");
    add(se, @"Find Next", CMD_FINDNEXT, @"g");
    add(se, @"Replace…", CMD_REPLACE, @"h");
    add(se, @"Go to Line…", CMD_GOTO, @"l");
    NSMenuItem* si = [[NSMenuItem alloc] initWithTitle:@"Search" action:nil keyEquivalent:@""];
    si.submenu = se; [bar addItem:si];

    NSMenu* vw = [[NSMenu alloc] initWithTitle:@"View"];
    add(vw, @"Text mode", CMD_TEXT, @"");
    add(vw, @"Hex mode", CMD_HEX, @"H");
    NSMenuItem* vi = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
    vi.submenu = vw; [bar addItem:vi];

    [NSApp setMainMenu:bar];
}

@interface JPApp : NSResponder
@end
@implementation JPApp
- (void)jpCmd:(id)sender {
    app_cmd(&g_app, (int)[sender tag]);
    if (g_app.quit) [NSApp terminate:nil];
}
@end

void plat_invalidate() { [g_view setNeedsDisplay:YES]; }
void plat_set_title(const char* utf8) { [g_win setTitle:u2ns(utf8)]; }
void plat_clipboard_set(const std::string& s) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:u2ns(s.c_str(), (int)s.size()) forType:NSPasteboardTypeString];
}
bool plat_clipboard_get(std::string& s) {
    NSString* t = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!t) return false;
    s = t.UTF8String ? t.UTF8String : "";
    return true;
}
void plat_scroll_set(int, int, int, int, int, int) {}
void plat_caret(int, int, int, bool) {}
void plat_cursor(int ibeam) {
    if (ibeam) [[NSCursor IBeamCursor] set];
    else [[NSCursor arrowCursor] set];
}
int plat_ask_save(const char* name) {
    NSAlert* al = [[NSAlert alloc] init];
    al.messageText = [NSString stringWithFormat:@"Save changes to %s?", name];
    [al addButtonWithTitle:@"Save"];
    [al addButtonWithTitle:@"Don't Save"];
    [al addButtonWithTitle:@"Cancel"];
    NSModalResponse r = [al runModal];
    if (r == NSAlertFirstButtonReturn) return 1;
    if (r == NSAlertSecondButtonReturn) return 2;
    return 0;
}
bool plat_dlg_open(std::vector<std::string>& out) {
    out.clear();
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.allowsMultipleSelection = YES;
    p.canChooseDirectories = NO;
    if ([p runModal] != NSModalResponseOK) return false;
    for (NSURL* u in p.URLs) out.push_back(u.path.UTF8String);
    return !out.empty();
}
bool plat_dlg_save(std::string& path) {
    NSSavePanel* p = [NSSavePanel savePanel];
    if ([p runModal] != NSModalResponseOK) return false;
    path = p.URL.path.UTF8String;
    return true;
}
bool plat_dlg_goto(int maxline, int cur, int* out) {
    NSAlert* al = [[NSAlert alloc] init];
    al.messageText = [NSString stringWithFormat:@"Go to line (1–%d)", maxline];
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 120, 24)];
    tf.stringValue = [NSString stringWithFormat:@"%d", cur];
    al.accessoryView = tf;
    [al addButtonWithTitle:@"OK"];
    [al addButtonWithTitle:@"Cancel"];
    if ([al runModal] != NSAlertFirstButtonReturn) return false;
    *out = tf.intValue;
    return true;
}
void plat_beep() { NSBeep(); }
void plat_about() {
    NSAlert* al = [[NSAlert alloc] init];
    al.messageText = @"jptxt 0.1.1";
    al.informativeText = @"A super-fast native notepad.\nmmap + piece table, syntax, hex lister.\nOne instance. Triple-Esc closes.\nNo Electron, no scripting, no sidebars.";
    [al runModal];
}
void plat_find_dialog(bool replace) {
    NSAlert* al = [[NSAlert alloc] init];
    al.messageText = replace ? @"Replace" : @"Find (regex)";
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    tf.stringValue = u2ns(g_app.find_pat.c_str());
    NSTextField* rf = nil;
    if (replace) {
        NSView* box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 240, 56)];
        tf.frame = NSMakeRect(0, 28, 240, 24);
        rf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
        rf.placeholderString = @"Replace with";
        [box addSubview:tf];
        [box addSubview:rf];
        al.accessoryView = box;
    } else al.accessoryView = tf;
    [al addButtonWithTitle:replace ? @"Replace" : @"Find"];
    [al addButtonWithTitle:@"Cancel"];
    if ([al runModal] != NSAlertFirstButtonReturn) return;
    std::string pat = tf.stringValue.UTF8String;
    std::string repl = rf ? rf.stringValue.UTF8String : "";
    app_find(&g_app, pat.c_str(), repl.c_str(), 2, replace ? 2 : 0);
}

static int take_singleton_lock() {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/jptxt-%d.lock", (int)getuid());
    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return -2; }
    return fd;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        static int g_lockfd = -1;
        g_lockfd = take_singleton_lock();
        if (g_lockfd == -2) {
            NSMutableArray* parts = [NSMutableArray array];
            for (int i = 1; i < argc; i++)
                [parts addObject:[NSString stringWithUTF8String:argv[i]]];
            NSString* blob = [parts componentsJoinedByString:@"\n"];
            [[NSDistributedNotificationCenter defaultCenter]
                postNotificationName:@"net.juricap.jptxt.open"
                              object:blob
                            userInfo:nil
                  deliverImmediately:YES];
            return 0;
        }

        app_init(&g_app);
        plat_set_font(14);
        [[NSDistributedNotificationCenter defaultCenter]
            addObserverForName:@"net.juricap.jptxt.open"
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* n) {
            NSString* obj = [n object];
            if (obj.length) app_open_blob(&g_app, obj.UTF8String, strlen(obj.UTF8String));
            [g_win makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            plat_set_title(app_title(&g_app));
        }];

        NSRect r = NSMakeRect(200, 200, 1100, 720);
        g_win = [[NSWindow alloc] initWithContentRect:r
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered defer:NO];
        JPView* v = [[JPView alloc] initWithFrame:r];
        g_view = v;
        g_win.contentView = v;
        g_win.delegate = v;
        [g_win setAcceptsMouseMovedEvents:YES];
        build_menu();
        for (int i = 1; i < argc; i++) app_open_path(&g_app, argv[i]);
        plat_set_title(app_title(&g_app));
        [g_win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

