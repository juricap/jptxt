# jptxt

Super-fast native notepad. Speed first, then weight, then features.

Open this folder as an Obsidian vault (`File → Open vault → obsidian/`).

## Notes

- [[Choices]] — why C++17, why not SciTE/Notepad++/lite-xl, windowing, singleton, triple-Esc
- [[Design]] — mmap, piece table, paint, hex lister, IPC
- [[Status]] — what works, measured numbers, known gaps
- [[Shortcuts]] — keys and menus

## Run

```
build\jptxt.exe
build\jptxt.exe path\to\file.txt
```

Not on PATH. One process: a second launch hands files to the running window.

Repo: `git@github.com:juricap/jptxt.git`
