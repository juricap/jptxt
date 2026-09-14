#if defined(_WIN32) || defined(__APPLE__)
#error x11.cpp is for Unix
#endif

#include "jptxt.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xlocale.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>

static Display* g_dpy;
static Window   g_win;
static GC       g_gc;
static XFontSet g_fs;
static int      g_scr;
static App      g_app;
static int      g_font_px = 14;
static Atom     g_wm_delete, g_utf8, g_targets, g_clip, g_ptext, g_open;
static int      g_lockfd = -1;
static std::string g_clip_own;
static bool     g_have_clip = false;
static Pixmap   g_pixmap;
static int      g_pw, g_ph;
static Colormap g_cmap;
static int      g_depth;

static unsigned long xcol(uint32_t rgb) {
    XColor c{};
    c.red   = ((rgb >> 16) & 255) * 257;
    c.green = ((rgb >> 8) & 255) * 257;
    c.blue  = (rgb & 255) * 257;
    c.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(g_dpy, g_cmap, &c);
    return c.pixel;
}

static void load_fontset(int px) {
    if (g_fs) XFreeFontSet(g_dpy, g_fs);
    char spec[128];
    snprintf(spec, sizeof(spec),
             "-*-fixed-medium-r-normal-*-%d-*-*-*-*-*-iso10646-1,-*-fixed-medium-r-*-*-%d-*-*-*-*-*-*-*",
             px, px);
    char** miss = nullptr; int nmiss = 0; char* def = nullptr;
    g_fs = XCreateFontSet(g_dpy, spec, &miss, &nmiss, &def);
    if (nmiss && miss) XFreeStringList(miss);
    if (!g_fs) {
        g_fs = XCreateFontSet(g_dpy, "fixed", &miss, &nmiss, &def);
        if (nmiss && miss) XFreeStringList(miss);
    }
    XFontSetExtents* ext = XExtentsOfFontSet(g_fs);
    int cw = 8, ch = 16, as = 12;
    if (ext) {
        ch = ext->max_logical_extent.height;
        as = -ext->max_logical_extent.y;
        cw = ext->max_logical_extent.width;
        if (cw < 1) cw = 8;
        if (ch < 1) ch = 16;
    }
    app_set_metrics(&g_app, cw, ch, as);
}

void plat_set_font(int px) {
    g_font_px = px;
    g_app.font_px = px;
    if (g_dpy) {
        load_fontset(px);
        app_layout_scroll(&g_app);
        plat_invalidate();
    }
}

struct PaintCtx { Drawable dst; };
static void d_fill(void* ctx, int x, int y, int w, int h, uint32_t rgb) {
    auto* p = (PaintCtx*)ctx;
    XSetForeground(g_dpy, g_gc, xcol(rgb));
    XFillRectangle(g_dpy, p->dst, g_gc, x, y, w < 1 ? 1 : w, h < 1 ? 1 : h);
}
static void d_text(void* ctx, int x, int y, const char* s, int n, uint32_t rgb) {
    auto* p = (PaintCtx*)ctx;
    if (n <= 0) return;
    XSetForeground(g_dpy, g_gc, xcol(rgb));
    Xutf8DrawString(g_dpy, p->dst, g_fs, g_gc, x, y + g_app.ascent, s, n);
}

static void ensure_pixmap(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g_pixmap && w == g_pw && h == g_ph) return;
    if (g_pixmap) XFreePixmap(g_dpy, g_pixmap);
    g_pixmap = XCreatePixmap(g_dpy, g_win, w, h, g_depth);
    g_pw = w; g_ph = h;
}

static void paint() {
    XWindowAttributes wa{};
    XGetWindowAttributes(g_dpy, g_win, &wa);
    ensure_pixmap(wa.width, wa.height);
    PaintCtx pc{g_pixmap};
    Draw dr{};
    dr.ctx = &pc;
    dr.cell_w = g_app.cell_w;
    dr.cell_h = g_app.cell_h;
    dr.ascent = g_app.ascent;
    dr.fill = d_fill;
    dr.text = d_text;
    app_paint(&g_app, &dr, wa.width, wa.height);
    XCopyArea(g_dpy, g_pixmap, g_win, g_gc, 0, 0, wa.width, wa.height, 0, 0);
}

void plat_invalidate() {
    if (!g_dpy || !g_win) return;
    XWindowAttributes wa{};
    XGetWindowAttributes(g_dpy, g_win, &wa);
    XClearArea(g_dpy, g_win, 0, 0, wa.width, wa.height, True);
}
void plat_set_title(const char* utf8) {
    if (!g_dpy || !g_win) return;
    XStoreName(g_dpy, g_win, utf8);
    XSetIconName(g_dpy, g_win, utf8);
}
void plat_clipboard_set(const std::string& s) {
    g_clip_own = s;
    g_have_clip = true;
    if (g_dpy && g_win) XSetSelectionOwner(g_dpy, g_clip, g_win, CurrentTime);
}
bool plat_clipboard_get(std::string& s) {
    s.clear();
    Window owner = XGetSelectionOwner(g_dpy, g_clip);
    if (owner == g_win && g_have_clip) { s = g_clip_own; return true; }
    Atom prop = XInternAtom(g_dpy, "JPTXT_CLIP", False);
    XConvertSelection(g_dpy, g_clip, g_utf8, prop, g_win, CurrentTime);
    XEvent ev;
    for (int i = 0; i < 50; i++) {
        if (XCheckTypedWindowEvent(g_dpy, g_win, SelectionNotify, &ev)) {
            if (ev.xselection.property == None) return false;
            Atom type; int fmt; unsigned long n, extra; unsigned char* data = nullptr;
            XGetWindowProperty(g_dpy, g_win, prop, 0, 1 << 20, True, AnyPropertyType,
                               &type, &fmt, &n, &extra, &data);
            if (data) {
                s.assign((char*)data, n);
                XFree(data);
                return true;
            }
            return false;
        }
        usleep(2000);
    }
    return false;
}
void plat_scroll_set(int, int, int, int, int, int) {}
void plat_caret(int, int, int, bool) {}
void plat_cursor(int ibeam) {
    if (!g_dpy || !g_win) return;
    static Cursor beam, arrow;
    static bool init = false;
    if (!init) {
        beam = XCreateFontCursor(g_dpy, 152); // XC_xterm
        arrow = XCreateFontCursor(g_dpy, 68); // XC_left_ptr
        init = true;
    }
    XDefineCursor(g_dpy, g_win, ibeam ? beam : arrow);
}
int plat_ask_save(const char* name) {
    fprintf(stderr, "Save changes to %s? [y/n/c] ", name);
    fflush(stderr);
    char buf[8] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) return 0;
    if (buf[0] == 'y' || buf[0] == 'Y') return 1;
    if (buf[0] == 'n' || buf[0] == 'N') return 2;
    return 0;
}

static bool zenity(const char* args, std::string* out) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "zenity %s 2>/dev/null", args);
    FILE* f = popen(cmd, "r");
    if (!f) return false;
    char buf[4096];
    if (!fgets(buf, sizeof(buf), f)) { pclose(f); return false; }
    pclose(f);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    if (out) *out = buf;
    return n > 0;
}

bool plat_dlg_open(std::vector<std::string>& outv) {
    outv.clear();
    std::string p;
    if (zenity("--file-selection --title=Open", &p)) { outv.push_back(p); return true; }
    fprintf(stderr, "Open path: ");
    fflush(stderr);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return false;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    if (!n) return false;
    outv.emplace_back(buf);
    return true;
}
bool plat_dlg_save(std::string& path) {
    std::string p;
    if (zenity("--file-selection --save --confirm-overwrite --title=Save", &p)) { path = p; return true; }
    fprintf(stderr, "Save path: ");
    fflush(stderr);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return false;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    if (!n) return false;
    path = buf;
    return true;
}
bool plat_dlg_goto(int maxline, int cur, int* out) {
    char args[256];
    snprintf(args, sizeof(args), "--entry --title='Go to line' --text='Line 1-%d' --entry-text=%d", maxline, cur);
    std::string p;
    if (zenity(args, &p)) { *out = atoi(p.c_str()); return true; }
    fprintf(stderr, "Go to line: ");
    fflush(stderr);
    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) return false;
    *out = atoi(buf);
    return true;
}
void plat_beep() { if (g_dpy) XBell(g_dpy, 0); }
void plat_error(const char* utf8) {
    fprintf(stderr, "jptxt: %s\n", utf8);
    zenity("--error --title=jptxt --text='Could not save the file.'", nullptr);
}
void plat_about() {
    zenity("--info --title='About jptxt' --text='jptxt 0.1.2\\nA super-fast native notepad.\\nOne instance. Triple-Esc closes.'", nullptr);
}
void plat_find_dialog(bool replace) {
    std::string pat, repl;
    if (!zenity("--entry --title=Find --text='Find (regex)'", &pat)) {
        fprintf(stderr, "Find: ");
        fflush(stderr);
        char buf[512];
        if (!fgets(buf, sizeof(buf), stdin)) return;
        size_t n = strlen(buf);
        while (n && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0;
        pat = buf;
    }
    if (replace) {
        if (!zenity("--entry --title=Replace --text='Replace with'", &repl)) {
            fprintf(stderr, "Replace with: ");
            fflush(stderr);
            char buf[512];
            if (fgets(buf, sizeof(buf), stdin)) {
                size_t n = strlen(buf);
                while (n && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0;
                repl = buf;
            }
        }
    }
    app_find(&g_app, pat.c_str(), repl.c_str(), 2, replace ? 2 : 0);
}

static uint32_t xmods(unsigned st) {
    uint32_t m = 0;
    if (st & ShiftMask) m |= JP_SHIFT;
    if (st & ControlMask) m |= JP_CTRL;
    if (st & Mod1Mask) m |= JP_ALT;
    return m;
}

static void handle_key(XKeyEvent* e) {
    KeySym ks = 0;
    char buf[32];
    int n = XLookupString(e, buf, sizeof(buf), &ks, nullptr);
    uint32_t mods = xmods(e->state);
    if (mods & JP_CTRL) {
        KeySym lks = XLookupKeysym(e, 0);
        if (lks >= 'A' && lks <= 'Z') lks += 32;
        switch (lks) {
        case 'n': app_cmd(&g_app, CMD_NEW); return;
        case 'o': app_cmd(&g_app, CMD_OPEN); return;
        case 's': app_cmd(&g_app, CMD_SAVE); return;
        case 'w': app_cmd(&g_app, CMD_CLOSE); return;
        case 'q': app_cmd(&g_app, CMD_EXIT); return;
        case 'z': app_cmd(&g_app, CMD_UNDO); return;
        case 'y': app_cmd(&g_app, CMD_REDO); return;
        case 'x': app_cmd(&g_app, CMD_CUT); return;
        case 'c': app_cmd(&g_app, CMD_COPY); return;
        case 'v': app_cmd(&g_app, CMD_PASTE); return;
        case 'a': app_cmd(&g_app, CMD_SELALL); return;
        case 'f': app_cmd(&g_app, CMD_FIND); return;
        case 'h': app_cmd(&g_app, (mods & JP_SHIFT) ? CMD_HEX : CMD_REPLACE); return;
        case 'g': app_cmd(&g_app, CMD_GOTO); return;
        case '0': app_cmd(&g_app, CMD_FONT_RST); return;
        case XK_plus: case XK_equal: case XK_KP_Add: app_cmd(&g_app, CMD_FONT_UP); return;
        case XK_minus: case XK_KP_Subtract: app_cmd(&g_app, CMD_FONT_DN); return;
        case XK_Tab: app_cmd(&g_app, (mods & JP_SHIFT) ? CMD_PREVTAB : CMD_NEXTTAB); return;
        }
    }
    Key k;
    bool spec = true;
    switch (ks) {
    case XK_Left: k = Key::Left; break;
    case XK_Right: k = Key::Right; break;
    case XK_Up: k = Key::Up; break;
    case XK_Down: k = Key::Down; break;
    case XK_Home: k = Key::Home; break;
    case XK_End: k = Key::End; break;
    case XK_Page_Up: k = Key::PageUp; break;
    case XK_Page_Down: k = Key::PageDown; break;
    case XK_BackSpace: k = Key::Backspace; break;
    case XK_Delete: k = Key::Del; break;
    case XK_Tab: k = Key::Tab; break;
    case XK_Return: k = Key::Enter; break;
    case XK_Escape: k = Key::Escape; break;
    case XK_Insert: k = Key::Insert; break;
    case XK_F3: k = Key::F3; break;
    default: spec = false; break;
    }
    if (spec) { app_key(&g_app, k, mods); return; }
    if (n > 0 && (unsigned char)buf[0] >= 32 && !(mods & JP_CTRL)) {
        int adv = 0;
        uint32_t cp = utf8_next((const uint8_t*)buf, (const uint8_t*)buf + n, &adv);
        if (cp) app_char(&g_app, cp);
    }
}

static char g_lockpath[256];

static int take_singleton_lock() {
    snprintf(g_lockpath, sizeof(g_lockpath), "/tmp/jptxt-%d.lock", (int)getuid());
    int fd = open(g_lockpath, O_CREAT | O_RDWR, 0644);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return -2; }
    return fd;
}

static void write_lock_window(Window w) {
    if (g_lockfd < 0) return;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%lu\n", (unsigned long)w);
    lseek(g_lockfd, 0, SEEK_SET);
    if (ftruncate(g_lockfd, 0) == 0) {
        ssize_t wr = write(g_lockfd, buf, (size_t)n);
        (void)wr;
    }
}

static Window read_lock_window() {
    FILE* f = fopen(g_lockpath, "r");
    if (!f) return 0;
    unsigned long w = 0;
    int ok = fscanf(f, "%lu", &w);
    fclose(f);
    return ok == 1 ? (Window)w : 0;
}

static int xerr(Display*, XErrorEvent* e) {
    char buf[256];
    XGetErrorText(g_dpy, e->error_code, buf, sizeof(buf));
    fprintf(stderr, "jptxt X error: %s req=%u res=0x%lx\n",
            buf, e->request_code, (unsigned long)e->resourceid);
    return 0;
}

static void x11_ingest_open_prop(Window w) {
    Atom type; int fmt; unsigned long n = 0, extra = 0; unsigned char* data = nullptr;
    if (XGetWindowProperty(g_dpy, w, g_open, 0, 1 << 16, True, AnyPropertyType,
                           &type, &fmt, &n, &extra, &data) == Success && data) {
        app_open_blob(&g_app, (char*)data, n);
        XFree(data);
        plat_set_title(app_title(&g_app));
        plat_invalidate();
    }
}

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    if (argc >= 2 && strcmp(argv[1], "--selftest") == 0)
        return doc_selftest(stdout);
    if (argc >= 3 && strcmp(argv[1], "--bench") == 0)
        return doc_bench(argv[2], stdout);

    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) { fprintf(stderr, "jptxt: cannot open X display\n"); return 1; }
    XSetErrorHandler(xerr);
    g_scr = DefaultScreen(g_dpy);
    g_cmap = DefaultColormap(g_dpy, g_scr);
    g_depth = DefaultDepth(g_dpy, g_scr);
    g_open = XInternAtom(g_dpy, "JPTXT_OPEN", False);

    g_lockfd = take_singleton_lock();
    if (g_lockfd == -2) {
        Window exist = read_lock_window();
        if (exist) {
            std::string blob;
            for (int i = 1; i < argc; i++) {
                if (!blob.empty()) blob.push_back('\n');
                blob += argv[i];
            }
            if (!blob.empty())
                XChangeProperty(g_dpy, exist, g_open, XA_STRING, 8, PropModeReplace,
                                (unsigned char*)blob.data(), (int)blob.size());
            XClientMessageEvent ev{};
            ev.type = ClientMessage;
            ev.window = exist;
            ev.message_type = g_open;
            ev.format = 32;
            ev.data.l[0] = 1;
            XSendEvent(g_dpy, exist, False, NoEventMask, (XEvent*)&ev);
            XMapRaised(g_dpy, exist);
            XFlush(g_dpy);
        }
        XCloseDisplay(g_dpy);
        return 0;
    }

    app_init(&g_app);
    load_fontset(14);

    unsigned long bg = xcol(COL_BG);
    Window root = DefaultRootWindow(g_dpy);
    g_win = XCreateSimpleWindow(g_dpy, root, 80, 80, 1100, 720, 0, bg, bg);
    if (!g_win) {
        fprintf(stderr, "jptxt: XCreateSimpleWindow failed (root=%lu)\n", (unsigned long)root);
        return 1;
    }
    write_lock_window(g_win);
    g_gc = XCreateGC(g_dpy, g_win, 0, nullptr);
    XSelectInput(g_dpy, g_win,
        ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | StructureNotifyMask | FocusChangeMask | PropertyChangeMask);
    g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    g_utf8 = XInternAtom(g_dpy, "UTF8_STRING", False);
    g_clip = XInternAtom(g_dpy, "CLIPBOARD", False);
    g_targets = XInternAtom(g_dpy, "TARGETS", False);
    g_ptext = XA_STRING;
    XSetWMProtocols(g_dpy, g_win, &g_wm_delete, 1);
    plat_set_title("jptxt");
    XMapWindow(g_dpy, g_win);

    for (int i = 1; i < argc; i++) app_open_path(&g_app, argv[i]);
    plat_set_title(app_title(&g_app));

    bool down = false;
    for (;;) {
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        if (g_app.quit) break;
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) paint();
            break;
        case ConfigureNotify:
            app_resize(&g_app, ev.xconfigure.width, ev.xconfigure.height);
            break;
        case KeyPress:
            handle_key(&ev.xkey);
            if (g_app.quit) goto done;
            break;
        case ButtonPress:
            if (ev.xbutton.button == 4) { app_wheel(&g_app, -3, xmods(ev.xbutton.state)); break; }
            if (ev.xbutton.button == 5) { app_wheel(&g_app, 3, xmods(ev.xbutton.state)); break; }
            down = ev.xbutton.button == 1;
            app_mouse_down(&g_app, ev.xbutton.x, ev.xbutton.y,
                           ev.xbutton.button == 2 ? 3 : 1, xmods(ev.xbutton.state));
            break;
        case ButtonRelease:
            down = false;
            app_mouse_up(&g_app, ev.xbutton.x, ev.xbutton.y, 1);
            break;
        case MotionNotify:
            plat_cursor(app_cursor_at(&g_app, ev.xmotion.x, ev.xmotion.y));
            if (down) app_mouse_move(&g_app, ev.xmotion.x, ev.xmotion.y, xmods(ev.xmotion.state));
            break;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == g_wm_delete) {
                app_cmd(&g_app, CMD_EXIT);
                if (g_app.quit) goto done;
            } else if (ev.xclient.message_type == g_open) {
                x11_ingest_open_prop(g_win);
            }
            break;
        case PropertyNotify:
            if (ev.xproperty.atom == g_open && ev.xproperty.state == PropertyNewValue)
                x11_ingest_open_prop(g_win);
            break;
        case SelectionRequest: {
            XSelectionRequestEvent* req = &ev.xselectionrequest;
            XEvent r{};
            r.xselection.type = SelectionNotify;
            r.xselection.display = req->display;
            r.xselection.requestor = req->requestor;
            r.xselection.selection = req->selection;
            r.xselection.target = req->target;
            r.xselection.time = req->time;
            r.xselection.property = None;
            if (g_have_clip && (req->target == g_utf8 || req->target == XA_STRING)) {
                XChangeProperty(g_dpy, req->requestor, req->property, req->target, 8, PropModeReplace,
                                (unsigned char*)g_clip_own.data(), (int)g_clip_own.size());
                r.xselection.property = req->property;
            } else if (req->target == g_targets) {
                Atom t[2] = {g_utf8, XA_STRING};
                XChangeProperty(g_dpy, req->requestor, req->property, XA_ATOM, 32, PropModeReplace,
                                (unsigned char*)t, 2);
                r.xselection.property = req->property;
            }
            XSendEvent(g_dpy, req->requestor, True, 0, &r);
            break;
        }
        }
    }
done:
    if (g_pixmap) XFreePixmap(g_dpy, g_pixmap);
    if (g_fs) XFreeFontSet(g_dpy, g_fs);
    XFreeGC(g_dpy, g_gc);
    XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
    return 0;
}

