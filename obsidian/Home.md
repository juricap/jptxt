# jptxt

Super-fast native notepad. Speed first, then weight, then features.

Open this folder as an Obsidian vault (`File → Open vault → obsidian/`).

## Notes

- [[Choices]] — why C++17, why not SciTE/Notepad++/lite-xl, windowing, singleton, triple-Esc
- [[Design]] — mmap, piece table, paint, hex lister, IPC
- [[Status]] — what works, measured numbers, known gaps
- [[Shortcuts]] — keys and menus

## Install

```powershell
irm https://raw.githubusercontent.com/juricap/jptxt/main/install.ps1 | iex
```

```sh
curl -fsSL https://raw.githubusercontent.com/juricap/jptxt/main/install.sh | bash
```

Artifacts: [GitHub Releases](https://github.com/juricap/jptxt/releases).

## Run

```
jptxt
jptxt path/to/file.txt
build\jptxt.exe
```

One process: a second launch hands files to the running window.

Repo: `git@github.com:juricap/jptxt.git`
