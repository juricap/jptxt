#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

#include "jptxt.h"
#include "resource.h"

static HWND g_hwnd;
static HANDLE g_single;
static App  g_app;
static HFONT g_font;
static int   g_font_px = 14;
static HDC   g_mdc;
static HBITMAP g_bmp, g_oldbmp;
static int g_bw, g_bh;
static HACCEL g_accel;
static FINDREPLACEW g_fr;
static wchar_t g_find[512], g_repl[512];
static UINT g_findmsg;
static HWND g_findwnd;
static bool g_tracking = false;

static std::wstring u2w(const char* s, int n = -1) {
    if (!s) return L"";
    if (n < 0) n = (int)strlen(s);
    if (n == 0) return L"";
    int wn = MultiByteToWideChar(CP_UTF8, 0, s, n, nullptr, 0);
    std::wstring w(wn, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, n, w.data(), wn);
    return w;
}
static std::string w2u(const wchar_t* s, int n = -1) {
    if (!s) return {};
    if (n < 0) n = (int)wcslen(s);
    if (n == 0) return {};
    int un = WideCharToMultiByte(CP_UTF8, 0, s, n, nullptr, 0, nullptr, nullptr);
    std::string u(un, 0);
    WideCharToMultiByte(CP_UTF8, 0, s, n, u.data(), un, nullptr, nullptr);
    return u;
}

static COLORREF cr(uint32_t rgb) {
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

static void make_font(int px) {
    if (g_font) DeleteObject(g_font);
    g_font_px = px;
    g_app.font_px = px;
    LOGFONTW lf{};
    lf.lfHeight = -px;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy(lf.lfFaceName, L"Consolas");
    g_font = CreateFontIndirectW(&lf);
    HDC hdc = GetDC(g_hwnd ? g_hwnd : nullptr);
    HGDIOBJ old = SelectObject(hdc, g_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, L"M", 1, &sz);
    SelectObject(hdc, old);
    ReleaseDC(g_hwnd, hdc);
    app_set_metrics(&g_app, sz.cx, tm.tmHeight, tm.tmAscent);
}

static void resize_back(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g_mdc && w == g_bw && h == g_bh) return;
    HDC wdc = GetDC(g_hwnd);
    if (!g_mdc) g_mdc = CreateCompatibleDC(wdc);
    else if (g_oldbmp) SelectObject(g_mdc, g_oldbmp);
    if (g_bmp) DeleteObject(g_bmp);
    g_bmp = CreateCompatibleBitmap(wdc, w, h);
    g_oldbmp = (HBITMAP)SelectObject(g_mdc, g_bmp);
    g_bw = w; g_bh = h;
    ReleaseDC(g_hwnd, wdc);
}

struct PaintCtx {
    HDC hdc;
    RECT clip;
};

static void d_fill(void* ctx, int x, int y, int w, int h, uint32_t rgb) {
    auto* p = (PaintCtx*)ctx;
    RECT r{x, y, x + w, y + h};
    HBRUSH b = CreateSolidBrush(cr(rgb));
    FillRect(p->hdc, &r, b);
    DeleteObject(b);
}
static void d_text(void* ctx, int x, int y, const char* s, int n, uint32_t rgb) {
    auto* p = (PaintCtx*)ctx;
    if (n <= 0) return;
    wchar_t wbuf[2048];
    int wn = MultiByteToWideChar(CP_UTF8, 0, s, n, wbuf, 2048);
    if (wn <= 0) return;
    SetTextColor(p->hdc, cr(rgb));
    SetBkMode(p->hdc, TRANSPARENT);
    ExtTextOutW(p->hdc, x, y, ETO_CLIPPED, &p->clip, wbuf, wn, nullptr);
}

static void paint() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    resize_back(w, h);
    PaintCtx pc{g_mdc, rc};
    SelectObject(g_mdc, g_font);
    Draw dr{};
    dr.ctx = &pc;
    dr.cell_w = g_app.cell_w;
    dr.cell_h = g_app.cell_h;
    dr.ascent = g_app.ascent;
    dr.fill = d_fill;
    dr.text = d_text;
    HideCaret(g_hwnd);
    app_paint(&g_app, &dr, w, h);
    int cx, cy, ch;
    bool vis = app_caret_px(&g_app, &cx, &cy, &ch);
    plat_caret(cx, cy, ch, vis);
    if (vis) ShowCaret(g_hwnd);
}

void plat_invalidate() {
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}
void plat_set_title(const char* utf8) {
    if (!g_hwnd) return;
    SetWindowTextW(g_hwnd, u2w(utf8).c_str());
}
void plat_clipboard_set(const std::string& s) {
    std::wstring w = u2w(s.c_str(), (int)s.size());
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* p = GlobalLock(h);
        memcpy(p, w.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
bool plat_clipboard_get(std::string& s) {
    s.clear();
    if (!OpenClipboard(g_hwnd)) return false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    bool ok = false;
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) { s = w2u(p); GlobalUnlock(h); ok = true; }
    }
    CloseClipboard();
    return ok;
}
void plat_scroll_set(int vmax, int vpage, int vpos, int hmax, int hpage, int hpos) {
    if (!g_hwnd) return;
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0; si.nMax = vmax < 0 ? 0 : vmax; si.nPage = vpage < 1 ? 1 : (UINT)vpage; si.nPos = vpos;
    SetScrollInfo(g_hwnd, SB_VERT, &si, TRUE);
    si.nMax = hmax < 0 ? 0 : hmax; si.nPage = hpage < 1 ? 1 : (UINT)hpage; si.nPos = hpos;
    SetScrollInfo(g_hwnd, SB_HORZ, &si, TRUE);
}
void plat_caret(int x, int y, int h, bool show) {
    if (!g_hwnd) return;
    DestroyCaret();
    if (!show) return;
    CreateCaret(g_hwnd, nullptr, 1, h);
    SetCaretPos(x, y);
}
void plat_cursor(int ibeam) {
    SetCursor(LoadCursor(nullptr, ibeam ? IDC_IBEAM : IDC_ARROW));
}
int plat_ask_save(const char* name) {
    wchar_t msg[512];
    swprintf(msg, 512, L"Save changes to %s?", u2w(name).c_str());
    int r = MessageBoxW(g_hwnd, msg, L"jptxt", MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDYES) return 1;
    if (r == IDNO) return 2;
    return 0;
}
bool plat_dlg_open(std::vector<std::string>& out) {
    out.clear();
    wchar_t buf[32768];
    buf[0] = 0;
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"All files\0*.*\0Text\0*.txt;*.md;*.c;*.h;*.cpp;*.rs;*.py;*.js;*.json;*.xml;*.html;*.css;*.go;*.java\0";
    of.lpstrFile = buf;
    of.nMaxFile = 32768;
    of.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return false;
    // multi-select: dir\0file\0file\0\0  or single path
    wchar_t* p = buf;
    std::wstring dir = p;
    p += dir.size() + 1;
    if (*p == 0) {
        out.push_back(w2u(dir.c_str()));
        return true;
    }
    while (*p) {
        std::wstring full = dir + L"\\" + p;
        out.push_back(w2u(full.c_str()));
        p += wcslen(p) + 1;
    }
    return !out.empty();
}
bool plat_dlg_save(std::string& path) {
    wchar_t buf[32768];
    buf[0] = 0;
    if (!path.empty()) {
        std::wstring w = u2w(path.c_str());
        wcsncpy(buf, w.c_str(), 32760);
    }
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"All files\0*.*\0Text\0*.txt\0";
    of.lpstrFile = buf;
    of.nMaxFile = 32768;
    of.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    of.lpstrDefExt = L"txt";
    if (!GetSaveFileNameW(&of)) return false;
    path = w2u(buf);
    return true;
}

static int g_goto_max, g_goto_cur, g_goto_out;
static INT_PTR CALLBACK GotoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    (void)l;
    if (m == WM_INITDIALOG) {
        wchar_t t[32];
        swprintf(t, 32, L"%d", g_goto_cur);
        SetDlgItemTextW(h, IDC_LINE, t);
        SendDlgItemMessageW(h, IDC_LINE, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(h, IDC_LINE));
        return FALSE;
    }
    if (m == WM_COMMAND) {
        if (LOWORD(w) == IDOK) {
            wchar_t t[32];
            GetDlgItemTextW(h, IDC_LINE, t, 32);
            g_goto_out = _wtoi(t);
            EndDialog(h, 1);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, 0); return TRUE; }
    }
    return FALSE;
}
bool plat_dlg_goto(int maxline, int cur, int* out) {
    g_goto_max = maxline; g_goto_cur = cur; g_goto_out = cur;
    if (DialogBoxParamW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDD_GOTO), g_hwnd, GotoProc, 0) != 1)
        return false;
    *out = g_goto_out;
    return true;
}
void plat_beep() { MessageBeep(MB_ICONWARNING); }
void plat_error(const char* utf8) {
    MessageBoxW(g_hwnd, u2w(utf8).c_str(), L"jptxt", MB_OK | MB_ICONERROR);
}
void plat_about() {
    MessageBoxW(g_hwnd,
        L"jptxt 0.1.1\n"
        L"A super-fast native notepad.\n\n"
        L"mmap + piece table, syntax highlight, hex lister.\n"
        L"One instance. Triple-Esc closes.\n"
        L"No Electron, no scripting, no sidebars.\n\n"
        L"Win32 / Cocoa / X11",
        L"About jptxt", MB_OK | MB_ICONINFORMATION);
}
void plat_set_font(int px) {
    make_font(px);
    app_layout_scroll(&g_app);
    plat_invalidate();
}
void plat_find_dialog(bool replace) {
    memset(&g_fr, 0, sizeof(g_fr));
    g_fr.lStructSize = sizeof(g_fr);
    g_fr.hwndOwner = g_hwnd;
    g_fr.lpstrFindWhat = g_find;
    g_fr.wFindWhatLen = 512;
    g_fr.lpstrReplaceWith = g_repl;
    g_fr.wReplaceWithLen = 512;
    g_fr.Flags = FR_DOWN | FR_HIDEWHOLEWORD;
    if (replace) g_findwnd = ReplaceTextW(&g_fr);
    else g_findwnd = FindTextW(&g_fr);
}

static int scroll_cmd(HWND hwnd, int bar, WPARAM wp) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, bar, &si);
    int pos = si.nPos;
    switch (LOWORD(wp)) {
    case SB_LINEUP: pos--; break;
    case SB_LINEDOWN: pos++; break;
    case SB_PAGEUP: pos -= (int)si.nPage; break;
    case SB_PAGEDOWN: pos += (int)si.nPage; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = si.nMin; break;
    case SB_BOTTOM: pos = si.nMax; break;
    }
    if (pos < si.nMin) pos = si.nMin;
    int maxp = si.nMax - (int)si.nPage + 1;
    if (maxp < si.nMin) maxp = si.nMin;
    if (pos > maxp) pos = maxp;
    return pos;
}

static void handle_key(WPARAM vk, bool down) {
    if (!down) return;
    uint32_t mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= JP_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= JP_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= JP_ALT;
    Key k;
    switch (vk) {
    case VK_LEFT: k = Key::Left; break;
    case VK_RIGHT: k = Key::Right; break;
    case VK_UP: k = Key::Up; break;
    case VK_DOWN: k = Key::Down; break;
    case VK_HOME: k = Key::Home; break;
    case VK_END: k = Key::End; break;
    case VK_PRIOR: k = Key::PageUp; break;
    case VK_NEXT: k = Key::PageDown; break;
    case VK_BACK: k = Key::Backspace; break;
    case VK_DELETE: k = Key::Del; break;
    case VK_TAB: k = Key::Tab; break;
    case VK_RETURN: k = Key::Enter; break;
    case VK_ESCAPE: k = Key::Escape; break;
    case VK_INSERT: k = Key::Insert; break;
    case VK_F3: k = Key::F3; break;
    default: return;
    }
    // Ctrl+Tab is an accelerator; still handle Tab here when not ctrl
    if (k == Key::Tab && (mods & JP_CTRL)) return;
    app_key(&g_app, k, mods);
    if (g_app.quit && g_hwnd) DestroyWindow(g_hwnd);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_findmsg && g_findmsg) {
        FINDREPLACEW* fr = (FINDREPLACEW*)l;
        if (fr->Flags & FR_DIALOGTERM) { g_findwnd = nullptr; return 0; }
        std::string pat = w2u(fr->lpstrFindWhat);
        std::string repl = fr->lpstrReplaceWith ? w2u(fr->lpstrReplaceWith) : "";
        int flags = 0;
        if (fr->Flags & FR_MATCHCASE) flags |= 1;
        flags |= 2; // regex
        int action = 0;
        if (fr->Flags & FR_REPLACE) action = 2;
        else if (fr->Flags & FR_REPLACEALL) action = 3;
        else if (!(fr->Flags & FR_DOWN)) action = 1;
        app_find(&g_app, pat.c_str(), repl.c_str(), flags, action);
        return 0;
    }
    switch (m) {
    case WM_CREATE:
        DragAcceptFiles(h, TRUE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        app_cmd(&g_app, CMD_EXIT);
        if (g_app.quit) DestroyWindow(h);
        return 0;
    case WM_SIZE:
        app_resize(&g_app, LOWORD(l), HIWORD(l));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        paint();
        BitBlt(hdc, 0, 0, g_bw, g_bh, g_mdc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND:
        app_cmd(&g_app, (int)LOWORD(w));
        if (g_app.quit) DestroyWindow(h);
        return 0;
    case WM_KEYDOWN:
        handle_key(w, true);
        return 0;
    case WM_CHAR: {
        if (w < 32) return 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) return 0;
        app_char(&g_app, (uint32_t)w);
        return 0;
    }
    case WM_UNICHAR:
        if (w != UNICODE_NOCHAR) app_char(&g_app, (uint32_t)w);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        SetCapture(h);
        app_mouse_down(&g_app, GET_X_LPARAM(l), GET_Y_LPARAM(l), 1,
                       (GetKeyState(VK_SHIFT) & 0x8000) ? JP_SHIFT : 0);
        return 0;
    case WM_RBUTTONDOWN:
        return 0;
    case WM_MBUTTONDOWN:
        app_mouse_down(&g_app, GET_X_LPARAM(l), GET_Y_LPARAM(l), 3, 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        app_mouse_dbl(&g_app, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        app_mouse_up(&g_app, GET_X_LPARAM(l), GET_Y_LPARAM(l), 1);
        return 0;
    case WM_MOUSEMOVE:
        app_mouse_move(&g_app, GET_X_LPARAM(l), GET_Y_LPARAM(l),
                       (w & MK_SHIFT) ? JP_SHIFT : 0);
        plat_cursor(app_cursor_at(&g_app, GET_X_LPARAM(l), GET_Y_LPARAM(l)));
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        int lines = -delta / WHEEL_DELTA * 3;
        uint32_t mods = 0;
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) mods |= JP_CTRL;
        if (GET_KEYSTATE_WPARAM(w) & MK_SHIFT) mods |= JP_SHIFT;
        app_wheel(&g_app, lines, mods);
        return 0;
    }
    case WM_VSCROLL: {
        int pos = scroll_cmd(h, SB_VERT, w);
        Doc* d = app_doc(&g_app);
        if (d) { d->top_line = pos; app_layout_scroll(&g_app); plat_invalidate(); }
        return 0;
    }
    case WM_HSCROLL: {
        int pos = scroll_cmd(h, SB_HORZ, w);
        Doc* d = app_doc(&g_app);
        if (d) { d->hscroll = pos; app_layout_scroll(&g_app); plat_invalidate(); }
        return 0;
    }
    case WM_SETFOCUS: {
        int cx, cy, ch;
        bool vis = app_caret_px(&g_app, &cx, &cy, &ch);
        plat_caret(cx, cy, ch, vis);
        if (vis) ShowCaret(h);
        return 0;
    }
    case WM_KILLFOCUS:
        DestroyCaret();
        return 0;
    case WM_COPYDATA: {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)l;
        if (!cds || cds->dwData != JPTXT_COPYDATA_MAGIC) return FALSE;
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        BringWindowToTop(h);
        if (cds->lpData && cds->cbData)
            app_open_blob(&g_app, (const char*)cds->lpData, (size_t)cds->cbData);
        plat_set_title(app_title(&g_app));
        plat_invalidate();
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)w;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            wchar_t path[32768];
            if (DragQueryFileW(drop, i, path, 32768))
                app_open_path(&g_app, w2u(path).c_str());
        }
        DragFinish(drop);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        SetFocus(h);
        return MA_ACTIVATE;
    }
    return DefWindowProcW(h, m, w, l);
}

static void handoff_to_existing(HWND exist, LPWSTR* argv, int argc) {
    std::string blob;
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == L'-' && argv[i][1] == L'-') continue;
            std::string p = w2u(argv[i]);
            if (p.empty()) continue;
            if (!blob.empty()) blob.push_back('\n');
            blob += p;
        }
    }
    COPYDATASTRUCT cds{};
    cds.dwData = JPTXT_COPYDATA_MAGIC;
    cds.cbData = (DWORD)blob.size();
    cds.lpData = blob.empty() ? nullptr : (void*)blob.data();
    DWORD pid = 0;
    GetWindowThreadProcessId(exist, &pid);
    if (pid) AllowSetForegroundWindow(pid);
    if (IsIconic(exist)) ShowWindow(exist, SW_RESTORE);
    SendMessageW(exist, WM_COPYDATA, 0, (LPARAM)&cds);
    SetForegroundWindow(exist);
    BringWindowToTop(exist);
}

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    SetProcessDPIAware();

    g_findmsg = RegisterWindowMessageW(FINDMSGSTRING);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool selftest = argv && argc >= 2 && wcscmp(argv[1], L"--selftest") == 0;
    bool bench = argv && argc >= 3 && wcscmp(argv[1], L"--bench") == 0;

    if (selftest || bench) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        int rc = selftest ? doc_selftest(stdout) : doc_bench(w2u(argv[2]).c_str(), stdout);
        if (argv) LocalFree(argv);
        return rc;
    }

    g_single = CreateMutexW(nullptr, TRUE, L"Local\\jptxt-singleton");
    if (g_single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND exist = nullptr;
        for (int i = 0; i < 40 && !exist; i++) {
            exist = FindWindowW(L"jptxt", nullptr);
            if (!exist) Sleep(25);
        }
        if (exist) handoff_to_existing(exist, argv, argc);
        if (argv) LocalFree(argv);
        if (g_single) CloseHandle(g_single);
        return 0;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"jptxt";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    app_init(&g_app);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 1100, wh = 720;
    g_hwnd = CreateWindowExW(0, L"jptxt", L"jptxt",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        nullptr, LoadMenu(inst, MAKEINTRESOURCEW(IDR_MENU)), inst, nullptr);
    if (!g_hwnd) return 1;

    make_font(14);
    g_accel = LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDR_ACCEL));

    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == L'-') continue;
            app_open_path(&g_app, w2u(argv[i]).c_str());
        }
        LocalFree(argv);
        argv = nullptr;
    }

    plat_set_title(app_title(&g_app));
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (g_findwnd && IsDialogMessageW(g_findwnd, &msg)) continue;
        if (g_accel && TranslateAcceleratorW(g_hwnd, g_accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_mdc) {
        SelectObject(g_mdc, g_oldbmp);
        DeleteObject(g_bmp);
        DeleteDC(g_mdc);
    }
    if (g_font) DeleteObject(g_font);
    if (g_single) CloseHandle(g_single);
    return (int)msg.wParam;
}

