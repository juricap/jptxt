#include "jptxt.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <regex>

static void lower_ascii(std::string& s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
}

static bool literal_find(const std::string& hay, const std::string& needle, bool icase,
                         bool down, uint64_t base, uint64_t from_local,
                         uint64_t* a, uint64_t* b) {
    if (needle.empty() || hay.empty()) return false;
    if (!icase) {
        if (down) {
            size_t p = hay.find(needle, (size_t)from_local);
            if (p == std::string::npos) return false;
            *a = base + p;
            *b = base + p + needle.size();
            return true;
        } else {
            size_t start = from_local > 0 ? (size_t)from_local : 0;
            if (start > hay.size()) start = hay.size();
            size_t p = hay.rfind(needle, start);
            if (p == std::string::npos) return false;
            *a = base + p;
            *b = base + p + needle.size();
            return true;
        }
    }
    std::string H = hay, N = needle;
    lower_ascii(H); lower_ascii(N);
    if (down) {
        size_t p = H.find(N, (size_t)from_local);
        if (p == std::string::npos) return false;
        *a = base + p;
        *b = base + p + needle.size();
        return true;
    } else {
        size_t start = from_local > 0 ? (size_t)from_local : 0;
        if (start > H.size()) start = H.size();
        size_t p = H.rfind(N, start);
        if (p == std::string::npos) return false;
        *a = base + p;
        *b = base + p + needle.size();
        return true;
    }
}

static bool looks_regex(const std::string& p) {
    for (char c : p) {
        if (strchr(".*+?[](){}|^$\\", c)) return true;
    }
    return false;
}

bool find_in_doc(const Doc* d, const std::string& pat, bool regex, bool icase, bool down,
                 uint64_t from, uint64_t* a, uint64_t* b) {
    if (!d || pat.empty()) return false;
    if (from > d->len) from = d->len;

    bool use_re = regex || looks_regex(pat);
    std::regex re;
    bool re_ok = false;
    if (use_re) {
        try {
            auto fl = std::regex_constants::ECMAScript;
            if (icase) fl = fl | std::regex_constants::icase;
            re = std::regex(pat, fl);
            re_ok = true;
        } catch (...) {
            re_ok = false;
            use_re = false;
        }
    }

    const uint64_t WIN = 1u << 20; // 1 MB windows
    const uint64_t OV  = (uint64_t)std::min<size_t>(pat.size() + 256, 4096);

    if (down) {
        uint64_t pos = from;
        while (pos < d->len) {
            uint64_t n = d->len - pos;
            if (n > WIN) n = WIN;
            std::string chunk;
            doc_read(d, pos, n, &chunk);
            if (use_re && re_ok) {
                std::cmatch m;
                try {
                    if (std::regex_search(chunk.c_str(), chunk.c_str() + chunk.size(), m, re) && m.length() > 0) {
                        *a = pos + (uint64_t)m.position(0);
                        *b = *a + (uint64_t)m.length(0);
                        return true;
                    }
                } catch (...) {}
            } else {
                uint64_t aa, bb;
                if (literal_find(chunk, pat, icase, true, pos, 0, &aa, &bb)) {
                    *a = aa; *b = bb; return true;
                }
            }
            if (pos + n >= d->len) break;
            uint64_t step = n > OV ? n - OV : n;
            pos += step;
        }
        return false;
    }

    // backward
    uint64_t pos = from;
    while (true) {
        uint64_t start = pos > WIN ? pos - WIN : 0;
        uint64_t n = pos - start;
        if (n == 0 && start == 0) {
            // include last chance at 0
        }
        std::string chunk;
        doc_read(d, start, n, &chunk);
        if (use_re && re_ok) {
            // find last match in window that ends <= from
            try {
                auto begin = std::cregex_iterator(chunk.c_str(), chunk.c_str() + chunk.size(), re);
                auto endit = std::cregex_iterator();
                bool found = false;
                uint64_t ba = 0, bb = 0;
                for (auto it = begin; it != endit; ++it) {
                    if (it->length() == 0) continue;
                    uint64_t aa = start + (uint64_t)it->position(0);
                    uint64_t ee = aa + (uint64_t)it->length(0);
                    if (ee <= from && (aa > ba || !found)) { ba = aa; bb = ee; found = true; }
                }
                if (found) { *a = ba; *b = bb; return true; }
            } catch (...) {}
        } else {
            uint64_t aa, bb;
            uint64_t from_local = chunk.empty() ? 0 : chunk.size() - 1;
            if (literal_find(chunk, pat, icase, false, start, from_local, &aa, &bb) && bb <= from) {
                *a = aa; *b = bb; return true;
            }
        }
        if (start == 0) break;
        uint64_t step = n > OV ? n - OV : n;
        if (step == 0) step = 1;
        pos = start + OV;
        if (pos >= from) pos = start;
        if (pos == 0) break;
    }
    return false;
}

