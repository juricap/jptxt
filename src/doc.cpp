#include "jptxt.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

int utf8_cp_len(const uint8_t* p, const uint8_t* end) {
    if (p >= end) return 0;
    uint8_t c = *p;
    if (c < 0x80) return 1;
    int n = (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (p + n > end) return 1;
    return n;
}

uint32_t utf8_next(const uint8_t* p, const uint8_t* end, int* adv) {
    if (p >= end) { *adv = 0; return 0; }
    uint8_t c = *p;
    if (c < 0x80) { *adv = 1; return c; }
    int n = utf8_cp_len(p, end);
    *adv = n;
    uint32_t cp = 0;
    if (n == 2) cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
    else if (n == 3) cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    else if (n == 4) cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    else cp = c;
    return cp;
}

int utf8_put(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// ---------------------------------------------------------------------------
// mmap
// ---------------------------------------------------------------------------

static bool map_open(FileMap* m, const char* path) {
    m->data = nullptr;
    m->size = 0;
#ifdef _WIN32
    m->file = nullptr;
    m->map = nullptr;
    wchar_t w[32768];
    int wn = MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 32768);
    if (wn <= 0) return false;
    HANDLE f = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz)) { CloseHandle(f); return false; }
    m->size = (uint64_t)sz.QuadPart;
    m->file = (void*)f;
    if (m->size == 0) return true;
    HANDLE map = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map) { CloseHandle(f); m->file = nullptr; return false; }
    void* p = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    if (!p) { CloseHandle(map); CloseHandle(f); m->file = nullptr; return false; }
    m->map = (void*)map;
    m->data = (const uint8_t*)p;
    return true;
#else
    m->fd = open(path, O_RDONLY);
    if (m->fd < 0) return false;
    struct stat st{};
    if (fstat(m->fd, &st) != 0) { close(m->fd); m->fd = -1; return false; }
    m->size = (uint64_t)st.st_size;
    if (m->size == 0) return true;
    void* p = mmap(nullptr, (size_t)m->size, PROT_READ, MAP_PRIVATE, m->fd, 0);
    if (p == MAP_FAILED) { close(m->fd); m->fd = -1; return false; }
#ifdef MADV_SEQUENTIAL
    madvise(p, (size_t)m->size, MADV_SEQUENTIAL);
#endif
    m->data = (const uint8_t*)p;
    return true;
#endif
}

void doc_unmap(Doc* d) {
#ifdef _WIN32
    if (d->map.data) { UnmapViewOfFile(d->map.data); d->map.data = nullptr; }
    if (d->map.map)  { CloseHandle((HANDLE)d->map.map); d->map.map = nullptr; }
    if (d->map.file) { CloseHandle((HANDLE)d->map.file); d->map.file = nullptr; }
#else
    if (d->map.data && d->map.size) { munmap((void*)d->map.data, (size_t)d->map.size); d->map.data = nullptr; }
    if (d->map.fd >= 0) { close(d->map.fd); d->map.fd = -1; }
#endif
    d->map.size = 0;
}

Doc::~Doc() { doc_unmap(this); }

Doc::Doc(Doc&& o) noexcept { *this = std::move(o); }

Doc& Doc::operator=(Doc&& o) noexcept {
    if (this == &o) return *this;
    doc_unmap(this);
    path = std::move(o.path);
    name = std::move(o.name);
    map = o.map; o.map = FileMap{};
    add = std::move(o.add);
    pcs = std::move(o.pcs);
    line_off = std::move(o.line_off);
    hl_at = std::move(o.hl_at);
    len = o.len;
    binary = o.binary; hex = o.hex; readonly = o.readonly; dirty = o.dirty;
    enc = o.enc; eol = o.eol; lang = o.lang;
    caret = o.caret; sel_anchor = o.sel_anchor; pref_col = o.pref_col;
    top_line = o.top_line; hscroll = o.hscroll; insert = o.insert;
    undo = std::move(o.undo); redo = std::move(o.redo);
    save_undo_size = o.save_undo_size;
    return *this;
}

// ---------------------------------------------------------------------------
// detect
// ---------------------------------------------------------------------------

static bool valid_utf8(const uint8_t* p, uint64_t n) {
    uint64_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        int need = 0;
        if (c < 0x80) { i++; continue; }
        else if ((c >> 5) == 0x6) need = 1;
        else if ((c >> 4) == 0xE) need = 2;
        else if ((c >> 3) == 0x1E) need = 3;
        else return false;
        if (i + 1 + need > n) return false;
        for (int k = 0; k < need; k++) if ((p[i + 1 + k] >> 6) != 2) return false;
        i += 1 + need;
    }
    return true;
}

static bool utf16_heuristic(const uint8_t* p, uint64_t n, bool* be) {
    if (n < 16) return false;
    uint64_t take = n < 512 ? n : 512;
    if (take & 1) take--;
    uint64_t even0 = 0, odd0 = 0;
    for (uint64_t i = 0; i < take; i += 2) {
        if (p[i] == 0) even0++;
        if (p[i + 1] == 0) odd0++;
    }
    uint64_t pairs = take / 2;
    if (pairs == 0) return false;
    // UTF-16LE: high bytes often 0 for ASCII -> odd index 0
    if (odd0 * 5 > pairs * 3 && even0 * 5 < pairs) { *be = false; return true; }
    if (even0 * 5 > pairs * 3 && odd0 * 5 < pairs) { *be = true; return true; }
    return false;
}

void detect_file(const uint8_t* p, uint64_t n, bool* binary, Enc* enc, Eol* eol) {
    *binary = false;
    *enc = Enc::UTF8;
    *eol = Eol::LF;
    if (n == 0) return;

    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        *enc = Enc::UTF8BOM;
    } else if (n >= 4 && p[0] == 0xFF && p[1] == 0xFE && p[2] == 0x00 && p[3] == 0x00) {
        *enc = Enc::UTF16LE; // actually UTF-32LE; treat as binary
        *binary = true;
        return;
    } else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        *enc = Enc::UTF16LE;
    } else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        *enc = Enc::UTF16BE;
    } else {
        bool be = false;
        if (utf16_heuristic(p, n, &be)) {
            *enc = be ? Enc::UTF16BE : Enc::UTF16LE;
        } else {
            // zlib txtvsbin: block list 0-6, 14-31
            uint64_t sample = n < 8192 ? n : 8192;
            bool bin = false;
            for (uint64_t i = 0; i < sample; i++) {
                uint8_t c = p[i];
                if (c <= 6 || (c >= 14 && c <= 31)) { bin = true; break; }
            }
            if (bin) {
                *binary = true;
                *enc = Enc::Latin1;
                return;
            }
            if (!valid_utf8(p, sample)) *enc = Enc::Latin1;
            else *enc = Enc::UTF8;
        }
    }

    uint64_t sample = n < 65536 ? n : 65536;
    uint64_t cr = 0, lf = 0, crlf = 0;
    if (*enc == Enc::UTF16LE || *enc == Enc::UTF16BE) {
        bool be = *enc == Enc::UTF16BE;
        for (uint64_t i = 0; i + 1 < sample; i += 2) {
            uint16_t ch = be ? (uint16_t)((p[i] << 8) | p[i + 1])
                             : (uint16_t)(p[i] | (p[i + 1] << 8));
            if (ch == '\r') {
                uint16_t nx = 0;
                if (i + 3 < sample)
                    nx = be ? (uint16_t)((p[i + 2] << 8) | p[i + 3])
                            : (uint16_t)(p[i + 2] | (p[i + 3] << 8));
                if (nx == '\n') { crlf++; i += 2; }
                else cr++;
            } else if (ch == '\n') lf++;
        }
    } else {
        for (uint64_t i = 0; i < sample; i++) {
            if (p[i] == '\r') {
                if (i + 1 < sample && p[i + 1] == '\n') { crlf++; i++; }
                else cr++;
            } else if (p[i] == '\n') lf++;
        }
    }
    int kinds = (crlf > 0) + (lf > 0) + (cr > 0);
    if (kinds > 1) *eol = Eol::Mixed;
    else if (crlf >= lf && crlf >= cr && crlf) *eol = Eol::CRLF;
    else if (cr > lf) *eol = Eol::CR;
    else *eol = Eol::LF;
}

static std::string utf16_to_utf8(const uint8_t* p, uint64_t n, bool be) {
    std::string o;
    o.reserve((size_t)(n + n / 2));
    uint64_t i = 0;
    if (n >= 2 && ((be && p[0] == 0xFE && p[1] == 0xFF) || (!be && p[0] == 0xFF && p[1] == 0xFE)))
        i = 2;
    auto rd = [&](uint64_t k) -> uint16_t {
        if (k + 1 >= n) return 0;
        return be ? (uint16_t)((p[k] << 8) | p[k + 1]) : (uint16_t)(p[k] | (p[k + 1] << 8));
    };
    while (i + 1 < n) {
        uint16_t w = rd(i); i += 2;
        uint32_t cp = w;
        if (w >= 0xD800 && w <= 0xDBFF && i + 1 < n) {
            uint16_t w2 = rd(i);
            if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                cp = 0x10000 + (((uint32_t)w - 0xD800) << 10) + (w2 - 0xDC00);
                i += 2;
            }
        }
        char tmp[4];
        int k = utf8_put(cp, tmp);
        o.append(tmp, k);
    }
    return o;
}

static std::string latin1_to_utf8(const uint8_t* p, uint64_t n) {
    std::string o;
    o.reserve((size_t)(n + n / 4));
    for (uint64_t i = 0; i < n; i++) {
        char tmp[4];
        int k = utf8_put(p[i], tmp);
        o.append(tmp, k);
    }
    return o;
}

// ---------------------------------------------------------------------------
// pieces
// ---------------------------------------------------------------------------

static const uint8_t* piece_ptr(const Doc* d, const Piece& p) {
    if (p.src == 0) return d->map.data + p.off;
    return (const uint8_t*)d->add.data() + p.off;
}

template<typename F>
static void for_chunks(const Doc* d, uint64_t pos, uint64_t n, F fn) {
    if (n == 0 || pos >= d->len) return;
    if (pos + n > d->len) n = d->len - pos;
    uint64_t cur = 0;
    for (const Piece& p : d->pcs) {
        if (pos >= cur + p.len) { cur += p.len; continue; }
        uint64_t start = pos > cur ? pos - cur : 0;
        uint64_t take = p.len - start;
        if (take > n) take = n;
        fn(piece_ptr(d, p) + start, take);
        n -= take;
        pos += take;
        cur += p.len;
        if (!n) break;
    }
}

uint8_t doc_at(const Doc* d, uint64_t pos) {
    uint8_t b = 0;
    for_chunks(d, pos, 1, [&](const uint8_t* p, uint64_t) { b = p[0]; });
    return b;
}

void doc_read(const Doc* d, uint64_t pos, uint64_t n, std::string* out) {
    out->clear();
    if (n == 0) return;
    out->reserve((size_t)(n < 1u << 20 ? n : 1u << 20));
    for_chunks(d, pos, n, [&](const uint8_t* p, uint64_t k) {
        out->append((const char*)p, (size_t)k);
    });
}

static size_t split_at(Doc* d, uint64_t pos) {
    if (pos > d->len) pos = d->len;
    uint64_t cur = 0;
    for (size_t i = 0; i < d->pcs.size(); i++) {
        Piece& p = d->pcs[i];
        if (pos == cur) return i;
        if (pos < cur + p.len) {
            uint64_t k = pos - cur;
            Piece r{p.src, p.off + k, p.len - k};
            p.len = k;
            d->pcs.insert(d->pcs.begin() + (i + 1), r);
            return i + 1;
        }
        cur += p.len;
    }
    return d->pcs.size();
}

static void coalesce(Doc* d) {
    if (d->pcs.size() < 2) return;
    std::vector<Piece> o;
    o.reserve(d->pcs.size());
    for (auto& p : d->pcs) {
        if (p.len == 0) continue;
        if (!o.empty() && o.back().src == p.src && o.back().off + o.back().len == p.off)
            o.back().len += p.len;
        else o.push_back(p);
    }
    d->pcs.swap(o);
}

uint64_t utf8_next_pos(const Doc* d, uint64_t pos) {
    if (pos >= d->len) return d->len;
    uint8_t buf[4];
    uint64_t n = d->len - pos;
    if (n > 4) n = 4;
    int i = 0;
    for_chunks(d, pos, n, [&](const uint8_t* p, uint64_t k) {
        memcpy(buf + i, p, (size_t)k);
        i += (int)k;
    });
    int adv = utf8_cp_len(buf, buf + i);
    if (adv < 1) adv = 1;
    uint64_t np = pos + (uint64_t)adv;
    return np > d->len ? d->len : np;
}

uint64_t utf8_prev(const Doc* d, uint64_t pos) {
    if (pos == 0) return 0;
    uint64_t p = pos - 1;
    int steps = 0;
    while (p > 0 && steps < 3) {
        uint8_t b = doc_at(d, p);
        if ((b & 0xC0) != 0x80) break;
        p--; steps++;
    }
    return p;
}

// ---------------------------------------------------------------------------
// line index
// ---------------------------------------------------------------------------

void doc_reindex(Doc* d) {
    d->line_off.clear();
    d->line_off.push_back(0);
    d->hl_at.clear();
    d->hl_at.push_back(0);
    if (d->binary || d->len == 0) return;
    if (d->len > 32) d->line_off.reserve((size_t)(d->len / 32 + 8));

    uint64_t pos = 0;
    for_chunks(d, 0, d->len, [&](const uint8_t* p, uint64_t n) {
        uint64_t i = 0;
        while (i < n) {
            const uint8_t* nl = (const uint8_t*)memchr(p + i, '\n', (size_t)(n - i));
            if (!nl) break;
            d->line_off.push_back(pos + (uint64_t)(nl - p) + 1);
            i = (uint64_t)(nl - p) + 1;
        }
        pos += n;
    });
    // Old Mac CR-only: no LF in the file but CR present
    if (d->line_off.size() == 1 && d->len) {
        bool any_cr = false;
        for_chunks(d, 0, d->len, [&](const uint8_t* p, uint64_t n) {
            if (!any_cr && memchr(p, '\r', (size_t)n)) any_cr = true;
        });
        if (any_cr) {
            pos = 0;
            d->line_off.assign(1, 0);
            for_chunks(d, 0, d->len, [&](const uint8_t* p, uint64_t n) {
                uint64_t i = 0;
                while (i < n) {
                    const uint8_t* cr = (const uint8_t*)memchr(p + i, '\r', (size_t)(n - i));
                    if (!cr) break;
                    d->line_off.push_back(pos + (uint64_t)(cr - p) + 1);
                    i = (uint64_t)(cr - p) + 1;
                }
                pos += n;
            });
        }
    }
    d->hl_at.assign(d->line_off.size(), 0);
#ifdef _WIN32
    if (d->len > (8ull << 20))
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
}

static void shift_lines(Doc* d, uint64_t pos, int64_t delta) {
    // first line with offset > pos
    auto it = std::upper_bound(d->line_off.begin(), d->line_off.end(), pos);
    for (; it != d->line_off.end(); ++it) {
        int64_t v = (int64_t)*it + delta;
        *it = v < 0 ? 0 : (uint64_t)v;
    }
}

void doc_index_edit(Doc* d, uint64_t pos, int64_t delta, const char* inserted, uint64_t nins) {
    if (d->binary) return;
    if (d->line_off.empty()) { doc_reindex(d); return; }

    if (delta < 0) {
        uint64_t a = pos;
        uint64_t b = pos + (uint64_t)(-delta);
        auto lo = std::upper_bound(d->line_off.begin(), d->line_off.end(), a);
        auto hi = std::upper_bound(d->line_off.begin(), d->line_off.end(), b);
        size_t i0 = (size_t)(lo - d->line_off.begin());
        size_t i1 = (size_t)(hi - d->line_off.begin());
        if (i1 > i0) {
            d->line_off.erase(d->line_off.begin() + i0, d->line_off.begin() + i1);
            if (i1 <= d->hl_at.size() && i0 < d->hl_at.size()) {
                size_t h1 = i1 < d->hl_at.size() ? i1 : d->hl_at.size();
                d->hl_at.erase(d->hl_at.begin() + i0, d->hl_at.begin() + h1);
            }
        }
        shift_lines(d, pos, delta);
    } else {
        shift_lines(d, pos, delta);
        if (inserted && nins) {
            std::vector<uint64_t> extra;
            for (uint64_t i = 0; i < nins; i++) {
                if (inserted[i] == '\n') extra.push_back(pos + i + 1);
                else if (inserted[i] == '\r' && (i + 1 >= nins || inserted[i + 1] != '\n'))
                    extra.push_back(pos + i + 1);
            }
            if (!extra.empty()) {
                auto it = std::upper_bound(d->line_off.begin(), d->line_off.end(), pos);
                size_t idx = (size_t)(it - d->line_off.begin());
                d->line_off.insert(it, extra.begin(), extra.end());
                if (d->hl_at.size() < d->line_off.size())
                    d->hl_at.insert(d->hl_at.begin() + (idx < d->hl_at.size() ? idx : d->hl_at.size()),
                                    extra.size(), 0);
            }
        }
    }
    if (d->line_off.empty() || d->line_off[0] != 0) {
        d->line_off.insert(d->line_off.begin(), 0);
        d->hl_at.insert(d->hl_at.begin(), 0);
    }
}

uint64_t doc_line_count(const Doc* d) {
    if (d->hex) return (uint64_t)app_hex_rows(d);
    return d->line_off.empty() ? 1 : (uint64_t)d->line_off.size();
}

int app_hex_rows(const Doc* d) {
    if (!d) return 1;
    uint64_t n = d->len;
    if (n == 0) return 1;
    return (int)((n + 15) / 16);
}

void doc_line_span(const Doc* d, uint64_t line, uint64_t* a, uint64_t* b) {
    uint64_t lc = d->line_off.empty() ? 1 : (uint64_t)d->line_off.size();
    if (line >= lc) { *a = d->len; *b = d->len; return; }
    *a = d->line_off[(size_t)line];
    uint64_t e = (line + 1 < lc) ? d->line_off[(size_t)line + 1] : d->len;
    if (e > *a && doc_at(d, e - 1) == '\n') e--;
    if (e > *a && doc_at(d, e - 1) == '\r') e--;
    *b = e;
}

uint64_t doc_line_of(const Doc* d, uint64_t pos) {
    if (d->hex) return pos / 16;
    if (d->line_off.empty()) return 0;
    auto it = std::upper_bound(d->line_off.begin(), d->line_off.end(), pos);
    if (it == d->line_off.begin()) return 0;
    return (uint64_t)((it - d->line_off.begin()) - 1);
}

static int disp_cols_bytes(const uint8_t* p, uint64_t n, int tabw, int maxcol) {
    int col = 0;
    uint64_t i = 0;
    while (i < n && col < maxcol) {
        if (p[i] == '\t') { int t = tabw - (col % tabw); col += t; i++; }
        else {
            int adv = utf8_cp_len(p + i, p + n);
            col++;
            i += (uint64_t)adv;
        }
    }
    return col;
}

uint64_t doc_col_of(const Doc* d, uint64_t pos) {
    if (d->hex) return (pos % 16);
    uint64_t line = doc_line_of(d, pos);
    uint64_t a, b;
    doc_line_span(d, line, &a, &b);
    if (pos < a) pos = a;
    if (pos > b) pos = b;
    std::string s;
    doc_read(d, a, pos - a, &s);
    int col = 0;
    const uint8_t* p = (const uint8_t*)s.data();
    uint64_t n = s.size();
    uint64_t i = 0;
    while (i < n) {
        if (p[i] == '\t') { col += 4 - (col % 4); i++; }
        else { i += (uint64_t)utf8_cp_len(p + i, p + n); col++; }
    }
    return (uint64_t)col;
}

uint64_t doc_pos_at(const Doc* d, uint64_t line, uint64_t col) {
    if (d->hex) {
        uint64_t p = line * 16 + col;
        return p > d->len ? d->len : p;
    }
    uint64_t a, b;
    doc_line_span(d, line, &a, &b);
    std::string s;
    doc_read(d, a, b - a, &s);
    int c = 0;
    uint64_t i = 0;
    const uint8_t* p = (const uint8_t*)s.data();
    uint64_t n = s.size();
    while (i < n) {
        int w = 1;
        uint64_t adv;
        if (p[i] == '\t') { w = 4 - (c % 4); adv = 1; }
        else { adv = (uint64_t)utf8_cp_len(p + i, p + n); }
        if ((uint64_t)c + (uint64_t)((p[i] == '\t') ? w : 1) > col) break;
        c += (p[i] == '\t') ? w : 1;
        i += adv;
    }
    return a + i;
}

// ---------------------------------------------------------------------------
// edits
// ---------------------------------------------------------------------------

static void record(Doc* d, Edit e) {
    d->undo.push_back(std::move(e));
    if (d->undo.size() > 256) {
        // keep last 256 grouped-ish: drop oldest
        d->undo.erase(d->undo.begin());
        if (d->save_undo_size > 0) d->save_undo_size--;
    }
    d->redo.clear();
    d->dirty = true;
}

void doc_insert(Doc* d, uint64_t pos, const char* s, uint64_t n) {
    if (!s || n == 0 || d->readonly) return;
    if (pos > d->len) pos = d->len;
    size_t idx = split_at(d, pos);
    Piece np{1, (uint64_t)d->add.size(), n};
    d->add.append(s, (size_t)n);
    d->pcs.insert(d->pcs.begin() + idx, np);
    d->len += n;
    coalesce(d);
    doc_index_edit(d, pos, (int64_t)n, s, n);
    record(d, Edit{pos, {}, std::string(s, (size_t)n)});
}

void doc_erase(Doc* d, uint64_t pos, uint64_t n) {
    if (n == 0 || d->readonly || pos >= d->len) return;
    if (pos + n > d->len) n = d->len - pos;
    std::string gone;
    doc_read(d, pos, n, &gone);
    size_t i0 = split_at(d, pos);
    size_t i1 = split_at(d, pos + n);
    d->pcs.erase(d->pcs.begin() + i0, d->pcs.begin() + i1);
    d->len -= n;
    coalesce(d);
    doc_index_edit(d, pos, -(int64_t)n, nullptr, 0);
    record(d, Edit{pos, std::move(gone), {}});
}

void doc_apply(Doc* d, const Edit& e, bool reverse) {
    // used by undo: reverse=true means undo this edit
    bool saved_ro = d->readonly;
    d->readonly = false;
    // suppress nested record by temporarily... we still record. So implement raw.
    // We'll poke pieces without record by cloning logic.
    std::string ins = reverse ? e.removed : e.inserted;
    std::string del = reverse ? e.inserted : e.removed;
    // erase inserted (or original inserted)
    if (!del.empty()) {
        uint64_t n = del.size();
        uint64_t pos = e.pos;
        if (pos > d->len) pos = d->len;
        if (pos + n > d->len) n = d->len - pos;
        size_t i0 = split_at(d, pos);
        size_t i1 = split_at(d, pos + n);
        d->pcs.erase(d->pcs.begin() + i0, d->pcs.begin() + i1);
        d->len -= n;
        coalesce(d);
        doc_index_edit(d, pos, -(int64_t)n, nullptr, 0);
    }
    if (!ins.empty()) {
        uint64_t pos = e.pos;
        if (pos > d->len) pos = d->len;
        size_t idx = split_at(d, pos);
        Piece np{1, (uint64_t)d->add.size(), ins.size()};
        d->add.append(ins);
        d->pcs.insert(d->pcs.begin() + idx, np);
        d->len += ins.size();
        coalesce(d);
        doc_index_edit(d, pos, (int64_t)ins.size(), ins.data(), ins.size());
    }
    d->readonly = saved_ro;
    d->dirty = ((int)d->undo.size() != d->save_undo_size);
}

// ---------------------------------------------------------------------------
// load / save / empty
// ---------------------------------------------------------------------------

static std::string basename_utf8(const char* path) {
    const char* s = path;
    const char* b = s;
    for (; *s; s++) if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}

static const char* ext_of(const char* name) {
    const char* e = strrchr(name, '.');
    return e ? e : "";
}

#ifndef _WIN32
static int _stricmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#endif

Lang lang_from_name(const char* name) {
    const char* e = ext_of(name);
    auto eq = [&](const char* a) {
        if (!e || !*e) return false;
        const char* p = e[0] == '.' ? e + 1 : e;
        while (*p && *a) {
            char c = *p, d = *a;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
            if (c != d) return false;
            p++; a++;
        }
        return *p == 0 && *a == 0;
    };
    const char* base = name;
    const char* slash = name;
    for (const char* q = name; *q; q++) if (*q == '/' || *q == '\\') slash = q + 1;
    base = slash;
    if (eq("c") || eq("h")) return Lang::C;
    if (eq("cpp") || eq("cxx") || eq("cc") || eq("hpp") || eq("hh") || eq("hxx") || eq("inl")) return Lang::Cpp;
    if (eq("cs")) return Lang::CSharp;
    if (eq("java")) return Lang::Java;
    if (eq("js") || eq("mjs") || eq("cjs") || eq("ts") || eq("tsx") || eq("jsx")) return Lang::JS;
    if (eq("go")) return Lang::Go;
    if (eq("rs")) return Lang::Rust;
    if (eq("py") || eq("pyw")) return Lang::Python;
    if (eq("sh") || eq("bash") || eq("zsh") || eq("ksh")) return Lang::Shell;
    if (eq("json") || eq("jsonc")) return Lang::JSON;
    if (eq("xml") || eq("html") || eq("htm") || eq("svg") || eq("xhtml") || eq("csproj")) return Lang::XML;
    if (eq("md") || eq("markdown")) return Lang::MD;
    if (eq("ini") || eq("cfg") || eq("conf") || eq("toml") || eq("properties")) return Lang::Ini;
    if (eq("css") || eq("scss")) return Lang::CSS;
    if (eq("lua")) return Lang::Lua;
    if (eq("sql")) return Lang::SQL;
    if (eq("rb")) return Lang::Ruby;
    if (eq("kt") || eq("kts")) return Lang::Kotlin;
    if (eq("swift")) return Lang::Swift;
    if (eq("mk") || eq("mak") || _stricmp(base, "Makefile") == 0 || _stricmp(base, "makefile") == 0)
        return Lang::Make;
    if (eq("cmake") || _stricmp(base, "CMakeLists.txt") == 0) return Lang::Make;
    return Lang::Plain;
}

const char* enc_name(Enc e) {
    switch (e) {
    case Enc::UTF8: return "UTF-8";
    case Enc::UTF8BOM: return "UTF-8 BOM";
    case Enc::UTF16LE: return "UTF-16LE";
    case Enc::UTF16BE: return "UTF-16BE";
    case Enc::Latin1: return "Latin-1";
    }
    return "?";
}
const char* eol_name(Eol e) {
    switch (e) {
    case Eol::LF: return "LF";
    case Eol::CRLF: return "CRLF";
    case Eol::CR: return "CR";
    case Eol::Mixed: return "Mixed";
    }
    return "?";
}

void doc_init_empty(Doc* d, int untitled_n) {
    doc_unmap(d);
    d->path.clear();
    char buf[64];
    snprintf(buf, sizeof(buf), "Untitled-%d", untitled_n);
    d->name = buf;
    d->add.clear();
    d->pcs.clear();
    d->line_off = {0};
    d->hl_at = {0};
    d->len = 0;
    d->binary = d->hex = d->readonly = d->dirty = false;
#ifdef _WIN32
    d->eol = Eol::CRLF;
#else
    d->eol = Eol::LF;
#endif
    d->enc = Enc::UTF8;
    d->lang = Lang::Plain;
    d->caret = d->sel_anchor = 0;
    d->pref_col = -1;
    d->top_line = 0;
    d->hscroll = 0;
    d->insert = true;
    d->undo.clear();
    d->redo.clear();
    d->save_undo_size = 0;
}

static void use_bytes_as_orig(Doc* d) {
    d->pcs.clear();
    if (d->map.size && d->map.data) {
        d->pcs.push_back(Piece{0, 0, d->map.size});
        d->len = d->map.size;
    } else {
        d->len = 0;
    }
}

static void use_string_as_add(Doc* d, std::string&& s) {
    doc_unmap(d);
    d->add = std::move(s);
    d->pcs.clear();
    d->len = d->add.size();
    if (d->len) d->pcs.push_back(Piece{1, 0, d->len});
}

static FILE* g_load_trace;

void doc_set_trace(FILE* f) { g_load_trace = f; }

static void tlog(const char* msg) {
    if (!g_load_trace) return;
    fprintf(g_load_trace, "  %s\n", msg);
    fflush(g_load_trace);
}

bool doc_load(Doc* d, const char* path_utf8) {
    doc_unmap(d);
    d->add.clear();
    d->pcs.clear();
    d->undo.clear();
    d->redo.clear();
    tlog("map_open...");
    if (!map_open(&d->map, path_utf8)) { tlog("map_open FAIL"); return false; }
    {
        char b[80];
        snprintf(b, sizeof(b), "mapped %llu @ %p", (unsigned long long)d->map.size, (void*)d->map.data);
        tlog(b);
    }

    d->path = path_utf8;
    d->name = basename_utf8(path_utf8);
    d->lang = lang_from_name(d->name.c_str());
    d->dirty = false;
    d->save_undo_size = 0;
    d->caret = d->sel_anchor = 0;
    d->top_line = 0;
    d->hscroll = 0;
    d->pref_col = -1;
    d->insert = true;

    const uint8_t* p = d->map.data;
    uint64_t n = d->map.size;
    tlog("detect...");
    detect_file(p ? p : (const uint8_t*)"", n, &d->binary, &d->enc, &d->eol);

    if (d->binary) {
        tlog("binary");
        d->hex = true;
        d->readonly = true;
        use_bytes_as_orig(d);
        d->line_off = {0};
        d->hl_at = {0};
        return true;
    }

    d->hex = false;
    d->readonly = false;

    if (d->enc == Enc::UTF16LE || d->enc == Enc::UTF16BE) {
        std::string u = utf16_to_utf8(p, n, d->enc == Enc::UTF16BE);
        use_string_as_add(d, std::move(u));
    } else if (d->enc == Enc::Latin1) {
        std::string u = latin1_to_utf8(p, n);
        use_string_as_add(d, std::move(u));
    } else {
        // UTF-8, maybe skip BOM
        use_bytes_as_orig(d);
        if (d->enc == Enc::UTF8BOM && d->len >= 3) {
            d->pcs[0].off = 3;
            d->pcs[0].len -= 3;
            d->len -= 3;
        }
    }
    tlog("reindex...");
    doc_reindex(d);
    {
        char b[80];
        snprintf(b, sizeof(b), "lines %llu", (unsigned long long)d->line_off.size());
        tlog(b);
    }
    tlog("load done");
    return true;
}

#ifdef _WIN32
static bool write_all(HANDLE f, const uint8_t* p, uint64_t n) {
    while (n) {
        DWORD chunk = n > 1u << 20 ? (1u << 20) : (DWORD)n;
        DWORD w = 0;
        if (!WriteFile(f, p, chunk, &w, nullptr) || w == 0) return false;
        p += w; n -= w;
    }
    return true;
}
#else
static bool write_all(int fd, const uint8_t* p, uint64_t n) {
    while (n) {
        size_t chunk = n > (1u << 20) ? (1u << 20) : (size_t)n;
        ssize_t w = write(fd, p, chunk);
        if (w <= 0) return false;
        p += (size_t)w; n -= (size_t)w;
    }
    return true;
}
#endif

bool doc_save(Doc* d, const char* path_utf8) {
    if (!path_utf8 || !*path_utf8) return false;
#ifdef _WIN32
    wchar_t w[32768];
    if (MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, w, 32768) <= 0) return false;
    wchar_t tmp[32768];
    wcsncpy(tmp, w, 32760);
    tmp[32760] = 0;
    wcscat(tmp, L".jptxt~");
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    if (d->enc == Enc::UTF8BOM) {
        static const uint8_t bom[3] = {0xEF, 0xBB, 0xBF};
        ok = write_all(f, bom, 3);
    }
    if (ok) {
        for (auto& p : d->pcs) {
            if (!write_all(f, piece_ptr(d, p), p.len)) { ok = false; break; }
        }
    }
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp); return false; }
    if (!MoveFileExW(tmp, w, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        DeleteFileW(tmp);
        return false;
    }
#else
    std::string tmp = std::string(path_utf8) + ".jptxt~";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool ok = true;
    if (d->enc == Enc::UTF8BOM) {
        static const uint8_t bom[3] = {0xEF, 0xBB, 0xBF};
        ok = write_all(fd, bom, 3);
    }
    if (ok) {
        for (auto& p : d->pcs) {
            if (!write_all(fd, piece_ptr(d, p), p.len)) { ok = false; break; }
        }
    }
    close(fd);
    if (!ok) { unlink(tmp.c_str()); return false; }
    if (rename(tmp.c_str(), path_utf8) != 0) { unlink(tmp.c_str()); return false; }
#endif
    d->path = path_utf8;
    d->name = basename_utf8(path_utf8);
    d->lang = lang_from_name(d->name.c_str());
    d->dirty = false;
    d->save_undo_size = (int)d->undo.size();
    return true;
}

int doc_bench(const char* path, FILE* out) {
    if (!out) out = stdout;
    if (!path || !*path) {
        fprintf(out, "bench: missing path\n");
        return 2;
    }
    auto t0 = std::chrono::steady_clock::now();
    Doc d;
    bool ok = doc_load(&d, path);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    fprintf(out, "%s ok=%d bytes=%llu lines=%llu hex=%d bin=%d enc=%s time=%.2f ms\n",
            path,
            ok ? 1 : 0,
            (unsigned long long)d.len,
            (unsigned long long)(ok ? doc_line_count(&d) : 0),
            ok && d.hex ? 1 : 0,
            ok && d.binary ? 1 : 0,
            ok ? enc_name(d.enc) : "?",
            ms);
    fflush(out);
    return ok ? 0 : 2;
}

int doc_selftest(FILE* out) {
    if (!out) out = stdout;
    int fail = 0;
    auto check = [&](bool c, const char* msg) {
        if (!c) { fprintf(out, "FAIL %s\n", msg); fail++; }
    };

    Doc d;
    doc_init_empty(&d, 1);
    check(d.len == 0, "empty len");
    doc_insert(&d, 0, "hello\nworld\n", 12);
    check(d.len == 12, "insert len");
    check(doc_line_count(&d) == 3, "three lines (trailing nl)");
    std::string s;
    doc_read(&d, 0, d.len, &s);
    check(s == "hello\nworld\n", "read back");
    doc_erase(&d, 5, 1); // drop first newline -> "helloworld\n"
    doc_read(&d, 0, d.len, &s);
    check(s == "helloworld\n", "erase nl");
    doc_insert(&d, 5, "\n", 1);
    doc_read(&d, 0, d.len, &s);
    check(s == "hello\nworld\n", "reinsert nl");

    bool bin = false; Enc enc = Enc::UTF8; Eol eol = Eol::LF;
    static const uint8_t mz[] = { 'M', 'Z', 0x90, 0x00, 0x03, 0x00 };
    detect_file(mz, sizeof(mz), &bin, &enc, &eol);
    check(bin, "MZ stub is binary");
    const uint8_t txt[] = { 'a', 'b', 'c', '\n' };
    detect_file(txt, sizeof(txt), &bin, &enc, &eol);
    check(!bin, "abc\\n is text");

#ifdef _WIN32
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    strncat(tmp, "jptxt-selftest.txt", MAX_PATH - strlen(tmp) - 1);
#else
    const char* tmp = "/tmp/jptxt-selftest.txt";
#endif
    check(doc_save(&d, tmp), "save");
    Doc d2;
    check(doc_load(&d2, tmp), "reload");
    std::string s2;
    doc_read(&d2, 0, d2.len, &s2);
    check(s2 == "hello\nworld\n", "roundtrip");
#ifdef _WIN32
    DeleteFileA(tmp);
#else
    unlink(tmp);
#endif

    if (!fail) fprintf(out, "selftest ok\n");
    else fprintf(out, "selftest %d failed\n", fail);
    fflush(out);
    return fail ? 3 : 0;
}

