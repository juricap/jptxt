# Choices

Decisions that should not get silently reversed. Related: [[Design]], [[Status]].

## Language: C++17

Wanted: instant startup, tiny binary, native windowing on Win/Mac/Linux, no scripting, regex search.

| Option | Verdict |
|---|---|
| **C++17** | Chosen. Win32 / Cocoa `.mm` / X11 without extra runtimes. `std::vector` / `std::string` / `std::regex` without a crate ecosystem. |
| Rust | Fast enough, but Cocoa and Win32 FFI add weight and pain. `winit`/`wgpu` are the opposite of “light”. |
| Go | GC + cgo for native UI. Startup not as sharp as a static C++ exe. |
| C | Fine for the buffer; C++17 is the same ABI with less manual container code. |

## Do not fork SciTE / Notepad++ / lite-xl

| Candidate | Why not |
|---|---|
| SciTE + Scintilla | Proven editor, but the document is fully materialized. Notepad3 FAQ: no mmap mode; 100–500 MB gets heavy. SciTE ships Lua. |
| Notepad2 / Notepad3 | Instant on Windows, same Scintilla ceiling, **Windows only**. |
| lite / lite-xl | Tiny, but Lua is the product. User: no scripting. |
| Qt / GTK / egui / winit+wgpu | Startup and binary size go the wrong way. |
| Neatpad (catch22, 2005) | Right *ideas* (piece chain, custom view). Windows-only, unfinished syntax. Borrow algorithms, not the tree. |
| Qem | mmap engine only; extra crate; still need a GUI. |

**jptxt is original C++17** that copies the *algorithms* (mmap + piece table + viewport paint + TC-style hex), not a 150 kLOC strip job.

## Windowing: platform APIs only

No Qt, GTK, SDL, GLFW, winit, or GPU stack.

- Windows: Win32 + GDI (`ExtTextOutW`), native menu, `FindTextW`
- macOS: Cocoa (`NSWindow` / `NSView` / `NSFont`)
- Linux: Xlib + `XFontSet` (no Xft, no GTK)

Regex: `std::regex` (ECMAScript) over 1 MB windows. Invalid pattern → literal search.

## Singleton

One process per user session.

- **Why:** a notepad that opens from Explorer / `jptxt file` should add a tab, not spawn another window. Matches “no bloat”.
- **Windows:** named mutex `Local\jptxt-singleton` + `WM_COPYDATA` (`dwData = 'JPTX'`) with newline-separated UTF-8 paths. `--bench` skips the mutex.
- **macOS:** `flock` on `/tmp/jptxt-$UID.lock` + `NSDistributedNotification` `net.juricap.jptxt.open`
- **Linux:** same flock + root-window `JPTXT_WINDOW` property and `JPTXT_OPEN` ClientMessage

Second instance focuses / restores the first, then exits.

## Triple-Esc closes

Three Escape presses with ≤ 500 ms between consecutive presses runs **File → Exit** (save prompts). Status bar shows `Esc 1/3` then `Esc 2/3`.

- One Esc is a no-op (does not clear selection) so it is hard to quit by accident.
- Ctrl+W still closes a tab.
- Find dialog Esc still belongs to the common dialog.

## Encoding / binary

Detect order: BOM → UTF-16 NUL heuristic → zlib-style control-byte block list (0–6, 14–31) → UTF-8 vs Latin-1.

Binaries open **read-only hex** (address · hex · ASCII), like TC Lister mode 3. `View → Text` can force text.

## Files > 100 MB

Index newlines with `memchr` only (an early CR+LF scan was O(n²) and took 54 s for 10 MB). After index, Windows trims the working set so RSS is index + viewport, not the whole map.
