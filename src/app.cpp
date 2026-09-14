#include "jptxt.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>

static uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static int g_untitled = 1;
static bool g_drag = false;
static int  g_drag_tab = -1;

static uint64_t sel_lo(const Doc* d) { return d->caret < d->sel_anchor ? d->caret : d->sel_anchor; }
static uint64_t sel_hi(const Doc* d) { return d->caret < d->sel_anchor ? d->sel_anchor : d->caret; }
static bool has_sel(const Doc* d) { return d->caret != d->sel_anchor; }

static const char* eol_str(Eol e) {
    switch (e) {
    case Eol::CRLF: return "\r\n";
    case Eol::CR: return "\r";
    default: return "\n";
    }
}

int app_ed_top(const App* a) { return a->tab_h; }
int app_ed_h(const App* a) {
    int h = a->h - a->tab_h - a->status_h;
    return h < 0 ? 0 : h;
}
int app_vis_lines(const App* a) {
    if (a->cell_h <= 0) return 1;
    int n = app_ed_h(a) / a->cell_h;
    return n < 1 ? 1 : n;
}

Doc* app_doc(App* a) {
    if (a->tabs.empty()) return nullptr;
    if (a->cur < 0) a->cur = 0;
    if (a->cur >= (int)a->tabs.size()) a->cur = (int)a->tabs.size() - 1;
    return a->tabs[a->cur].get();
}

void app_set_metrics(App* a, int cell_w, int cell_h, int ascent) {
    a->cell_w = cell_w < 1 ? 8 : cell_w;
    a->cell_h = cell_h < 1 ? 16 : cell_h;
    a->ascent = ascent;
    a->tab_h = a->cell_h + 12;
    a->status_h = a->cell_h + 8;
}

void app_resize(App* a, int w, int h) {
    a->w = w; a->h = h;
    app_layout_scroll(a);
}

void app_init(App* a) {
    *a = App{};
    app_set_metrics(a, 8, 16, 12);
    app_new_tab(a);
}

void app_new_tab(App* a) {
    auto d = std::make_unique<Doc>();
    doc_init_empty(d.get(), g_untitled++);
    a->tabs.push_back(std::move(d));
    a->cur = (int)a->tabs.size() - 1;
    plat_set_title(app_title(a));
    plat_invalidate();
}

void app_open_blob(App* a, const char* paths, size_t n) {
    if (!paths || !n) return;
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && paths[e] != '\n' && paths[e] != '\0') e++;
        if (e > i) {
            std::string one(paths + i, e - i);
            while (!one.empty() && (one.back() == '\r' || one.back() == ' ')) one.pop_back();
            if (!one.empty()) app_open_path(a, one.c_str());
        }
        i = e + 1;
    }
}

bool app_open_path(App* a, const char* path) {
    if (!path || !*path) return false;
    // reuse empty untitled
    Doc* cur = app_doc(a);
    std::unique_ptr<Doc> nd = std::make_unique<Doc>();
    if (!doc_load(nd.get(), path)) {
        a->status = "Failed to open";
        plat_beep();
        plat_invalidate();
        return false;
    }
    if (cur && cur->path.empty() && cur->len == 0 && !cur->dirty && a->tabs.size() == 1) {
        a->tabs[0] = std::move(nd);
        a->cur = 0;
    } else {
        a->tabs.push_back(std::move(nd));
        a->cur = (int)a->tabs.size() - 1;
    }
    plat_set_title(app_title(a));
    app_layout_scroll(a);
    plat_invalidate();
    return true;
}

static bool save_doc(App* a, Doc* d, bool save_as) {
    std::string path = d->path;
    if (save_as || path.empty() || d->readonly) {
        if (!plat_dlg_save(path)) return false;
    }
    if (!doc_save(d, path.c_str())) {
        a->status = "Save failed";
        plat_beep();
        plat_error("Could not save the file.\n\n"
                   "The folder may not be writable, or the file is still open elsewhere.");
        plat_invalidate();
        return false;
    }
    a->status = "Saved";
    plat_set_title(app_title(a));
    plat_invalidate();
    return true;
}

bool app_save(App* a, bool save_as) {
    Doc* d = app_doc(a);
    if (!d) return false;
    return save_doc(a, d, save_as);
}

void app_close_tab(App* a) {
    Doc* d = app_doc(a);
    if (!d) return;
    if (d->dirty) {
        int r = plat_ask_save(d->name.c_str());
        if (r == 0) return;
        if (r == 1) { if (!save_doc(a, d, d->path.empty())) return; }
    }
    a->tabs.erase(a->tabs.begin() + a->cur);
    if (a->tabs.empty()) app_new_tab(a);
    if (a->cur >= (int)a->tabs.size()) a->cur = (int)a->tabs.size() - 1;
    plat_set_title(app_title(a));
    app_layout_scroll(a);
    plat_invalidate();
}

const char* app_title(App* a) {
    static char buf[1024];
    Doc* d = app_doc(a);
    if (!d) return "jptxt";
    snprintf(buf, sizeof(buf), "%s%s — jptxt", d->dirty ? "*" : "", d->name.c_str());
    return buf;
}

void app_status_text(App* a, char* buf, int cap) {
    Doc* d = app_doc(a);
    if (!d) { snprintf(buf, cap, " "); return; }
    if (!a->status.empty()) {
        snprintf(buf, cap, "%s", a->status.c_str());
        return;
    }
    uint64_t line = doc_line_of(d, d->caret) + 1;
    uint64_t col  = doc_col_of(d, d->caret) + 1;
    const char* mode = d->hex ? "HEX" : (d->binary ? "BIN" : "TEXT");
    char sz[64];
    if (d->len < 1024) snprintf(sz, sizeof(sz), "%llu B", (unsigned long long)d->len);
    else if (d->len < 1024ull * 1024) snprintf(sz, sizeof(sz), "%.1f KB", d->len / 1024.0);
    else snprintf(sz, sizeof(sz), "%.1f MB", d->len / (1024.0 * 1024.0));
    snprintf(buf, cap, "Ln %llu  Col %llu    %s    %s    %s    %s    %s    %s",
             (unsigned long long)line, (unsigned long long)col,
             enc_name(d->enc), eol_name(d->eol),
             a->overwrite ? "OVR" : "INS",
             sz, mode, d->readonly ? "RO" : "RW");
}

void app_layout_scroll(App* a) {
    Doc* d = app_doc(a);
    if (!d) return;
    int vis = app_vis_lines(a);
    int64_t lc = d->hex ? app_hex_rows(d) : (int64_t)doc_line_count(d);
    if (d->top_line > lc - 1) d->top_line = lc > 0 ? lc - 1 : 0;
    if (d->top_line < 0) d->top_line = 0;
    int vmax = (int)(lc < 1 ? 1 : lc);
    int vpage = vis;
    int vpos = (int)d->top_line;
    int hmax = 512;
    int hpage = a->cell_w > 0 ? (a->w / a->cell_w) : 80;
    if (hpage < 1) hpage = 1;
    plat_scroll_set(vmax - 1, vpage, vpos, hmax, hpage, d->hscroll);
}

void app_ensure_caret_visible(App* a) {
    Doc* d = app_doc(a);
    if (!d) return;
    int64_t line = (int64_t)doc_line_of(d, d->caret);
    int vis = app_vis_lines(a);
    if (line < d->top_line) d->top_line = line;
    if (line >= d->top_line + vis) d->top_line = line - vis + 1;
    if (d->top_line < 0) d->top_line = 0;
    int col = (int)doc_col_of(d, d->caret);
    int hvis = a->cell_w > 0 ? (a->w / a->cell_w) - 2 : 80;
    if (hvis < 8) hvis = 8;
    if (col < d->hscroll) d->hscroll = col;
    if (col >= d->hscroll + hvis) d->hscroll = col - hvis + 1;
    app_layout_scroll(a);
}

bool app_caret_px(App* a, int* x, int* y, int* h) {
    Doc* d = app_doc(a);
    if (!d || d->hex) { *x = *y = 0; *h = a->cell_h; return false; }
    int64_t line = (int64_t)doc_line_of(d, d->caret);
    int col = (int)doc_col_of(d, d->caret);
    *x = (col - d->hscroll) * a->cell_w;
    *y = app_ed_top(a) + (int)(line - d->top_line) * a->cell_h;
    *h = a->cell_h;
    if (*y < app_ed_top(a) || *y >= app_ed_top(a) + app_ed_h(a)) return false;
    if (*x < 0 || *x >= a->w) return false;
    return true;
}

int app_cursor_at(App* a, int x, int y) {
    if (y < a->tab_h) return 0;
    if (y >= a->h - a->status_h) return 0;
    return 1;
}

static void set_caret(Doc* d, uint64_t p, bool extend) {
    if (p > d->len) p = d->len;
    d->caret = p;
    if (!extend) d->sel_anchor = p;
    d->pref_col = -1;
}

static void delete_sel(Doc* d) {
    if (!has_sel(d) || d->readonly) return;
    uint64_t a = sel_lo(d), b = sel_hi(d);
    doc_erase(d, a, b - a);
    d->caret = d->sel_anchor = a;
}

static std::string sel_text(const Doc* d) {
    std::string s;
    if (!has_sel(d)) return s;
    uint64_t a = sel_lo(d), b = sel_hi(d);
    if (d->hex) {
        // formatted dump
        char line[160];
        uint64_t pos = a;
        while (pos < b) {
            uint64_t row = pos / 16 * 16;
            uint64_t n = 16;
            if (row + n > d->len) n = d->len - row;
            std::string bytes;
            doc_read(d, row, n, &bytes);
            int off = snprintf(line, sizeof(line), "%08llX  ", (unsigned long long)row);
            for (uint64_t i = 0; i < 16; i++) {
                if (i == 8) { line[off++] = ' '; }
                if (i < n && row + i >= a && row + i < b)
                    off += snprintf(line + off, sizeof(line) - off, "%02X ", (uint8_t)bytes[(size_t)i]);
                else if (i < n)
                    off += snprintf(line + off, sizeof(line) - off, "%02X ", (uint8_t)bytes[(size_t)i]);
                else
                    off += snprintf(line + off, sizeof(line) - off, "   ");
            }
            line[off++] = ' ';
            for (uint64_t i = 0; i < n; i++) {
                unsigned char c = (unsigned char)bytes[(size_t)i];
                line[off++] = (c >= 32 && c < 127) ? (char)c : '.';
            }
            line[off++] = '\n';
            line[off] = 0;
            s.append(line, off);
            pos = row + 16;
            if (pos == 0) break;
        }
        return s;
    }
    doc_read(d, a, b - a, &s);
    return s;
}

static bool word_char(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

static uint64_t word_left(const Doc* d, uint64_t p) {
    if (p == 0) return 0;
    p = utf8_prev(d, p);
    while (p > 0 && !word_char(doc_at(d, p))) p = utf8_prev(d, p);
    while (p > 0) {
        uint64_t q = utf8_prev(d, p);
        if (!word_char(doc_at(d, q))) break;
        p = q;
        if (q == 0) break;
    }
    if (p > 0 && word_char(doc_at(d, p))) {
        // include this char; if prev is also word we already stopped
    }
    while (p > 0 && word_char(doc_at(d, p))) {
        uint64_t q = utf8_prev(d, p);
        if (!word_char(doc_at(d, q))) break;
        p = q;
    }
    return p;
}
static uint64_t word_right(const Doc* d, uint64_t p) {
    while (p < d->len && !word_char(doc_at(d, p))) p = utf8_next_pos(d, p);
    while (p < d->len && word_char(doc_at(d, p))) p = utf8_next_pos(d, p);
    return p;
}

static void move_vert(Doc* d, int dir, bool extend) {
    uint64_t line = doc_line_of(d, d->caret);
    int col = d->pref_col >= 0 ? d->pref_col : (int)doc_col_of(d, d->caret);
    d->pref_col = col;
    int64_t nl = (int64_t)line + dir;
    int64_t lc = d->hex ? (int64_t)app_hex_rows(d) : (int64_t)doc_line_count(d);
    if (nl < 0) { set_caret(d, 0, extend); d->pref_col = col; return; }
    if (nl >= lc) { set_caret(d, d->len, extend); d->pref_col = col; return; }
    uint64_t np = doc_pos_at(d, (uint64_t)nl, (uint64_t)col);
    d->caret = np;
    if (!extend) d->sel_anchor = np;
    d->pref_col = col;
}

static void indent_block(Doc* d, bool un) {
    if (d->readonly || d->hex) return;
    uint64_t a = sel_lo(d), b = sel_hi(d);
    uint64_t l0 = doc_line_of(d, a);
    uint64_t l1 = doc_line_of(d, b);
    if (has_sel(d) && b > 0 && doc_line_of(d, b - 1) < l1 && doc_at(d, b - 1) == '\n')
        l1--;
    if (!has_sel(d)) { l0 = l1 = doc_line_of(d, d->caret); }
    int64_t delta_start = 0, delta_caret = 0;
    uint64_t caret0 = d->caret, anc0 = d->sel_anchor;
    for (int64_t l = (int64_t)l1; l >= (int64_t)l0; l--) {
        uint64_t s, e;
        doc_line_span(d, (uint64_t)l, &s, &e);
        if (un) {
            if (s < d->len && doc_at(d, s) == '\t') {
                doc_erase(d, s, 1);
                if (caret0 > s) delta_caret -= 1;
                if (anc0 > s) delta_start -= 1;
            } else {
                int k = 0;
                while (k < 4 && s + k < e && doc_at(d, s + k) == ' ') k++;
                if (k) {
                    doc_erase(d, s, (uint64_t)k);
                    if (caret0 > s) delta_caret -= k;
                    if (anc0 > s) delta_start -= k;
                }
            }
        } else {
            doc_insert(d, s, "\t", 1);
            if (caret0 >= s) delta_caret += 1;
            if (anc0 >= s) delta_start += 1;
        }
    }
    d->caret = (uint64_t)((int64_t)caret0 + delta_caret);
    d->sel_anchor = (uint64_t)((int64_t)anc0 + delta_start);
}

static void copy_sel(App* a, bool cut) {
    Doc* d = app_doc(a);
    if (!d) return;
    std::string s = sel_text(d);
    if (s.empty() && !d->hex) {
        // copy whole line
        uint64_t line = doc_line_of(d, d->caret);
        uint64_t s0, e;
        doc_line_span(d, line, &s0, &e);
        uint64_t end = (line + 1 < doc_line_count(d)) ? d->line_off[(size_t)line + 1] : d->len;
        doc_read(d, s0, end - s0, &s);
    }
    if (!s.empty()) plat_clipboard_set(s);
    if (cut && !d->readonly && !d->hex) delete_sel(d);
}

static void paste(App* a) {
    Doc* d = app_doc(a);
    if (!d || d->readonly || d->hex) return;
    std::string s;
    if (!plat_clipboard_get(s) || s.empty()) return;
    delete_sel(d);
    doc_insert(d, d->caret, s.data(), s.size());
    d->caret += s.size();
    d->sel_anchor = d->caret;
}

static void undo(Doc* d) {
    if (d->undo.empty() || d->readonly) return;
    Edit e = std::move(d->undo.back());
    d->undo.pop_back();
    doc_apply(d, e, true);
    d->redo.push_back(std::move(e));
    d->caret = e.pos + (e.removed.size()); // after undo, original text is back
    // actually after reverse, inserted is removed; caret at pos
    d->caret = e.pos + e.removed.size();
    d->sel_anchor = d->caret;
    d->dirty = ((int)d->undo.size() != d->save_undo_size);
}
static void redo(Doc* d) {
    if (d->redo.empty() || d->readonly) return;
    Edit e = std::move(d->redo.back());
    d->redo.pop_back();
    doc_apply(d, e, false);
    d->undo.push_back(e);
    d->caret = e.pos + e.inserted.size();
    d->sel_anchor = d->caret;
    d->dirty = ((int)d->undo.size() != d->save_undo_size);
}

void app_char(App* a, uint32_t cp) {
    Doc* d = app_doc(a);
    if (!d || d->readonly || d->hex) return;
    if (cp < 32 && cp != 9) return;
    char tmp[8];
    int n = utf8_put(cp, tmp);
    delete_sel(d);
    if (a->overwrite && d->caret < d->len) {
        uint64_t np = utf8_next_pos(d, d->caret);
        uint8_t c = doc_at(d, d->caret);
        if (c != '\n' && c != '\r') doc_erase(d, d->caret, np - d->caret);
    }
    doc_insert(d, d->caret, tmp, (uint64_t)n);
    d->caret += (uint64_t)n;
    d->sel_anchor = d->caret;
    a->status.clear();
    app_ensure_caret_visible(a);
    plat_set_title(app_title(a));
    plat_invalidate();
}

void app_key(App* a, Key k, uint32_t mods) {
    Doc* d = app_doc(a);
    if (!d) return;
    bool shift = (mods & JP_SHIFT) != 0;
    bool ctrl  = (mods & JP_CTRL) != 0;

    if (k == Key::Escape) {
        uint64_t t = now_ms();
        if (t - a->esc_ms > JPTXT_ESC_GAP_MS) a->esc_n = 0;
        a->esc_n++;
        a->esc_ms = t;
        if (a->esc_n >= JPTXT_ESC_CLOSE_N) {
            a->esc_n = 0;
            a->status.clear();
            app_cmd(a, CMD_EXIT);
            return;
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "Esc %d/%d — %s",
                 a->esc_n, JPTXT_ESC_CLOSE_N,
                 a->esc_n == 1 ? "twice more to close" : "once more to close");
        a->status = buf;
        plat_invalidate();
        return;
    }

    a->status.clear();
    a->esc_n = 0;

    if (k == Key::F3) {
        app_find(a, a->find_pat.c_str(), nullptr, a->find_flags | (shift ? 0 : 0), shift ? 1 : 0);
        return;
    }

    if (k == Key::Tab) {
        if (d->hex || d->readonly) return;
        if (has_sel(d) || shift) indent_block(d, shift);
        else {
            doc_insert(d, d->caret, "\t", 1);
            d->caret++; d->sel_anchor = d->caret;
        }
        app_ensure_caret_visible(a);
        plat_set_title(app_title(a));
        plat_invalidate();
        return;
    }
    if (k == Key::Enter) {
        if (d->hex || d->readonly) return;
        delete_sel(d);
        std::string ins = eol_str(d->eol == Eol::Mixed ? Eol::LF : d->eol);
        // auto-indent
        uint64_t ls, le;
        doc_line_span(d, doc_line_of(d, d->caret), &ls, &le);
        std::string line;
        doc_read(d, ls, le > ls ? le - ls : 0, &line);
        std::string indent;
        for (char c : line) {
            if (c == ' ' || c == '\t') indent.push_back(c);
            else break;
        }
        ins += indent;
        doc_insert(d, d->caret, ins.data(), ins.size());
        d->caret += ins.size();
        d->sel_anchor = d->caret;
        app_ensure_caret_visible(a);
        plat_set_title(app_title(a));
        plat_invalidate();
        return;
    }
    if (k == Key::Backspace) {
        if (d->hex || d->readonly) return;
        if (has_sel(d)) delete_sel(d);
        else if (d->caret > 0) {
            uint64_t p = ctrl ? word_left(d, d->caret) : utf8_prev(d, d->caret);
            doc_erase(d, p, d->caret - p);
            d->caret = d->sel_anchor = p;
        }
        app_ensure_caret_visible(a);
        plat_set_title(app_title(a));
        plat_invalidate();
        return;
    }
    if (k == Key::Del) {
        if (d->hex || d->readonly) return;
        if (has_sel(d)) delete_sel(d);
        else if (d->caret < d->len) {
            uint64_t p = ctrl ? word_right(d, d->caret) : utf8_next_pos(d, d->caret);
            doc_erase(d, d->caret, p - d->caret);
        }
        app_ensure_caret_visible(a);
        plat_set_title(app_title(a));
        plat_invalidate();
        return;
    }
    if (k == Key::Insert) {
        a->overwrite = !a->overwrite;
        plat_invalidate();
        return;
    }

    uint64_t np = d->caret;
    if (k == Key::Left) {
        np = ctrl ? word_left(d, d->caret) : (d->hex ? (d->caret > 0 ? d->caret - 1 : 0) : utf8_prev(d, d->caret));
        set_caret(d, np, shift);
    } else if (k == Key::Right) {
        np = ctrl ? word_right(d, d->caret) : (d->hex ? (d->caret < d->len ? d->caret + 1 : d->len) : utf8_next_pos(d, d->caret));
        set_caret(d, np, shift);
    } else if (k == Key::Up) {
        move_vert(d, -1, shift);
    } else if (k == Key::Down) {
        move_vert(d, 1, shift);
    } else if (k == Key::Home) {
        if (ctrl) set_caret(d, 0, shift);
        else {
            uint64_t ls, le;
            doc_line_span(d, doc_line_of(d, d->caret), &ls, &le);
            set_caret(d, d->hex ? (d->caret / 16) * 16 : ls, shift);
        }
    } else if (k == Key::End) {
        if (ctrl) set_caret(d, d->len, shift);
        else {
            uint64_t ls, le;
            doc_line_span(d, doc_line_of(d, d->caret), &ls, &le);
            set_caret(d, d->hex ? std::min(d->len, (d->caret / 16) * 16 + 16) : le, shift);
        }
    } else if (k == Key::PageUp) {
        move_vert(d, -app_vis_lines(a), shift);
    } else if (k == Key::PageDown) {
        move_vert(d, app_vis_lines(a), shift);
    }
    app_ensure_caret_visible(a);
    plat_invalidate();
}

void app_cmd(App* a, int cmd) {
    Doc* d = app_doc(a);
    switch (cmd) {
    case CMD_NEW: app_new_tab(a); break;
    case CMD_OPEN: {
        std::vector<std::string> paths;
        if (plat_dlg_open(paths)) for (auto& p : paths) app_open_path(a, p.c_str());
        break;
    }
    case CMD_SAVE: app_save(a, false); break;
    case CMD_SAVEAS: app_save(a, true); break;
    case CMD_CLOSE: app_close_tab(a); break;
    case CMD_EXIT: {
        while (!a->tabs.empty()) {
            int n = (int)a->tabs.size();
            app_close_tab(a);
            if ((int)a->tabs.size() == n && app_doc(a) && app_doc(a)->dirty) return; // cancelled
            if ((int)a->tabs.size() == n) { a->quit = true; return; }
        }
        a->quit = true;
        break;
    }
    case CMD_UNDO: if (d) undo(d); plat_set_title(app_title(a)); plat_invalidate(); break;
    case CMD_REDO: if (d) redo(d); plat_set_title(app_title(a)); plat_invalidate(); break;
    case CMD_CUT: copy_sel(a, true); plat_set_title(app_title(a)); plat_invalidate(); break;
    case CMD_COPY: copy_sel(a, false); break;
    case CMD_PASTE: paste(a); plat_set_title(app_title(a)); app_ensure_caret_visible(a); plat_invalidate(); break;
    case CMD_SELALL: if (d) { d->sel_anchor = 0; d->caret = d->len; plat_invalidate(); } break;
    case CMD_INDENT: if (d) indent_block(d, false); plat_invalidate(); break;
    case CMD_UNINDENT: if (d) indent_block(d, true); plat_invalidate(); break;
    case CMD_FIND: plat_find_dialog(false); break;
    case CMD_REPLACE: plat_find_dialog(true); break;
    case CMD_FINDNEXT: app_find(a, a->find_pat.c_str(), nullptr, a->find_flags, 0); break;
    case CMD_FINDPREV: app_find(a, a->find_pat.c_str(), nullptr, a->find_flags, 1); break;
    case CMD_GOTO: {
        if (!d) break;
        int line = 1;
        int maxl = d->hex ? app_hex_rows(d) : (int)doc_line_count(d);
        int cur = (int)doc_line_of(d, d->caret) + 1;
        if (plat_dlg_goto(maxl, cur, &line) && line >= 1) {
            uint64_t p = doc_pos_at(d, (uint64_t)(line - 1), 0);
            set_caret(d, p, false);
            app_ensure_caret_visible(a);
            plat_invalidate();
        }
        break;
    }
    case CMD_HEX:
        if (d) { d->hex = true; app_ensure_caret_visible(a); plat_invalidate(); }
        break;
    case CMD_TEXT:
        if (d) { d->hex = false; if (!d->binary) d->readonly = false; app_ensure_caret_visible(a); plat_invalidate(); }
        break;
    case CMD_FONT_UP: a->want_font = a->font_px + 1; if (a->want_font > 48) a->want_font = 48; plat_set_font(a->want_font); break;
    case CMD_FONT_DN: a->want_font = a->font_px - 1; if (a->want_font < 8) a->want_font = 8; plat_set_font(a->want_font); break;
    case CMD_FONT_RST: a->want_font = 14; plat_set_font(14); break;
    case CMD_ABOUT: plat_about(); break;
    case CMD_NEXTTAB:
        if (!a->tabs.empty()) { a->cur = (a->cur + 1) % (int)a->tabs.size(); plat_set_title(app_title(a)); plat_invalidate(); }
        break;
    case CMD_PREVTAB:
        if (!a->tabs.empty()) { a->cur = (a->cur - 1 + (int)a->tabs.size()) % (int)a->tabs.size(); plat_set_title(app_title(a)); plat_invalidate(); }
        break;
    }
}

bool app_find(App* a, const char* pat, const char* repl, int flags, int action) {
    Doc* d = app_doc(a);
    if (!d) return false;
    if (pat) a->find_pat = pat;
    if (repl) a->repl_pat = repl;
    a->find_flags = flags;
    if (a->find_pat.empty()) return false;
    bool icase = (flags & 1) == 0; // flag 1 = match case, so icase when not set
    bool regex = (flags & 2) != 0;
    // Windows FR_MATCHCASE is 4... we use: bit0 match-case, bit1 regex. Platform maps.
    icase = (flags & 1) == 0;
    bool down = action != 1;

    if (action == 2 || action == 3) {
        if (d->readonly || d->hex) return false;
        if (action == 2) {
            if (has_sel(d) || a->has_match) {
                uint64_t a0 = sel_lo(d), b0 = sel_hi(d);
                std::string r = a->repl_pat;
                doc_erase(d, a0, b0 - a0);
                doc_insert(d, a0, r.data(), r.size());
                d->caret = a0 + r.size();
                d->sel_anchor = d->caret;
            }
            // then find next
            action = 0;
            down = true;
        } else {
            int n = 0;
            uint64_t from = 0;
            for (;;) {
                uint64_t aa, bb;
                if (!find_in_doc(d, a->find_pat, regex, icase, true, from, &aa, &bb)) break;
                std::string r = a->repl_pat;
                doc_erase(d, aa, bb - aa);
                doc_insert(d, aa, r.data(), r.size());
                from = aa + r.size();
                n++;
                if (n > 1000000) break;
            }
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "Replaced %d", n);
            a->status = tmp;
            plat_set_title(app_title(a));
            plat_invalidate();
            return n > 0;
        }
    }

    uint64_t from = down ? d->caret : (d->caret > 0 ? d->caret - 1 : 0);
    if (down && a->has_match && d->caret == a->last_match_a) from = a->last_match_b;
    uint64_t aa, bb;
    bool ok = find_in_doc(d, a->find_pat, regex, icase, down, from, &aa, &bb);
    if (!ok && down && from > 0) ok = find_in_doc(d, a->find_pat, regex, icase, true, 0, &aa, &bb);
    if (!ok && !down) ok = find_in_doc(d, a->find_pat, regex, icase, false, d->len, &aa, &bb);
    if (!ok) {
        a->has_match = false;
        a->status = "Not found";
        plat_beep();
        plat_invalidate();
        return false;
    }
    a->has_match = true;
    a->last_match_a = aa;
    a->last_match_b = bb;
    d->sel_anchor = aa;
    d->caret = bb;
    app_ensure_caret_visible(a);
    plat_invalidate();
    return true;
}

// ---------------- mouse / tabs ----------------

static int tab_x0(const App* a, int i) {
    int x = 4;
    for (int k = 0; k < i && k < (int)a->tabs.size(); k++) {
        int w = (int)a->tabs[k]->name.size() * a->cell_w + 36;
        if (w < 80) w = 80;
        if (w > 220) w = 220;
        x += w;
    }
    return x;
}
static int tab_w(const App* a, int i) {
    int w = (int)a->tabs[i]->name.size() * a->cell_w + 36;
    if (w < 80) w = 80;
    if (w > 220) w = 220;
    return w;
}

static uint64_t pos_from_xy(App* a, int x, int y) {
    Doc* d = app_doc(a);
    if (!d) return 0;
    int edy = y - app_ed_top(a);
    int64_t line = d->top_line + (edy / a->cell_h);
    if (line < 0) line = 0;
    int64_t lc = d->hex ? app_hex_rows(d) : (int64_t)doc_line_count(d);
    if (line >= lc) return d->len;
    if (d->hex) {
        // layout: 8addr + 2 + 16*3 + extra space at 8 + 1 + ascii
        // "00000000  XX XX .. " then ascii
        int col = x / a->cell_w;
        int hex0 = 10;
        int ascii0 = 10 + 16 * 3 + 2;
        int byte = 0;
        if (col >= ascii0) byte = col - ascii0;
        else if (col >= hex0) {
            int hx = col - hex0;
            if (hx >= 8 * 3 + 1) hx--; // middle gap
            byte = hx / 3;
        }
        if (byte < 0) byte = 0;
        if (byte > 15) byte = 15;
        uint64_t p = (uint64_t)line * 16 + (uint64_t)byte;
        return p > d->len ? d->len : p;
    }
    int col = d->hscroll + x / a->cell_w;
    if (col < 0) col = 0;
    return doc_pos_at(d, (uint64_t)line, (uint64_t)col);
}

void app_mouse_down(App* a, int x, int y, int btn, uint32_t mods) {
    Doc* d = app_doc(a);
    if (!d) return;
    if (y < a->tab_h) {
        for (int i = 0; i < (int)a->tabs.size(); i++) {
            int x0 = tab_x0(a, i), w = tab_w(a, i);
            if (x >= x0 && x < x0 + w) {
                if (btn == 3) { // middle close
                    int old = a->cur; a->cur = i; app_close_tab(a); return;
                }
                if (x >= x0 + w - 18 && btn == 1) { a->cur = i; app_close_tab(a); return; }
                a->cur = i;
                plat_set_title(app_title(a));
                plat_invalidate();
                return;
            }
        }
        return;
    }
    if (y >= a->h - a->status_h) return;
    uint64_t p = pos_from_xy(a, x, y);
    if (btn == 1) {
        set_caret(d, p, (mods & JP_SHIFT) != 0);
        g_drag = true;
        app_ensure_caret_visible(a);
        plat_invalidate();
    }
}

void app_mouse_dbl(App* a, int x, int y) {
    Doc* d = app_doc(a);
    if (!d || y < a->tab_h) return;
    uint64_t p = pos_from_xy(a, x, y);
    if (d->hex) {
        uint64_t row = (p / 16) * 16;
        d->sel_anchor = row;
        d->caret = row + 16 > d->len ? d->len : row + 16;
    } else {
        d->sel_anchor = word_left(d, p ? p : 0);
        if (p < d->len && word_char(doc_at(d, p))) {
            d->sel_anchor = word_left(d, utf8_next_pos(d, p));
            d->caret = word_right(d, p);
        } else {
            d->sel_anchor = p;
            d->caret = utf8_next_pos(d, p);
        }
    }
    plat_invalidate();
}

void app_mouse_up(App* a, int x, int y, int btn) {
    (void)a; (void)x; (void)y; (void)btn;
    g_drag = false;
}

void app_mouse_move(App* a, int x, int y, uint32_t mods) {
    (void)mods;
    if (!g_drag) return;
    Doc* d = app_doc(a);
    if (!d) return;
    uint64_t p = pos_from_xy(a, x, y);
    d->caret = p;
    app_ensure_caret_visible(a);
    plat_invalidate();
}

void app_wheel(App* a, int lines, uint32_t mods) {
    Doc* d = app_doc(a);
    if (!d) return;
    if (mods & JP_CTRL) {
        if (lines < 0) app_cmd(a, CMD_FONT_UP);
        else app_cmd(a, CMD_FONT_DN);
        return;
    }
    d->top_line += lines;
    int64_t lc = d->hex ? app_hex_rows(d) : (int64_t)doc_line_count(d);
    if (d->top_line < 0) d->top_line = 0;
    if (d->top_line > lc - 1) d->top_line = lc > 0 ? lc - 1 : 0;
    app_layout_scroll(a);
    plat_invalidate();
}

// ---------------- paint ----------------

static void fill(Draw* d, int x, int y, int w, int h, uint32_t c) {
    if (d->fill) d->fill(d->ctx, x, y, w, h, c);
}
static void text(Draw* d, int x, int y, const char* s, int n, uint32_t c) {
    if (n < 0) n = (int)strlen(s);
    if (n && d->text) d->text(d->ctx, x, y, s, n, c);
}

void app_paint(App* a, Draw* dr, int w, int h) {
    a->w = w; a->h = h;
    Doc* doc = app_doc(a);
    fill(dr, 0, 0, w, h, COL_BG);

    // tabs
    fill(dr, 0, 0, w, a->tab_h, COL_TAB);
    fill(dr, 0, a->tab_h - 1, w, 1, COL_TAB_B);
    for (int i = 0; i < (int)a->tabs.size(); i++) {
        int x0 = tab_x0(a, i), tw = tab_w(a, i);
        bool on = i == a->cur;
        fill(dr, x0, 4, tw - 2, a->tab_h - 4, on ? COL_TAB_A : COL_GUT);
        if (on) fill(dr, x0, a->tab_h - 3, tw - 2, 3, COL_TAB_B);
        char label[256];
        snprintf(label, sizeof(label), "%s%s", a->tabs[i]->dirty ? "*" : "", a->tabs[i]->name.c_str());
        int tx = x0 + 8;
        int ty = 4 + (a->tab_h - 4 - a->cell_h) / 2;
        text(dr, tx, ty, label, -1, on ? COL_FG : COL_DIM);
        text(dr, x0 + tw - 16, ty, "x", 1, COL_DIM);
    }

    int edy = app_ed_top(a);
    int edh = app_ed_h(a);
    fill(dr, 0, edy, w, edh, COL_BG);

    if (doc) {
        int vis = app_vis_lines(a);
        if (doc->hex) {
            int64_t rows = app_hex_rows(doc);
            for (int i = 0; i < vis; i++) {
                int64_t row = doc->top_line + i;
                if (row >= rows) break;
                uint64_t off = (uint64_t)row * 16;
                uint64_t n = 16;
                if (off >= doc->len) n = 0;
                else if (off + n > doc->len) n = doc->len - off;
                std::string bytes;
                if (n) doc_read(doc, off, n, &bytes);
                int y = edy + i * a->cell_h;
                uint64_t slo = sel_lo(doc), shi = sel_hi(doc);
                bool anysel = has_sel(doc);

                char addr[32];
                snprintf(addr, sizeof(addr), "%08llX", (unsigned long long)off);
                text(dr, 0, y, addr, 8, COL_ADDR);

                int x = 10 * a->cell_w;
                for (int b = 0; b < 16; b++) {
                    if (b == 8) x += a->cell_w;
                    char hx[4] = "  ";
                    if (b < (int)n) snprintf(hx, sizeof(hx), "%02X", (uint8_t)bytes[(size_t)b]);
                    uint64_t bp = off + (uint64_t)b;
                    bool sel = anysel && bp >= slo && bp < shi && b < (int)n;
                    if (sel) fill(dr, x, y, 3 * a->cell_w, a->cell_h, COL_SEL);
                    if (b < (int)n) text(dr, x, y, hx, 2, sel ? COL_SEL_FG : COL_FG);
                    x += 3 * a->cell_w;
                }
                x += a->cell_w;
                for (int b = 0; b < (int)n; b++) {
                    unsigned char c = (unsigned char)bytes[(size_t)b];
                    char ch = (c >= 32 && c < 127) ? (char)c : '.';
                    uint64_t bp = off + (uint64_t)b;
                    bool sel = anysel && bp >= slo && bp < shi;
                    if (sel) fill(dr, x, y, a->cell_w, a->cell_h, COL_SEL);
                    text(dr, x, y, &ch, 1, sel ? COL_SEL_FG : COL_ASC);
                    x += a->cell_w;
                }
            }
        } else {
            uint64_t lc = doc_line_count(doc);
            uint64_t slo = sel_lo(doc), shi = sel_hi(doc);
            bool anysel = has_sel(doc);
            std::vector<Span> spans;
            for (int i = 0; i < vis; i++) {
                int64_t line = doc->top_line + i;
                if (line < 0 || (uint64_t)line >= lc) break;
                int y = edy + i * a->cell_h;
                uint64_t a0, b0;
                doc_line_span(doc, (uint64_t)line, &a0, &b0);
                std::string s;
                uint64_t take = b0 - a0;
                if (take > 16384) take = 16384; // don't paint insane lines fully
                doc_read(doc, a0, take, &s);

                uint8_t st = 0;
                if ((uint64_t)line < doc->hl_at.size()) st = doc->hl_at[(size_t)line];
                spans.clear();
                highlight_line(doc->lang, s.c_str(), (int)s.size(), st, &spans);

                // expand tabs into a display buffer of runs
                // paint selection first
                if (anysel) {
                    uint64_t ls = a0, le = b0;
                    uint64_t sa = slo > ls ? slo : ls;
                    uint64_t sb = shi < le ? shi : le;
                    if (shi > le && slo <= le) sb = le; // selection continues
                    if (sa < sb || (slo <= ls && shi > le)) {
                        int c0 = (int)doc_col_of(doc, sa) - doc->hscroll;
                        int c1;
                        if (shi > le) c1 = 10000;
                        else c1 = (int)doc_col_of(doc, sb) - doc->hscroll;
                        if (c1 < c0) c1 = c0 + 1;
                        int x0 = c0 * a->cell_w;
                        int x1 = c1 * a->cell_w;
                        if (x1 < 0) x1 = 0;
                        if (x0 < w && x1 > 0) {
                            if (x0 < 0) x0 = 0;
                            fill(dr, x0, y, x1 - x0, a->cell_h, COL_SEL);
                        }
                    }
                    // empty line selected
                    if (slo <= ls && shi > le && s.empty()) {
                        fill(dr, 0, y, a->cell_w, a->cell_h, COL_SEL);
                    }
                }
                if (a->has_match) {
                    if (a->last_match_a >= a0 && a->last_match_a < b0) {
                        int c0 = (int)doc_col_of(doc, a->last_match_a) - doc->hscroll;
                        int c1 = (int)doc_col_of(doc, a->last_match_b > b0 ? b0 : a->last_match_b) - doc->hscroll;
                        fill(dr, c0 * a->cell_w, y, (c1 - c0) * a->cell_w, a->cell_h, COL_FIND);
                    }
                }

                // draw text with syntax, honouring hscroll
                // Build expanded string with tabs
                if (spans.empty()) spans.push_back(Span{0, 100000, Hl::Normal});
                int col = 0;
                size_t bi = 0;
                const char* p = s.c_str();
                int sn = (int)s.size();
                int si = 0;
                while (si < sn) {
                    int bytes = 1;
                    int wch = 1;
                    if (p[si] == '\t') { wch = 4 - (col % 4); bytes = 1; }
                    else bytes = utf8_cp_len((const uint8_t*)p + si, (const uint8_t*)p + sn);

                    Hl h = Hl::Normal;
                    for (auto& sp : spans) {
                        if (col >= sp.col && col < sp.col + sp.n) { h = sp.hl; break; }
                    }
                    int dcol0 = col;
                    int dcol1 = col + wch;
                    if (dcol1 > doc->hscroll && dcol0 < doc->hscroll + w / a->cell_w + 2) {
                        int x = (dcol0 - doc->hscroll) * a->cell_w;
                        uint32_t fg = hl_color(h);
                        bool sel = anysel && (a0 + (uint64_t)si) >= slo && (a0 + (uint64_t)si) < shi;
                        if (sel) fg = COL_SEL_FG;
                        if (p[si] == '\t') {
                            // skip, background already
                        } else {
                            text(dr, x, y, p + si, bytes, fg);
                        }
                    }
                    col += wch;
                    si += bytes;
                    if (col - doc->hscroll > w / a->cell_w + 4) break;
                }
            }
        }
    }

    // status
    int sy = h - a->status_h;
    fill(dr, 0, sy, w, a->status_h, COL_STATUS);
    char st[512];
    app_status_text(a, st, sizeof(st));
    int ty = sy + (a->status_h - a->cell_h) / 2;
    text(dr, 8, ty, st, -1, COL_ST_FG);
}

