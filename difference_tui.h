#ifndef DIFFERENCE_TUI_H
#define DIFFERENCE_TUI_H

#include "vex.h"
#include "io/file/file.h"
#include "nio/mem.h"
#include "ui/panel/terminal_diff_panel.h"
#include "ui/surface/terminal_canvas.h"
#include "events/terminal_input.h"
#include "diff_engine.h"

// difference_tui.h — size-agnostic TUI viewport for live file-watch diffs.

enum {
    DIFFERENCE_TUI_MAX_EDITS = 64u
};

typedef struct DifferenceEditRecord {
    u64 sequence;
    char *path;
    char *relativePath;
    char eventLabel[16];
    char timestamp[32];
    FileEventType eventType;
    i64 fileSize;
    char *oldText;
    bytes oldLen;
    char *newText;
    bytes newLen;
    DiffResult diff;
} DifferenceEditRecord;

typedef struct DifferenceTui {
    MemoryArena *frameArena;
    MemoryArena *recordArena;
    TerminalInput input;
    TerminalDiffPanel panel;
    i32 outFd;

    bytes width;
    bytes height;
    u32 *backCells;
    u16 *backStyles;
    u32 *backFg;
    u32 *backBg;
    u32 *frontCells;
    u16 *frontStyles;
    u32 *frontFg;
    u32 *frontBg;

    const char *watchRoot;
    bytes trackedFiles;
    u64 editSequence;
    DifferenceEditRecord records[DIFFERENCE_TUI_MAX_EDITS];
    bytes recordCount;

    bytes scrollOffset;
    bytes totalRows;
    bool followTail;
    bool dirty;
    bool running;
} DifferenceTui;

bool DifferenceTui_init(DifferenceTui *tui, const char *watchRoot,
                        TerminalDiffLayoutMode initialLayout, bool useColor);
fn DifferenceTui_free(DifferenceTui *tui);
fn DifferenceTui_setTrackedFiles(DifferenceTui *tui, bytes count);
bool DifferenceTui_pushEdit(DifferenceTui *tui,
                            const FileWatchEvent *event,
                            const char *oldText, bytes oldLen,
                            const char *newText, bytes newLen);
fn DifferenceTui_clearEdits(DifferenceTui *tui);
bool DifferenceTui_resize(DifferenceTui *tui, bytes width, bytes height);
fn DifferenceTui_renderFrame(DifferenceTui *tui);
fn DifferenceTui_handleInputByte(DifferenceTui *tui, i32 byte);
fn DifferenceTui_handleKeySeq(DifferenceTui *tui, const char *seq, bytes len);
i32 DifferenceTui_run(DifferenceTui *tui, FileEventWatcher *watcher);

#endif
