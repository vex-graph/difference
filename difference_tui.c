#include "vex.h"
#include "difference_tui.h"

#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "type/re_type.h"

#define TYPE_DIFFERENCE_TUI Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 42u)

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: DifferenceTui
 * ============================================================================
 * Size-agnostic fullscreen TUI for difference built on determinant's
 * TerminalCanvas, TerminalSurface, TerminalCompositor, TerminalInput,
 * TerminalTextArea, and TerminalDiffPanel. Supports collapsed-by-default edit
 * cards, aligned numeric columns, clickable rows and modals (Help / Settings
 * with inline custom-caret text fields), and atomic DEC 2026 + Mode 2048 live
 * resize rendering.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DifferenceTui (difference/difference_tui.c)
 * ============================================================================
 * Retains up to 1024 live edits in an isolated MemoryArena, aligns sequence
 * numbers and +/- diff stats across all rows, paints the full TerminalCanvas
 * viewport on every edit, click, key, or terminal resize, and presents cell
 * deltas atomically via TerminalSurface into TerminalCompositor.
 * ============================================================================
 */

enum {
    SETTINGS_ITEM_COUNT = 8u,
    SETTINGS_ITEM_CONTEXT = 5u,
    SETTINGS_ITEM_WORKSPACE_MB = 6u,
    SETTINGS_ITEM_MAX_EDITS = 7u,
    CONTEXT_LINES_MIN = 0u,
    CONTEXT_LINES_MAX = 12u,
    DIFFERENCE_MIN_COLS = 50u,
    DIFFERENCE_MIN_ROWS = 20u,
    EDIT_FIELD_COLS = 7u,
    EDIT_FIELD_MAX_DIGITS = 6u,
    SELECTION_CIRCLE_GLYPH = 0x25CFu,
    EDIT_ICON_GLYPH = 0x270Eu
};

static atomic_bool g_tuiStop = false;
static atomic_bool g_tuiResized = false;

;;GETTER
static inline MemoryArena *Tui_frameArena(const DifferenceTui *t)
{
    return (*t).frameArena;
}
;;SETTER
static inline fn Tui_setFrameArena(DifferenceTui *t, MemoryArena *a)
{
    (*t).frameArena = a;
}
;;GETTER
static inline MemoryArena *Tui_recordArena(const DifferenceTui *t)
{
    return (*t).recordArena;
}
;;SETTER
static inline fn Tui_setRecordArena(DifferenceTui *t, MemoryArena *a)
{
    (*t).recordArena = a;
}
;;GETTER
static inline MemoryArena *Tui_diffScratch(const DifferenceTui *t)
{
    return (*t).diffScratch;
}
;;SETTER
static inline fn Tui_setDiffScratch(DifferenceTui *t, MemoryArena *a)
{
    (*t).diffScratch = a;
}
;;GETTER
static inline TerminalSurface *Tui_surfaceMut(DifferenceTui *t)
{
    return &(*t).surface;
}
;;GETTER
static inline TerminalCompositor *Tui_compositor(const DifferenceTui *t)
{
    return (*t).compositor;
}
;;SETTER
static inline fn Tui_setCompositor(DifferenceTui *t, TerminalCompositor *c)
{
    (*t).compositor = c;
}
;;GETTER
static inline TerminalInput *Tui_inputMut(DifferenceTui *t)
{
    return &(*t).input;
}
;;GETTER
static inline TerminalDiffPanel *Tui_panelMut(DifferenceTui *t)
{
    return &(*t).panel;
}
;;GETTER
static inline const TerminalDiffPanel *Tui_panel(const DifferenceTui *t)
{
    return &(*t).panel;
}
;;GETTER
static inline TerminalTextArea *Tui_editField(const DifferenceTui *t)
{
    return (*t).editField;
}
;;SETTER
static inline fn Tui_setEditField(DifferenceTui *t, TerminalTextArea *f)
{
    (*t).editField = f;
}
;;GETTER
static inline utf8 *Tui_editBufMut(DifferenceTui *t)
{
    return (*t).editBuf;
}
;;GETTER
static inline bytes Tui_editBufCap(const DifferenceTui *t)
{
    return sizeof((*t).editBuf);
}
;;SETTER
static inline fn TextArea_setCursorPen(TerminalTextArea *f, TerminalPen pen)
{
    (*f).cursorPen = pen;
}
;;GETTER
static inline i32 Tui_editingSetting(const DifferenceTui *t)
{
    return (*t).editingSetting;
}
;;SETTER
static inline fn Tui_setEditingSetting(DifferenceTui *t, i32 s)
{
    (*t).editingSetting = s;
}
;;GETTER
static inline i32 Tui_outFd(const DifferenceTui *t)
{
    return (*t).outFd;
}
;;SETTER
static inline fn Tui_setOutFd(DifferenceTui *t, i32 fd)
{
    (*t).outFd = fd;
}
;;GETTER
static inline bytes Tui_width(const DifferenceTui *t)
{
    return (*t).width;
}
;;SETTER
static inline fn Tui_setWidth(DifferenceTui *t, bytes w)
{
    (*t).width = w;
}
;;GETTER
static inline bytes Tui_height(const DifferenceTui *t)
{
    return (*t).height;
}
;;SETTER
static inline fn Tui_setHeight(DifferenceTui *t, bytes h)
{
    (*t).height = h;
}
;;SETTER
static inline fn Tui_setBackCells(DifferenceTui *t, u32 *p)
{
    (*t).backCells = p;
}
;;SETTER
static inline fn Tui_setBackStyles(DifferenceTui *t, u16 *p)
{
    (*t).backStyles = p;
}
;;SETTER
static inline fn Tui_setBackFg(DifferenceTui *t, u32 *p)
{
    (*t).backFg = p;
}
;;SETTER
static inline fn Tui_setBackBg(DifferenceTui *t, u32 *p)
{
    (*t).backBg = p;
}
;;SETTER
static inline fn Tui_setFrontCells(DifferenceTui *t, u32 *p)
{
    (*t).frontCells = p;
}
;;SETTER
static inline fn Tui_setFrontStyles(DifferenceTui *t, u16 *p)
{
    (*t).frontStyles = p;
}
;;SETTER
static inline fn Tui_setFrontFg(DifferenceTui *t, u32 *p)
{
    (*t).frontFg = p;
}
;;SETTER
static inline fn Tui_setFrontBg(DifferenceTui *t, u32 *p)
{
    (*t).frontBg = p;
}
;;GETTER
static inline string Tui_watchRoot(const DifferenceTui *t)
{
    return (*t).watchRoot;
}
;;SETTER
static inline fn Tui_setWatchRoot(DifferenceTui *t, string r)
{
    (*t).watchRoot = r;
}
;;GETTER
static inline bytes Tui_trackedFiles(const DifferenceTui *t)
{
    return (*t).trackedFiles;
}
;;SETTER
static inline fn Tui_setTrackedFiles(DifferenceTui *t, bytes n)
{
    (*t).trackedFiles = n;
}
;;GETTER
static inline u64 Tui_editSeq(const DifferenceTui *t)
{
    return (*t).editSequence;
}
;;SETTER
static inline fn Tui_setEditSeq(DifferenceTui *t, u64 s)
{
    (*t).editSequence = s;
}
;;GETTER
static inline u64 Tui_totalAdded(const DifferenceTui *t)
{
    return (*t).totalAdded;
}
;;SETTER
static inline fn Tui_setTotalAdded(DifferenceTui *t, u64 n)
{
    (*t).totalAdded = n;
}
;;GETTER
static inline u64 Tui_totalRemoved(const DifferenceTui *t)
{
    return (*t).totalRemoved;
}
;;SETTER
static inline fn Tui_setTotalRemoved(DifferenceTui *t, u64 n)
{
    (*t).totalRemoved = n;
}
;;GETTER
static inline DifferenceEditRecord *Tui_recordsMut(DifferenceTui *t)
{
    return (*t).records;
}
;;GETTER
static inline bytes Tui_recordCount(const DifferenceTui *t)
{
    return (*t).recordCount;
}
;;SETTER
static inline fn Tui_setRecordCount(DifferenceTui *t, bytes n)
{
    (*t).recordCount = n;
}
;;GETTER
static inline u32 Tui_maxEdits(const DifferenceTui *t)
{
    return (*t).maxEdits;
}
;;SETTER
static inline fn Tui_setMaxEditsField(DifferenceTui *t, u32 m)
{
    (*t).maxEdits = m;
}
;;GETTER
static inline u32 Tui_workspaceLimitMb(const DifferenceTui *t)
{
    return (*t).workspaceLimitMb;
}
;;SETTER
static inline fn Tui_setWorkspaceLimitMbField(DifferenceTui *t, u32 m)
{
    (*t).workspaceLimitMb = m;
}
;;GETTER
static inline bytes Tui_selectedIndex(const DifferenceTui *t)
{
    return (*t).selectedIndex;
}
;;SETTER
static inline fn Tui_setSelectedIndex(DifferenceTui *t, bytes i)
{
    (*t).selectedIndex = i;
}
;;GETTER
static inline bytes Tui_scrollOffset(const DifferenceTui *t)
{
    return (*t).scrollOffset;
}
;;SETTER
static inline fn Tui_setScrollOffset(DifferenceTui *t, bytes o)
{
    (*t).scrollOffset = o;
}
;;GETTER
static inline bytes Tui_totalRows(const DifferenceTui *t)
{
    return (*t).totalRows;
}
;;SETTER
static inline fn Tui_setTotalRows(DifferenceTui *t, bytes r)
{
    (*t).totalRows = r;
}
;;GETTER
static inline bool Tui_collapseMode(const DifferenceTui *t)
{
    return (*t).collapseMode;
}
;;SETTER
static inline fn Tui_setCollapseMode(DifferenceTui *t, bool c)
{
    (*t).collapseMode = c;
}
;;GETTER
static inline u32 Tui_contextLines(const DifferenceTui *t)
{
    return (*t).contextLines;
}
;;SETTER
static inline fn Tui_setContextLines(DifferenceTui *t, u32 n)
{
    (*t).contextLines = n;
}
;;GETTER
static inline DifferenceOverlayMode Tui_overlay(const DifferenceTui *t)
{
    return (*t).overlay;
}
;;SETTER
static inline fn Tui_setOverlay(DifferenceTui *t, DifferenceOverlayMode o)
{
    (*t).overlay = o;
}
;;GETTER
static inline bytes Tui_settingsCursor(const DifferenceTui *t)
{
    return (*t).settingsCursor;
}
;;SETTER
static inline fn Tui_setSettingsCursor(DifferenceTui *t, bytes c)
{
    (*t).settingsCursor = c;
}
;;GETTER
static inline bool Tui_utf8C3Pending(const DifferenceTui *t)
{
    return (*t).utf8C3Pending;
}
;;SETTER
static inline fn Tui_setUtf8C3Pending(DifferenceTui *t, bool p)
{
    (*t).utf8C3Pending = p;
}
;;GETTER
static inline bytes Tui_helpBtnX0(const DifferenceTui *t)
{
    return (*t).helpBtnX0;
}
;;SETTER
static inline fn Tui_setHelpBtnX0(DifferenceTui *t, bytes x)
{
    (*t).helpBtnX0 = x;
}
;;GETTER
static inline bytes Tui_helpBtnX1(const DifferenceTui *t)
{
    return (*t).helpBtnX1;
}
;;SETTER
static inline fn Tui_setHelpBtnX1(DifferenceTui *t, bytes x)
{
    (*t).helpBtnX1 = x;
}
;;GETTER
static inline bytes Tui_settingsBtnX0(const DifferenceTui *t)
{
    return (*t).settingsBtnX0;
}
;;SETTER
static inline fn Tui_setSettingsBtnX0(DifferenceTui *t, bytes x)
{
    (*t).settingsBtnX0 = x;
}
;;GETTER
static inline bytes Tui_settingsBtnX1(const DifferenceTui *t)
{
    return (*t).settingsBtnX1;
}
;;SETTER
static inline fn Tui_setSettingsBtnX1(DifferenceTui *t, bytes x)
{
    (*t).settingsBtnX1 = x;
}
;;GETTER
static inline bytes Tui_modalX(const DifferenceTui *t)
{
    return (*t).modalX;
}
;;SETTER
static inline fn Tui_setModalX(DifferenceTui *t, bytes x)
{
    (*t).modalX = x;
}
;;GETTER
static inline bytes Tui_modalY(const DifferenceTui *t)
{
    return (*t).modalY;
}
;;SETTER
static inline fn Tui_setModalY(DifferenceTui *t, bytes y)
{
    (*t).modalY = y;
}
;;GETTER
static inline bytes Tui_modalW(const DifferenceTui *t)
{
    return (*t).modalW;
}
;;SETTER
static inline fn Tui_setModalW(DifferenceTui *t, bytes w)
{
    (*t).modalW = w;
}
;;GETTER
static inline bytes Tui_modalH(const DifferenceTui *t)
{
    return (*t).modalH;
}
;;SETTER
static inline fn Tui_setModalH(DifferenceTui *t, bytes h)
{
    (*t).modalH = h;
}
;;GETTER
static inline bytes Tui_modalCloseX0(const DifferenceTui *t)
{
    return (*t).modalCloseX0;
}
;;SETTER
static inline fn Tui_setModalCloseX0(DifferenceTui *t, bytes x)
{
    (*t).modalCloseX0 = x;
}
;;GETTER
static inline bytes Tui_modalCloseX1(const DifferenceTui *t)
{
    return (*t).modalCloseX1;
}
;;SETTER
static inline fn Tui_setModalCloseX1(DifferenceTui *t, bytes x)
{
    (*t).modalCloseX1 = x;
}
;;GETTER
static inline bytes Tui_ctxMinusX0(const DifferenceTui *t)
{
    return (*t).ctxMinusX0;
}
;;SETTER
static inline fn Tui_setCtxMinusX0(DifferenceTui *t, bytes x)
{
    (*t).ctxMinusX0 = x;
}
;;GETTER
static inline bytes Tui_ctxMinusX1(const DifferenceTui *t)
{
    return (*t).ctxMinusX1;
}
;;SETTER
static inline fn Tui_setCtxMinusX1(DifferenceTui *t, bytes x)
{
    (*t).ctxMinusX1 = x;
}
;;GETTER
static inline bytes Tui_ctxPlusX0(const DifferenceTui *t)
{
    return (*t).ctxPlusX0;
}
;;SETTER
static inline fn Tui_setCtxPlusX0(DifferenceTui *t, bytes x)
{
    (*t).ctxPlusX0 = x;
}
;;GETTER
static inline bytes Tui_ctxPlusX1(const DifferenceTui *t)
{
    return (*t).ctxPlusX1;
}
;;SETTER
static inline fn Tui_setCtxPlusX1(DifferenceTui *t, bytes x)
{
    (*t).ctxPlusX1 = x;
}
;;GETTER
static inline bytes Tui_ctxRowY(const DifferenceTui *t)
{
    return (*t).ctxRowY;
}
;;SETTER
static inline fn Tui_setCtxRowY(DifferenceTui *t, bytes y)
{
    (*t).ctxRowY = y;
}
;;GETTER
static inline bytes Tui_wsEditX0(const DifferenceTui *t)
{
    return (*t).wsEditX0;
}
;;SETTER
static inline fn Tui_setWsEditX0(DifferenceTui *t, bytes x)
{
    (*t).wsEditX0 = x;
}
;;GETTER
static inline bytes Tui_wsEditX1(const DifferenceTui *t)
{
    return (*t).wsEditX1;
}
;;SETTER
static inline fn Tui_setWsEditX1(DifferenceTui *t, bytes x)
{
    (*t).wsEditX1 = x;
}
;;GETTER
static inline bytes Tui_wsRowY(const DifferenceTui *t)
{
    return (*t).wsRowY;
}
;;SETTER
static inline fn Tui_setWsRowY(DifferenceTui *t, bytes y)
{
    (*t).wsRowY = y;
}
;;GETTER
static inline bytes Tui_wsFieldX0(const DifferenceTui *t)
{
    return (*t).wsFieldX0;
}
;;SETTER
static inline fn Tui_setWsFieldX0(DifferenceTui *t, bytes x)
{
    (*t).wsFieldX0 = x;
}
;;GETTER
static inline bytes Tui_wsFieldX1(const DifferenceTui *t)
{
    return (*t).wsFieldX1;
}
;;SETTER
static inline fn Tui_setWsFieldX1(DifferenceTui *t, bytes x)
{
    (*t).wsFieldX1 = x;
}
;;GETTER
static inline bytes Tui_editsEditX0(const DifferenceTui *t)
{
    return (*t).editsEditX0;
}
;;SETTER
static inline fn Tui_setEditsEditX0(DifferenceTui *t, bytes x)
{
    (*t).editsEditX0 = x;
}
;;GETTER
static inline bytes Tui_editsEditX1(const DifferenceTui *t)
{
    return (*t).editsEditX1;
}
;;SETTER
static inline fn Tui_setEditsEditX1(DifferenceTui *t, bytes x)
{
    (*t).editsEditX1 = x;
}
;;GETTER
static inline bytes Tui_editsRowY(const DifferenceTui *t)
{
    return (*t).editsRowY;
}
;;SETTER
static inline fn Tui_setEditsRowY(DifferenceTui *t, bytes y)
{
    (*t).editsRowY = y;
}
;;GETTER
static inline bytes Tui_editsFieldX0(const DifferenceTui *t)
{
    return (*t).editsFieldX0;
}
;;SETTER
static inline fn Tui_setEditsFieldX0(DifferenceTui *t, bytes x)
{
    (*t).editsFieldX0 = x;
}
;;GETTER
static inline bytes Tui_editsFieldX1(const DifferenceTui *t)
{
    return (*t).editsFieldX1;
}
;;SETTER
static inline fn Tui_setEditsFieldX1(DifferenceTui *t, bytes x)
{
    (*t).editsFieldX1 = x;
}
;;GETTER
static inline bool Tui_followTail(const DifferenceTui *t)
{
    return (*t).followTail;
}
;;SETTER
static inline fn Tui_setFollowTail(DifferenceTui *t, bool f)
{
    (*t).followTail = f;
}
;;GETTER
static inline bool Tui_dirty(const DifferenceTui *t)
{
    return (*t).dirty;
}
;;SETTER
static inline fn Tui_setDirty(DifferenceTui *t, bool d)
{
    (*t).dirty = d;
}
;;GETTER
static inline bool Tui_running(const DifferenceTui *t)
{
    return (*t).running;
}
;;SETTER
static inline fn Tui_setRunning(DifferenceTui *t, bool r)
{
    (*t).running = r;
}
;;GETTER
static inline DifferenceTuiIdleFn Tui_idleHook(const DifferenceTui *t)
{
    return (*t).idleHook;
}
;;SETTER
static inline fn Tui_setIdleHookField(DifferenceTui *t, DifferenceTuiIdleFn h)
{
    (*t).idleHook = h;
}
;;GETTER
static inline any Tui_idleUserdata(const DifferenceTui *t)
{
    return (*t).idleUserdata;
}
;;SETTER
static inline fn Tui_setIdleUserdata(DifferenceTui *t, any u)
{
    (*t).idleUserdata = u;
}
;;GETTER
static inline DifferenceTuiWorkspaceLimitFn Tui_wsLimitHook(const DifferenceTui *t)
{
    return (*t).workspaceLimitHook;
}
;;SETTER
static inline fn Tui_setWsLimitHookField(DifferenceTui *t, DifferenceTuiWorkspaceLimitFn h)
{
    (*t).workspaceLimitHook = h;
}
;;GETTER
static inline any Tui_wsLimitUserdata(const DifferenceTui *t)
{
    return (*t).workspaceLimitUserdata;
}
;;SETTER
static inline fn Tui_setWsLimitUserdata(DifferenceTui *t, any u)
{
    (*t).workspaceLimitUserdata = u;
}
;;GETTER
static inline DifferenceTuiSettingsChangedFn Tui_settingsChangedHook(const DifferenceTui *t)
{
    return (*t).settingsChangedHook;
}
;;SETTER
static inline fn Tui_setSettingsChangedHookField(DifferenceTui *t, DifferenceTuiSettingsChangedFn h)
{
    (*t).settingsChangedHook = h;
}
;;GETTER
static inline any Tui_settingsChangedUserdata(const DifferenceTui *t)
{
    return (*t).settingsChangedUserdata;
}
;;SETTER
static inline fn Tui_setSettingsChangedUserdata(DifferenceTui *t, any u)
{
    (*t).settingsChangedUserdata = u;
}

;;GETTER
static inline string WatchEv_rel(const FileWatchEvent *e)
{
    return (*e).relativePath;
}
;;GETTER
static inline string WatchEv_path(const FileWatchEvent *e)
{
    return (*e).path;
}
;;GETTER
static inline FileEventType WatchEv_type(const FileWatchEvent *e)
{
    return (*e).type;
}
;;GETTER
static inline i64 WatchEv_epoch(const FileWatchEvent *e)
{
    return (*e).epochSeconds;
}
;;GETTER
static inline i64 WatchEv_size(const FileWatchEvent *e)
{
    return (*e).fileSize;
}

;;GETTER
static inline u64 Rec_seq(const DifferenceEditRecord *r)
{
    return (*r).sequence;
}
;;GETTER
static inline string Rec_rel(const DifferenceEditRecord *r)
{
    return (*r).relativePath;
}
;;GETTER
static inline string Rec_label(const DifferenceEditRecord *r)
{
    return (*r).eventLabel;
}
;;GETTER
static inline string Rec_ts(const DifferenceEditRecord *r)
{
    return (*r).timestamp;
}
;;GETTER
static inline FileEventType Rec_type(const DifferenceEditRecord *r)
{
    return (*r).eventType;
}
;;GETTER
static inline i64 Rec_size(const DifferenceEditRecord *r)
{
    return (*r).fileSize;
}
;;GETTER
static inline string Rec_oldText(const DifferenceEditRecord *r)
{
    return (*r).oldText;
}
;;GETTER
static inline bytes Rec_oldLen(const DifferenceEditRecord *r)
{
    return (*r).oldLen;
}
;;GETTER
static inline string Rec_newText(const DifferenceEditRecord *r)
{
    return (*r).newText;
}
;;GETTER
static inline bytes Rec_newLen(const DifferenceEditRecord *r)
{
    return (*r).newLen;
}
;;GETTER
static inline DiffResult *Rec_diffMut(DifferenceEditRecord *r)
{
    return &(*r).diff;
}
;;GETTER
static inline DiffResult Rec_diffVal(const DifferenceEditRecord *r)
{
    return (*r).diff;
}
;;SETTER
static inline fn Rec_setDiff(DifferenceEditRecord *r, DiffResult d)
{
    (*r).diff = d;
}
;;GETTER
static inline bool Rec_expanded(const DifferenceEditRecord *r)
{
    return (*r).expanded;
}
;;SETTER
static inline fn Rec_setExpanded(DifferenceEditRecord *r, bool e)
{
    (*r).expanded = e;
}
;;SETTER
static inline fn Rec_assign(DifferenceEditRecord *dst, DifferenceEditRecord src)
{
    *dst = src;
}

/** Returns decimal digit length of v (minimum 1). */
static u32 digitCountU64(u64 v) {
    u32 d = 1u;
    while (v >= 10u) {
        v /= 10u;
        d++;
    }
    return d;
}

/** Signal handler marking graceful TUI exit on SIGINT or SIGTERM. */
static fn onTuiStop(i32 sig) {
    (discard) sig;
    atomic_store_explicit(&g_tuiStop, true, memory_order_release);
}

/** Signal handler marking terminal dimension change on SIGWINCH. */
static fn onTuiResize(i32 sig) {
    (discard) sig;
    atomic_store_explicit(&g_tuiResized, true, memory_order_release);
}

/** Installs a signal handler without SA_RESTART so poll wakes immediately. */
static fn installSignal(i32 sig, fn (*handler)(i32)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, nullptr);
}

/** Writes all len bytes to fd, retrying short writes. */
static fn writeAll(i32 fd, string buf, bytes len) {
    bytes sent = 0u;
    while (sent < len) {
        i64 n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            break;
        sent += (bytes) n;
    }
}

/** Maps FileEventType bits to an uppercase badge label. */
static string tuiEventLabel(FileEventType type) {
    if (type & FILE_EVENT_DELETED)
        return "DELETED";
    if (type & FILE_EVENT_CREATED)
        return "NEW";
    if (type & FILE_EVENT_RENAMED)
        return "MOVED";
    return "EDIT";
}

/** Picks badge background color for a FileEventType. */
static u32 tuiEventBg(FileEventType type) {
    if (type & FILE_EVENT_DELETED)
        return TERMINAL_RGB(165, 42, 52);
    if (type & FILE_EVENT_CREATED)
        return TERMINAL_RGB(34, 120, 68);
    if (type & FILE_EVENT_RENAMED)
        return TERMINAL_RGB(48, 92, 165);
    return TERMINAL_RGB(178, 118, 28);
}

/** Formats epoch seconds into HH:MM:SS local time. */
static fn tuiFormatTime(i64 epochSec, utf8 *buf, bytes cap) {
    timestamp t = epochSec > 0 ? (timestamp) epochSec : time(nullptr);
    struct tm tmBuf;
    if (localtime_r(&t, &tmBuf))
        strftime(buf, cap, "%H:%M:%S", &tmBuf);
    else
        snprintf(buf, cap, "00:00:00");
}

/** Formats byte size into human-readable B / KB / MB / GB / TB string. */
static i32 formatByteSize(i64 fileSize, utf8 *buf, bytes cap) {
    if (fileSize < 0)
        return snprintf(buf, cap, "0 B");
    if (fileSize < 1024)
        return snprintf(buf, cap, "%lld B", (long long) fileSize);
    d64 val = (d64) fileSize / 1024.0;
    string unit = "KB";
    if (val >= 1024.0) {
        val /= 1024.0;
        unit = "MB";
    }
    if (val >= 1024.0) {
        val /= 1024.0;
        unit = "GB";
    }
    if (val >= 1024.0) {
        val /= 1024.0;
        unit = "TB";
    }
    return snprintf(buf, cap, "%.2f %s", val, unit);
}

/** Normalizes a relative path so it always starts with a single leading '/'. */
static i32 formatSlashPath(string raw, utf8 *buf, bytes cap) {
    string p = (raw && raw[0]) ? raw : "unnamed";
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    while (p[0] == '/')
        p++;
    if (!p[0])
        return snprintf(buf, cap, "/");
    return snprintf(buf, cap, "/%s", p);
}

/** Queries live terminal width and height via TIOCGWINSZ with safe minimums. */
static fn queryTermSize(bytes *outW, bytes *outH) {
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *outW = (bytes) ws.ws_col;
        *outH = (bytes) ws.ws_row;
        return;
    }
    *outW = 80u;
    *outH = 24u;
}

/** Duplicates len bytes into arena with trailing NUL byte. */
static utf8 *arenaDupBytes(MemoryArena *arena, string src, bytes len) {
    if (!arena)
        return nullptr;
    utf8 *copy = (utf8*) MemoryArena_alloc(arena, TYPE_DIFFERENCE_TUI, len + 1u);
    if (!copy)
        return nullptr;
    if (len > 0u && src)
        memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

/** Releases DiffResult owned by one edit record. */
static fn releaseEditRecord(DifferenceEditRecord *rec) {
    DiffEngine_free(Rec_diffMut(rec));
    memset(rec, 0, sizeof(*rec));
}

/** Opens the determinant TerminalTextArea for editing a numeric setting row. */
static fn beginSettingEdit(DifferenceTui *tui, i32 item) {
    TerminalTextArea *field = Tui_editField(tui);
    if (!field)
        return;
    utf8 *buf = Tui_editBufMut(tui);
    bytes cap = Tui_editBufCap(tui);
    u32 val = (item == (i32) SETTINGS_ITEM_WORKSPACE_MB)
                  ? Tui_workspaceLimitMb(tui)
                  : Tui_maxEdits(tui);
    snprintf(buf, cap, "%u", (unsigned) val);
    (discard) TerminalTextArea_bind(field, buf, cap);
    Tui_setEditingSetting(tui, item);
    Tui_setDirty(tui, true);
}

/** Commits the active TerminalTextArea numeric edit to the target setting. */
static fn commitSettingEdit(DifferenceTui *tui) {
    i32 item = Tui_editingSetting(tui);
    if (item < 0)
        return;
    TerminalTextArea *field = Tui_editField(tui);
    string text = field ? TerminalTextArea_getBuffer(field) : "";
    unsigned long parsed = strtoul(text, nullptr, 10);
    Tui_setEditingSetting(tui, -1);
    if (parsed > 0ul) {
        if (item == (i32) SETTINGS_ITEM_WORKSPACE_MB)
            DifferenceTui_setWorkspaceLimitMb(tui, (u32) parsed);
        else if (item == (i32) SETTINGS_ITEM_MAX_EDITS)
            DifferenceTui_setMaxEdits(tui, (u32) parsed);
    }
    Tui_setDirty(tui, true);
}

/** Cancels the active TerminalTextArea edit without modifying the setting. */
static fn cancelSettingEdit(DifferenceTui *tui) {
    if (Tui_editingSetting(tui) >= 0) {
        Tui_setEditingSetting(tui, -1);
        Tui_setDirty(tui, true);
    }
}

/** Computes the total rendered row height of a single edit record. */
static bytes recordBlockRows(TerminalDiffPanel *panel, const DifferenceEditRecord *rec, bytes bodyW) {
    if (!Rec_expanded(rec))
        return 1u;
    DiffResult dv = Rec_diffVal(rec);
    bytes diffRows = TerminalDiffPanel_measureRows(panel, dv.hunks, dv.hunkCount, (u32) bodyW);
    return 1u + (diffRows > 0u ? diffRows : 1u) + 1u;
}

/** Recomputes all active edit diffs when contextLines changes in Settings. */
static fn recomputeAllDiffs(DifferenceTui *tui) {
    MemoryArena *ra = Tui_recordArena(tui);
    MemoryArena *scratch = Tui_diffScratch(tui);
    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    u32 ctx = Tui_contextLines(tui);
    for (bytes i = 0u; i < count; i++) {
        DiffResult nextDiff;
        if (DiffEngine_computeIn(ra, scratch,
                                 Rec_oldText(&recs[i]), Rec_oldLen(&recs[i]),
                                 Rec_newText(&recs[i]), Rec_newLen(&recs[i]),
                                 ctx, &nextDiff)) {
            DiffEngine_free(Rec_diffMut(&recs[i]));
            Rec_setDiff(&recs[i], nextDiff);
        }
    }
}

/** Invokes the registered settings-changed hook with a snapshot of all 8 settings. */
static fn notifySettingsChanged(DifferenceTui *tui) {
    if (!tui)
        return;
    DifferenceTuiSettingsChangedFn hook = Tui_settingsChangedHook(tui);
    if (!hook)
        return;
    DifferenceSettings snap;
    DifferenceTui_exportSettings(tui, &snap);
    hook(&snap, Tui_settingsChangedUserdata(tui));
}

/** Toggles global collapse mode and applies it to all current edit records. */
static fn toggleCollapseMode(DifferenceTui *tui) {
    bool next = !Tui_collapseMode(tui);
    Tui_setCollapseMode(tui, next);
    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    for (bytes i = 0u; i < count; i++)
        Rec_setExpanded(&recs[i], !next);
    Tui_setDirty(tui, true);
    notifySettingsChanged(tui);
}

/** Cycles diff layout mode between AUTO, SIDE_BY_SIDE, and UNIFIED. */
static fn cycleDiffLayout(DifferenceTui *tui) {
    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffLayoutMode cur = TerminalDiffPanel_layoutMode(panel);
    if (cur == TERMINAL_DIFF_LAYOUT_AUTO)
        TerminalDiffPanel_setLayoutMode(panel, TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE);
    else if (cur == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE)
        TerminalDiffPanel_setLayoutMode(panel, TERMINAL_DIFF_LAYOUT_UNIFIED);
    else
        TerminalDiffPanel_setLayoutMode(panel, TERMINAL_DIFF_LAYOUT_AUTO);
    Tui_setDirty(tui, true);
    notifySettingsChanged(tui);
}

/** Adjusts contextLines by delta within [CONTEXT_LINES_MIN, CONTEXT_LINES_MAX]. */
static fn adjustContextLines(DifferenceTui *tui, i32 delta) {
    i32 cur = (i32) Tui_contextLines(tui);
    i32 next = cur + delta;
    if (next < (i32) CONTEXT_LINES_MIN)
        next = (i32) CONTEXT_LINES_MIN;
    if (next > (i32) CONTEXT_LINES_MAX)
        next = (i32) CONTEXT_LINES_MAX;
    if ((u32) next != Tui_contextLines(tui)) {
        Tui_setContextLines(tui, (u32) next);
        recomputeAllDiffs(tui);
        Tui_setDirty(tui, true);
        notifySettingsChanged(tui);
    }
}

/** Steps maxEdits by 100 within [DIFFERENCE_TUI_MIN_EDITS, DIFFERENCE_TUI_MAX_EDITS]. */
static fn stepMaxEdits(DifferenceTui *tui, i32 dir) {
    i32 cur = (i32) Tui_maxEdits(tui);
    i32 next = cur + dir * (i32) DIFFERENCE_TUI_EDITS_STEP;
    if (next < (i32) DIFFERENCE_TUI_MIN_EDITS)
        next = (i32) DIFFERENCE_TUI_MIN_EDITS;
    if (next > (i32) DIFFERENCE_TUI_MAX_EDITS)
        next = (i32) DIFFERENCE_TUI_MAX_EDITS;
    DifferenceTui_setMaxEdits(tui, (u32) next);
}

/** Activates or toggles the setting item at index (or adjusts numeric stepper). */
static fn activateSetting(DifferenceTui *tui, bytes item, i32 numericDelta) {
    if (item == 0u)
        toggleCollapseMode(tui);
    else if (item == 1u)
        cycleDiffLayout(tui);
    else if (item == 2u) {
        Tui_setFollowTail(tui, !Tui_followTail(tui));
        Tui_setDirty(tui, true);
        notifySettingsChanged(tui);
    } else if (item == 3u) {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
        th.useColor = !th.useColor;
        TerminalDiffPanel_setTheme(panel, th);
        Tui_setDirty(tui, true);
        notifySettingsChanged(tui);
    } else if (item == 4u) {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
        th.showDiffIcons = !th.showDiffIcons;
        TerminalDiffPanel_setTheme(panel, th);
        Tui_setDirty(tui, true);
        notifySettingsChanged(tui);
    } else if (item == SETTINGS_ITEM_CONTEXT)
        adjustContextLines(tui, numericDelta != 0 ? numericDelta : 1);
    else if (item == SETTINGS_ITEM_WORKSPACE_MB) {
        if (numericDelta == 0)
            beginSettingEdit(tui, (i32) SETTINGS_ITEM_WORKSPACE_MB);
        else {
            i32 cur = (i32) Tui_workspaceLimitMb(tui);
            i32 next = cur + numericDelta * 128;
            if (next < (i32) DIFFERENCE_TUI_MIN_WORKSPACE_MB)
                next = (i32) DIFFERENCE_TUI_MIN_WORKSPACE_MB;
            DifferenceTui_setWorkspaceLimitMb(tui, (u32) next);
        }
    } else if (item == SETTINGS_ITEM_MAX_EDITS) {
        if (numericDelta == 0)
            beginSettingEdit(tui, (i32) SETTINGS_ITEM_MAX_EDITS);
        else
            stepMaxEdits(tui, numericDelta > 0 ? 1 : -1);
    }
}

/** Ensures the selected edit's summary row is visible inside the scroll viewport. */
static fn ensureSelectedVisible(DifferenceTui *tui) {
    bytes count = Tui_recordCount(tui);
    if (count == 0u)
        return;
    bytes sel = Tui_selectedIndex(tui);
    if (sel >= count)
        sel = count - 1u;
    bytes width = Tui_width(tui);
    bytes height = Tui_height(tui);
    bytes bodyH = height > 2u ? height - 2u : 1u;
    bytes bodyW = width;
    TerminalDiffPanel *panel = Tui_panelMut(tui);
    DifferenceEditRecord *recs = Tui_recordsMut(tui);

    bytes rowY = 0u;
    for (bytes i = 0u; i < sel; i++)
        rowY += recordBlockRows(panel, &recs[i], bodyW);

    bytes scroll = Tui_scrollOffset(tui);
    if (rowY < scroll)
        Tui_setScrollOffset(tui, rowY);
    else if (rowY >= scroll + bodyH)
        Tui_setScrollOffset(tui, rowY - bodyH + 1u);
}

/** Initializes DifferenceTui with determinant surface, compositor, input, text field, and arenas. */
bool DifferenceTui_init(DifferenceTui *tui, string watchRoot,
                        TerminalDiffLayoutMode initialLayout, bool useColor) {
    if (!tui)
        return false;
    memset(tui, 0, sizeof(*tui));

    MemoryArena *frameArena = MemoryArena(64u * 1024u * 1024u);
    MemoryArena *recordArena = MemoryArena(256u * 1024u * 1024u);
    MemoryArena *diffScratch = MemoryArena(64u * 1024u * 1024u);
    TerminalCompositor *comp = TerminalCompositor_1(STDOUT_FILENO);
    TerminalTextArea *editField = TerminalTextArea_2(Tui_editBufMut(tui), Tui_editBufCap(tui));
    if (!frameArena || !recordArena || !diffScratch || !comp || !editField) {
        if (editField)
            TerminalTextArea_free(editField);
        if (comp)
            TerminalCompositor_free(comp);
        if (frameArena)
            MemoryArena_destroy(frameArena);
        if (recordArena)
            MemoryArena_destroy(recordArena);
        if (diffScratch)
            MemoryArena_destroy(diffScratch);
        return false;
    }

    Tui_setFrameArena(tui, frameArena);
    Tui_setRecordArena(tui, recordArena);
    Tui_setDiffScratch(tui, diffScratch);
    Tui_setCompositor(tui, comp);
    Tui_setEditField(tui, editField);
    Tui_setEditingSetting(tui, -1);
    Tui_setOutFd(tui, STDOUT_FILENO);
    *Tui_surfaceMut(tui) = TerminalSurface_0();
    *Tui_inputMut(tui) = TerminalInput_0();

    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffPanel_init(panel);
    TerminalDiffPanel_setLayoutMode(panel, initialLayout);
    TerminalDiffTheme th = TerminalDiffPanel_defaultTheme();
    th.showHeader = false;
    th.useColor = useColor;
    TerminalDiffPanel_setTheme(panel, th);

    Tui_setWatchRoot(tui, watchRoot ? watchRoot : ".");
    Tui_setMaxEditsField(tui, DIFFERENCE_TUI_DEFAULT_MAX_EDITS);
    Tui_setWorkspaceLimitMbField(tui, DIFFERENCE_TUI_DEFAULT_WORKSPACE_MB);
    Tui_setCollapseMode(tui, true);
    Tui_setContextLines(tui, 3u);
    Tui_setOverlay(tui, DIFFERENCE_OVERLAY_NONE);
    Tui_setSettingsCursor(tui, 0u);
    Tui_setFollowTail(tui, true);
    Tui_setDirty(tui, true);
    Tui_setRunning(tui, false);
    return true;
}

/** Destroys all edit diffs, text field, compositor, and arenas owned by DifferenceTui. */
fn DifferenceTui_free(DifferenceTui *tui) {
    if (!tui)
        return;
    DifferenceTui_clearEdits(tui);
    TerminalTextArea *ef = Tui_editField(tui);
    TerminalCompositor *comp = Tui_compositor(tui);
    MemoryArena *fa = Tui_frameArena(tui);
    MemoryArena *ra = Tui_recordArena(tui);
    MemoryArena *ds = Tui_diffScratch(tui);
    if (ef)
        TerminalTextArea_free(ef);
    if (comp)
        TerminalCompositor_free(comp);
    if (fa)
        MemoryArena_destroy(fa);
    if (ra)
        MemoryArena_destroy(ra);
    if (ds)
        MemoryArena_destroy(ds);
    memset(tui, 0, sizeof(*tui));
}

/** Redirects TUI output to fd (or disables writes when fd < 0 for headless tests). */
fn DifferenceTui_setOutputFd(DifferenceTui *tui, i32 fd) {
    if (!tui)
        return;
    Tui_setOutFd(tui, fd);
    TerminalCompositor *comp = Tui_compositor(tui);
    if (comp && fd >= 0)
        (discard) TerminalCompositor_setFd(comp, fd);
}

/** Updates the tracked file count displayed in the top bar. */
fn DifferenceTui_setTrackedFiles(DifferenceTui *tui, bytes count) {
    if (!tui)
        return;
    Tui_setTrackedFiles(tui, count);
    Tui_setDirty(tui, true);
}

/** Registers an optional incremental idle callback stepped inside DifferenceTui_run. */
fn DifferenceTui_setIdleHook(DifferenceTui *tui, DifferenceTuiIdleFn hook, any userdata) {
    if (!tui)
        return;
    Tui_setIdleHookField(tui, hook);
    Tui_setIdleUserdata(tui, userdata);
}

/** Registers a callback invoked when the user changes Workspace limit (MB) in Settings. */
fn DifferenceTui_setWorkspaceLimitHook(DifferenceTui *tui, DifferenceTuiWorkspaceLimitFn hook, any userdata) {
    if (!tui)
        return;
    Tui_setWsLimitHookField(tui, hook);
    Tui_setWsLimitUserdata(tui, userdata);
}

/** Registers a callback invoked whenever any persisted TUI setting changes. */
fn DifferenceTui_setSettingsChangedHook(DifferenceTui *tui, DifferenceTuiSettingsChangedFn hook, any userdata) {
    if (!tui)
        return;
    Tui_setSettingsChangedHookField(tui, hook);
    Tui_setSettingsChangedUserdata(tui, userdata);
}

/** Exports the current TUI option values into a DifferenceSettings record. */
fn DifferenceTui_exportSettings(const DifferenceTui *tui, DifferenceSettings *out) {
    if (!tui || !out)
        return;
    *out = DifferenceSettings_0();
    DifferenceSettings_setCollapseMode(out, Tui_collapseMode(tui));
    (discard) DifferenceSettings_setDiffLayout(out, TerminalDiffPanel_layoutMode(Tui_panel(tui)));
    DifferenceSettings_setFollowTail(out, Tui_followTail(tui));
    TerminalDiffTheme th = TerminalDiffPanel_theme(Tui_panel(tui));
    DifferenceSettings_setColor(out, th.useColor);
    DifferenceSettings_setDiffIcons(out, th.showDiffIcons);
    (discard) DifferenceSettings_setContextLines(out, Tui_contextLines(tui));
    (discard) DifferenceSettings_setWorkspaceLimitMb(out, Tui_workspaceLimitMb(tui));
    (discard) DifferenceSettings_setMaxEdits(out, Tui_maxEdits(tui));
}

/** Applies a loaded DifferenceSettings record onto DifferenceTui without triggering a save. */
fn DifferenceTui_applySettings(DifferenceTui *tui, const DifferenceSettings *settings) {
    if (!tui || !settings)
        return;
    bool collapse = DifferenceSettings_isCollapseMode(settings);
    Tui_setCollapseMode(tui, collapse);
    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    for (bytes i = 0u; i < count; i++)
        Rec_setExpanded(&recs[i], !collapse);

    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffPanel_setLayoutMode(panel, DifferenceSettings_getDiffLayout(settings));
    TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
    th.useColor = DifferenceSettings_isColor(settings);
    th.showDiffIcons = DifferenceSettings_isDiffIcons(settings);
    TerminalDiffPanel_setTheme(panel, th);

    Tui_setFollowTail(tui, DifferenceSettings_isFollowTail(settings));

    u32 ctx = DifferenceSettings_getContextLines(settings);
    if (ctx != Tui_contextLines(tui)) {
        Tui_setContextLines(tui, ctx);
        if (count > 0u)
            recomputeAllDiffs(tui);
    }

    u32 wsMb = DifferenceSettings_getWorkspaceLimitMb(settings);
    Tui_setWorkspaceLimitMbField(tui, wsMb);
    DifferenceTuiWorkspaceLimitFn wsHook = Tui_wsLimitHook(tui);
    if (wsHook)
        wsHook(wsMb, Tui_wsLimitUserdata(tui));

    u32 maxE = DifferenceSettings_getMaxEdits(settings);
    Tui_setMaxEditsField(tui, maxE);
    if (count > (bytes) maxE) {
        bytes excess = count - (bytes) maxE;
        for (bytes i = 0u; i < excess; i++)
            releaseEditRecord(&recs[i]);
        memmove(&recs[0], &recs[excess], (bytes) maxE * sizeof(DifferenceEditRecord));
        Tui_setRecordCount(tui, (bytes) maxE);
        bytes sel = Tui_selectedIndex(tui);
        Tui_setSelectedIndex(tui, sel >= excess ? sel - excess : 0u);
    }
    Tui_setDirty(tui, true);
}

/** Clamps and updates the workspace data limit in MB and notifies any registered hooks. */
fn DifferenceTui_setWorkspaceLimitMb(DifferenceTui *tui, u32 limitMb) {
    if (!tui)
        return;
    if (limitMb < (u32) DIFFERENCE_TUI_MIN_WORKSPACE_MB)
        limitMb = (u32) DIFFERENCE_TUI_MIN_WORKSPACE_MB;
    if (limitMb > (u32) DIFFERENCE_TUI_MAX_WORKSPACE_MB)
        limitMb = (u32) DIFFERENCE_TUI_MAX_WORKSPACE_MB;
    bool changed = (limitMb != Tui_workspaceLimitMb(tui));
    Tui_setWorkspaceLimitMbField(tui, limitMb);
    DifferenceTuiWorkspaceLimitFn hook = Tui_wsLimitHook(tui);
    if (hook)
        hook(limitMb, Tui_wsLimitUserdata(tui));
    Tui_setDirty(tui, true);
    if (changed)
        notifySettingsChanged(tui);
}

/** Clamps maxEdits within [100, 1024], trims oldest stored edits if needed, and notifies hook. */
fn DifferenceTui_setMaxEdits(DifferenceTui *tui, u32 maxEdits) {
    if (!tui)
        return;
    if (maxEdits < (u32) DIFFERENCE_TUI_MIN_EDITS)
        maxEdits = (u32) DIFFERENCE_TUI_MIN_EDITS;
    if (maxEdits > (u32) DIFFERENCE_TUI_MAX_EDITS)
        maxEdits = (u32) DIFFERENCE_TUI_MAX_EDITS;
    bool changed = (maxEdits != Tui_maxEdits(tui));
    Tui_setMaxEditsField(tui, maxEdits);

    bytes count = Tui_recordCount(tui);
    if (count > (bytes) maxEdits) {
        DifferenceEditRecord *recs = Tui_recordsMut(tui);
        bytes excess = count - (bytes) maxEdits;
        for (bytes i = 0u; i < excess; i++)
            releaseEditRecord(&recs[i]);
        memmove(&recs[0], &recs[excess], (bytes) maxEdits * sizeof(DifferenceEditRecord));
        Tui_setRecordCount(tui, (bytes) maxEdits);
        bytes sel = Tui_selectedIndex(tui);
        Tui_setSelectedIndex(tui, sel >= excess ? sel - excess : 0u);
    }
    Tui_setDirty(tui, true);
    if (changed)
        notifySettingsChanged(tui);
}

/** Clears all recorded edits, resets cumulative totals, and resets the record arena. */
fn DifferenceTui_clearEdits(DifferenceTui *tui) {
    if (!tui)
        return;
    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    for (bytes i = 0; i < count; i++)
        releaseEditRecord(&recs[i]);
    Tui_setRecordCount(tui, 0u);
    Tui_setSelectedIndex(tui, 0u);
    Tui_setScrollOffset(tui, 0u);
    Tui_setTotalRows(tui, 0u);
    Tui_setTotalAdded(tui, 0u);
    Tui_setTotalRemoved(tui, 0u);
    MemoryArena *ra = Tui_recordArena(tui);
    if (ra)
        MemoryArena_freeAll(ra);
    Tui_setDirty(tui, true);
}

/** Appends a live file edit record and computes its structured diff for TUI reflow. */
bool DifferenceTui_pushEdit(DifferenceTui *tui,
                            const FileWatchEvent *event,
                            string oldText, bytes oldLen,
                            string newText, bytes newLen) {
    if (!tui || !event)
        return false;

    MemoryArena *ra = Tui_recordArena(tui);
    if (!ra)
        return false;

    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    bytes limit = (bytes) Tui_maxEdits(tui);
    if (limit == 0u || limit > (bytes) DIFFERENCE_TUI_MAX_EDITS)
        limit = (bytes) DIFFERENCE_TUI_MAX_EDITS;
    while (count >= limit) {
        releaseEditRecord(&recs[0]);
        memmove(&recs[0], &recs[1], (count - 1u) * sizeof(DifferenceEditRecord));
        count--;
        Tui_setRecordCount(tui, count);
        if (Tui_selectedIndex(tui) > 0u)
            Tui_setSelectedIndex(tui, Tui_selectedIndex(tui) - 1u);
    }

    string path = WatchEv_path(event) ? WatchEv_path(event) : "";
    string rel = (WatchEv_rel(event) && WatchEv_rel(event)[0]) ? WatchEv_rel(event) : path;
    utf8 *oldCopy = arenaDupBytes(ra, oldText ? oldText : "", oldLen);
    utf8 *newCopy = arenaDupBytes(ra, newText ? newText : "", newLen);
    utf8 *pathCopy = arenaDupBytes(ra, path, strlen(path));
    utf8 *relCopy = arenaDupBytes(ra, rel, strlen(rel));
    if (!oldCopy || !newCopy || !pathCopy || !relCopy) {
        DifferenceTui_clearEdits(tui);
        oldCopy = arenaDupBytes(ra, oldText ? oldText : "", oldLen);
        newCopy = arenaDupBytes(ra, newText ? newText : "", newLen);
        pathCopy = arenaDupBytes(ra, path, strlen(path));
        relCopy = arenaDupBytes(ra, rel, strlen(rel));
        count = 0u;
        if (!oldCopy || !newCopy || !pathCopy || !relCopy)
            return false;
    }

    DiffResult diff;
    if (!DiffEngine_computeIn(ra, Tui_diffScratch(tui), oldCopy, oldLen, newCopy, newLen, Tui_contextLines(tui), &diff)) {
        DifferenceTui_clearEdits(tui);
        oldCopy = arenaDupBytes(ra, oldText ? oldText : "", oldLen);
        newCopy = arenaDupBytes(ra, newText ? newText : "", newLen);
        pathCopy = arenaDupBytes(ra, path, strlen(path));
        relCopy = arenaDupBytes(ra, rel, strlen(rel));
        count = 0u;
        if (!oldCopy || !newCopy || !pathCopy || !relCopy)
            return false;
        if (!DiffEngine_computeIn(ra, Tui_diffScratch(tui), oldCopy, oldLen, newCopy, newLen, Tui_contextLines(tui), &diff))
            return false;
    }

    u64 seq = Tui_editSeq(tui) + 1u;
    Tui_setEditSeq(tui, seq);
    Tui_setTotalAdded(tui, Tui_totalAdded(tui) + (u64) diff.linesAdded);
    Tui_setTotalRemoved(tui, Tui_totalRemoved(tui) + (u64) diff.linesRemoved);

    DifferenceEditRecord built;
    memset(&built, 0, sizeof(built));
    built.sequence = seq;
    built.path = pathCopy;
    built.relativePath = relCopy;
    snprintf(built.eventLabel, sizeof(built.eventLabel), "%s", tuiEventLabel(WatchEv_type(event)));
    tuiFormatTime(WatchEv_epoch(event), built.timestamp, sizeof(built.timestamp));
    built.eventType = WatchEv_type(event);
    built.fileSize = WatchEv_size(event);
    built.oldText = oldCopy;
    built.oldLen = oldLen;
    built.newText = newCopy;
    built.newLen = newLen;
    built.diff = diff;
    built.expanded = !Tui_collapseMode(tui);
    Rec_assign(&recs[count], built);

    Tui_setRecordCount(tui, count + 1u);
    if (Tui_followTail(tui))
        Tui_setSelectedIndex(tui, count);
    Tui_setDirty(tui, true);
    return true;
}

/** Reallocates front/back planes and resets compositor state for atomic repaint. */
bool DifferenceTui_resize(DifferenceTui *tui, bytes width, bytes height) {
    if (!tui)
        return false;
    if (width < 1u)
        width = 1u;
    if (height < 1u)
        height = 1u;

    MemoryArena *fa = Tui_frameArena(tui);
    TerminalCompositor *comp = Tui_compositor(tui);
    if (!fa || !comp)
        return false;
    MemoryArena_freeAll(fa);

    bytes cells = width * height;
    u32 *bc = (u32*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u32));
    u16 *bs = (u16*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u16));
    u32 *bf = (u32*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u32));
    u32 *bb = (u32*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u32));
    u32 *fc = (u32*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u32));
    u16 *fs = (u16*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u16));
    u32 *ff = (u32*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u32));
    u32 *fb = (u32*) MemoryArena_alloc(fa, TYPE_DIFFERENCE_TUI, cells * sizeof(u32));
    if (!bc || !bs || !bf || !bb || !fc || !fs || !ff || !fb)
        return false;

    for (bytes i = 0; i < cells; i++) {
        bc[i] = (u32) ' ';
        bs[i] = 0u;
        bf[i] = TERMINAL_COLOR_DEFAULT;
        bb[i] = TERMINAL_COLOR_DEFAULT;
        fc[i] = 0xFFFFFFFFu;
        fs[i] = 0xFFFFu;
        ff[i] = TERMINAL_COLOR_DEFAULT;
        fb[i] = TERMINAL_COLOR_DEFAULT;
    }

    Tui_setWidth(tui, width);
    Tui_setHeight(tui, height);
    Tui_setBackCells(tui, bc);
    Tui_setBackStyles(tui, bs);
    Tui_setBackFg(tui, bf);
    Tui_setBackBg(tui, bb);
    Tui_setFrontCells(tui, fc);
    Tui_setFrontStyles(tui, fs);
    Tui_setFrontFg(tui, ff);
    Tui_setFrontBg(tui, fb);

    TerminalCanvas back = TerminalCanvas_6(bc, bs, bf, bb, width, height);
    TerminalCanvas front = TerminalCanvas_6(fc, fs, ff, fb, width, height);
    TerminalSurface *surf = Tui_surfaceMut(tui);
    if (!TerminalSurface_bind(surf, &back, &front, TerminalCompositor_blitRun, comp))
        return false;
    if (!TerminalSurface_setFrame(surf, TerminalCompositor_phase, comp))
        return false;
    (discard) TerminalCompositor_setPresentWidth(comp, width);
    TerminalCompositor_forget(comp);

    Tui_setDirty(tui, true);
    return true;
}

/** Paints one edit summary row with right-aligned #N, +A, -R columns and circle selection icon. */
static fn paintEditRow(TerminalCanvas *body, bytes sy, bytes bodyW,
                       const DifferenceEditRecord *rec, bool selected, bool useColor,
                       u32 maxSeqDigits, u32 maxAddDigits, u32 maxDelDigits) {
    u32 rowBg = TERMINAL_COLOR_DEFAULT;
    if (useColor)
        rowBg = selected ? TERMINAL_RGB(34, 46, 66) : TERMINAL_RGB(20, 25, 34);
    u16 baseStyle = (!useColor && selected) ? TERMINAL_STYLE_REVERSE : 0u;
    TerminalPen basePen = TerminalPen_3(baseStyle,
                                        useColor ? TERMINAL_RGB(220, 228, 236) : TERMINAL_COLOR_DEFAULT,
                                        rowBg);
    for (bytes c = 0u; c < bodyW; c++)
        TerminalCanvas_glyph(body, c, sy, (u32) ' ', basePen);

    DiffResult dv = Rec_diffVal(rec);
    utf8 addRaw[24];
    utf8 delRaw[24];
    utf8 addBuf[32];
    utf8 delBuf[32];
    utf8 timeBuf[24];
    (discard) snprintf(addRaw, sizeof(addRaw), "+%u", (unsigned) dv.linesAdded);
    (discard) snprintf(delRaw, sizeof(delRaw), "-%u", (unsigned) dv.linesRemoved);
    bytes addColW = (bytes) maxAddDigits + 1u;
    bytes delColW = (bytes) maxDelDigits + 1u;
    i32 an = snprintf(addBuf, sizeof(addBuf), "%*s", (int) addColW, addRaw);
    i32 dn = snprintf(delBuf, sizeof(delBuf), "%*s", (int) delColW, delRaw);
    i32 tn = snprintf(timeBuf, sizeof(timeBuf), "%s", Rec_ts(rec));
    bytes rightLen = (bytes) (an > 0 ? an : 0) + 1u + (bytes) (dn > 0 ? dn : 0) + 2u
                     + (bytes) (tn > 0 ? tn : 0) + 1u;
    bytes rightStart = bodyW > rightLen ? bodyW - rightLen : 0u;

    bytes x = 0u;
    if (x + 2u < bodyW) {
        TerminalPen ptrPen = TerminalPen_3(TERMINAL_STYLE_BOLD | baseStyle,
                                           useColor ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT,
                                           rowBg);
        TerminalCanvas_glyph(body, x, sy, selected ? (u32) SELECTION_CIRCLE_GLYPH : (u32) ' ', ptrPen);
        x += 2u;
    }

    utf8 seqRaw[24];
    utf8 seqBuf[32];
    (discard) snprintf(seqRaw, sizeof(seqRaw), "#%llu", (unsigned long long) Rec_seq(rec));
    bytes seqColW = (bytes) maxSeqDigits + 1u;
    i32 sn = snprintf(seqBuf, sizeof(seqBuf), "%*s ", (int) seqColW, seqRaw);
    if (sn > 0 && x < rightStart) {
        bytes avail = rightStart - x;
        bytes n = (bytes) sn < avail ? (bytes) sn : avail;
        TerminalPen seqPen = TerminalPen_3(baseStyle,
                                           useColor ? TERMINAL_RGB(140, 152, 170) : TERMINAL_COLOR_DEFAULT,
                                           rowBg);
        TerminalCanvas_text(body, x, sy, seqBuf, n, seqPen);
        x += n;
    }

    utf8 pillBuf[24];
    i32 pn = snprintf(pillBuf, sizeof(pillBuf), " %s ", Rec_label(rec));
    if (pn > 0 && x < rightStart) {
        bytes avail = rightStart - x;
        bytes n = (bytes) pn < avail ? (bytes) pn : avail;
        TerminalPen pillPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                            useColor ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                            useColor ? tuiEventBg(Rec_type(rec)) : rowBg);
        TerminalCanvas_text(body, x, sy, pillBuf, n, pillPen);
        x += n + 1u;
    }

    utf8 sizeBuf[32];
    i32 szn = formatByteSize(Rec_size(rec), sizeBuf, sizeof(sizeBuf));
    bytes sizeLen = szn > 0 ? (bytes) szn : 0u;

    utf8 pathBuf[512];
    i32 pathLen = formatSlashPath(Rec_rel(rec), pathBuf, sizeof(pathBuf));
    if (pathLen > 0 && x < rightStart) {
        bytes gapForSize = (sizeLen > 0u && rightStart > x + sizeLen + 3u) ? sizeLen + 2u : 0u;
        bytes maxPath = rightStart > x + gapForSize + 1u ? rightStart - x - gapForSize - 1u : 0u;
        TerminalPen pathPen = TerminalPen_3(TERMINAL_STYLE_BOLD | baseStyle,
                                            useColor ? TERMINAL_RGB(240, 246, 252) : TERMINAL_COLOR_DEFAULT,
                                            rowBg);
        if (maxPath > 0u) {
            if ((bytes) pathLen <= maxPath) {
                TerminalCanvas_text(body, x, sy, pathBuf, (bytes) pathLen, pathPen);
                x += (bytes) pathLen + 2u;
            } else if (maxPath > 3u) {
                TerminalCanvas_text(body, x, sy, pathBuf, maxPath - 3u, pathPen);
                TerminalCanvas_text(body, x + maxPath - 3u, sy, "...", 3u, pathPen);
                x += maxPath + 2u;
            }
        }
    }

    if (sizeLen > 0u && x + sizeLen < rightStart) {
        TerminalPen szPen = TerminalPen_3(baseStyle,
                                          useColor ? TERMINAL_RGB(139, 150, 168) : TERMINAL_COLOR_DEFAULT,
                                          rowBg);
        TerminalCanvas_text(body, x, sy, sizeBuf, sizeLen, szPen);
    }

    if (rightStart < bodyW) {
        bytes rx = rightStart;
        TerminalPen timePen = TerminalPen_3(baseStyle,
                                            useColor ? TERMINAL_RGB(130, 142, 160) : TERMINAL_COLOR_DEFAULT,
                                            rowBg);
        TerminalPen addPen = TerminalPen_3(TERMINAL_STYLE_BOLD | baseStyle,
                                           useColor ? TERMINAL_RGB(126, 231, 135) : TERMINAL_COLOR_DEFAULT,
                                           rowBg);
        TerminalPen delPen = TerminalPen_3(TERMINAL_STYLE_BOLD | baseStyle,
                                           useColor ? TERMINAL_RGB(255, 123, 114) : TERMINAL_COLOR_DEFAULT,
                                           rowBg);
        if (an > 0 && rx < bodyW) {
            TerminalCanvas_text(body, rx, sy, addBuf, (bytes) an, addPen);
            rx += (bytes) an;
        }
        rx++;
        if (dn > 0 && rx < bodyW) {
            TerminalCanvas_text(body, rx, sy, delBuf, (bytes) dn, delPen);
            rx += (bytes) dn;
        }
        rx += 2u;
        if (tn > 0 && rx < bodyW)
            TerminalCanvas_text(body, rx, sy, timeBuf, (bytes) tn, timePen);
    }
}

/** Paints a centered bordered modal frame with a clickable close pill. */
static fn paintModalFrame(DifferenceTui *tui, TerminalCanvas *root,
                          string title, bytes mw, bytes mh, bool color) {
    bytes width = Tui_width(tui);
    bytes height = Tui_height(tui);
    if (mw > width - 2u)
        mw = width > 4u ? width - 2u : width;
    if (mh > height - 2u)
        mh = height > 4u ? height - 2u : height;
    bytes mx = (width - mw) / 2u;
    bytes my = (height - mh) / 2u;

    Tui_setModalX(tui, mx);
    Tui_setModalY(tui, my);
    Tui_setModalW(tui, mw);
    Tui_setModalH(tui, mh);

    u32 bg = color ? TERMINAL_RGB(24, 30, 42) : TERMINAL_COLOR_DEFAULT;
    u32 borderFg = color ? TERMINAL_RGB(98, 152, 224) : TERMINAL_COLOR_DEFAULT;
    TerminalPen fillPen = TerminalPen_3(0u, color ? TERMINAL_RGB(220, 228, 236) : TERMINAL_COLOR_DEFAULT, bg);
    TerminalPen borderPen = TerminalPen_3(0u, borderFg, bg);
    TerminalPen titlePen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                         color ? TERMINAL_RGB(245, 250, 255) : TERMINAL_COLOR_DEFAULT, bg);

    for (bytes r = 0u; r < mh; r++) {
        for (bytes c = 0u; c < mw; c++)
            TerminalCanvas_glyph(root, mx + c, my + r, (u32) ' ', fillPen);
    }

    TerminalCanvas_glyph(root, mx, my, 0x250Cu, borderPen);
    TerminalCanvas_glyph(root, mx + mw - 1u, my, 0x2510u, borderPen);
    TerminalCanvas_glyph(root, mx, my + mh - 1u, 0x2514u, borderPen);
    TerminalCanvas_glyph(root, mx + mw - 1u, my + mh - 1u, 0x2518u, borderPen);
    for (bytes c = 1u; c + 1u < mw; c++) {
        TerminalCanvas_glyph(root, mx + c, my, 0x2500u, borderPen);
        TerminalCanvas_glyph(root, mx + c, my + mh - 1u, 0x2500u, borderPen);
    }
    for (bytes r = 1u; r + 1u < mh; r++) {
        TerminalCanvas_glyph(root, mx, my + r, 0x2502u, borderPen);
        TerminalCanvas_glyph(root, mx + mw - 1u, my + r, 0x2502u, borderPen);
    }

    bytes tlen = strlen(title);
    if (tlen + 4u < mw)
        TerminalCanvas_text(root, mx + 2u, my, title, tlen, titlePen);

    if (mw >= 8u) {
        bytes cx0 = mx + mw - 5u;
        TerminalPen closePen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                             color ? TERMINAL_RGB(255, 220, 218) : TERMINAL_COLOR_DEFAULT,
                                             color ? TERMINAL_RGB(140, 42, 52) : bg);
        TerminalCanvas_text(root, cx0, my, " x ", 3u, closePen);
        Tui_setModalCloseX0(tui, cx0);
        Tui_setModalCloseX1(tui, cx0 + 3u);
    }
}

/** Paints a label (regular) followed by a highlighted bold keycap pill. */
static bytes paintKeyHint(TerminalCanvas *root, bytes x, bytes y,
                          string label, string keycap,
                          bool active, bool color, u32 barBg) {
    u32 capBg = active ? TERMINAL_RGB(48, 92, 165) : TERMINAL_RGB(40, 48, 64);
    u32 capFg = active ? TERMINAL_RGB(250, 252, 255) : TERMINAL_RGB(214, 226, 240);
    TerminalPen lblPen = TerminalPen_3(0u,
                                       color ? TERMINAL_RGB(165, 176, 192) : TERMINAL_COLOR_DEFAULT,
                                       barBg);
    TerminalPen keyPen = TerminalPen_3(TERMINAL_STYLE_BOLD | ((!color && active) ? TERMINAL_STYLE_REVERSE : 0u),
                                       color ? capFg : TERMINAL_COLOR_DEFAULT,
                                       color ? capBg : barBg);
    bytes llen = strlen(label);
    bytes klen = strlen(keycap);
    TerminalCanvas_text(root, x, y, label, llen, lblPen);
    TerminalCanvas_glyph(root, x + llen, y, (u32) ' ', lblPen);
    TerminalCanvas_text(root, x + llen + 1u, y, keycap, klen, keyPen);
    return llen + 1u + klen;
}

/** Paints the Help shortcuts overlay with left-aligned names and right-aligned keys. */
static fn paintHelpOverlay(DifferenceTui *tui, TerminalCanvas *root, bool color) {
    string names[] = {
        "Expand or collapse diff",
        "Toggle collapse mode (all)",
        "Open or close settings",
        "Select previous / next diff",
        "Scroll code horizontally",
        "Reset horizontal scroll",
        "Scroll viewport",
        "Cycle diff layout",
        "Toggle follow tail",
        "Clear modifications",
        "Quit"
    };
    string keys[] = {
        "enter / space / click",
        "option + o",
        "ctrl + o",
        "k / j  up / down",
        "h / l  left / right",
        "0",
        "pgup / pgdn / wheel",
        "s",
        "f",
        "c",
        "q"
    };
    bytes nlines = sizeof(names) / sizeof(names[0]);
    paintModalFrame(tui, root, " Shortcuts ", 60u, nlines + 5u, color);

    bytes mx = Tui_modalX(tui);
    bytes my = Tui_modalY(tui);
    bytes mw = Tui_modalW(tui);
    bytes mh = Tui_modalH(tui);
    u32 bg = color ? TERMINAL_RGB(24, 30, 42) : TERMINAL_COLOR_DEFAULT;
    TerminalPen namePen = TerminalPen_3(0u, color ? TERMINAL_RGB(210, 220, 232) : TERMINAL_COLOR_DEFAULT, bg);
    TerminalPen keyPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                       color ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT,
                                       color ? TERMINAL_RGB(34, 44, 62) : bg);

    bytes innerRight = mx + mw - 2u;
    for (bytes i = 0u; i < nlines && i + 2u < mh - 2u; i++) {
        bytes ry = my + 2u + i;
        utf8 kbuf[40];
        i32 kn = snprintf(kbuf, sizeof(kbuf), " %s ", keys[i]);
        bytes klen = kn > 0 ? (bytes) kn : 0u;
        bytes kx = innerRight > klen ? innerRight - klen : mx + 2u;
        bytes nlen = strlen(names[i]);
        bytes maxName = kx > mx + 3u ? kx - (mx + 3u) : 0u;
        if (nlen > maxName)
            nlen = maxName;
        if (nlen > 0u)
            TerminalCanvas_text(root, mx + 2u, ry, names[i], nlen, namePen);
        if (klen > 0u && kx + klen <= mx + mw - 1u)
            TerminalCanvas_text(root, kx, ry, kbuf, klen, keyPen);
    }

    if (mh >= 4u && mw >= 24u) {
        bytes fy = my + mh - 2u;
        (discard) paintKeyHint(root, mx + 2u, fy, "Toggle", " ? ", false, color, bg);
        bytes closeW = 6u + 5u;
        if (innerRight >= mx + 2u + closeW)
            (discard) paintKeyHint(root, innerRight - closeW, fy, "Close", " esc ", false, color, bg);
    }
}

/** Paints an editable numeric setting row (either clickable pill + edit icon or live TerminalTextArea). */
static fn paintEditableNumberRow(DifferenceTui *tui, TerminalCanvas *root,
                                 bytes ry, bytes innerRight, bytes minX,
                                 i32 itemIdx, u32 value, string suffix,
                                 bool color, u32 rbg, u16 rstyle,
                                 bytes *outEditX0, bytes *outEditX1,
                                 bytes *outFieldX0, bytes *outFieldX1) {
    bytes suffixLen = suffix ? strlen(suffix) : 0u;
    bool isEditing = (Tui_editingSetting(tui) == itemIdx);
    if (isEditing && Tui_editField(tui)) {
        bytes fieldW = (bytes) EDIT_FIELD_COLS;
        bytes totalW = fieldW + suffixLen;
        bytes fx = innerRight > totalW ? innerRight - totalW : minX;
        u32 fieldBg = color ? TERMINAL_RGB(16, 22, 32) : rbg;
        TerminalPen fieldPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                             color ? TERMINAL_RGB(245, 250, 255) : TERMINAL_COLOR_DEFAULT,
                                             fieldBg);
        TerminalPen caretPen = TerminalPen_3(TERMINAL_STYLE_BOLD | TERMINAL_STYLE_REVERSE,
                                             color ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT,
                                             fieldBg);
        for (bytes c = 0u; c < fieldW; c++)
            TerminalCanvas_glyph(root, fx + c, ry, (u32) ' ', fieldPen);

        TerminalTextArea *field = Tui_editField(tui);
        (discard) TerminalTextArea_setPen(field, fieldPen);
        TextArea_setCursorPen(field, caretPen);
        TerminalCanvas sub = TerminalCanvas_sub(root, fx, ry, fieldW, 1u);
        TerminalTextArea_render(field, &sub);

        if (suffixLen > 0u) {
            TerminalPen sufPen = TerminalPen_3(rstyle,
                                               color ? TERMINAL_RGB(165, 176, 192) : TERMINAL_COLOR_DEFAULT,
                                               rbg);
            TerminalCanvas_text(root, fx + fieldW, ry, suffix, suffixLen, sufPen);
        }
        *outEditX0 = fx;
        *outEditX1 = fx + totalW;
        *outFieldX0 = fx;
        *outFieldX1 = fx + fieldW;
        return;
    }

    utf8 vbuf[32];
    i32 vn = suffixLen > 0u
                 ? snprintf(vbuf, sizeof(vbuf), " %u%s ", (unsigned) value, suffix)
                 : snprintf(vbuf, sizeof(vbuf), " %u ", (unsigned) value);
    bytes vlen = vn > 0 ? (bytes) vn : 0u;
    bytes iconW = 3u;
    bytes totalW = vlen + 1u + iconW;
    bytes vx = innerRight > totalW ? innerRight - totalW : minX;

    TerminalPen valPen = TerminalPen_3(TERMINAL_STYLE_BOLD | rstyle,
                                       color ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                       color ? TERMINAL_RGB(32, 42, 58) : rbg);
    TerminalPen iconPen = TerminalPen_3(TERMINAL_STYLE_BOLD | rstyle,
                                        color ? TERMINAL_RGB(245, 250, 255) : TERMINAL_COLOR_DEFAULT,
                                        color ? TERMINAL_RGB(48, 92, 165) : rbg);
    if (vlen > 0u)
        TerminalCanvas_text(root, vx, ry, vbuf, vlen, valPen);
    bytes ix = vx + vlen + 1u;
    TerminalCanvas_glyph(root, ix, ry, (u32) ' ', iconPen);
    TerminalCanvas_glyph(root, ix + 1u, ry, (u32) EDIT_ICON_GLYPH, iconPen);
    TerminalCanvas_glyph(root, ix + 2u, ry, (u32) ' ', iconPen);

    *outEditX0 = vx;
    *outEditX1 = ix + iconW;
    *outFieldX0 = 0u;
    *outFieldX1 = 0u;
}

/** Paints the Settings overlay with left-aligned labels, circle selection icon, and editable fields. */
static fn paintSettingsOverlay(DifferenceTui *tui, TerminalCanvas *root, bool color) {
    paintModalFrame(tui, root, " Settings ", 60u, 14u, color);

    bytes mx = Tui_modalX(tui);
    bytes my = Tui_modalY(tui);
    bytes mw = Tui_modalW(tui);
    bytes mh = Tui_modalH(tui);
    u32 bg = color ? TERMINAL_RGB(24, 30, 42) : TERMINAL_COLOR_DEFAULT;
    u32 selBg = color ? TERMINAL_RGB(38, 54, 78) : TERMINAL_COLOR_DEFAULT;
    bytes innerRight = mx + mw - 2u;

    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffLayoutMode lm = TerminalDiffPanel_layoutMode(panel);
    TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
    string layoutStr = " AUTO ";
    if (lm == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE)
        layoutStr = " SPLIT ";
    else if (lm == TERMINAL_DIFF_LAYOUT_UNIFIED)
        layoutStr = " UNIFIED ";

    string labels[SETTINGS_ITEM_COUNT] = {
        "Collapse mode",
        "Diff layout",
        "Follow tail",
        "Color",
        "Diff icons",
        "Context lines",
        "Workspace limit",
        "Max edits"
    };

    bytes cur = Tui_settingsCursor(tui);
    for (bytes i = 0u; i < SETTINGS_ITEM_COUNT && 2u + i < mh - 2u; i++) {
        bytes ry = my + 2u + i;
        bool isSel = (i == cur);
        u32 rbg = isSel ? selBg : bg;
        u16 rstyle = (!color && isSel) ? TERMINAL_STYLE_REVERSE : 0u;
        TerminalPen rowPen = TerminalPen_3(rstyle,
                                           color ? TERMINAL_RGB(225, 232, 240) : TERMINAL_COLOR_DEFAULT,
                                           rbg);
        for (bytes c = 1u; c + 1u < mw; c++)
            TerminalCanvas_glyph(root, mx + c, ry, (u32) ' ', rowPen);

        TerminalPen ptrPen = TerminalPen_3(TERMINAL_STYLE_BOLD | rstyle,
                                           color ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT,
                                           rbg);
        TerminalCanvas_glyph(root, mx + 2u, ry, isSel ? (u32) SELECTION_CIRCLE_GLYPH : (u32) ' ', ptrPen);
        TerminalCanvas_text(root, mx + 4u, ry, labels[i], strlen(labels[i]), rowPen);

        if (i == SETTINGS_ITEM_CONTEXT) {
            utf8 nbuf[16];
            i32 nn = snprintf(nbuf, sizeof(nbuf), " %2u ", (unsigned) Tui_contextLines(tui));
            bytes numLen = nn > 0 ? (bytes) nn : 4u;
            bytes ctrlW = 3u + numLen + 3u;
            bytes ctrlX = innerRight > ctrlW ? innerRight - ctrlW : mx + 4u;

            TerminalPen btnPen = TerminalPen_3(TERMINAL_STYLE_BOLD | rstyle,
                                               color ? TERMINAL_RGB(245, 250, 255) : TERMINAL_COLOR_DEFAULT,
                                               color ? TERMINAL_RGB(48, 92, 165) : rbg);
            TerminalPen numPen = TerminalPen_3(TERMINAL_STYLE_BOLD | rstyle,
                                               color ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                               color ? TERMINAL_RGB(32, 42, 58) : rbg);
            TerminalCanvas_text(root, ctrlX, ry, " - ", 3u, btnPen);
            Tui_setCtxMinusX0(tui, ctrlX);
            Tui_setCtxMinusX1(tui, ctrlX + 3u);

            TerminalCanvas_text(root, ctrlX + 3u, ry, nbuf, numLen, numPen);

            bytes plusX = ctrlX + 3u + numLen;
            TerminalCanvas_text(root, plusX, ry, " + ", 3u, btnPen);
            Tui_setCtxPlusX0(tui, plusX);
            Tui_setCtxPlusX1(tui, plusX + 3u);
            Tui_setCtxRowY(tui, ry);
        } else if (i == SETTINGS_ITEM_WORKSPACE_MB) {
            bytes ex0 = 0u, ex1 = 0u, fx0 = 0u, fx1 = 0u;
            paintEditableNumberRow(tui, root, ry, innerRight, mx + 4u,
                                   (i32) SETTINGS_ITEM_WORKSPACE_MB,
                                   Tui_workspaceLimitMb(tui), " MB",
                                   color, rbg, rstyle,
                                   &ex0, &ex1, &fx0, &fx1);
            Tui_setWsEditX0(tui, ex0);
            Tui_setWsEditX1(tui, ex1);
            Tui_setWsRowY(tui, ry);
            Tui_setWsFieldX0(tui, fx0);
            Tui_setWsFieldX1(tui, fx1);
        } else if (i == SETTINGS_ITEM_MAX_EDITS) {
            bytes ex0 = 0u, ex1 = 0u, fx0 = 0u, fx1 = 0u;
            paintEditableNumberRow(tui, root, ry, innerRight, mx + 4u,
                                   (i32) SETTINGS_ITEM_MAX_EDITS,
                                   Tui_maxEdits(tui), "",
                                   color, rbg, rstyle,
                                   &ex0, &ex1, &fx0, &fx1);
            Tui_setEditsEditX0(tui, ex0);
            Tui_setEditsEditX1(tui, ex1);
            Tui_setEditsRowY(tui, ry);
            Tui_setEditsFieldX0(tui, fx0);
            Tui_setEditsFieldX1(tui, fx1);
        } else {
            string valText = " NO ";
            bool isOn = false;
            if (i == 0u) {
                isOn = Tui_collapseMode(tui);
                valText = isOn ? " YES " : " NO ";
            } else if (i == 1u) {
                isOn = true;
                valText = layoutStr;
            } else if (i == 2u) {
                isOn = Tui_followTail(tui);
                valText = isOn ? " YES " : " NO ";
            } else if (i == 3u) {
                isOn = color;
                valText = isOn ? " YES " : " NO ";
            } else {
                isOn = th.showDiffIcons || !color;
                valText = isOn ? " YES " : " NO ";
            }
            bytes vlen = strlen(valText);
            bytes valX = innerRight > vlen ? innerRight - vlen : mx + 4u;
            u32 pillBg = isOn ? TERMINAL_RGB(34, 120, 68) : TERMINAL_RGB(56, 64, 78);
            if (i == 1u)
                pillBg = TERMINAL_RGB(48, 92, 165);
            TerminalPen valPen = TerminalPen_3(TERMINAL_STYLE_BOLD | rstyle,
                                               color ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                               color ? pillBg : rbg);
            TerminalCanvas_text(root, valX, ry, valText, vlen, valPen);
        }
    }

    if (mh >= 4u && mw >= 36u) {
        bytes fy = my + mh - 2u;
        bytes lx = mx + 2u;
        if (Tui_editingSetting(tui) >= 0) {
            lx += paintKeyHint(root, lx, fy, "Save", " enter ", false, color, bg) + 2u;
            bytes cancelW = 7u + 5u;
            if (innerRight >= mx + 2u + cancelW)
                (discard) paintKeyHint(root, innerRight - cancelW, fy, "Cancel", " esc ", false, color, bg);
        } else {
            lx += paintKeyHint(root, lx, fy, "Edit/Toggle", " enter ", false, color, bg) + 2u;
            if (lx + 14u < innerRight - 11u)
                (discard) paintKeyHint(root, lx, fy, "Step", " -/+ ", false, color, bg);
            bytes closeW = 6u + 5u;
            if (innerRight >= mx + 2u + closeW)
                (discard) paintKeyHint(root, innerRight - closeW, fy, "Close", " esc ", false, color, bg);
        }
    }
}

/** Paints one horizontally centered line onto canvas at row y. */
static fn paintCenteredLine(TerminalCanvas *canvas, bytes y, string text, bytes len, TerminalPen pen) {
    bytes w = TerminalCanvas_getWidth(canvas);
    bytes h = TerminalCanvas_getHeight(canvas);
    if (y >= h || w == 0u || len == 0u)
        return;
    if (len > w)
        len = w;
    bytes cx = (w - len) / 2u;
    TerminalCanvas_text(canvas, cx, y, text, len, pen);
}

/** Paints a centered minimum-size notice when the terminal is smaller than 50x20. */
static fn paintTooSmallNotice(TerminalCanvas *root, bytes width, bytes height, bool color) {
    TerminalPen titlePen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                         color ? TERMINAL_RGB(255, 180, 120) : TERMINAL_COLOR_DEFAULT,
                                         TERMINAL_COLOR_DEFAULT);
    TerminalPen subPen = TerminalPen_3(0u, color ? TERMINAL_RGB(160, 172, 188) : TERMINAL_COLOR_DEFAULT,
                                       TERMINAL_COLOR_DEFAULT);
    TerminalPen sizePen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                        color ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT,
                                        TERMINAL_COLOR_DEFAULT);
    utf8 curBuf[48];
    i32 cn = snprintf(curBuf, sizeof(curBuf), "Current: %zux%zu", (size_t) width, (size_t) height);
    bytes startY = height > 3u ? (height - 3u) / 2u : 0u;
    paintCenteredLine(root, startY, "Terminal too small", 18u, titlePen);
    paintCenteredLine(root, startY + 1u, "Minimum size: 50x20", 19u, subPen);
    if (cn > 0)
        paintCenteredLine(root, startY + 2u, curBuf, (bytes) cn, sizePen);
}

/** Paints the centered, word-wrapped welcome screen when no edits have been recorded yet. */
static fn paintWelcomeEmptyState(TerminalCanvas *body, bool color) {
    bytes bodyW = TerminalCanvas_getWidth(body);
    bytes bodyH = TerminalCanvas_getHeight(body);
    string title = "Welcome to Difference";
    string sub = "Anything you, your agent, or anything else modifies in this directory will show up here.";
    string footer = "Happy developing! ^^";

    bytes maxWrap = bodyW > 4u ? bodyW - 4u : bodyW;
    if (maxWrap > 50u)
        maxWrap = 50u;
    if (maxWrap < 12u)
        maxWrap = bodyW;

    string linePtrs[8];
    bytes lineLens[8];
    bytes wrappedCount = 0u;
    string p = sub;
    while (*p && wrappedCount < 8u) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        bytes rem = strlen(p);
        bytes take = rem;
        if (take > maxWrap) {
            bytes cut = maxWrap;
            while (cut > 0u && p[cut] != ' ')
                cut--;
            take = cut > 0u ? cut : maxWrap;
        }
        linePtrs[wrappedCount] = p;
        lineLens[wrappedCount] = take;
        wrappedCount++;
        p += take;
    }

    TerminalPen titlePen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                         color ? TERMINAL_RGB(235, 244, 255) : TERMINAL_COLOR_DEFAULT,
                                         TERMINAL_COLOR_DEFAULT);
    TerminalPen subPen = TerminalPen_3(0u, color ? TERMINAL_RGB(150, 162, 178) : TERMINAL_COLOR_DEFAULT,
                                       TERMINAL_COLOR_DEFAULT);
    TerminalPen footPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                        color ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT,
                                        TERMINAL_COLOR_DEFAULT);

    bytes totalH = 1u + 1u + wrappedCount + 1u + 1u;
    bytes startY = bodyH > totalH ? (bodyH - totalH) / 2u : 0u;
    paintCenteredLine(body, startY, title, strlen(title), titlePen);
    for (bytes i = 0u; i < wrappedCount; i++)
        paintCenteredLine(body, startY + 2u + i, linePtrs[i], lineLens[i], subPen);
    paintCenteredLine(body, startY + 2u + wrappedCount + 1u, footer, strlen(footer), footPen);
}

/** Composes top bar, scrollable collapsible diff feed, bottom bar, and active overlay, then presents. */
fn DifferenceTui_renderFrame(DifferenceTui *tui) {
    if (!tui || Tui_width(tui) == 0u || Tui_height(tui) == 0u)
        return;

    TerminalSurface *surf = Tui_surfaceMut(tui);
    if (!TerminalSurface_isBound(surf))
        return;

    bytes width = Tui_width(tui);
    bytes height = Tui_height(tui);
    TerminalCanvas root = TerminalSurface_getBack(surf);
    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
    bool color = th.useColor;

    TerminalCanvas_fill(&root, (u32) ' ', TerminalPen_0());

    if (width < (bytes) DIFFERENCE_MIN_COLS || height < (bytes) DIFFERENCE_MIN_ROWS) {
        paintTooSmallNotice(&root, width, height, color);
        if (Tui_outFd(tui) >= 0)
            (discard) TerminalSurface_present(surf);
        Tui_setDirty(tui, false);
        return;
    }

    bytes bodyH = height > 2u ? height - 2u : 1u;
    bytes bodyW = width;
    TerminalCanvas body = TerminalCanvas_sub(&root, 0u, 1u, bodyW, bodyH);

    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);

    bytes total = 0u;
    u64 maxSeq = 0u;
    u32 maxAdded = 0u;
    u32 maxRemoved = 0u;
    for (bytes i = 0u; i < count; i++) {
        total += recordBlockRows(panel, &recs[i], bodyW);
        if (Rec_seq(&recs[i]) > maxSeq)
            maxSeq = Rec_seq(&recs[i]);
        DiffResult dv = Rec_diffVal(&recs[i]);
        if (dv.linesAdded > maxAdded)
            maxAdded = dv.linesAdded;
        if (dv.linesRemoved > maxRemoved)
            maxRemoved = dv.linesRemoved;
    }
    u32 maxSeqDigits = digitCountU64(maxSeq);
    u32 maxAddDigits = digitCountU64((u64) maxAdded);
    u32 maxDelDigits = digitCountU64((u64) maxRemoved);
    Tui_setTotalRows(tui, total);

    bytes maxScroll = total > bodyH ? total - bodyH : 0u;
    bytes scroll = Tui_scrollOffset(tui);
    if (Tui_followTail(tui))
        scroll = maxScroll;
    else if (scroll > maxScroll)
        scroll = maxScroll;
    Tui_setScrollOffset(tui, scroll);

    u32 barBg = color ? TERMINAL_RGB(22, 27, 36) : TERMINAL_COLOR_DEFAULT;
    TerminalPen topPen = TerminalPen_3(0u, color ? TERMINAL_RGB(175, 186, 200) : TERMINAL_COLOR_DEFAULT, barBg);
    for (bytes c = 0u; c < width; c++)
        TerminalCanvas_glyph(&root, c, 0u, (u32) ' ', topPen);

    TerminalPen brandPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                         color ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                         color ? TERMINAL_RGB(48, 92, 165) : barBg);
    TerminalCanvas_text(&root, 1u, 0u, " Difference ", 12u, brandPen);

    utf8 filesBuf[40];
    utf8 modsBuf[48];
    utf8 addTotBuf[32];
    utf8 delTotBuf[32];
    i32 fln = snprintf(filesBuf, sizeof(filesBuf), "%zu files  ", (size_t) Tui_trackedFiles(tui));
    i32 mdn = snprintf(modsBuf, sizeof(modsBuf), "%llu modifications  ", (unsigned long long) Tui_editSeq(tui));
    i32 adn = snprintf(addTotBuf, sizeof(addTotBuf), "+%llu", (unsigned long long) Tui_totalAdded(tui));
    i32 dln = snprintf(delTotBuf, sizeof(delTotBuf), "-%llu", (unsigned long long) Tui_totalRemoved(tui));
    bytes rightTopLen = (bytes) (fln > 0 ? fln : 0) + (bytes) (mdn > 0 ? mdn : 0)
                        + (bytes) (adn > 0 ? adn : 0) + 1u + (bytes) (dln > 0 ? dln : 0) + 1u;
    if (width > 14u + rightTopLen) {
        bytes rx = width - rightTopLen;
        if (fln > 0) {
            TerminalCanvas_text(&root, rx, 0u, filesBuf, (bytes) fln, topPen);
            rx += (bytes) fln;
        }
        if (mdn > 0) {
            TerminalCanvas_text(&root, rx, 0u, modsBuf, (bytes) mdn, topPen);
            rx += (bytes) mdn;
        }
        TerminalPen addPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                           color ? TERMINAL_RGB(126, 231, 135) : TERMINAL_COLOR_DEFAULT,
                                           barBg);
        TerminalPen delPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                           color ? TERMINAL_RGB(255, 123, 114) : TERMINAL_COLOR_DEFAULT,
                                           barBg);
        if (adn > 0) {
            TerminalCanvas_text(&root, rx, 0u, addTotBuf, (bytes) adn, addPen);
            rx += (bytes) adn;
        }
        rx++;
        if (dln > 0)
            TerminalCanvas_text(&root, rx, 0u, delTotBuf, (bytes) dln, delPen);
    }

    if (count == 0u)
        paintWelcomeEmptyState(&body, color);
    else {
        bytes cursor = 0u;
        bytes sel = Tui_selectedIndex(tui);
        bytes stickyIdx = count;
        for (bytes i = 0u; i < count; i++) {
            bytes blockH = recordBlockRows(panel, &recs[i], bodyW);
            if (Rec_expanded(&recs[i]) && cursor < scroll && scroll < cursor + blockH)
                stickyIdx = i;
            if (cursor + blockH <= scroll) {
                cursor += blockH;
                continue;
            }
            if (cursor >= scroll + bodyH)
                break;

            if (cursor >= scroll && cursor - scroll < bodyH)
                paintEditRow(&body, cursor - scroll, bodyW, &recs[i], i == sel, color,
                             maxSeqDigits, maxAddDigits, maxDelDigits);
            cursor++;

            if (Rec_expanded(&recs[i])) {
                DiffResult dv = Rec_diffVal(&recs[i]);
                bytes diffRows = TerminalDiffPanel_measureRows(panel, dv.hunks, dv.hunkCount, (u32) bodyW);
                bytes contentRows = diffRows > 0u ? diffRows : 1u;
                if (cursor + contentRows > scroll && cursor < scroll + bodyH) {
                    bytes localScroll = scroll > cursor ? scroll - cursor : 0u;
                    bytes topY = cursor > scroll ? cursor - scroll : 0u;
                    bytes availH = bodyH - topY;
                    bytes subH = contentRows - localScroll;
                    if (subH > availH)
                        subH = availH;
                    TerminalCanvas sub = TerminalCanvas_sub(&body, 0u, topY, bodyW, subH);
                    if (dv.hunkCount > 0u)
                        (discard) TerminalDiffPanel_paintCanvas(panel, nullptr, dv.hunks,
                                                                dv.hunkCount, &sub, localScroll);
                    else {
                        TerminalPen notePen = TerminalPen_3(0u,
                                                            color ? TERMINAL_RGB(139, 148, 158) : TERMINAL_COLOR_DEFAULT,
                                                            TERMINAL_COLOR_DEFAULT);
                        TerminalCanvas_text(&sub, 4u, 0u, "(no text changes)", 17u, notePen);
                    }
                }
                cursor += contentRows + 1u;
            }
        }
        if (stickyIdx < count && bodyH > 0u)
            paintEditRow(&body, 0u, bodyW, &recs[stickyIdx], true, color,
                         maxSeqDigits, maxAddDigits, maxDelDigits);
    }

    bytes botY = height - 1u;
    TerminalPen botPen = TerminalPen_3(0u, color ? TERMINAL_RGB(150, 160, 174) : TERMINAL_COLOR_DEFAULT, barBg);
    for (bytes c = 0u; c < width; c++)
        TerminalCanvas_glyph(&root, c, botY, (u32) ' ', botPen);

    bytes helpBtnW = 4u + 1u + 3u;
    bytes setBtnW = 8u + 1u + 10u;
    bytes rightBotLen = helpBtnW + 2u + setBtnW + 1u;
    bytes rightBotX = width > rightBotLen ? width - rightBotLen : 0u;

    string watchRoot = Tui_watchRoot(tui);
    bytes dirLen = strlen(watchRoot);
    if (dirLen > 0u && rightBotX > 2u) {
        bytes maxDir = rightBotX - 2u;
        bytes drawDir = dirLen < maxDir ? dirLen : maxDir;
        TerminalCanvas_text(&root, 1u, botY, watchRoot, drawDir, topPen);
    }

    if (rightBotX + helpBtnW <= width) {
        bool helpActive = (Tui_overlay(tui) == DIFFERENCE_OVERLAY_HELP);
        bytes w = paintKeyHint(&root, rightBotX, botY, "Help", " ? ", helpActive, color, barBg);
        Tui_setHelpBtnX0(tui, rightBotX);
        Tui_setHelpBtnX1(tui, rightBotX + w);
    }
    bytes setX = rightBotX + helpBtnW + 2u;
    if (setX + setBtnW <= width) {
        bool setActive = (Tui_overlay(tui) == DIFFERENCE_OVERLAY_SETTINGS);
        bytes w = paintKeyHint(&root, setX, botY, "Settings", " ctrl + o ", setActive, color, barBg);
        Tui_setSettingsBtnX0(tui, setX);
        Tui_setSettingsBtnX1(tui, setX + w);
    }

    if (Tui_overlay(tui) == DIFFERENCE_OVERLAY_HELP)
        paintHelpOverlay(tui, &root, color);
    else if (Tui_overlay(tui) == DIFFERENCE_OVERLAY_SETTINGS)
        paintSettingsOverlay(tui, &root, color);

    if (Tui_outFd(tui) >= 0)
        (discard) TerminalSurface_present(surf);
    Tui_setDirty(tui, false);
}

/** Scrolls viewport up by delta lines and pauses follow-tail mode. */
static fn scrollUp(DifferenceTui *tui, bytes delta) {
    bytes cur = Tui_scrollOffset(tui);
    Tui_setScrollOffset(tui, cur > delta ? cur - delta : 0u);
    Tui_setFollowTail(tui, false);
    Tui_setDirty(tui, true);
}

/** Scrolls viewport down by delta lines and re-engages follow-tail at the bottom. */
static fn scrollDown(DifferenceTui *tui, bytes delta) {
    bytes bodyH = Tui_height(tui) > 2u ? Tui_height(tui) - 2u : 1u;
    bytes total = Tui_totalRows(tui);
    bytes maxScroll = total > bodyH ? total - bodyH : 0u;
    bytes next = Tui_scrollOffset(tui) + delta;
    if (next >= maxScroll) {
        Tui_setScrollOffset(tui, maxScroll);
        Tui_setFollowTail(tui, true);
    } else
        Tui_setScrollOffset(tui, next);
    Tui_setDirty(tui, true);
}

/** Moves the active feed selection up or down by one edit record. */
static fn moveSelection(DifferenceTui *tui, i32 delta) {
    bytes count = Tui_recordCount(tui);
    if (count == 0u)
        return;
    bytes sel = Tui_selectedIndex(tui);
    if (delta < 0) {
        if (sel > 0u)
            sel--;
        Tui_setFollowTail(tui, false);
    } else if (delta > 0) {
        if (sel + 1u < count)
            sel++;
        if (sel + 1u == count)
            Tui_setFollowTail(tui, true);
    }
    Tui_setSelectedIndex(tui, sel);
    ensureSelectedVisible(tui);
    Tui_setDirty(tui, true);
}

/** Toggles expand/collapse on the currently selected edit record. */
static fn toggleSelectedRecord(DifferenceTui *tui) {
    bytes count = Tui_recordCount(tui);
    if (count == 0u)
        return;
    bytes sel = Tui_selectedIndex(tui);
    if (sel >= count)
        sel = count - 1u;
    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    Rec_setExpanded(&recs[sel], !Rec_expanded(&recs[sel]));
    Tui_setFollowTail(tui, false);
    ensureSelectedVisible(tui);
    Tui_setDirty(tui, true);
}

/** Dispatches a left mouse click at 0-based screen cell (x, y). */
static fn handleLeftClick(DifferenceTui *tui, bytes x, bytes y) {
    bytes height = Tui_height(tui);
    if (height == 0u)
        return;

    if (y == height - 1u) {
        cancelSettingEdit(tui);
        if (x >= Tui_helpBtnX0(tui) && x < Tui_helpBtnX1(tui)) {
            Tui_setOverlay(tui, Tui_overlay(tui) == DIFFERENCE_OVERLAY_HELP
                                    ? DIFFERENCE_OVERLAY_NONE
                                    : DIFFERENCE_OVERLAY_HELP);
            Tui_setDirty(tui, true);
            return;
        }
        if (x >= Tui_settingsBtnX0(tui) && x < Tui_settingsBtnX1(tui)) {
            Tui_setOverlay(tui, Tui_overlay(tui) == DIFFERENCE_OVERLAY_SETTINGS
                                    ? DIFFERENCE_OVERLAY_NONE
                                    : DIFFERENCE_OVERLAY_SETTINGS);
            Tui_setDirty(tui, true);
            return;
        }
    }

    DifferenceOverlayMode ov = Tui_overlay(tui);
    if (ov != DIFFERENCE_OVERLAY_NONE) {
        bytes mx = Tui_modalX(tui);
        bytes my = Tui_modalY(tui);
        bytes mw = Tui_modalW(tui);
        bytes mh = Tui_modalH(tui);
        bool inside = (x >= mx && x < mx + mw && y >= my && y < my + mh);
        bool hitClose = (y == my && x >= Tui_modalCloseX0(tui) && x < Tui_modalCloseX1(tui));
        if (!inside || hitClose) {
            commitSettingEdit(tui);
            Tui_setOverlay(tui, DIFFERENCE_OVERLAY_NONE);
            Tui_setDirty(tui, true);
            return;
        }
        if (ov == DIFFERENCE_OVERLAY_SETTINGS && y >= my + 2u && y < my + 2u + SETTINGS_ITEM_COUNT) {
            bytes item = y - (my + 2u);
            if (Tui_editingSetting(tui) == (i32) item && Tui_editField(tui)) {
                bytes fx0 = (item == SETTINGS_ITEM_WORKSPACE_MB) ? Tui_wsFieldX0(tui) : Tui_editsFieldX0(tui);
                bytes fx1 = (item == SETTINGS_ITEM_WORKSPACE_MB) ? Tui_wsFieldX1(tui) : Tui_editsFieldX1(tui);
                if (x >= fx0 && x < fx1) {
                    (discard) TerminalTextArea_setCursorByte(Tui_editField(tui), x - fx0);
                    Tui_setDirty(tui, true);
                    return;
                }
            }
            if (Tui_editingSetting(tui) >= 0)
                commitSettingEdit(tui);
            Tui_setSettingsCursor(tui, item);
            if (item == SETTINGS_ITEM_CONTEXT && y == Tui_ctxRowY(tui)) {
                if (x >= Tui_ctxMinusX0(tui) && x < Tui_ctxMinusX1(tui))
                    adjustContextLines(tui, -1);
                else if (x >= Tui_ctxPlusX0(tui) && x < Tui_ctxPlusX1(tui))
                    adjustContextLines(tui, 1);
                else
                    Tui_setDirty(tui, true);
            } else if (item == SETTINGS_ITEM_WORKSPACE_MB && y == Tui_wsRowY(tui)) {
                if (x >= Tui_wsEditX0(tui) && x < Tui_wsEditX1(tui))
                    beginSettingEdit(tui, (i32) SETTINGS_ITEM_WORKSPACE_MB);
                else
                    Tui_setDirty(tui, true);
            } else if (item == SETTINGS_ITEM_MAX_EDITS && y == Tui_editsRowY(tui)) {
                if (x >= Tui_editsEditX0(tui) && x < Tui_editsEditX1(tui))
                    beginSettingEdit(tui, (i32) SETTINGS_ITEM_MAX_EDITS);
                else
                    Tui_setDirty(tui, true);
            } else
                activateSetting(tui, item, 0);
        }
        return;
    }

    if (y >= 1u && y < height - 1u) {
        bytes scroll = Tui_scrollOffset(tui);
        bytes targetRow = scroll + (y - 1u);
        bytes bodyW = Tui_width(tui);
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        DifferenceEditRecord *recs = Tui_recordsMut(tui);
        bytes count = Tui_recordCount(tui);
        bytes cursor = 0u;
        for (bytes i = 0u; i < count; i++) {
            bytes blockH = recordBlockRows(panel, &recs[i], bodyW);
            bool isStickyClick = (y == 1u && Rec_expanded(&recs[i]) && cursor < scroll && scroll < cursor + blockH);
            if (targetRow == cursor || isStickyClick) {
                Tui_setSelectedIndex(tui, i);
                Rec_setExpanded(&recs[i], !Rec_expanded(&recs[i]));
                if (isStickyClick)
                    Tui_setScrollOffset(tui, cursor);
                Tui_setFollowTail(tui, false);
                Tui_setDirty(tui, true);
                return;
            }
            if (targetRow > cursor && targetRow < cursor + blockH) {
                Tui_setSelectedIndex(tui, i);
                Tui_setDirty(tui, true);
                return;
            }
            cursor += blockH;
        }
    }
}

/** Handles one plain keyboard byte (or feeds mouse bytes through TerminalInput). */
fn DifferenceTui_handleInputByte(DifferenceTui *tui, i32 byte) {
    if (!tui)
        return;

    if (Tui_utf8C3Pending(tui)) {
        Tui_setUtf8C3Pending(tui, false);
        if (byte == 0xB8 || byte == 0x98) {
            toggleCollapseMode(tui);
            return;
        }
    }

    TerminalInput *inp = Tui_inputMut(tui);
    if (TerminalInput_feed(inp, byte)) {
        TerminalPointerEvent ev = TerminalInput_getEvent(inp);
        if (ev.kind == TERMINAL_POINTER_WHEEL_UP)
            scrollUp(tui, 3u);
        else if (ev.kind == TERMINAL_POINTER_WHEEL_DOWN)
            scrollDown(tui, 3u);
        else if (ev.kind == TERMINAL_POINTER_WHEEL_LEFT) {
            TerminalDiffPanel *panel = Tui_panelMut(tui);
            bytes sx = TerminalDiffPanel_scrollX(panel);
            TerminalDiffPanel_setScrollX(panel, sx > 4u ? sx - 4u : 0u);
            Tui_setDirty(tui, true);
        } else if (ev.kind == TERMINAL_POINTER_WHEEL_RIGHT) {
            TerminalDiffPanel *panel = Tui_panelMut(tui);
            TerminalDiffPanel_setScrollX(panel, TerminalDiffPanel_scrollX(panel) + 4u);
            Tui_setDirty(tui, true);
        } else if (ev.kind == TERMINAL_POINTER_PRESS && ev.button == TERMINAL_BUTTON_LEFT)
            handleLeftClick(tui, ev.x, ev.y);
        return;
    }
    if (TerminalInput_isPending(inp))
        return;

    if (byte == 0xC3) {
        Tui_setUtf8C3Pending(tui, true);
        return;
    }

    if (Tui_overlay(tui) == DIFFERENCE_OVERLAY_SETTINGS && Tui_editingSetting(tui) >= 0) {
        TerminalTextArea *field = Tui_editField(tui);
        if (byte == 3) {
            Tui_setRunning(tui, false);
            return;
        }
        if (byte == 27) {
            cancelSettingEdit(tui);
            return;
        }
        if (byte == '\r' || byte == '\n') {
            commitSettingEdit(tui);
            return;
        }
        if (field) {
            if (byte == 0x7F || byte == 0x08) {
                if (TerminalTextArea_backspace(field))
                    Tui_setDirty(tui, true);
            } else if (byte == 1) {
                (discard) TerminalTextArea_setCursorByte(field, 0u);
                Tui_setDirty(tui, true);
            } else if (byte == 5) {
                (discard) TerminalTextArea_setCursorByte(field, TerminalTextArea_getLength(field));
                Tui_setDirty(tui, true);
            } else if (byte >= '0' && byte <= '9' && TerminalTextArea_getLength(field) < (bytes) EDIT_FIELD_MAX_DIGITS) {
                if (TerminalTextArea_insertChar(field, byte))
                    Tui_setDirty(tui, true);
            }
        }
        return;
    }

    if (byte == 15) {
        cancelSettingEdit(tui);
        Tui_setOverlay(tui, Tui_overlay(tui) == DIFFERENCE_OVERLAY_SETTINGS
                                ? DIFFERENCE_OVERLAY_NONE
                                : DIFFERENCE_OVERLAY_SETTINGS);
        Tui_setDirty(tui, true);
        return;
    }

    if (byte == '?') {
        cancelSettingEdit(tui);
        Tui_setOverlay(tui, Tui_overlay(tui) == DIFFERENCE_OVERLAY_HELP
                                ? DIFFERENCE_OVERLAY_NONE
                                : DIFFERENCE_OVERLAY_HELP);
        Tui_setDirty(tui, true);
        return;
    }

    if (byte == 27) {
        if (Tui_overlay(tui) != DIFFERENCE_OVERLAY_NONE) {
            Tui_setOverlay(tui, DIFFERENCE_OVERLAY_NONE);
            Tui_setDirty(tui, true);
        }
        return;
    }

    if (byte == 'q' || byte == 'Q' || byte == 3) {
        Tui_setRunning(tui, false);
        return;
    }

    DifferenceOverlayMode ov = Tui_overlay(tui);
    if (ov == DIFFERENCE_OVERLAY_HELP)
        return;
    if (ov == DIFFERENCE_OVERLAY_SETTINGS) {
        bytes cur = Tui_settingsCursor(tui);
        if (byte == 'j') {
            Tui_setSettingsCursor(tui, (cur + 1u) % SETTINGS_ITEM_COUNT);
            Tui_setDirty(tui, true);
        } else if (byte == 'k') {
            Tui_setSettingsCursor(tui, cur > 0u ? cur - 1u : SETTINGS_ITEM_COUNT - 1u);
            Tui_setDirty(tui, true);
        } else if (byte == '\r' || byte == '\n' || byte == ' ')
            activateSetting(tui, cur, 0);
        else if (byte == '+' || byte == '=')
            activateSetting(tui, cur, 1);
        else if (byte == '-' || byte == '_')
            activateSetting(tui, cur, -1);
        return;
    }

    bytes bodyH = Tui_height(tui) > 2u ? Tui_height(tui) - 2u : 1u;
    bytes halfPage = bodyH > 2u ? bodyH / 2u : 1u;

    if (byte == 'j')
        moveSelection(tui, 1);
    else if (byte == 'k')
        moveSelection(tui, -1);
    else if (byte == 'h') {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        bytes sx = TerminalDiffPanel_scrollX(panel);
        TerminalDiffPanel_setScrollX(panel, sx > 4u ? sx - 4u : 0u);
        Tui_setDirty(tui, true);
    } else if (byte == 'l') {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        TerminalDiffPanel_setScrollX(panel, TerminalDiffPanel_scrollX(panel) + 4u);
        Tui_setDirty(tui, true);
    } else if (byte == '0') {
        TerminalDiffPanel_setScrollX(Tui_panelMut(tui), 0u);
        Tui_setDirty(tui, true);
    } else if (byte == '\r' || byte == '\n' || byte == ' ')
        toggleSelectedRecord(tui);
    else if (byte == 'd')
        scrollDown(tui, halfPage);
    else if (byte == 'u')
        scrollUp(tui, halfPage);
    else if (byte == 'g') {
        Tui_setSelectedIndex(tui, 0u);
        Tui_setScrollOffset(tui, 0u);
        Tui_setFollowTail(tui, false);
        Tui_setDirty(tui, true);
    } else if (byte == 'G') {
        bytes count = Tui_recordCount(tui);
        if (count > 0u)
            Tui_setSelectedIndex(tui, count - 1u);
        Tui_setFollowTail(tui, true);
        Tui_setDirty(tui, true);
    } else if (byte == 'f' || byte == 'F') {
        Tui_setFollowTail(tui, !Tui_followTail(tui));
        Tui_setDirty(tui, true);
        notifySettingsChanged(tui);
    } else if (byte == 'c' || byte == 'C')
        DifferenceTui_clearEdits(tui);
    else if (byte == 's' || byte == 'S')
        cycleDiffLayout(tui);
}

/** Parses Ghostty/Mode-2048 in-band resize report ESC [ 48 ; rows ; cols ... t. */
static bool tryParseInBandResize(DifferenceTui *tui, string seq, bytes len) {
    if (len < 7u || seq[0] != '\x1b' || seq[1] != '[' || seq[len - 1u] != 't')
        return false;
    if (seq[2] != '4' || seq[3] != '8' || seq[4] != ';')
        return false;
    bytes rows = 0u;
    bytes cols = 0u;
    bytes i = 5u;
    while (i < len && seq[i] >= '0' && seq[i] <= '9') {
        rows = rows * 10u + (bytes) (seq[i] - '0');
        i++;
    }
    if (i >= len || seq[i] != ';')
        return false;
    i++;
    while (i < len && seq[i] >= '0' && seq[i] <= '9') {
        cols = cols * 10u + (bytes) (seq[i] - '0');
        i++;
    }
    if (rows == 0u || cols == 0u)
        return false;
    if (cols != Tui_width(tui) || rows != Tui_height(tui)) {
        (discard) DifferenceTui_resize(tui, cols, rows);
        DifferenceTui_renderFrame(tui);
    }
    return true;
}

/** Handles bare Esc, ANSI arrows, Option+O, PageUp/PageDown, and Mode-2048 resize sequences. */
fn DifferenceTui_handleKeySeq(DifferenceTui *tui, string seq, bytes len) {
    if (!tui || !seq || len == 0u || seq[0] != '\x1b')
        return;

    if (len == 1u) {
        if (Tui_editingSetting(tui) >= 0) {
            cancelSettingEdit(tui);
            return;
        }
        if (Tui_overlay(tui) != DIFFERENCE_OVERLAY_NONE) {
            Tui_setOverlay(tui, DIFFERENCE_OVERLAY_NONE);
            Tui_setDirty(tui, true);
        }
        return;
    }

    if (len == 2u && (seq[1] == 'o' || seq[1] == 'O')) {
        toggleCollapseMode(tui);
        return;
    }

    if (tryParseInBandResize(tui, seq, len))
        return;

    if (len < 3u || seq[1] != '[')
        return;

    DifferenceOverlayMode ov = Tui_overlay(tui);
    if (ov == DIFFERENCE_OVERLAY_SETTINGS) {
        if (Tui_editingSetting(tui) >= 0) {
            TerminalTextArea *field = Tui_editField(tui);
            if (!field)
                return;
            if (len == 3u && seq[2] == 'D') {
                TerminalTextArea_moveCursor(field, 0, -1);
                Tui_setDirty(tui, true);
            } else if (len == 3u && seq[2] == 'C') {
                TerminalTextArea_moveCursor(field, 0, 1);
                Tui_setDirty(tui, true);
            } else if (len == 3u && seq[2] == 'H') {
                (discard) TerminalTextArea_setCursorByte(field, 0u);
                Tui_setDirty(tui, true);
            } else if (len == 3u && seq[2] == 'F') {
                (discard) TerminalTextArea_setCursorByte(field, TerminalTextArea_getLength(field));
                Tui_setDirty(tui, true);
            } else if (len == 4u && seq[2] == '3' && seq[3] == '~') {
                if (TerminalTextArea_delete(field))
                    Tui_setDirty(tui, true);
            }
            return;
        }
        bytes cur = Tui_settingsCursor(tui);
        if (len == 3u && seq[2] == 'A') {
            Tui_setSettingsCursor(tui, cur > 0u ? cur - 1u : SETTINGS_ITEM_COUNT - 1u);
            Tui_setDirty(tui, true);
        } else if (len == 3u && seq[2] == 'B') {
            Tui_setSettingsCursor(tui, (cur + 1u) % SETTINGS_ITEM_COUNT);
            Tui_setDirty(tui, true);
        } else if (len == 3u && seq[2] == 'C')
            activateSetting(tui, cur, 1);
        else if (len == 3u && seq[2] == 'D')
            activateSetting(tui, cur, -1);
        return;
    }
    if (ov == DIFFERENCE_OVERLAY_HELP)
        return;

    bytes bodyH = Tui_height(tui) > 2u ? Tui_height(tui) - 2u : 1u;
    if (len == 3u && seq[2] == 'A')
        moveSelection(tui, -1);
    else if (len == 3u && seq[2] == 'B')
        moveSelection(tui, 1);
    else if (len == 3u && seq[2] == 'C') {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        TerminalDiffPanel_setScrollX(panel, TerminalDiffPanel_scrollX(panel) + 4u);
        Tui_setDirty(tui, true);
    } else if (len == 3u && seq[2] == 'D') {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        bytes sx = TerminalDiffPanel_scrollX(panel);
        TerminalDiffPanel_setScrollX(panel, sx > 4u ? sx - 4u : 0u);
        Tui_setDirty(tui, true);
    } else if (len == 4u && seq[2] == '5' && seq[3] == '~')
        scrollUp(tui, bodyH);
    else if (len == 4u && seq[2] == '6' && seq[3] == '~')
        scrollDown(tui, bodyH);
}

/** Runs the interactive TUI loop with Mode-2048/SIGWINCH instant transactional resize. */
i32 DifferenceTui_run(DifferenceTui *tui, FileEventWatcher *watcher) {
    if (!tui || !watcher)
        return 1;
    TerminalCompositor *comp = Tui_compositor(tui);
    if (!comp)
        return 1;

    struct termios origTerm;
    bool haveTerm = (tcgetattr(STDIN_FILENO, &origTerm) == 0);
    if (haveTerm) {
        struct termios raw = origTerm;
        raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN);
        raw.c_iflag &= (tcflag_t) ~(IXON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    atomic_store_explicit(&g_tuiStop, false, memory_order_release);
    atomic_store_explicit(&g_tuiResized, false, memory_order_release);
    installSignal(SIGINT, onTuiStop);
    installSignal(SIGTERM, onTuiStop);
    installSignal(SIGWINCH, onTuiResize);

    (discard) TerminalCompositor_enter(comp);
    string modesOn = "\x1b[?1000h\x1b[?1006h\x1b[?2048h";
    writeAll(Tui_outFd(tui), modesOn, strlen(modesOn));

    bytes w = 80u;
    bytes h = 24u;
    queryTermSize(&w, &h);
    (discard) DifferenceTui_resize(tui, w, h);
    Tui_setRunning(tui, true);
    DifferenceTui_renderFrame(tui);

    while (Tui_running(tui) && !atomic_load_explicit(&g_tuiStop, memory_order_acquire)) {
        if (atomic_exchange_explicit(&g_tuiResized, false, memory_order_acq_rel)) {
            queryTermSize(&w, &h);
            if (w != Tui_width(tui) || h != Tui_height(tui)) {
                (discard) DifferenceTui_resize(tui, w, h);
                DifferenceTui_renderFrame(tui);
            }
        }

        FileEventWatcher_pump(watcher);

        bool idlePending = false;
        DifferenceTuiIdleFn idleHook = Tui_idleHook(tui);
        if (idleHook) {
            idlePending = idleHook(Tui_idleUserdata(tui));
            if (!idlePending)
                Tui_setIdleHookField(tui, nullptr);
        }

        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, idlePending ? 0 : 25) > 0 && (pfd.revents & POLLIN)) {
            utf8 buf[64];
            i64 n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                bytes nb = (bytes) n;
                if (nb == 1u && buf[0] == '\x1b')
                    DifferenceTui_handleKeySeq(tui, buf, nb);
                else if (nb == 2u && buf[0] == '\x1b' && (buf[1] == 'o' || buf[1] == 'O'))
                    DifferenceTui_handleKeySeq(tui, buf, nb);
                else if (nb >= 3u && buf[0] == '\x1b' && buf[1] == '[' && buf[2] != '<' && buf[2] != 'M')
                    DifferenceTui_handleKeySeq(tui, buf, nb);
                else
                    for (bytes i = 0; i < nb; i++)
                        DifferenceTui_handleInputByte(tui, (u8) buf[i]);
            }
        }

        if (atomic_exchange_explicit(&g_tuiResized, false, memory_order_acq_rel)) {
            queryTermSize(&w, &h);
            if (w != Tui_width(tui) || h != Tui_height(tui)) {
                (discard) DifferenceTui_resize(tui, w, h);
                DifferenceTui_renderFrame(tui);
            }
        }

        if (Tui_dirty(tui))
            DifferenceTui_renderFrame(tui);
    }

    string modesOff = "\x1b[?2048l\x1b[?1006l\x1b[?1000l";
    writeAll(Tui_outFd(tui), modesOff, strlen(modesOff));
    (discard) TerminalCompositor_leave(comp);
    if (haveTerm)
        tcsetattr(STDIN_FILENO, TCSANOW, &origTerm);
    return 0;
}
