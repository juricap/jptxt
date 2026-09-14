#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <memory>
#include "resource.h"

#define JPTXT_VERSION "0.1.1"
#define JPTXT_ESC_CLOSE_N     3
#define JPTXT_ESC_GAP_MS      500
#define JPTXT_COPYDATA_MAGIC  0x4A505458u /* 'JPTX' */

// 0xRRGGBB
enum : uint32_t {
    COL_BG      = 0x1E1E1E,
    COL_FG      = 0xD4D4D4,
    COL_SEL     = 0x264F78,
    COL_SEL_FG  = 0xFFFFFF,
    COL_TAB     = 0x252526,
    COL_TAB_A   = 0x1E1E1E,
    COL_TAB_B   = 0x007ACC,
    COL_GUT     = 0x2D2D2D,
    COL_STATUS  = 0x007ACC,
    COL_ST_FG   = 0xFFFFFF,
    COL_KW      = 0x569CD6,
    COL_CMT     = 0x6A9955,
    COL_STR     = 0xCE9178,
    COL_NUM     = 0xB5CEA8,
    COL_TYPE    = 0x4EC9B0,
    COL_PRE     = 0xC586C0,
    COL_PUN     = 0x808080,
    COL_ADDR    = 0x608B4E,
    COL_ASC     = 0xDCDCAA,
    COL_FIND    = 0x623315,
    COL_DIM     = 0x858585,
    COL_CLOSE   = 0xC74646,
    COL_BAR     = 0x3E3E42
};

enum class Key : int {
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    Backspace, Del, Tab, Enter, Escape, Insert, F3
};

enum Mods : uint32_t { JP_SHIFT = 1, JP_CTRL = 2, JP_ALT = 4 };

enum class Enc : uint8_t { UTF8, UTF8BOM, UTF16LE, UTF16BE, Latin1 };
enum class Eol : uint8_t { LF, CRLF, CR, Mixed };
enum class Lang : uint8_t {
    Plain, C, Cpp, CSharp, Java, JS, Go, Rust, Python, Shell,
    JSON, XML, MD, Ini, CSS, Make, Lua, SQL, Ruby, Kotlin, Swift
};

enum class Hl : uint8_t { Normal, Keyword, Comment, String, Number, Type, Preproc, Punct };

struct Span {
    int col;   // display column
    int n;     // display columns
    Hl  hl;
};

struct Piece {
    uint8_t  src; // 0 original mmap, 1 add-buffer
    uint64_t off;
    uint64_t len;
};

struct Edit {
    uint64_t    pos;
    std::string removed;
    std::string inserted;
};

struct FileMap {
    const uint8_t* data = nullptr;
    uint64_t       size = 0;
#ifdef _WIN32
    void* file = nullptr;
    void* map  = nullptr;
#else
    int   fd   = -1;
#endif
};

struct Doc {
    std::string path;     // utf-8
    std::string name;
    FileMap     map;
    std::string add;
    std::vector<Piece>    pcs;
    std::vector<uint64_t> line_off; // start offset of each line in logical bytes
    std::vector<uint8_t>  hl_at;    // highlighter state at start of line
    uint64_t len = 0;

    bool binary   = false;
    bool hex      = false;
    bool readonly = false;
    bool dirty    = false;
    Enc  enc      = Enc::UTF8;
    Eol  eol      = Eol::LF;
    Lang lang     = Lang::Plain;

    uint64_t caret      = 0;
    uint64_t sel_anchor = 0;
    int      pref_col   = -1;
    int64_t  top_line   = 0;
    int      hscroll    = 0;
    bool     insert     = true;

    std::vector<Edit> undo;
    std::vector<Edit> redo;
    int save_undo_size = 0;

    ~Doc();
    Doc() = default;
    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;
    Doc(Doc&&) noexcept;
    Doc& operator=(Doc&&) noexcept;
};

struct Draw {
    void* ctx = nullptr;
    int cell_w = 8, cell_h = 16, ascent = 12;
    void (*fill)(void* ctx, int x, int y, int w, int h, uint32_t rgb) = nullptr;
    void (*text)(void* ctx, int x, int y, const char* s, int n, uint32_t rgb) = nullptr;
};

struct App {
    std::vector<std::unique_ptr<Doc>> tabs;
    int cur = 0;
    int w = 1000, h = 700;
    int cell_w = 8, cell_h = 16, ascent = 12;
    int tab_h = 28, status_h = 22;
    int font_px = 14;
    int want_font = 14;
    bool overwrite = false;
    std::string status;
    std::string find_pat;
    std::string repl_pat;
    int  find_flags = 0; // 1=case, 2=regex
    bool find_down  = true;
    uint64_t last_match_a = 0, last_match_b = 0;
    bool has_match = false;
    bool quit = false;
    int      esc_n  = 0;
    uint64_t esc_ms = 0;
};

// --- doc ---
void     doc_init_empty(Doc* d, int untitled_n);
bool     doc_load(Doc* d, const char* path_utf8);
void     doc_set_trace(FILE* f);
int      doc_bench(const char* path, FILE* out);   // 0 ok, 2 fail
int      doc_selftest(FILE* out);                  // 0 ok
bool     doc_save(Doc* d, const char* path_utf8);
void     doc_unmap(Doc* d);
uint64_t doc_line_count(const Doc* d);
void     doc_line_span(const Doc* d, uint64_t line, uint64_t* a, uint64_t* b); // content, no EOL
uint64_t doc_line_of(const Doc* d, uint64_t pos);
uint64_t doc_col_of(const Doc* d, uint64_t pos); // display col
uint64_t doc_pos_at(const Doc* d, uint64_t line, uint64_t col);
void     doc_read(const Doc* d, uint64_t pos, uint64_t n, std::string* out);
uint8_t  doc_at(const Doc* d, uint64_t pos);
void     doc_insert(Doc* d, uint64_t pos, const char* s, uint64_t n);
void     doc_erase(Doc* d, uint64_t pos, uint64_t n);
void     doc_apply(Doc* d, const Edit& e, bool reverse);
void     doc_reindex(Doc* d);
void     doc_index_edit(Doc* d, uint64_t pos, int64_t delta, const char* inserted, uint64_t nins);
Lang     lang_from_name(const char* name);
const char* enc_name(Enc e);
const char* eol_name(Eol e);

// --- syntax ---
uint8_t highlight_line(Lang lang, const char* s, int n, uint8_t state, std::vector<Span>* out);
uint32_t hl_color(Hl h);

// --- search ---
bool find_in_doc(const Doc* d, const std::string& pat, bool regex, bool icase, bool down,
                 uint64_t from, uint64_t* a, uint64_t* b);

// --- utf ---
int      utf8_cp_len(const uint8_t* p, const uint8_t* end);
uint32_t utf8_next(const uint8_t* p, const uint8_t* end, int* adv);
int      utf8_put(uint32_t cp, char out[4]);
uint64_t utf8_prev(const Doc* d, uint64_t pos);
uint64_t utf8_next_pos(const Doc* d, uint64_t pos);

// --- detect ---
void detect_file(const uint8_t* p, uint64_t n, bool* binary, Enc* enc, Eol* eol);

// --- app ---
void app_init(App* a);
void app_set_metrics(App* a, int cell_w, int cell_h, int ascent);
void app_resize(App* a, int w, int h);
Doc* app_doc(App* a);
void app_new_tab(App* a);
bool app_open_path(App* a, const char* path);
void app_open_blob(App* a, const char* paths, size_t n); // newline-separated utf-8 paths
bool app_save(App* a, bool save_as);
void app_close_tab(App* a);
void app_cmd(App* a, int cmd);
void app_key(App* a, Key k, uint32_t mods);
void app_char(App* a, uint32_t cp);
void app_mouse_down(App* a, int x, int y, int btn, uint32_t mods); // btn 1 left 2 right 3 mid
void app_mouse_up(App* a, int x, int y, int btn);
void app_mouse_move(App* a, int x, int y, uint32_t mods);
void app_mouse_dbl(App* a, int x, int y);
void app_wheel(App* a, int lines, uint32_t mods);
void app_paint(App* a, Draw* d, int w, int h);
void app_layout_scroll(App* a);
bool app_caret_px(App* a, int* x, int* y, int* h);
int  app_cursor_at(App* a, int x, int y); // 0 arrow 1 ibeam
bool app_find(App* a, const char* pat, const char* repl, int flags, int action);
// action: 0 find, 1 find prev, 2 replace, 3 replace all
const char* app_title(App* a);
void app_status_text(App* a, char* buf, int cap);
int  app_ed_top(const App* a);
int  app_ed_h(const App* a);
int  app_vis_lines(const App* a);
int  app_hex_rows(const Doc* d);
void app_ensure_caret_visible(App* a);

// --- platform (each backend) ---
void plat_invalidate();
void plat_set_title(const char* utf8);
void plat_clipboard_set(const std::string& s);
bool plat_clipboard_get(std::string& s);
void plat_scroll_set(int vmax, int vpage, int vpos, int hmax, int hpage, int hpos);
void plat_caret(int x, int y, int h, bool show);
void plat_cursor(int ibeam);
int  plat_ask_save(const char* name); // 0 cancel, 1 save, 2 discard
bool plat_dlg_open(std::vector<std::string>& out);
bool plat_dlg_save(std::string& path);
bool plat_dlg_goto(int maxline, int cur, int* out);
void plat_beep();
void plat_error(const char* utf8);
void plat_find_dialog(bool replace);
void plat_about();
void plat_set_font(int px);

