#ifndef DIFFERENCE_TUI_H
#define DIFFERENCE_TUI_H

#include "vex.h"
#include "io/file/file.h"
#include "nio/mem.h"
#include "ui/compositor/terminal_compositor.h"
#include "ui/field/terminal_text_area.h"
#include "ui/panel/terminal_diff_panel.h"
#include "ui/surface/terminal_canvas.h"
#include "ui/surface/terminal_surface.h"
#include "events/terminal_input.h"
#include "diff_engine.h"
#include "difference_settings.h"

/** difference_tui.h, size-agnostic TUI viewport for live file-watch diffs. */

enum {
    DIFFERENCE_TUI_MAX_EDITS = 1024u,
    DIFFERENCE_TUI_DEFAULT_MAX_EDITS = 1024u,
    DIFFERENCE_TUI_MIN_EDITS = 100u,
    DIFFERENCE_TUI_EDITS_STEP = 100u,
    DIFFERENCE_TUI_DEFAULT_WORKSPACE_MB = 1024u,
    DIFFERENCE_TUI_MIN_WORKSPACE_MB = 1u,
    DIFFERENCE_TUI_MAX_WORKSPACE_MB = 65536u
};

typedef enum DifferenceOverlayMode {
    DIFFERENCE_OVERLAY_NONE = 0,
    DIFFERENCE_OVERLAY_HELP,
    DIFFERENCE_OVERLAY_SETTINGS
} DifferenceOverlayMode;

typedef struct DifferenceEditRecord {
    u64 sequence;
    utf8 *path;
    utf8 *relativePath;
    utf8 eventLabel[16];
    utf8 timestamp[32];
    FileEventType eventType;
    i64 fileSize;
    utf8 *oldText;
    bytes oldLen;
    utf8 *newText;
    bytes newLen;
    DiffResult diff;
    bool expanded;
} DifferenceEditRecord;

typedef bool (*DifferenceTuiIdleFn)(any userdata);
typedef fn (*DifferenceTuiWorkspaceLimitFn)(u32 limitMb, any userdata);
typedef fn (*DifferenceTuiSettingsChangedFn)(const DifferenceSettings *settings, any userdata);

typedef struct DifferenceTui {
    MemoryArena *frameArena;
    MemoryArena *recordArena;
    MemoryArena *diffScratch;
    TerminalSurface surface;
    TerminalCompositor *compositor;
    TerminalInput input;
    TerminalDiffPanel panel;
    TerminalTextArea *editField;
    utf8 editBuf[16];
    i32 editingSetting;
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

    string watchRoot;
    bytes trackedFiles;
    u64 editSequence;
    u64 totalAdded;
    u64 totalRemoved;
    DifferenceEditRecord records[DIFFERENCE_TUI_MAX_EDITS];
    bytes recordCount;
    u32 maxEdits;
    u32 workspaceLimitMb;

    bytes selectedIndex;
    bytes scrollOffset;
    bytes totalRows;
    bool collapseMode;
    u32 contextLines;
    DifferenceOverlayMode overlay;
    bytes settingsCursor;
    bool utf8C3Pending;

    bytes helpBtnX0;
    bytes helpBtnX1;
    bytes settingsBtnX0;
    bytes settingsBtnX1;
    bytes modalX;
    bytes modalY;
    bytes modalW;
    bytes modalH;
    bytes modalCloseX0;
    bytes modalCloseX1;
    bytes ctxMinusX0;
    bytes ctxMinusX1;
    bytes ctxPlusX0;
    bytes ctxPlusX1;
    bytes ctxRowY;
    bytes wsEditX0;
    bytes wsEditX1;
    bytes wsRowY;
    bytes wsFieldX0;
    bytes wsFieldX1;
    bytes editsEditX0;
    bytes editsEditX1;
    bytes editsRowY;
    bytes editsFieldX0;
    bytes editsFieldX1;

    bool followTail;
    bool dirty;
    bool running;
    DifferenceTuiIdleFn idleHook;
    any idleUserdata;
    DifferenceTuiWorkspaceLimitFn workspaceLimitHook;
    any workspaceLimitUserdata;
    DifferenceTuiSettingsChangedFn settingsChangedHook;
    any settingsChangedUserdata;
} DifferenceTui;

bool DifferenceTui_init(DifferenceTui *tui, string watchRoot,
                        TerminalDiffLayoutMode initialLayout, bool useColor);
fn DifferenceTui_free(DifferenceTui *tui);
fn DifferenceTui_setOutputFd(DifferenceTui *tui, i32 fd);
fn DifferenceTui_setTrackedFiles(DifferenceTui *tui, bytes count);
fn DifferenceTui_setIdleHook(DifferenceTui *tui, DifferenceTuiIdleFn hook, any userdata);
fn DifferenceTui_setWorkspaceLimitHook(DifferenceTui *tui, DifferenceTuiWorkspaceLimitFn hook, any userdata);
fn DifferenceTui_setSettingsChangedHook(DifferenceTui *tui, DifferenceTuiSettingsChangedFn hook, any userdata);
fn DifferenceTui_applySettings(DifferenceTui *tui, const DifferenceSettings *settings);
fn DifferenceTui_exportSettings(const DifferenceTui *tui, DifferenceSettings *out);
fn DifferenceTui_setWorkspaceLimitMb(DifferenceTui *tui, u32 limitMb);
fn DifferenceTui_setMaxEdits(DifferenceTui *tui, u32 maxEdits);
bool DifferenceTui_pushEdit(DifferenceTui *tui,
                            const FileWatchEvent *event,
                            string oldText, bytes oldLen,
                            string newText, bytes newLen);
fn DifferenceTui_clearEdits(DifferenceTui *tui);
bool DifferenceTui_resize(DifferenceTui *tui, bytes width, bytes height);
fn DifferenceTui_renderFrame(DifferenceTui *tui);
fn DifferenceTui_handleInputByte(DifferenceTui *tui, i32 byte);
fn DifferenceTui_handleKeySeq(DifferenceTui *tui, string seq, bytes len);
i32 DifferenceTui_run(DifferenceTui *tui, FileEventWatcher *watcher);

#endif
