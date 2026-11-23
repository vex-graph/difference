#include "vex.h"
#include "difference_printer.h"

#include <string.h>
#include <time.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: DifferencePrinter
 * ============================================================================
 * Custom realtime file-watch diff printer for difference. Layers a custom
 * watch-event header (sequence index, color-coded event pill, timestamp,
 * byte size, +/- visual stat bar, and responsive layout tag) over
 * determinant's TerminalDiffPanel.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DifferencePrinter (difference/difference_printer.c)
 * ============================================================================
 * Formats live file edits as a scrollable terminal log, delegating responsive
 * side-by-side vs unified hunk rendering and inline word/letter highlighting
 * to determinant's TerminalDiffPanel.
 * ============================================================================
 */

;;GETTER
static inline TerminalDiffPanel *Printer_panelMut(DifferencePrinter *p)
{
    return &(*p).panel;
}
;;GETTER
static inline u64 Printer_editCounter(const DifferencePrinter *p)
{
    return (*p).editCounter;
}
;;SETTER
static inline fn Printer_setEditCounter(DifferencePrinter *p, u64 v)
{
    (*p).editCounter = v;
}
;;GETTER
static inline i64 Printer_lastFileSize(const DifferencePrinter *p)
{
    return (*p).lastFileSize;
}
;;SETTER
static inline fn Printer_setLastFileSize(DifferencePrinter *p, i64 v)
{
    (*p).lastFileSize = v;
}
;;GETTER
static inline FileEventType Printer_lastEventType(const DifferencePrinter *p)
{
    return (*p).lastEventType;
}
;;SETTER
static inline fn Printer_setLastEventType(DifferencePrinter *p, FileEventType t)
{
    (*p).lastEventType = t;
}
;;GETTER
static inline u32 Printer_maxAddDigits(const DifferencePrinter *p)
{
    return (*p).maxAddDigits;
}
;;SETTER
static inline fn Printer_setMaxAddDigits(DifferencePrinter *p, u32 d)
{
    (*p).maxAddDigits = d;
}
;;GETTER
static inline u32 Printer_maxDelDigits(const DifferencePrinter *p)
{
    return (*p).maxDelDigits;
}
;;SETTER
static inline fn Printer_setMaxDelDigits(DifferencePrinter *p, u32 d)
{
    (*p).maxDelDigits = d;
}

/** Returns the decimal digit length of v (minimum 1). */
static u32 printerDigitCount(u64 v) {
    u32 d = 1u;
    while (v >= 10u) {
        v /= 10u;
        d++;
    }
    return d;
}

;;GETTER
static string WatchEv_rel(const FileWatchEvent *e)
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
static inline string Meta_path(const TerminalDiffMeta *m)
{
    return (*m).path;
}
;;GETTER
static inline string Meta_event(const TerminalDiffMeta *m)
{
    return (*m).eventLabel;
}
;;GETTER
static inline string Meta_timestamp(const TerminalDiffMeta *m)
{
    return (*m).timestamp;
}
;;GETTER
static inline u32 Meta_added(const TerminalDiffMeta *m)
{
    return (*m).linesAdded;
}
;;GETTER
static inline u32 Meta_removed(const TerminalDiffMeta *m)
{
    return (*m).linesRemoved;
}
;;GETTER
static inline const TerminalDiffHunk *Diff_hunks(const DiffResult *d)
{
    return (*d).hunks;
}
;;GETTER
static inline bytes Diff_hunkCount(const DiffResult *d)
{
    return (*d).hunkCount;
}
;;GETTER
static inline u32 Diff_added(const DiffResult *d)
{
    return (*d).linesAdded;
}
;;GETTER
static inline u32 Diff_removed(const DiffResult *d)
{
    return (*d).linesRemoved;
}

/** Emits a 24-bit foreground SGR sequence when color is active. */
static fn emitRgbFg(FILE *out, bool useColor, u32 c) {
    if (!useColor || !TERMINAL_COLOR_IS_SET(c))
        return;
    utf8 sgr[32];
    snprintf(sgr, sizeof(sgr), "\x1b[38;2;%u;%u;%um",
             (u32) TERMINAL_COLOR_RED(c),
             (u32) TERMINAL_COLOR_GREEN(c),
             (u32) TERMINAL_COLOR_BLUE(c));
    fputs(sgr, out);
}

/** Emits a 24-bit background SGR sequence when color is active. */
static fn emitRgbBg(FILE *out, bool useColor, u32 c) {
    if (!useColor || !TERMINAL_COLOR_IS_SET(c))
        return;
    utf8 sgr[32];
    snprintf(sgr, sizeof(sgr), "\x1b[48;2;%u;%u;%um",
             (u32) TERMINAL_COLOR_RED(c),
             (u32) TERMINAL_COLOR_GREEN(c),
             (u32) TERMINAL_COLOR_BLUE(c));
    fputs(sgr, out);
}

/** Maps FileEventType bits to a compact pill label. */
static string eventPillLabel(FileEventType type) {
    if (type & FILE_EVENT_DELETED)
        return "DEL";
    if (type & FILE_EVENT_CREATED)
        return "NEW";
    if (type & FILE_EVENT_RENAMED)
        return "MOVE";
    return "EDIT";
}

/** Picks pill background color for a FileEventType. */
static u32 eventPillBg(FileEventType type) {
    if (type & FILE_EVENT_DELETED)
        return TERMINAL_RGB(165, 42, 52);
    if (type & FILE_EVENT_CREATED)
        return TERMINAL_RGB(34, 120, 68);
    if (type & FILE_EVENT_RENAMED)
        return TERMINAL_RGB(48, 92, 165);
    return TERMINAL_RGB(178, 118, 28);
}

/** Formats wall-clock epoch seconds into local HH:MM:SS. */
static fn formatTimestamp(i64 epochSec, utf8 *buf, bytes cap) {
    timestamp t = epochSec > 0 ? (timestamp) epochSec : time(nullptr);
    struct tm tmBuf;
    if (localtime_r(&t, &tmBuf))
        strftime(buf, cap, "%H:%M:%S", &tmBuf);
    else
        snprintf(buf, cap, "00:00:00");
}

/** Custom header hook rendering difference's event pill, path, timestamp, size, and +/- bar. */
static fn differenceHeaderHook(const TerminalDiffPanel *panel,
                               const TerminalDiffMeta *meta,
                               u32 columns,
                               TerminalDiffLayoutMode resolvedLayout,
                               FILE *out,
                               any userdata) {
    DifferencePrinter *pr = (DifferencePrinter*) userdata;
    TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
    bool color = th.useColor;
    bool uni = th.useUnicodeBorder;
    string path = (meta && Meta_path(meta)) ? Meta_path(meta) : "(unnamed)";
    string ev = (meta && Meta_event(meta)) ? Meta_event(meta) : "EDIT";
    string ts = (meta && Meta_timestamp(meta)) ? Meta_timestamp(meta) : "";
    u32 added = meta ? Meta_added(meta) : 0u;
    u32 removed = meta ? Meta_removed(meta) : 0u;
    u64 seq = pr ? Printer_editCounter(pr) : 0u;
    i64 sizeBytes = pr ? Printer_lastFileSize(pr) : -1;
    FileEventType evType = pr ? Printer_lastEventType(pr) : FILE_EVENT_MODIFIED;
    string layoutTag = (resolvedLayout == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE) ? "side-by-side" : "unified";

    fputc('\n', out);
    emitRgbFg(out, color, TERMINAL_RGB(88, 96, 112));
    for (u32 i = 0; i < columns; i++)
        fputs(uni ? "━" : "=", out);
    if (color)
        fputs("\x1b[0m", out);
    fputc('\n', out);

    u32 addW = (pr && Printer_maxAddDigits(pr) > 0u) ? Printer_maxAddDigits(pr) : printerDigitCount(added);
    u32 delW = (pr && Printer_maxDelDigits(pr) > 0u) ? Printer_maxDelDigits(pr) : printerDigitCount(removed);
    utf8 addStr[24];
    utf8 delStr[24];
    snprintf(addStr, sizeof(addStr), "+%u", (unsigned) added);
    snprintf(delStr, sizeof(delStr), "-%u", (unsigned) removed);

    emitRgbFg(out, color, TERMINAL_RGB(130, 140, 156));
    utf8 seg[128];
    snprintf(seg, sizeof(seg), "#%" PRIu64 " ", seq);
    fputs(seg, out);

    emitRgbBg(out, color, eventPillBg(evType));
    emitRgbFg(out, color, TERMINAL_RGB(250, 252, 255));
    if (color)
        fputs("\x1b[1m", out);
    fputc(' ', out);
    fputs(ev, out);
    fputc(' ', out);
    if (color)
        fputs("\x1b[0m", out);

    emitRgbFg(out, color, TERMINAL_RGB(240, 246, 252));
    if (color)
        fputs("\x1b[1m", out);
    fputc(' ', out);
    fputs(path, out);
    if (color)
        fputs("\x1b[0m", out);

    emitRgbFg(out, color, TERMINAL_RGB(126, 231, 135));
    snprintf(seg, sizeof(seg), "  %*s", (i32) (addW + 1u), addStr);
    fputs(seg, out);
    emitRgbFg(out, color, TERMINAL_RGB(255, 123, 114));
    snprintf(seg, sizeof(seg), " %*s", (i32) (delW + 1u), delStr);
    fputs(seg, out);

    u32 total = added + removed;
    if (total > 0) {
        u32 bars = total > 10u ? 10u : total;
        u32 plusBars = (added * bars + total - 1u) / total;
        if (added > 0 && plusBars == 0)
            plusBars = 1u;
        if (plusBars > bars)
            plusBars = bars;
        u32 minusBars = bars - plusBars;
        fputs(" ", out);
        emitRgbFg(out, color, TERMINAL_RGB(126, 231, 135));
        for (u32 b = 0; b < plusBars; b++)
            fputc('+', out);
        emitRgbFg(out, color, TERMINAL_RGB(255, 123, 114));
        for (u32 b = 0; b < minusBars; b++)
            fputc('-', out);
    }

    emitRgbFg(out, color, TERMINAL_RGB(139, 148, 158));
    if (sizeBytes >= 0) {
        snprintf(seg, sizeof(seg), "  %" PRId64 "B", sizeBytes);
        fputs(seg, out);
    }
    if (ts[0] != '\0') {
        fputs("  ", out);
        fputs(ts, out);
    }
    snprintf(seg, sizeof(seg), "  [%s %uc]", layoutTag, columns);
    fputs(seg, out);
    if (color)
        fputs("\x1b[0m", out);
    fputc('\n', out);

    emitRgbFg(out, color, TERMINAL_RGB(68, 76, 88));
    for (u32 i = 0; i < columns; i++)
        fputs(uni ? "─" : "-", out);
    if (color)
        fputs("\x1b[0m", out);
    fputc('\n', out);
}

/** Initializes DifferencePrinter with custom palette and custom watch-event header hook. */
fn DifferencePrinter_init(DifferencePrinter *printer) {
    if (!printer)
        return;
    memset(printer, 0, sizeof(*printer));
    TerminalDiffPanel *panel = Printer_panelMut(printer);
    TerminalDiffPanel_init(panel);

    TerminalDiffTheme th = TerminalDiffPanel_defaultTheme();
    th.borderColor = TERMINAL_RGB(78, 86, 102);
    th.hunkFg = TERMINAL_RGB(165, 214, 255);
    th.addFg = TERMINAL_RGB(166, 240, 174);
    th.addBg = TERMINAL_RGB(14, 36, 24);
    th.addSpanFg = TERMINAL_RGB(240, 255, 242);
    th.addSpanBg = TERMINAL_RGB(34, 124, 64);
    th.delFg = TERMINAL_RGB(255, 172, 168);
    th.delBg = TERMINAL_RGB(48, 18, 22);
    th.delSpanFg = TERMINAL_RGB(255, 238, 238);
    th.delSpanBg = TERMINAL_RGB(148, 38, 48);
    TerminalDiffPanel_setTheme(panel, th);
    TerminalDiffPanel_setHeaderHook(panel, differenceHeaderHook, printer);
}

/** Overrides target column width (0 uses live terminal width). */
fn DifferencePrinter_setColumns(DifferencePrinter *printer, u32 columns) {
    if (printer)
        TerminalDiffPanel_setColumns(Printer_panelMut(printer), columns);
}

/** Sets explicit layout mode (AUTO, UNIFIED, or SIDE_BY_SIDE). */
fn DifferencePrinter_setLayoutMode(DifferencePrinter *printer, TerminalDiffLayoutMode mode) {
    if (printer)
        TerminalDiffPanel_setLayoutMode(Printer_panelMut(printer), mode);
}

/** Enables or disables ANSI truecolor output. */
fn DifferencePrinter_setColor(DifferencePrinter *printer, bool useColor) {
    if (!printer)
        return;
    TerminalDiffPanel *panel = Printer_panelMut(printer);
    TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
    th.useColor = useColor;
    TerminalDiffPanel_setTheme(panel, th);
}

/** Prints a realtime file-watch edit diff to out using determinant's responsive renderer. */
bool DifferencePrinter_print(DifferencePrinter *printer,
                             const FileWatchEvent *event,
                             const DiffResult *diff,
                             FILE *out) {
    if (!printer || !event || !out)
        return false;

    Printer_setEditCounter(printer, Printer_editCounter(printer) + 1u);
    Printer_setLastFileSize(printer, WatchEv_size(event));
    Printer_setLastEventType(printer, WatchEv_type(event));
    u32 ad = printerDigitCount(diff ? Diff_added(diff) : 0u);
    u32 dd = printerDigitCount(diff ? Diff_removed(diff) : 0u);
    if (ad > Printer_maxAddDigits(printer))
        Printer_setMaxAddDigits(printer, ad);
    if (dd > Printer_maxDelDigits(printer))
        Printer_setMaxDelDigits(printer, dd);

    utf8 tsBuf[32];
    formatTimestamp(WatchEv_epoch(event), tsBuf, sizeof(tsBuf));

    TerminalDiffMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.path = WatchEv_rel(event) && WatchEv_rel(event)[0] ? WatchEv_rel(event) : WatchEv_path(event);
    meta.eventLabel = eventPillLabel(WatchEv_type(event));
    meta.timestamp = tsBuf;
    meta.linesAdded = diff ? Diff_added(diff) : 0u;
    meta.linesRemoved = diff ? Diff_removed(diff) : 0u;

    const TerminalDiffHunk *hunks = diff ? Diff_hunks(diff) : nullptr;
    bytes hunkCount = diff ? Diff_hunkCount(diff) : 0u;

    return TerminalDiffPanel_render(Printer_panelMut(printer), &meta, hunks, hunkCount, out);
}
