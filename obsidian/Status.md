# Status

v**0.1.1** — Windows, Linux (`ssh ai`), and Mac (`ssh mac`, arm64) verified.

Vault home: [[Home]]. Design: [[Design]]. Decisions: [[Choices]].

## Linux (`jurzai1`, g++ 11.4, X11)

| | |
|---|---|
| Binary | 291 KB ELF, `libX11` + libstdc++ only |
| `--selftest` | ok |
| sample.cpp | 0.07 ms |
| `/bin/ls` | binary → hex, 0.10 ms |
| 20 MB / 350k lines | 5.2 ms |
| 120 MB / 2.1 M lines | **30 ms** |
| xvfb GUI | runs (timeout smoke, no X errors) |

`--bench` / `--selftest` work without a display.

## Mac (`Devs-MacBook-Pro`, Darwin 25.2 / macOS 26.2, AppleClang, arm64)

| | |
|---|---|
| Binary | 229 KB Mach-O arm64 |
| `--selftest` | ok |
| sample.cpp | 0.11 ms |
| `/bin/ls` as a file | binary → hex, 0.09 ms |
| 20 MB / 350k lines | 4.2 ms |
| 120 MB / 2.1 M lines | **25 ms** |

## Works (Windows)

- Window, tabs, native menu, dark paint
- New / open / save / close, dirty prompts
- Caret, selection, copy/cut/paste, undo/redo
- Tab / Shift+Tab block indent, Enter auto-indent
- Regex find / replace / go to line
- Syntax on visible lines for the bundled languages
- Binary → hex lister; UTF-8 / BOM / UTF-16 / Latin-1
- mmap + piece table; 100 MB+ text
- **Singleton** (mutex + `WM_COPYDATA`)
- **Triple-Esc** to exit
- `--bench path` (skips singleton)

## Measured (this machine)

| | |
|---|---|
| `jptxt.exe` | ~375 KB, static CRT, no DLLs |
| `sample.cpp` open | 0.2 ms |
| 10 MB / 182k lines | 6 ms |
| **120 MB / 2.17 M lines** | **67 ms open, ~12 MB working set** after trim |
| Binary (`jptxt.exe` itself) | hex, 0.1 ms |

Idle-ish GUI with a small file: tens of MB of OS overhead at most; huge files do not keep the whole map in the working set after index.

## Known gaps

- Not on PATH (`jptxt` in a random prompt will fail — use `build\jptxt.exe`)
- No word wrap
- Block-comment state is not indexed (highlight is line-local on huge files)
- Hex is read-only (lister, not a hex editor)
- Undo coalescing is coarse (indent is many undo steps)
- Find dialog has no explicit “regex” checkbox (regex chars → regex, else literal; compile fail → literal)
- Cocoa/X11 singleton and triple-Esc are implemented but untested here
- No file-type association, no installer
- IME / complex scripts: BMP via `WM_CHAR` only

## Next (when needed)

- Installer / PATH / “Open with”
- Sparse line index for multi-GB
- Wrap, line numbers gutter (not a sidebar)
- Hex copy as raw bytes
- Idle working-set trim on Mac/Linux
