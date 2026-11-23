# difference

this is being edited as we speak lol

![difference catching this README being edited live](resources/img.png)

Realtime, size-agnostic terminal diff watcher for any directory, built natively in C23 on top of Vexgraph's Relational Engine (`re`) and `determinant`.

```sh
cd ~/path/to/project && difference
```

## Problem it is trying to solve (it is niche)

Standard diff workflows (`git diff`, `delta`, IDE diff panels) only show the cumulative change against `HEAD` inside a git repository. When you're iterating quickly in an editor, refactoring across multiple files, or working in a directory that isn't even tracked by git, you lose the chronological story of each individual save.

It also comes up constantly when working with AI agents, especially general-purpose or mobile/non-code agent apps, that edit files directly in your directory without showing you a live diff. Instead of running `git diff` manually after every turn to see what an agent or external tool actually touched, you can keep `difference` open beside your workspace and inspect every modification as it lands.

Piping a file watcher into a plain terminal diff printer also breaks the moment you resize your terminal window next to an IDE: fixed-width side-by-side columns wrap and garble scrollback, and switching between narrow and wide layouts requires restarting the command.

`difference` solves all three: it watches a directory scope directly, records each save as its own `#1`, `#2`, `#3` delta with line and inline word/letter highlighting, and renders inside a fullscreen TUI that reflows cleanly between side-by-side and unified layouts whenever the terminal resizes.

## Features
- **Save-by-save live edit timeline**: Seeds initial text snapshots for the watched directory and diffs every subsequent save against the previous version, regardless of whether the directory is a git repo.
- **Collapsed-by-default feed & sticky diff header**: Keeps edits compact by default so large changes don't flood the viewport; expand any edit by clicking its row or pressing `Enter`/`Space` (or toggle all with `Option + O`). While reading inside a long expanded diff, its header row pins to the top of the viewport so you always know which file you're reading.
- **Line + inline word/letter diffing**: Computes grouped hunks with configurable context lines and highlights the exact changed word or character spans on modified line pairs.
- **Per-edit metadata**: Displays edit sequence (`#1`, `#2`, ...), colored event pill (`NEW`, `EDIT`, `DELETED`, `MOVED`), `/relative/path`, human-readable file size, `+N -M` line counts, and local `HH:MM:SS` timestamp.
- **Size-agnostic responsive TUI**: Built on `determinant`'s `TerminalCanvas`, `TerminalSurface`, `TerminalCompositor`, `TerminalInput`, and `TerminalDiffPanel`. Automatically switches between **side-by-side** (`>= 100` cols) and **unified** (`< 100` cols) on Mode-2048 in-band resize notifications and `SIGWINCH`, presenting only changed cell runs atomically inside DEC 2026 synchronized frame brackets (`\x1b[?2026h` / `\x1b[?2026l`).
- **Interactive keyboard & mouse controls**: Navigate edits with `j`/`k`, `↑`/`↓`, mouse wheel, `PgUp`/`PgDn`, `d`/`u`, or `g`/`G`; scroll wide code horizontally with `h`/`l`, `←`/`→`, trackpad horizontal scroll, and `0` to reset; open clickable **Shortcuts** (`?`) and **Settings** (`Ctrl + O`, including ` - ` / ` + ` context-line steppers) dialogs; cycle `AUTO` / `SPLIT` / `UNIFIED` with `s`; toggle live tail with `f`; clear history with `c`; quit with `q` or `Ctrl-C`.
- **Stream fallback**: Pass `--stream` (or pipe stdout) to print diffs as a plain scrollable log instead of entering the alternate-screen TUI.
- **Noise filtering**: Skips binary files, dot-directories, build outputs (`cmake-build-*`, `build/`, `node_modules/`, `target/`, `dist/`, `__pycache__`), and editor safe-write temp files (`*.swp`, `*~`, `*___jb_tmp___`, `*___jb_old___`) by default.

## Dogfooding Across Vexgraph

`difference` is intentionally built directly on Vexgraph's own lower layers to stress-test them under real application workloads instead of relying only on isolated unit tests:

- **Relational Engine (`re`: `io/file`, `nio/arena`)**:
  - Exposed and fixed a pre-existing `CFStringRef` bug in `FileEventWatcher_start`.
  - Drove the addition of `FileWatchProps` (latency, debounce, recursion, directory/hidden/pattern/extension filters) and rich `FileWatchEvent` metadata (`relativePath`, `timestampNs`, `epochSeconds`, `fileSize`), with `Memory_alloc` confined strictly to `FileEventWatcher_init`.
  - Stress-tested `MemoryArena` against a ~477 MiB workspace, highlighting how non-reclaiming bump blocks (`> 4 KiB`) require separating resettable scratch arenas from auto-compacting persistent arenas.
- **`determinant` (`ui/panel`, `ui/surface`, `ui/compositor`, `events`)**:
  - Added a reusable, zero-alloc `TerminalDiffPanel` widget supporting both `FILE *` streaming and horizontally scrollable `TerminalCanvas` TUI painting so other Vexgraph TUIs (such as `semicolon`) can drop in responsive diff views directly.
  - Surfaced and fixed pre-existing bugs across `determinant`'s TUI stack (`terminal_surface.c`, `terminal_compositor.c`, `terminal_canvas.c`, `terminal_input.c`): static-inline getter collisions with public declarations, a latent `u32 **` pointer-indirection bug in `TerminalSurface`'s `emitRun`/`adoptFront`, a stray `oop/oop.h` include in `terminal_compositor.c`, and const-qualifier/unused-helper warnings, allowing `difference` to present natively through `TerminalSurface` + `TerminalCompositor` under `-Wall -Wextra -Werror`.
- **Workspace laws**: Built and verified at zero regressions against `./tools/lint` (`Relational Memory Law`, `String Law`, `Semantic Consistency Law`, `Living Documentation Law`, and `alloc_gate`).

## Future
- **Reusable compacting/growable arena helper in Relational Engine (`re`)**: Lift the scratch-arena + persistent-compaction pattern developed in `SnapshotStore` into `re` so other apps handling variable-sized buffers don't have to hand-roll it.
- **Cross-platform file watching**: Add Linux (`inotify`) and Windows (`ReadDirectoryChangesW`) backends to Relational Engine (`re`)'s `FileEventWatcher`.
- **Richer TUI navigation**: Jump between edits (`n`/`p`), filter by path or extension inside the TUI, and inspect an edit's full file context on demand.
- **Native `b` build target**: Link macOS framework flags cleanly through `tools/b` so `b build` / `b run` replaces the `tools/difference` wrapper script.

## Scope and Limitations

- **macOS only (for now)**: Live watching depends on Relational Engine (`re`)'s `FileEventWatcher`, which currently implements macOS FSEvents only.
- **Disk-save driven**: Only changes flushed to disk (e.g., `Cmd+S` or IDE auto-save) trigger diffs; unsaved in-memory editor buffers are not visible.
- **Text files up to 256 KiB**: Files larger than 256 KiB or containing NUL bytes in the first 4 KiB are skipped to keep snapshotting and LCS diffing fast and bounded.
- **In-memory history cap**: The TUI retains the most recent 64 edits in memory and does not persist history to disk after exit.
- **Not a VCS or merge tool**: `difference` does not read git commits, stage hunks, or apply patches, it is strictly a live observational diff viewer for a watched directory scope.
