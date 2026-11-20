#include "vex.h"
#include "difference_tui.h"

#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "type/re_type.h"

;;DEFINITION
#define TYPE_DIFFERENCE_TUI Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 42u)
/**
 * ============================================================================
 * DEFINITION: DifferenceTui
 * ============================================================================
 * Size-agnostic fullscreen TUI for difference built on determinant's
 * TerminalCanvas, TerminalInput, and TerminalDiffPanel. Reflows cleanly
 * between side-by-side and unified layouts on SIGWINCH using synchronized
 * ANSI output and cell-delta diffing.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DifferenceTui (difference/difference_tui.c)
 * ============================================================================
 * Retains recent live edits in an isolated MemoryArena, repaints the full
 * TerminalCanvas viewport on every edit, scroll, or terminal resize, and
 * flushes only changed rows inside synchronized frame brackets.
 * ============================================================================
 */

static atomic_bool g_tuiStop = false;
static atomic_bool g_tuiResized = false;

;;GETTER
static inline MemoryArena *Tui_frameArena(const DifferenceTui *t) { return (*t).frameArena; }
;;SETTER
static inline fn Tui_setFrameArena(DifferenceTui *t, MemoryArena *a) { (*t).frameArena = a; }
;;GETTER
static inline MemoryArena *Tui_recordArena(const DifferenceTui *t) { return (*t).recordArena; }
;;SETTER
static inline fn Tui_setRecordArena(DifferenceTui *t, MemoryArena *a) { (*t).recordArena = a; }
;;GETTER
static inline TerminalInput *Tui_inputMut(DifferenceTui *t) { return &(*t).input; }
;;GETTER
static inline TerminalDiffPanel *Tui_panelMut(DifferenceTui *t) { return &(*t).panel; }
;;GETTER
static inline i32 Tui_outFd(const DifferenceTui *t) { return (*t).outFd; }
;;SETTER
static inline fn Tui_setOutFd(DifferenceTui *t, i32 fd) { (*t).outFd = fd; }
;;GETTER
static inline bytes Tui_width(const DifferenceTui *t) { return (*t).width; }
;;SETTER
static inline fn Tui_setWidth(DifferenceTui *t, bytes w) { (*t).width = w; }
;;GETTER
static inline bytes Tui_height(const DifferenceTui *t) { return (*t).height; }
;;SETTER
static inline fn Tui_setHeight(DifferenceTui *t, bytes h) { (*t).height = h; }
;;GETTER
static inline u32 *Tui_backCells(const DifferenceTui *t) { return (*t).backCells; }
;;SETTER
static inline fn Tui_setBackCells(DifferenceTui *t, u32 *p) { (*t).backCells = p; }
;;GETTER
static inline u16 *Tui_backStyles(const DifferenceTui *t) { return (*t).backStyles; }
;;SETTER
static inline fn Tui_setBackStyles(DifferenceTui *t, u16 *p) { (*t).backStyles = p; }
;;GETTER
static inline u32 *Tui_backFg(const DifferenceTui *t) { return (*t).backFg; }
;;SETTER
static inline fn Tui_setBackFg(DifferenceTui *t, u32 *p) { (*t).backFg = p; }
;;GETTER
static inline u32 *Tui_backBg(const DifferenceTui *t) { return (*t).backBg; }
;;SETTER
static inline fn Tui_setBackBg(DifferenceTui *t, u32 *p) { (*t).backBg = p; }
;;GETTER
static inline u32 *Tui_frontCells(const DifferenceTui *t) { return (*t).frontCells; }
;;SETTER
static inline fn Tui_setFrontCells(DifferenceTui *t, u32 *p) { (*t).frontCells = p; }
;;GETTER
static inline u16 *Tui_frontStyles(const DifferenceTui *t) { return (*t).frontStyles; }
;;SETTER
static inline fn Tui_setFrontStyles(DifferenceTui *t, u16 *p) { (*t).frontStyles = p; }
;;GETTER
static inline u32 *Tui_frontFg(const DifferenceTui *t) { return (*t).frontFg; }
;;SETTER
static inline fn Tui_setFrontFg(DifferenceTui *t, u32 *p) { (*t).frontFg = p; }
;;GETTER
static inline u32 *Tui_frontBg(const DifferenceTui *t) { return (*t).frontBg; }
;;SETTER
static inline fn Tui_setFrontBg(DifferenceTui *t, u32 *p) { (*t).frontBg = p; }
;;GETTER
static inline const char *Tui_watchRoot(const DifferenceTui *t) { return (*t).watchRoot; }
;;SETTER
static inline fn Tui_setWatchRoot(DifferenceTui *t, const char *r) { (*t).watchRoot = r; }
;;GETTER
static inline bytes Tui_trackedFiles(const DifferenceTui *t) { return (*t).trackedFiles; }
;;SETTER
static inline fn Tui_setTrackedFiles(DifferenceTui *t, bytes n) { (*t).trackedFiles = n; }
;;GETTER
static inline u64 Tui_editSeq(const DifferenceTui *t) { return (*t).editSequence; }
;;SETTER
static inline fn Tui_setEditSeq(DifferenceTui *t, u64 s) { (*t).editSequence = s; }
;;GETTER
static inline DifferenceEditRecord *Tui_recordsMut(DifferenceTui *t) { return (*t).records; }
;;GETTER
static inline bytes Tui_recordCount(const DifferenceTui *t) { return (*t).recordCount; }
;;SETTER
static inline fn Tui_setRecordCount(DifferenceTui *t, bytes n) { (*t).recordCount = n; }
;;GETTER
static inline bytes Tui_scrollOffset(const DifferenceTui *t) { return (*t).scrollOffset; }
;;SETTER
static inline fn Tui_setScrollOffset(DifferenceTui *t, bytes o) { (*t).scrollOffset = o; }
;;GETTER
static inline bytes Tui_totalRows(const DifferenceTui *t) { return (*t).totalRows; }
;;SETTER
static inline fn Tui_setTotalRows(DifferenceTui *t, bytes r) { (*t).totalRows = r; }
;;GETTER
static inline bool Tui_followTail(const DifferenceTui *t) { return (*t).followTail; }
;;SETTER
static inline fn Tui_setFollowTail(DifferenceTui *t, bool f) { (*t).followTail = f; }
;;GETTER
static inline bool Tui_dirty(const DifferenceTui *t) { return (*t).dirty; }
;;SETTER
static inline fn Tui_setDirty(DifferenceTui *t, bool d) { (*t).dirty = d; }
;;GETTER
static inline bool Tui_running(const DifferenceTui *t) { return (*t).running; }
;;SETTER
static inline fn Tui_setRunning(DifferenceTui *t, bool r) { (*t).running = r; }

;;GETTER
static inline const char *WatchEv_rel(const FileWatchEvent *e) { return (*e).relativePath; }
;;GETTER
static inline const char *WatchEv_path(const FileWatchEvent *e) { return (*e).path; }
;;GETTER
static inline FileEventType WatchEv_type(const FileWatchEvent *e) { return (*e).type; }
;;GETTER
static inline i64 WatchEv_epoch(const FileWatchEvent *e) { return (*e).epochSeconds; }
;;GETTER
static inline i64 WatchEv_size(const FileWatchEvent *e) { return (*e).fileSize; }

;;GETTER
static inline u64 Rec_seq(const DifferenceEditRecord *r) { return (*r).sequence; }
;;GETTER
static inline const char *Rec_rel(const DifferenceEditRecord *r) { return (*r).relativePath; }
;;GETTER
static inline const char *Rec_label(const DifferenceEditRecord *r) { return (*r).eventLabel; }
;;GETTER
static inline const char *Rec_ts(const DifferenceEditRecord *r) { return (*r).timestamp; }
;;GETTER
static inline FileEventType Rec_type(const DifferenceEditRecord *r) { return (*r).eventType; }
;;GETTER
static inline i64 Rec_size(const DifferenceEditRecord *r) { return (*r).fileSize; }
;;GETTER
static inline DiffResult *Rec_diffMut(DifferenceEditRecord *r) { return &(*r).diff; }
;;GETTER
static inline DiffResult Rec_diffVal(const DifferenceEditRecord *r) { return (*r).diff; }
;;SETTER
static inline fn Rec_assign(DifferenceEditRecord *dst, DifferenceEditRecord src) { *dst = src; }

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

/** Writes all len bytes to fd, retrying short writes. */
static fn writeAll(i32 fd, const char *buf, bytes len) {
    bytes sent = 0u;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            break;
        sent += (bytes) n;
    }
}

/** Encodes one Unicode codepoint into UTF-8 bytes, returning written byte count. */
static bytes encodeUtf8(u32 cp, char out[4]) {
    if (cp == 0u || cp == 0xFFFFFFFFu) {
        out[0] = ' ';
        return 1u;
    }
    if (cp < 0x80u) {
        out[0] = (char) cp;
        return 1u;
    }
    if (cp < 0x800u) {
        out[0] = (char) (0xC0u | (cp >> 6));
        out[1] = (char) (0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        out[0] = (char) (0xE0u | (cp >> 12));
        out[1] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char) (0x80u | (cp & 0x3Fu));
        return 3u;
    }
    out[0] = (char) (0xF0u | (cp >> 18));
    out[1] = (char) (0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char) (0x80u | (cp & 0x3Fu));
    return 4u;
}

typedef struct FrameBuf {
    i32 fd;
    char data[16384];
    bytes len;
} FrameBuf;

;;GETTER
static inline i32 FBuf_fd(const FrameBuf *fb) { return (*fb).fd; }
;;GETTER
static inline char *FBuf_data(FrameBuf *fb) { return (*fb).data; }
;;GETTER
static inline bytes FBuf_len(const FrameBuf *fb) { return (*fb).len; }
;;SETTER
static inline fn FBuf_setLen(FrameBuf *fb, bytes n) { (*fb).len = n; }

/** Flushes buffered ANSI output to the terminal file descriptor. */
static fn fbufFlush(FrameBuf *fb) {
    bytes n = FBuf_len(fb);
    if (n > 0u) {
        writeAll(FBuf_fd(fb), FBuf_data(fb), n);
        FBuf_setLen(fb, 0u);
    }
}

/** Appends bytes to FrameBuf, flushing automatically when full. */
static fn fbufAppend(FrameBuf *fb, const char *src, bytes len) {
    const bytes cap = 16384u;
    if (len >= cap) {
        fbufFlush(fb);
        writeAll(FBuf_fd(fb), src, len);
        return;
    }
    bytes cur = FBuf_len(fb);
    if (cur + len > cap) {
        fbufFlush(fb);
        cur = 0u;
    }
    memcpy(FBuf_data(fb) + cur, src, len);
    FBuf_setLen(fb, cur + len);
}

/** Diffs back vs front cell planes and emits changed rows inside synchronized output. */
static fn presentCanvasDelta(DifferenceTui *tui) {
    i32 fd = Tui_outFd(tui);
    if (fd < 0)
        return;
    bytes width = Tui_width(tui);
    bytes height = Tui_height(tui);
    u32 *bc = Tui_backCells(tui);
    u16 *bs = Tui_backStyles(tui);
    u32 *bf = Tui_backFg(tui);
    u32 *bb = Tui_backBg(tui);
    u32 *fc = Tui_frontCells(tui);
    u16 *fs = Tui_frontStyles(tui);
    u32 *ff = Tui_frontFg(tui);
    u32 *fb = Tui_frontBg(tui);

    FrameBuf out;
    out.fd = fd;
    out.len = 0u;

    bool opened = false;
    u16 curStyle = 0xFFFFu;
    u32 curFg = 0x12345678u;
    u32 curBg = 0x12345678u;

    for (bytes y = 0u; y < height; y++) {
        bytes rowBase = y * width;
        bool rowChanged = false;
        for (bytes x = 0u; x < width; x++) {
            bytes idx = rowBase + x;
            if (bc[idx] != fc[idx] || bs[idx] != fs[idx] || bf[idx] != ff[idx] || bb[idx] != fb[idx]) {
                rowChanged = true;
                break;
            }
        }
        if (!rowChanged)
            continue;

        if (!opened) {
            fbufAppend(&out, "\x1b[?2026h", 8u);
            opened = true;
        }

        char cup[32];
        i32 cn = snprintf(cup, sizeof(cup), "\x1b[%zu;1H", (size_t) (y + 1u));
        if (cn > 0)
            fbufAppend(&out, cup, (bytes) cn);

        for (bytes x = 0u; x < width; x++) {
            bytes idx = rowBase + x;
            u32 cp = bc[idx];
            u16 st = bs[idx];
            u32 fg = bf[idx];
            u32 bg = bb[idx];

            fc[idx] = cp;
            fs[idx] = st;
            ff[idx] = fg;
            fb[idx] = bg;

            if (cp == 0u)
                continue;

            if (st != curStyle || fg != curFg || bg != curBg) {
                char sgr[64];
                bytes slen = 0u;
                memcpy(sgr, "\x1b[0", 3u);
                slen = 3u;
                if (st & TERMINAL_STYLE_BOLD) {
                    memcpy(sgr + slen, ";1", 2u);
                    slen += 2u;
                }
                if (TERMINAL_COLOR_IS_SET(fg)) {
                    i32 fnLen = snprintf(sgr + slen, sizeof(sgr) - slen, ";38;2;%u;%u;%u",
                                         (unsigned) TERMINAL_COLOR_RED(fg),
                                         (unsigned) TERMINAL_COLOR_GREEN(fg),
                                         (unsigned) TERMINAL_COLOR_BLUE(fg));
                    if (fnLen > 0)
                        slen += (bytes) fnLen;
                }
                if (TERMINAL_COLOR_IS_SET(bg)) {
                    i32 bnLen = snprintf(sgr + slen, sizeof(sgr) - slen, ";48;2;%u;%u;%u",
                                         (unsigned) TERMINAL_COLOR_RED(bg),
                                         (unsigned) TERMINAL_COLOR_GREEN(bg),
                                         (unsigned) TERMINAL_COLOR_BLUE(bg));
                    if (bnLen > 0)
                        slen += (bytes) bnLen;
                }
                sgr[slen++] = 'm';
                fbufAppend(&out, sgr, slen);
                curStyle = st;
                curFg = fg;
                curBg = bg;
            }

            char u8buf[4];
            bytes ulen = encodeUtf8(cp, u8buf);
            fbufAppend(&out, u8buf, ulen);
        }
    }

    if (opened) {
        fbufAppend(&out, "\x1b[0m\x1b[?2026l", 12u);
        fbufFlush(&out);
    }
}

/** Maps FileEventType bits to a compact badge label. */
static const char *tuiEventLabel(FileEventType type) {
    if (type & FILE_EVENT_DELETED)
        return "DEL";
    if (type & FILE_EVENT_CREATED)
        return "NEW";
    if (type & FILE_EVENT_RENAMED)
        return "MOVE";
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
static fn tuiFormatTime(i64 epochSec, char *buf, bytes cap) {
    time_t t = epochSec > 0 ? (time_t) epochSec : time(nullptr);
    struct tm tmBuf;
    if (localtime_r(&t, &tmBuf))
        strftime(buf, cap, "%H:%M:%S", &tmBuf);
    else
        snprintf(buf, cap, "00:00:00");
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
static char *arenaDupBytes(MemoryArena *arena, const char *src, bytes len) {
    if (!arena)
        return nullptr;
    char *copy = (char*) MemoryArena_alloc(arena, TYPE_DIFFERENCE_TUI, len + 1u);
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

/** Initializes DifferenceTui with determinant canvas panel, input decoder, and arenas. */
bool DifferenceTui_init(DifferenceTui *tui, const char *watchRoot,
                        TerminalDiffLayoutMode initialLayout, bool useColor) {
    if (!tui)
        return false;
    memset(tui, 0, sizeof(*tui));

    MemoryArena *frameArena = MemoryArena_create(64u * 1024u * 1024u);
    MemoryArena *recordArena = MemoryArena_create(128u * 1024u * 1024u);
    if (!frameArena || !recordArena) {
        if (frameArena)
            MemoryArena_destroy(frameArena);
        if (recordArena)
            MemoryArena_destroy(recordArena);
        return false;
    }

    Tui_setFrameArena(tui, frameArena);
    Tui_setRecordArena(tui, recordArena);
    Tui_setOutFd(tui, STDOUT_FILENO);
    *Tui_inputMut(tui) = TerminalInput_0();

    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffPanel_init(panel);
    TerminalDiffPanel_setLayoutMode(panel, initialLayout);
    TerminalDiffTheme th = TerminalDiffPanel_defaultTheme();
    th.showHeader = false;
    th.useColor = useColor;
    TerminalDiffPanel_setTheme(panel, th);

    Tui_setWatchRoot(tui, watchRoot ? watchRoot : ".");
    Tui_setFollowTail(tui, true);
    Tui_setDirty(tui, true);
    Tui_setRunning(tui, false);
    return true;
}

/** Destroys all edit diffs and arenas owned by DifferenceTui. */
fn DifferenceTui_free(DifferenceTui *tui) {
    if (!tui)
        return;
    DifferenceTui_clearEdits(tui);
    MemoryArena *fa = Tui_frameArena(tui);
    MemoryArena *ra = Tui_recordArena(tui);
    if (fa)
        MemoryArena_destroy(fa);
    if (ra)
        MemoryArena_destroy(ra);
    memset(tui, 0, sizeof(*tui));
}

/** Updates the tracked file count displayed in the top bar. */
fn DifferenceTui_setTrackedFiles(DifferenceTui *tui, bytes count) {
    if (!tui)
        return;
    Tui_setTrackedFiles(tui, count);
    Tui_setDirty(tui, true);
}

/** Clears all recorded edits and resets the record arena. */
fn DifferenceTui_clearEdits(DifferenceTui *tui) {
    if (!tui)
        return;
    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    for (bytes i = 0; i < count; i++)
        releaseEditRecord(&recs[i]);
    Tui_setRecordCount(tui, 0u);
    Tui_setScrollOffset(tui, 0u);
    Tui_setTotalRows(tui, 0u);
    MemoryArena *ra = Tui_recordArena(tui);
    if (ra)
        MemoryArena_freeAll(ra);
    Tui_setDirty(tui, true);
}

/** Appends a live file edit record and computes its structured diff for TUI reflow. */
bool DifferenceTui_pushEdit(DifferenceTui *tui,
                            const FileWatchEvent *event,
                            const char *oldText, bytes oldLen,
                            const char *newText, bytes newLen) {
    if (!tui || !event)
        return false;

    MemoryArena *ra = Tui_recordArena(tui);
    if (!ra)
        return false;

    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);
    if (count >= (bytes) DIFFERENCE_TUI_MAX_EDITS) {
        releaseEditRecord(&recs[0]);
        memmove(&recs[0], &recs[1], (count - 1u) * sizeof(DifferenceEditRecord));
        count--;
        Tui_setRecordCount(tui, count);
    }

    const char *path = WatchEv_path(event) ? WatchEv_path(event) : "";
    const char *rel = (WatchEv_rel(event) && WatchEv_rel(event)[0]) ? WatchEv_rel(event) : path;
    char *oldCopy = arenaDupBytes(ra, oldText ? oldText : "", oldLen);
    char *newCopy = arenaDupBytes(ra, newText ? newText : "", newLen);
    char *pathCopy = arenaDupBytes(ra, path, strlen(path));
    char *relCopy = arenaDupBytes(ra, rel, strlen(rel));
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
    if (!DiffEngine_compute(oldCopy, oldLen, newCopy, newLen, 3u, &diff))
        return false;

    u64 seq = Tui_editSeq(tui) + 1u;
    Tui_setEditSeq(tui, seq);

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
    Rec_assign(&recs[count], built);

    Tui_setRecordCount(tui, count + 1u);
    Tui_setDirty(tui, true);
    return true;
}

/** Reallocates front/back canvas planes for new (width, height) and forces clean repaint. */
bool DifferenceTui_resize(DifferenceTui *tui, bytes width, bytes height) {
    if (!tui)
        return false;
    if (width < 20u)
        width = 20u;
    if (height < 6u)
        height = 6u;

    MemoryArena *fa = Tui_frameArena(tui);
    if (!fa)
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

    if (Tui_outFd(tui) >= 0)
        writeAll(Tui_outFd(tui), "\x1b[2J\x1b[H", 7u);
    Tui_setDirty(tui, true);
    return true;
}

/** Paints one edit header strip (pill/path/+N -M/size/time) into body canvas. */
static fn paintEditBanner(TerminalCanvas *body, bytes sy, bytes bodyW,
                          const DifferenceEditRecord *rec, bool useColor) {
    u32 barBg = useColor ? TERMINAL_RGB(24, 30, 40) : TERMINAL_COLOR_DEFAULT;
    TerminalPen basePen = TerminalPen_3(0u, useColor ? TERMINAL_RGB(220, 228, 236) : TERMINAL_COLOR_DEFAULT, barBg);
    for (bytes c = 0; c < bodyW; c++)
        TerminalCanvas_glyph(body, c, sy, (u32) ' ', basePen);

    DiffResult dv = Rec_diffVal(rec);
    bytes x = 0u;
    char seqBuf[24];
    i32 sn = snprintf(seqBuf, sizeof(seqBuf), " #%llu ", (unsigned long long) Rec_seq(rec));
    if (sn > 0) {
        TerminalPen seqPen = TerminalPen_3(0u, useColor ? TERMINAL_RGB(130, 140, 156) : TERMINAL_COLOR_DEFAULT, barBg);
        TerminalCanvas_text(body, x, sy, seqBuf, (bytes) sn, seqPen);
        x += (bytes) sn;
    }

    char pillBuf[24];
    i32 pn = snprintf(pillBuf, sizeof(pillBuf), " %s ", Rec_label(rec));
    if (pn > 0 && x < bodyW) {
        TerminalPen pillPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                            useColor ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                            useColor ? tuiEventBg(Rec_type(rec)) : TERMINAL_COLOR_DEFAULT);
        TerminalCanvas_text(body, x, sy, pillBuf, (bytes) pn, pillPen);
        x += (bytes) pn;
    }

    char pathBuf[512];
    const char *rel = Rec_rel(rec);
    i32 fnLen = snprintf(pathBuf, sizeof(pathBuf), " %s ", rel ? rel : "(unnamed)");
    if (fnLen > 0 && x < bodyW) {
        TerminalPen pathPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                            useColor ? TERMINAL_RGB(240, 246, 252) : TERMINAL_COLOR_DEFAULT,
                                            barBg);
        TerminalCanvas_text(body, x, sy, pathBuf, (bytes) fnLen, pathPen);
        x += (bytes) fnLen;
    }

    char addBuf[32];
    i32 an = snprintf(addBuf, sizeof(addBuf), "+%u ", (unsigned) dv.linesAdded);
    if (an > 0 && x < bodyW) {
        TerminalPen addPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                           useColor ? TERMINAL_RGB(126, 231, 135) : TERMINAL_COLOR_DEFAULT,
                                           barBg);
        TerminalCanvas_text(body, x, sy, addBuf, (bytes) an, addPen);
        x += (bytes) an;
    }

    char delBuf[32];
    i32 dn = snprintf(delBuf, sizeof(delBuf), "-%u ", (unsigned) dv.linesRemoved);
    if (dn > 0 && x < bodyW) {
        TerminalPen delPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                           useColor ? TERMINAL_RGB(255, 123, 114) : TERMINAL_COLOR_DEFAULT,
                                           barBg);
        TerminalCanvas_text(body, x, sy, delBuf, (bytes) dn, delPen);
        x += (bytes) dn;
    }

    char metaBuf[64];
    i64 fsz = Rec_size(rec);
    i32 mn = fsz >= 0
        ? snprintf(metaBuf, sizeof(metaBuf), " %lldB  %s", (long long) fsz, Rec_ts(rec))
        : snprintf(metaBuf, sizeof(metaBuf), " %s", Rec_ts(rec));
    if (mn > 0 && x < bodyW) {
        TerminalPen metaPen = TerminalPen_3(0u, useColor ? TERMINAL_RGB(139, 148, 158) : TERMINAL_COLOR_DEFAULT, barBg);
        TerminalCanvas_text(body, x, sy, metaBuf, (bytes) mn, metaPen);
    }
}

/** Composes top bar, scrollable diff feed, scrollbar, and bottom keybind bar, then presents. */
fn DifferenceTui_renderFrame(DifferenceTui *tui) {
    if (!tui || Tui_width(tui) == 0u || Tui_height(tui) == 0u)
        return;

    bytes width = Tui_width(tui);
    bytes height = Tui_height(tui);
    TerminalCanvas root = TerminalCanvas_6(Tui_backCells(tui), Tui_backStyles(tui),
                                           Tui_backFg(tui), Tui_backBg(tui),
                                           width, height);
    TerminalDiffPanel *panel = Tui_panelMut(tui);
    TerminalDiffTheme th = TerminalDiffPanel_theme(panel);
    bool color = th.useColor;

    TerminalCanvas_fill(&root, (u32) ' ', TerminalPen_0());

    bytes bodyH = height > 2u ? height - 2u : 1u;
    bytes bodyW = width > 2u ? width - 1u : width;
    TerminalCanvas body = TerminalCanvas_sub(&root, 0u, 1u, bodyW, bodyH);

    DifferenceEditRecord *recs = Tui_recordsMut(tui);
    bytes count = Tui_recordCount(tui);

    bytes total = 0u;
    for (bytes i = 0; i < count; i++) {
        bytes diffRows = TerminalDiffPanel_measureRows(panel, recs[i].diff.hunks, recs[i].diff.hunkCount, (u32) bodyW);
        total += 2u + (diffRows > 0u ? diffRows : 1u) + 1u;
    }
    Tui_setTotalRows(tui, total);

    bytes maxScroll = total > bodyH ? total - bodyH : 0u;
    bytes scroll = Tui_scrollOffset(tui);
    if (Tui_followTail(tui))
        scroll = maxScroll;
    else if (scroll > maxScroll)
        scroll = maxScroll;
    Tui_setScrollOffset(tui, scroll);

    u32 resolvedCols = (u32) bodyW;
    TerminalDiffLayoutMode resolvedLayout = TerminalDiffPanel_resolveLayout(panel, &resolvedCols);
    TerminalDiffLayoutMode cfgMode = TerminalDiffPanel_layoutMode(panel);
    const char *modeLabel = "AUTO:UNIFIED";
    if (cfgMode == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE)
        modeLabel = "SPLIT";
    else if (cfgMode == TERMINAL_DIFF_LAYOUT_UNIFIED)
        modeLabel = "UNIFIED";
    else if (resolvedLayout == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE)
        modeLabel = "AUTO:SPLIT";

    u32 topBg = color ? TERMINAL_RGB(22, 27, 36) : TERMINAL_COLOR_DEFAULT;
    TerminalPen topPen = TerminalPen_3(0u, color ? TERMINAL_RGB(225, 232, 240) : TERMINAL_COLOR_DEFAULT, topBg);
    for (bytes c = 0; c < width; c++)
        TerminalCanvas_glyph(&root, c, 0u, (u32) ' ', topPen);

    TerminalPen brandPen = TerminalPen_3(TERMINAL_STYLE_BOLD,
                                         color ? TERMINAL_RGB(250, 252, 255) : TERMINAL_COLOR_DEFAULT,
                                         color ? TERMINAL_RGB(48, 92, 165) : TERMINAL_COLOR_DEFAULT);
    TerminalCanvas_text(&root, 0u, 0u, " difference ", 12u, brandPen);

    char topBuf[512];
    i32 tn = snprintf(topBuf, sizeof(topBuf), " %s  |  %zu files  |  %llu edits  |  %s  |  %zux%zu  |  %s",
                      Tui_watchRoot(tui),
                      (size_t) Tui_trackedFiles(tui),
                      (unsigned long long) Tui_editSeq(tui),
                      modeLabel,
                      (size_t) width,
                      (size_t) height,
                      Tui_followTail(tui) ? "FOLLOW" : "PAUSED");
    if (tn > 0 && width > 12u)
        TerminalCanvas_text(&root, 12u, 0u, topBuf, (bytes) tn, topPen);

    if (count == 0u) {
        bytes cy = bodyH / 2u;
        TerminalPen hintPen = TerminalPen_3(0u, color ? TERMINAL_RGB(139, 148, 158) : TERMINAL_COLOR_DEFAULT, TERMINAL_COLOR_DEFAULT);
        const char *msg = "Watching for file edits... Save a file in your editor to see live diffs here.";
        TerminalCanvas_text(&body, 2u, cy, msg, strlen(msg), hintPen);
    } else {
        bytes cursor = 0u;
        TerminalPen rulePen = TerminalPen_3(0u, color ? TERMINAL_RGB(68, 76, 88) : TERMINAL_COLOR_DEFAULT, TERMINAL_COLOR_DEFAULT);

        for (bytes i = 0; i < count; i++) {
            bytes diffRows = TerminalDiffPanel_measureRows(panel, recs[i].diff.hunks, recs[i].diff.hunkCount, (u32) bodyW);
            bytes blockH = 2u + (diffRows > 0u ? diffRows : 1u) + 1u;

            if (cursor + blockH <= scroll) {
                cursor += blockH;
                continue;
            }
            if (cursor >= scroll + bodyH)
                break;

            if (cursor >= scroll && cursor - scroll < bodyH)
                paintEditBanner(&body, cursor - scroll, bodyW, &recs[i], color);
            cursor++;

            if (cursor >= scroll && cursor - scroll < bodyH)
                TerminalCanvas_hline(&body, cursor - scroll, 0x2500u, rulePen);
            cursor++;

            bytes contentRows = diffRows > 0u ? diffRows : 1u;
            if (cursor + contentRows > scroll && cursor < scroll + bodyH) {
                bytes localScroll = scroll > cursor ? scroll - cursor : 0u;
                bytes topY = cursor > scroll ? cursor - scroll : 0u;
                bytes availH = bodyH - topY;
                bytes subH = contentRows - localScroll;
                if (subH > availH)
                    subH = availH;
                TerminalCanvas sub = TerminalCanvas_sub(&body, 0u, topY, bodyW, subH);
                if (recs[i].diff.hunkCount > 0u)
                    (discard) TerminalDiffPanel_paintCanvas(panel, nullptr, recs[i].diff.hunks,
                                                            recs[i].diff.hunkCount, &sub, localScroll);
                else {
                    TerminalPen notePen = TerminalPen_3(0u, color ? TERMINAL_RGB(139, 148, 158) : TERMINAL_COLOR_DEFAULT, TERMINAL_COLOR_DEFAULT);
                    TerminalCanvas_text(&sub, 2u, 0u, "(no text changes)", 17u, notePen);
                }
            }
            cursor += contentRows + 1u;
        }
    }

    if (width > 2u && bodyH > 0u) {
        bytes sbX = width - 1u;
        TerminalPen trackPen = TerminalPen_3(0u, color ? TERMINAL_RGB(48, 54, 64) : TERMINAL_COLOR_DEFAULT, TERMINAL_COLOR_DEFAULT);
        TerminalPen thumbPen = TerminalPen_3(0u, color ? TERMINAL_RGB(121, 192, 255) : TERMINAL_COLOR_DEFAULT, TERMINAL_COLOR_DEFAULT);
        bytes thumbH = total > bodyH ? (bodyH * bodyH) / total : bodyH;
        if (thumbH < 1u)
            thumbH = 1u;
        bytes thumbY = maxScroll > 0u ? (scroll * (bodyH - thumbH)) / maxScroll : 0u;
        for (bytes r = 0; r < bodyH; r++) {
            bool onThumb = (r >= thumbY && r < thumbY + thumbH);
            TerminalCanvas_glyph(&root, sbX, 1u + r, onThumb ? 0x2588u : 0x2502u, onThumb ? thumbPen : trackPen);
        }
    }

    bytes botY = height - 1u;
    TerminalPen botPen = TerminalPen_3(0u, color ? TERMINAL_RGB(150, 160, 174) : TERMINAL_COLOR_DEFAULT, topBg);
    for (bytes c = 0; c < width; c++)
        TerminalCanvas_glyph(&root, c, botY, (u32) ' ', botPen);
    const char *keys = " j/k up/down wheel: scroll  |  PgUp/PgDn: page  |  g/G: top/end  |  s: layout  |  f: follow  |  c: clear  |  q: quit";
    TerminalCanvas_text(&root, 0u, botY, keys, strlen(keys), botPen);

    presentCanvasDelta(tui);
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

/** Handles one plain keyboard byte (or feeds mouse bytes through TerminalInput). */
fn DifferenceTui_handleInputByte(DifferenceTui *tui, i32 byte) {
    if (!tui)
        return;
    TerminalInput *inp = Tui_inputMut(tui);
    if (TerminalInput_feed(inp, byte)) {
        TerminalPointerKind kind = TerminalInput_getKind(inp);
        if (kind == TERMINAL_POINTER_WHEEL_UP)
            scrollUp(tui, 3u);
        else if (kind == TERMINAL_POINTER_WHEEL_DOWN)
            scrollDown(tui, 3u);
        return;
    }
    if (TerminalInput_isPending(inp))
        return;

    bytes bodyH = Tui_height(tui) > 2u ? Tui_height(tui) - 2u : 1u;
    bytes halfPage = bodyH > 2u ? bodyH / 2u : 1u;

    if (byte == 'q' || byte == 'Q' || byte == 3)
        Tui_setRunning(tui, false);
    else if (byte == 'j')
        scrollDown(tui, 1u);
    else if (byte == 'k')
        scrollUp(tui, 1u);
    else if (byte == 'd' || byte == ' ')
        scrollDown(tui, halfPage);
    else if (byte == 'u')
        scrollUp(tui, halfPage);
    else if (byte == 'g') {
        Tui_setScrollOffset(tui, 0u);
        Tui_setFollowTail(tui, false);
        Tui_setDirty(tui, true);
    } else if (byte == 'G') {
        Tui_setFollowTail(tui, true);
        Tui_setDirty(tui, true);
    } else if (byte == 'f' || byte == 'F') {
        Tui_setFollowTail(tui, !Tui_followTail(tui));
        Tui_setDirty(tui, true);
    } else if (byte == 'c' || byte == 'C')
        DifferenceTui_clearEdits(tui);
    else if (byte == 's' || byte == 'S') {
        TerminalDiffPanel *panel = Tui_panelMut(tui);
        TerminalDiffLayoutMode cur = TerminalDiffPanel_layoutMode(panel);
        if (cur == TERMINAL_DIFF_LAYOUT_AUTO)
            TerminalDiffPanel_setLayoutMode(panel, TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE);
        else if (cur == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE)
            TerminalDiffPanel_setLayoutMode(panel, TERMINAL_DIFF_LAYOUT_UNIFIED);
        else
            TerminalDiffPanel_setLayoutMode(panel, TERMINAL_DIFF_LAYOUT_AUTO);
        Tui_setDirty(tui, true);
    }
}

/** Handles ANSI arrow and PageUp/PageDown escape sequences read from stdin. */
fn DifferenceTui_handleKeySeq(DifferenceTui *tui, const char *seq, bytes len) {
    if (!tui || !seq || len < 3u || seq[0] != '\x1b' || seq[1] != '[')
        return;
    bytes bodyH = Tui_height(tui) > 2u ? Tui_height(tui) - 2u : 1u;
    if (len == 3u && seq[2] == 'A')
        scrollUp(tui, 1u);
    else if (len == 3u && seq[2] == 'B')
        scrollDown(tui, 1u);
    else if (len == 4u && seq[2] == '5' && seq[3] == '~')
        scrollUp(tui, bodyH);
    else if (len == 4u && seq[2] == '6' && seq[3] == '~')
        scrollDown(tui, bodyH);
}

/** Runs the interactive TUI loop, pumping FileEventWatcher and handling resizes/input. */
i32 DifferenceTui_run(DifferenceTui *tui, FileEventWatcher *watcher) {
    if (!tui || !watcher)
        return 1;

    struct termios origTerm;
    bool haveTerm = (tcgetattr(STDIN_FILENO, &origTerm) == 0);
    if (haveTerm) {
        struct termios raw = origTerm;
        raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    atomic_store_explicit(&g_tuiStop, false, memory_order_release);
    atomic_store_explicit(&g_tuiResized, false, memory_order_release);
    signal(SIGINT, onTuiStop);
    signal(SIGTERM, onTuiStop);
    signal(SIGWINCH, onTuiResize);

    const char *enterSeq = "\x1b[?1049h\x1b[?25l\x1b[?1000h\x1b[?1006h";
    writeAll(Tui_outFd(tui), enterSeq, strlen(enterSeq));

    bytes w = 80u;
    bytes h = 24u;
    queryTermSize(&w, &h);
    (discard) DifferenceTui_resize(tui, w, h);
    Tui_setRunning(tui, true);
    DifferenceTui_renderFrame(tui);

    while (Tui_running(tui) && !atomic_load_explicit(&g_tuiStop, memory_order_acquire)) {
        if (atomic_exchange_explicit(&g_tuiResized, false, memory_order_acq_rel)) {
            queryTermSize(&w, &h);
            (discard) DifferenceTui_resize(tui, w, h);
        }

        FileEventWatcher_pump(watcher);

        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 40) > 0 && (pfd.revents & POLLIN)) {
            char buf[64];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                bytes nb = (bytes) n;
                if (nb >= 3u && buf[0] == '\x1b' && buf[1] == '[' && buf[2] != '<' && buf[2] != 'M')
                    DifferenceTui_handleKeySeq(tui, buf, nb);
                else
                    for (bytes i = 0; i < nb; i++)
                        DifferenceTui_handleInputByte(tui, (unsigned char) buf[i]);
            }
        }

        if (Tui_dirty(tui))
            DifferenceTui_renderFrame(tui);
    }

    const char *leaveSeq = "\x1b[?1006l\x1b[?1000l\x1b[?25h\x1b[0m\x1b[?1049l";
    writeAll(Tui_outFd(tui), leaveSeq, strlen(leaveSeq));
    if (haveTerm)
        tcsetattr(STDIN_FILENO, TCSANOW, &origTerm);
    return 0;
}
