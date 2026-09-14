# Design

How the editor is put together. Choices and history: [[Choices]]. What’s done: [[Status]].

## Layout of the tree

```
src/jptxt.h      types, commands, App/Doc
src/doc.cpp      mmap, piece table, line index, encoding, save
src/syntax.cpp   visible-line keyword / string / comment highlighter
src/search.cpp   chunked regex + literal
src/app.cpp      tabs, caret, paint, commands, triple-Esc
src/win32.cpp    Win32 + GDI + singleton mutex
src/cocoa.mm     Cocoa
src/x11.cpp      Xlib
src/jptxt.rc     menu, accelerators, Go-to dialog
```

No third-party libraries. CMake picks the platform file.

## Document

**Original file** is mapped read-only (`CreateFileMapping` / `mmap`).

**Edits** are a piece table: `{src, off, len}` spans into either the map or an append-only add buffer. Insert splits a piece and appends; delete drops pieces. Undo is inverse `{pos, removed, inserted}`.

**Line index** is `vector<uint64_t>` of line-start offsets, built with `memchr('\n')`. Display strips a trailing `\r`. Old Mac CR-only files are a fallback pass if there was no LF.

**Save** writes pieces to `path.jptxt~` then replaces. No giant concatenated string.

UTF-16 / Latin-1 are converted into the add buffer on load (one allocation). UTF-8 stays on the map.

## View

Only visible rows are painted. Syntax runs per visible line. Tabs are width 4. No wrap (wrap is slow and not required).

Hex rows = `ceil(size / 16)`. Caret is a byte offset. Copy from hex produces a dump.

## UI chrome

- Native menu, no toolbar, no sidebar
- Owner-drawn tabs (dark), status bar
- Native scrollbars
- Double-buffer (memory DC / pixmap)

## IPC (singleton)

Second process does not create a window. It serializes paths as UTF-8 + `\n` and:

1. Windows: `SendMessage(WM_COPYDATA)`
2. Mac: distributed notification
3. X11: property on the existing window + ClientMessage

The running `App` calls `app_open_path` / `app_open_blob` and raises the window.

## Triple-Esc

`App.esc_n` / `App.esc_ms`. Gap > 500 ms resets the count. Count 3 → `CMD_EXIT`. Other keys reset the count.
