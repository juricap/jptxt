#include "jptxt.h"
#include <cstring>
#include <cctype>

// highlighter state: 0 normal, 1 block-comment, 2 dquote string, 3 squote, 4 backtick, 5 line-comment (eol)

static bool is_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}
static bool is_ident0(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static bool kw_in(const char* w, int n, const char* const* tab) {
    for (int i = 0; tab[i]; i++) {
        const char* k = tab[i];
        int j = 0;
        while (j < n && k[j] && k[j] == w[j]) j++;
        if (j == n && k[j] == 0) return true;
    }
    return false;
}

static const char* const KW_C[] = {
    "auto","break","case","const","continue","default","do","else","enum","extern",
    "for","goto","if","inline","register","restrict","return","sizeof","static",
    "struct","switch","typedef","union","volatile","while","_Alignas","_Alignof",
    "_Atomic","_Bool","_Complex","_Generic","_Imaginary","_Noreturn","_Static_assert",
    "_Thread_local", nullptr
};
static const char* const TY_C[] = {
    "void","char","short","int","long","float","double","signed","unsigned","bool",
    "size_t","ptrdiff_t","intptr_t","uintptr_t","int8_t","int16_t","int32_t","int64_t",
    "uint8_t","uint16_t","uint32_t","uint64_t","FILE", nullptr
};
static const char* const KW_CPP[] = {
    "alignas","alignof","and","and_eq","asm","bitand","bitor","catch","class","compl",
    "concept","consteval","constexpr","constinit","const_cast","co_await","co_return",
    "co_yield","decltype","delete","dynamic_cast","explicit","export","false","friend",
    "mutable","namespace","new","noexcept","not","not_eq","nullptr","operator","or",
    "or_eq","override","private","protected","public","reinterpret_cast","requires",
    "static_assert","static_cast","template","this","thread_local","throw","true","try",
    "typeid","typename","using","virtual","xor","xor_eq","final","import","module",
    nullptr
};
static const char* const KW_CS[] = {
    "abstract","as","async","await","base","bool","break","byte","case","catch","checked",
    "class","const","continue","decimal","default","delegate","do","else","enum","event",
    "explicit","extern","false","finally","fixed","for","foreach","goto","if","implicit",
    "in","interface","internal","is","lock","namespace","new","null","object","operator",
    "out","override","params","private","protected","public","readonly","ref","return",
    "sealed","sizeof","stackalloc","static","struct","switch","this","throw","true","try",
    "typeof","unchecked","unsafe","using","virtual","void","volatile","while","var",
    "record","init","required","nameof","when","where","yield","get","set","value", nullptr
};
static const char* const TY_CS[] = {
    "int","uint","long","ulong","short","ushort","sbyte","byte","float","double","char",
    "string","bool","object","decimal","dynamic", nullptr
};
static const char* const KW_JAVA[] = {
    "abstract","assert","break","case","catch","class","const","continue","default","do",
    "else","enum","extends","final","finally","for","goto","if","implements","import",
    "instanceof","interface","native","new","package","private","protected","public",
    "return","static","strictfp","super","switch","synchronized","this","throw","throws",
    "transient","try","void","volatile","while","var","yield","record","sealed","permits",
    "non-sealed","true","false","null", nullptr
};
static const char* const TY_JAVA[] = {
    "boolean","byte","char","short","int","long","float","double","String","Object", nullptr
};
static const char* const KW_JS[] = {
    "async","await","break","case","catch","class","const","continue","debugger","default",
    "delete","do","else","export","extends","false","finally","for","from","function","get",
    "if","import","in","instanceof","let","new","null","of","return","set","static","super",
    "switch","this","throw","true","try","typeof","undefined","var","void","while","with",
    "yield","as","of", nullptr
};
static const char* const KW_GO[] = {
    "break","case","chan","const","continue","default","defer","else","fallthrough","for",
    "func","go","goto","if","import","interface","map","package","range","return","select",
    "struct","switch","type","var", nullptr
};
static const char* const TY_GO[] = {
    "bool","byte","complex64","complex128","error","float32","float64","int","int8","int16",
    "int32","int64","rune","string","uint","uint8","uint16","uint32","uint64","uintptr",
    "any","nil","true","false", nullptr
};
static const char* const KW_RS[] = {
    "as","async","await","break","const","continue","crate","dyn","else","enum","extern",
    "false","fn","for","if","impl","in","let","loop","match","mod","move","mut","pub",
    "ref","return","self","Self","static","struct","super","trait","true","type","unsafe",
    "use","where","while","async","await","dyn","abstract","become","box","do","final",
    "macro","override","priv","typeof","unsized","virtual","yield","try","union", nullptr
};
static const char* const TY_RS[] = {
    "bool","char","str","i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128",
    "usize","f32","f64","Self","Option","Result","Vec","String","Box","Rc","Arc", nullptr
};
static const char* const KW_PY[] = {
    "and","as","assert","async","await","break","class","continue","def","del","elif","else",
    "except","False","finally","for","from","global","if","import","in","is","lambda",
    "None","nonlocal","not","or","pass","raise","return","True","try","while","with","yield",
    nullptr
};
static const char* const KW_SH[] = {
    "if","then","else","elif","fi","for","while","until","do","done","case","esac","in",
    "function","select","time","coproc","[[","]]","{","}", nullptr
};
static const char* const KW_LUA[] = {
    "and","break","do","else","elseif","end","false","for","function","goto","if","in",
    "local","nil","not","or","repeat","return","then","true","until","while", nullptr
};
static const char* const KW_SQL[] = {
    "select","from","where","insert","into","values","update","set","delete","create",
    "table","index","view","drop","alter","join","left","right","inner","outer","on",
    "group","by","order","having","limit","offset","and","or","not","null","as","in",
    "exists","between","like","distinct","union","all","case","when","then","else","end",
    nullptr
};
static const char* const KW_RB[] = {
    "alias","and","begin","break","case","class","def","defined?","do","else","elsif","end",
    "ensure","false","for","if","in","module","next","nil","not","or","redo","rescue",
    "retry","return","self","super","then","true","undef","unless","until","when","while",
    "yield", nullptr
};
static const char* const KW_KT[] = {
    "as","break","class","continue","do","else","false","for","fun","if","in","interface",
    "is","null","object","package","return","super","this","throw","true","try","typealias",
    "typeof","val","var","when","while","by","catch","constructor","delegate","dynamic",
    "field","file","finally","get","import","init","param","property","receiver","set",
    "setparam","where","actual","abstract","annotation","companion","const","crossinline",
    "data","enum","expect","external","final","infix","inline","inner","internal","lateinit",
    "noinline","open","operator","out","override","private","protected","public","reified",
    "sealed","suspend","tailrec","vararg", nullptr
};
static const char* const KW_SW[] = {
    "associatedtype","class","deinit","enum","extension","fileprivate","func","import","init",
    "inout","internal","let","open","operator","private","protocol","public","rethrows",
    "static","struct","subscript","typealias","var","break","case","continue","default",
    "defer","do","else","fallthrough","for","guard","if","in","repeat","return","switch",
    "where","while","as","Any","catch","false","is","nil","super","self","Self","throw",
    "throws","true","try","async","await","actor", nullptr
};

static bool c_like(Lang l) {
    return l == Lang::C || l == Lang::Cpp || l == Lang::CSharp || l == Lang::Java ||
           l == Lang::JS || l == Lang::Go || l == Lang::Rust || l == Lang::CSS ||
           l == Lang::Kotlin || l == Lang::Swift;
}

static bool match_kw(Lang lang, const char* w, int n) {
    switch (lang) {
    case Lang::C: return kw_in(w, n, KW_C);
    case Lang::Cpp: return kw_in(w, n, KW_C) || kw_in(w, n, KW_CPP);
    case Lang::CSharp: return kw_in(w, n, KW_CS);
    case Lang::Java: return kw_in(w, n, KW_JAVA);
    case Lang::JS: return kw_in(w, n, KW_JS);
    case Lang::Go: return kw_in(w, n, KW_GO);
    case Lang::Rust: return kw_in(w, n, KW_RS);
    case Lang::Python: return kw_in(w, n, KW_PY);
    case Lang::Shell: return kw_in(w, n, KW_SH);
    case Lang::Lua: return kw_in(w, n, KW_LUA);
    case Lang::SQL: {
        char tmp[48];
        if (n >= (int)sizeof(tmp)) return false;
        for (int i = 0; i < n; i++) {
            char c = w[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            tmp[i] = c;
        }
        return kw_in(tmp, n, KW_SQL);
    }
    case Lang::Ruby: return kw_in(w, n, KW_RB);
    case Lang::Kotlin: return kw_in(w, n, KW_KT);
    case Lang::Swift: return kw_in(w, n, KW_SW);
    default: return false;
    }
}
static bool match_ty(Lang lang, const char* w, int n) {
    switch (lang) {
    case Lang::C: case Lang::Cpp: return kw_in(w, n, TY_C);
    case Lang::CSharp: return kw_in(w, n, TY_CS);
    case Lang::Java: return kw_in(w, n, TY_JAVA);
    case Lang::Go: return kw_in(w, n, TY_GO);
    case Lang::Rust: return kw_in(w, n, TY_RS);
    default: return false;
    }
}

static void push(std::vector<Span>* out, int col, int n, Hl h) {
    if (!out || n <= 0) return;
    if (!out->empty()) {
        Span& last = out->back();
        if (last.hl == h && last.col + last.n == col) { last.n += n; return; }
    }
    out->push_back(Span{col, n, h});
}

static int disp_adv(const char* s, int n, int i, int col, int* bytes) {
    if (i >= n) { *bytes = 0; return 0; }
    if (s[i] == '\t') { *bytes = 1; return 4 - (col % 4); }
    int b = utf8_cp_len((const uint8_t*)s + i, (const uint8_t*)s + n);
    *bytes = b; return 1;
}

uint8_t highlight_line(Lang lang, const char* s, int n, uint8_t state, std::vector<Span>* out) {
    if (n < 0) n = 0;
    int i = 0, col = 0;

    auto eat = [&](int bytes, int dcol, Hl h) {
        push(out, col, dcol, h);
        col += dcol;
        i += bytes;
    };

    // continue string/comment from previous line
    if (state == 1) { // block comment
        int start = 0, scol = 0;
        while (i < n) {
            if (i + 1 < n && s[i] == '*' && s[i + 1] == '/') {
                int b, d = disp_adv(s, n, i, col, &b); eat(b, d, Hl::Comment);
                d = disp_adv(s, n, i, col, &b); eat(b, d, Hl::Comment);
                state = 0;
                break;
            }
            int b, d = disp_adv(s, n, i, col, &b); eat(b, d, Hl::Comment);
        }
        if (state == 1) return 1;
    }
    if (state == 2 || state == 3 || state == 4) {
        char q = state == 2 ? '"' : state == 3 ? '\'' : '`';
        while (i < n) {
            if (s[i] == '\\' && i + 1 < n && lang != Lang::MD) {
                int b, d = disp_adv(s, n, i, col, &b); eat(b, d, Hl::String);
                d = disp_adv(s, n, i, col, &b); eat(b, d, Hl::String);
                continue;
            }
            int b, d = disp_adv(s, n, i, col, &b);
            eat(b, d, Hl::String);
            if (s[i - b] == q) { state = 0; break; }
        }
        if (state) return state;
    }

    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r' || c == '\n') break;

        // preprocessor
        if (c == '#' && col == 0 && (lang == Lang::C || lang == Lang::Cpp)) {
            int start = i, scol = col;
            while (i < n && s[i] != '\n') {
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            }
            push(out, scol, col - scol, Hl::Preproc);
            continue;
        }

        // comments
        if (c_like(lang) || lang == Lang::JSON) {
            if (lang != Lang::JSON && i + 1 < n && s[i] == '/' && s[i + 1] == '/') {
                int scol = col;
                while (i < n && s[i] != '\n') {
                    int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
                }
                push(out, scol, col - scol, Hl::Comment);
                continue;
            }
            if (lang != Lang::JSON && i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
                int scol = col;
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
                d = disp_adv(s, n, i, col, &b); col += d; i += b;
                bool closed = false;
                while (i < n) {
                    if (i + 1 < n && s[i] == '*' && s[i + 1] == '/') {
                        d = disp_adv(s, n, i, col, &b); col += d; i += b;
                        d = disp_adv(s, n, i, col, &b); col += d; i += b;
                        closed = true; break;
                    }
                    d = disp_adv(s, n, i, col, &b); col += d; i += b;
                }
                push(out, scol, col - scol, Hl::Comment);
                if (!closed) return 1;
                continue;
            }
        }
        if ((lang == Lang::Python || lang == Lang::Shell || lang == Lang::Ini || lang == Lang::Make || lang == Lang::Ruby) && c == '#') {
            int scol = col;
            while (i < n && s[i] != '\n') {
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            }
            push(out, scol, col - scol, Hl::Comment);
            continue;
        }
        if (lang == Lang::Lua && i + 1 < n && s[i] == '-' && s[i + 1] == '-') {
            int scol = col;
            while (i < n && s[i] != '\n') {
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            }
            push(out, scol, col - scol, Hl::Comment);
            continue;
        }
        if (lang == Lang::SQL && i + 1 < n && s[i] == '-' && s[i + 1] == '-') {
            int scol = col;
            while (i < n && s[i] != '\n') {
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            }
            push(out, scol, col - scol, Hl::Comment);
            continue;
        }

        // markdown heading / code fence
        if (lang == Lang::MD) {
            if (col == 0 && c == '#') {
                int scol = col;
                while (i < n && s[i] != '\n') {
                    int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
                }
                push(out, scol, col - scol, Hl::Keyword);
                continue;
            }
        }

        // strings
        if (c == '"' || (c == '\'' && lang != Lang::MD) || (c == '`' && (lang == Lang::JS || lang == Lang::Shell || lang == Lang::MD))) {
            char q = (char)c;
            uint8_t ns = q == '"' ? 2 : q == '\'' ? 3 : 4;
            int scol = col;
            int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            bool closed = false;
            while (i < n) {
                if (s[i] == '\\' && i + 1 < n && lang != Lang::MD) {
                    d = disp_adv(s, n, i, col, &b); col += d; i += b;
                    d = disp_adv(s, n, i, col, &b); col += d; i += b;
                    continue;
                }
                d = disp_adv(s, n, i, col, &b); col += d; i += b;
                if (s[i - b] == q) { closed = true; break; }
            }
            push(out, scol, col - scol, Hl::String);
            if (!closed) return ns;
            continue;
        }

        // numbers
        if (c >= '0' && c <= '9') {
            int scol = col;
            while (i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'x' || s[i] == 'X' ||
                             s[i] == 'b' || s[i] == 'B' || (s[i] >= 'a' && s[i] <= 'f') ||
                             (s[i] >= 'A' && s[i] <= 'F') || s[i] == '_')) {
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            }
            push(out, scol, col - scol, Hl::Number);
            continue;
        }

        // identifiers
        if (is_ident0((char)c)) {
            int s0 = i, scol = col;
            while (i < n && is_ident(s[i])) {
                int b, d = disp_adv(s, n, i, col, &b); col += d; i += b;
            }
            int ln = i - s0;
            Hl h = Hl::Normal;
            if (match_kw(lang, s + s0, ln)) h = Hl::Keyword;
            else if (match_ty(lang, s + s0, ln)) h = Hl::Type;
            else if (lang == Lang::JSON && ((ln == 4 && memcmp(s + s0, "true", 4) == 0) ||
                                            (ln == 5 && memcmp(s + s0, "false", 5) == 0) ||
                                            (ln == 4 && memcmp(s + s0, "null", 4) == 0)))
                h = Hl::Keyword;
            push(out, scol, col - scol, h);
            continue;
        }

        // punctuation
        if (c < 128 && !is_ident((char)c) && c != ' ' && c != '\t') {
            int b, d = disp_adv(s, n, i, col, &b);
            eat(b, d, Hl::Punct);
            continue;
        }

        int b, d = disp_adv(s, n, i, col, &b);
        eat(b, d, Hl::Normal);
    }
    return 0;
}

uint32_t hl_color(Hl h) {
    switch (h) {
    case Hl::Keyword: return COL_KW;
    case Hl::Comment: return COL_CMT;
    case Hl::String:  return COL_STR;
    case Hl::Number:  return COL_NUM;
    case Hl::Type:    return COL_TYPE;
    case Hl::Preproc: return COL_PRE;
    case Hl::Punct:   return COL_PUN;
    default:          return COL_FG;
    }
}

