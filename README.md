# jptxt

A super-fast, super-light native notepad. Tabs, basic syntax highlighting, block indent, regex search, and a Total Commander Lister-style hex view for binaries. Opens huge text files without loading them into a giant string.

**Speed first, then weight.** Features come after.

## Why not a fork of SciTE / Notepad++ / lite-xl?

| Candidate | Why it was rejected |
|---|---|
| **SciTE + Scintilla** | Best existing lightweight editor engine, but it materializes the whole document. Notepad3’s own FAQ: there is no mmap mode; 100–500 MB files get heavy. SciTE also ships Lua. |
| **Notepad2 / Notepad3** | Instant startup, Win32-native — **Windows only**, same Scintilla large-file ceiling. |
| **lite / lite-xl** | Tiny and fast, but **Lua scripting** is the product. |
| **Qt / GTK / egui / winit+wgpu** | Startup and binary size go the wrong way. |
| **Neatpad (catch22)** | Right *ideas* (piece chain, custom TextView) but 2005, Windows-only, unfinished syntax. |
| **Qem (Rust mmap engine)** | Backend only; still need a GUI. Extra crate, not a notepad. |

So jptxt is **original C++17** that borrows the *algorithms*, not a 150 kLOC tree to strip:

- **mmap** the file (open is a mapping, not a copy)
- **piece table** for edits (original bytes stay read-only)
- **line index** via `memchr` (100 MB of newlines in tens of milliseconds)
- **viewport paint** only — syntax on visible lines
- **native windowing**: Win32, Cocoa, X11 — no toolkit, no GPU stack, no scripting

Design notes live in the Obsidian vault: [`obsidian/`](obsidian/Home.md).

## What it does (v0.1.1)

- **Single instance**: a second launch focuses the running window and opens the files there
- Instant window; drop files or `build\jptxt.exe file1 file2 …`
- Tabs, no sidebar, a short native menu
- Copy / cut / paste, undo / redo
- **Tab / Shift+Tab** block indent
- Auto-indent on Enter
- Regex find / replace (invalid patterns fall back to literal)
- Basic syntax for C/C++, C#, Java, JS/TS, Go, Rust, Python, Shell, JSON, XML/HTML, Markdown, Ini/TOML, CSS, Lua, SQL, Ruby, Kotlin, Swift, Make
- Binary detection (BOM, UTF-16 heuristic, then zlib-style control-byte block list)
- **Hex lister** (address · hex · ASCII), read-only — auto for binaries, `View → Hex` for anything
- UTF-8 / UTF-8 BOM / UTF-16 LE·BE / Latin-1
- Files well over 100 MB (mmap + index; edits stream on save)

## Keys

| | |
|---|---|
| Ctrl+N / O / S / W | New, Open, Save, Close tab |
| Ctrl+Z / Y | Undo / Redo |
| Ctrl+X / C / V / A | Cut / Copy / Paste / Select all |
| Tab / Shift+Tab | Indent / unindent (block if selected) |
| Ctrl+F / H / G | Find (regex) / Replace / Go to line |
| F3 / Shift+F3 | Find next / previous |
| Ctrl+Tab | Next tab |
| Ctrl+Shift+H | Hex mode |
| Ctrl++ / − / 0 | Font size |
| Esc Esc Esc (quick) | Close the window (save prompts) |

The binary is `build\jptxt.exe` — it is not on PATH unless you put it there.

## Build

No third-party libraries. System APIs only.

### Windows (MSVC)

```bat
build.bat
```

or:

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Produces `build\jptxt.exe` (static CRT, one file, ~380 KB).

Measured on this machine (mmap + newline index, `--bench`):

| File | Size | Open |
|---|---|---|
| sample.cpp | 254 B | 0.2 ms |
| log-like text | 10 MB / 182k lines | 6 ms |
| log-like text | 120 MB / 2.17M lines | 67 ms |
| `jptxt.exe` (binary → hex) | 384 KB | 0.1 ms |

Working set after opening the 120 MB / 2.17 M-line file: **~12 MB** (line index stays; file pages are trimmed and faulted back in for the viewport).

### macOS

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Needs Cocoa (Xcode CLT).

### Linux

```sh
sudo apt install g++ cmake libx11-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

File dialogs use `zenity` when present, otherwise a stdin prompt.

## Layout

```
src/jptxt.h      shared types
src/doc.cpp      mmap, piece table, line index, encoding
src/syntax.cpp   keyword / string / comment highlighters
src/search.cpp   chunked regex + literal search
src/app.cpp      tabs, caret, paint, commands
src/win32.cpp    Win32 + GDI
src/cocoa.mm     Cocoa
src/x11.cpp      Xlib
```

## License

MIT
