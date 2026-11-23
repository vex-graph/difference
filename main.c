#include "vex.h"
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "io/file/file.h"
#include "nio/mem.h"
#include "diff_engine.h"
#include "difference_printer.h"
#include "difference_settings.h"
#include "difference_tui.h"
#include "main.h"
#include "snapshot_store.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: difference
 * ============================================================================
 * Realtime terminal diff watcher. Runs as a size-agnostic determinant TUI on
 * interactive terminals (reflowing seamlessly between side-by-side and unified
 * layouts on resize) or as a plain scrollable log stream with --stream.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: difference CLI (difference/main.c)
 * ============================================================================
 * Configures FileWatchProps, seeds initial file snapshots, starts the OS watch
 * stream, and routes live edits into DifferenceTui (default on TTY) or
 * DifferencePrinter (--stream / piped stdout).
 * ============================================================================
 */

static atomic_bool g_stopRequested = false;



;;GETTER
static inline SnapshotStore *App_storeMut(WatchApp *a)
{
    return &(*a).store;
}
;;GETTER
static inline SnapshotSeeder *App_seederMut(WatchApp *a)
{
    return &(*a).seeder;
}
;;GETTER
static inline DifferencePrinter *App_printerMut(WatchApp *a)
{
    return &(*a).printer;
}
;;GETTER
static inline DifferenceTui *App_tui(const WatchApp *a)
{
    return (*a).tui;
}
;;GETTER
static inline FILE *App_out(const WatchApp *a)
{
    return (*a).out;
}
;;GETTER
static inline bytes App_lastReported(const WatchApp *a)
{
    return (*a).lastReportedFiles;
}
;;SETTER
static inline fn App_setLastReported(WatchApp *a, bytes n)
{
    (*a).lastReportedFiles = n;
}
;;GETTER
static inline string App_settingsPath(const WatchApp *a)
{
    return (*a).settingsPath;
}
;;GETTER
static inline bool App_haveSettingsPath(const WatchApp *a)
{
    return (*a).haveSettingsPath;
}
;;GETTER
static inline string Ev_path(const FileWatchEvent *e)
{
    return (*e).path;
}
;;GETTER
static inline FileEventType Ev_type(const FileWatchEvent *e)
{
    return (*e).type;
}
;;GETTER
static inline string Snap_data(const SnapshotEntry *s)
{
    return (*s).data;
}
;;GETTER
static inline bytes Snap_length(const SnapshotEntry *s)
{
    return (*s).length;
}

/** Compares a CLI argument against an expected flag string. */
static bool argEq(string a, string b) {
    if (!a || !b)
        return false;
    bytes la = strlen(a);
    bytes lb = strlen(b);
    if (la != lb)
        return false;
    return la == 0u || memcmp(a, b, la) == 0;
}

/** Marks the stream watch loop for clean shutdown on SIGINT or SIGTERM. */
static fn onSignal(i32 sig) {
    (discard) sig;
    atomic_store_explicit(&g_stopRequested, true, memory_order_release);
}

/** Emits a single diff either into the live TUI or the stream printer. */
static fn emitDiff(WatchApp *app, const FileWatchEvent *event,
                   string oldData, bytes oldLen,
                   string newData, bytes newLen) {
    DifferenceTui *tui = App_tui(app);
    if (tui) {
        (discard) DifferenceTui_pushEdit(tui, event, oldData, oldLen, newData, newLen);
        return;
    }
    DiffResult diff;
    if (DiffEngine_compute(oldData, oldLen, newData, newLen, 3u, &diff)) {
        if (diff.hunkCount > 0)
            (discard) DifferencePrinter_print(App_printerMut(app), event, &diff, App_out(app));
        DiffEngine_free(&diff);
    }
}

/** Handles one pumped FileWatchEvent by diffing against the stored snapshot. */
static fn onWatchEvent(const FileWatchEvent *event, any userdata) {
    WatchApp *app = (WatchApp*) userdata;
    string path = Ev_path(event);
    FileEventType type = Ev_type(event);
    if (!path || !app)
        return;

    SnapshotStore *store = App_storeMut(app);
    const SnapshotEntry *prev = SnapshotStore_find(store, path);
    string oldData = prev ? Snap_data(prev) : "";
    bytes oldLen = prev ? Snap_length(prev) : 0u;

    if ((type & FILE_EVENT_DELETED) && !File_exists(path)) {
        if (prev)
            emitDiff(app, event, oldData, oldLen, "", 0u);
        (discard) SnapshotStore_remove(store, path);
        if (App_tui(app))
            DifferenceTui_setTrackedFiles(App_tui(app), SnapshotStore_count(store));
        return;
    }

    bytes newLen = 0u;
    bool isBinary = false;
    utf8 *newData = SnapshotStore_readFile(store, path, &newLen, &isBinary);
    if (isBinary)
        return;
    if (!newData) {
        if (prev && !File_exists(path)) {
            emitDiff(app, event, oldData, oldLen, "", 0u);
            (discard) SnapshotStore_remove(store, path);
            if (App_tui(app))
                DifferenceTui_setTrackedFiles(App_tui(app), SnapshotStore_count(store));
        }
        return;
    }

    if (prev && oldLen == newLen && (oldLen == 0u || memcmp(oldData, newData, oldLen) == 0)) {
        SnapshotStore_freeBuf(store, newData);
        return;
    }

    emitDiff(app, event, oldData, oldLen, newData, newLen);
    (discard) SnapshotStore_put(store, path, newData, newLen);
    SnapshotStore_freeBuf(store, newData);
    if (App_tui(app))
        DifferenceTui_setTrackedFiles(App_tui(app), SnapshotStore_count(store));
}

/** Updates SnapshotStore workspace data cap when edited live in TUI Settings. */
static fn onWorkspaceLimitChanged(u32 limitMb, any userdata) {
    WatchApp *app = (WatchApp*) userdata;
    if (!app)
        return;
    SnapshotStore_setMaxWorkspaceBytes(App_storeMut(app), (bytes) limitMb * 1024u * 1024u);
}

/** Persists TUI settings atomically under the R1 manifest root on every change. */
static fn onSettingsChanged(const DifferenceSettings *settings, any userdata) {
    WatchApp *app = (WatchApp*) userdata;
    if (!app || !settings || !App_haveSettingsPath(app))
        return;
    (discard) DifferenceSettings_save(settings, App_settingsPath(app));
}

/** Advances incremental directory seeding between TUI frames so startup paints immediately. */
static bool onIdleSeed(any userdata) {
    WatchApp *app = (WatchApp*) userdata;
    if (!app)
        return false;
    bool more = SnapshotSeeder_step(App_seederMut(app), 32u);
    bytes now = SnapshotStore_count(App_storeMut(app));
    if (now != App_lastReported(app)) {
        App_setLastReported(app, now);
        if (App_tui(app))
            DifferenceTui_setTrackedFiles(App_tui(app), now);
    }
    return more;
}

/** Prints CLI usage summary to stderr. */
static fn printUsage() {
    fputs("Usage: difference [dir] [--tui|--stream] [--unified|--split] [--cols N] [--no-color] [--all]\n", stderr);
}

/** Watches the target directory (default cwd) in TUI mode (TTY) or stream mode. */
i32 main(i32 argc, utf8 **argv) {
    string targetArg = ".";
    TerminalDiffLayoutMode cliLayout = TERMINAL_DIFF_LAYOUT_AUTO;
    bool cliLayoutSet = false;
    bool cliNoColor = false;
    u32 overrideCols = 0u;
    bool isTty = isatty(STDOUT_FILENO) != 0 && isatty(STDIN_FILENO) != 0;
    bool useTui = isTty;
    bool includeHidden = false;

    for (i32 i = 1; i < argc; i++) {
        if (argEq(argv[i], "--help") || argEq(argv[i], "-h")) {
            printUsage();
            return 0;
        } else if (argEq(argv[i], "--tui"))
            useTui = true;
        else if (argEq(argv[i], "--stream"))
            useTui = false;
        else if (argEq(argv[i], "--unified")) {
            cliLayout = TERMINAL_DIFF_LAYOUT_UNIFIED;
            cliLayoutSet = true;
        } else if (argEq(argv[i], "--split")) {
            cliLayout = TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE;
            cliLayoutSet = true;
        } else if (argEq(argv[i], "--no-color"))
            cliNoColor = true;
        else if (argEq(argv[i], "--all"))
            includeHidden = true;
        else if (argEq(argv[i], "--cols") && i + 1 < argc)
            overrideCols = (u32) atoi(argv[++i]);
        else if (argv[i][0] != '-')
            targetArg = argv[i];
        else {
            fprint(stderr, "difference: unknown option '", argv[i], "'");
            printUsage();
            return 2;
        }
    }

    utf8 resolved[PATH_MAX];
    if (!realpath(targetArg, resolved)) {
        fprint(stderr, "difference: cannot resolve path '", targetArg, "'");
        return 1;
    }

    if (!Memory_init(0)) {
        fputs("difference: failed to initialize engine memory\n", stderr);
        return 1;
    }

    WatchApp app;
    memset(&app, 0, sizeof(app));
    app.out = stdout;
    DifferenceSettings settings = DifferenceSettings_0();
    app.haveSettingsPath = DifferenceSettings_resolvePath(app.settingsPath, sizeof(app.settingsPath));
    if (app.haveSettingsPath)
        (discard) DifferenceSettings_load(&settings, app.settingsPath);
    if (cliLayoutSet)
        (discard) DifferenceSettings_setDiffLayout(&settings, cliLayout);
    if (cliNoColor || !isatty(STDOUT_FILENO))
        DifferenceSettings_setColor(&settings, false);

    TerminalDiffLayoutMode activeLayout = DifferenceSettings_getDiffLayout(&settings);
    bool activeColor = DifferenceSettings_isColor(&settings);

    SnapshotStore_init(&app.store, 256u * 1024u);
    SnapshotStore_setMaxWorkspaceBytes(&app.store,
                                       (bytes) DifferenceSettings_getWorkspaceLimitMb(&settings) * 1024u * 1024u);
    DifferencePrinter_init(&app.printer);
    DifferencePrinter_setLayoutMode(&app.printer, activeLayout);
    DifferencePrinter_setColumns(&app.printer, overrideCols);
    DifferencePrinter_setColor(&app.printer, activeColor);

    DifferenceTui tui;
    if (useTui) {
        if (!DifferenceTui_init(&tui, resolved, activeLayout, activeColor)) {
            fputs("difference: failed to initialize TUI\n", stderr);
            SnapshotStore_free(&app.store);
            Memory_freeAll();
            return 1;
        }
        DifferenceTui_setWorkspaceLimitHook(&tui, onWorkspaceLimitChanged, &app);
        DifferenceTui_applySettings(&tui, &settings);
        if (app.haveSettingsPath)
            DifferenceTui_setSettingsChangedHook(&tui, onSettingsChanged, &app);
        app.tui = &tui;
    }

    string defaultIgnores[] = {
        "*.swp", "*.swo", "*.tmp", "*~",
        "*___jb_tmp___", "*___jb_old___",
        "*.o", "*.a", "*.dylib", "*.so",
        ".DS_Store", "node_modules/", "cmake-build-", "build/"
    };
    FileWatchProps props = FileWatchProps_defaults();
    props.latencyMs = 80u;
    props.debounceMs = 60u;
    props.recursive = true;
    props.emitDirs = false;
    props.ignoreHidden = !includeHidden;
    props.ignorePatterns = defaultIgnores;
    props.ignoreCount = sizeof(defaultIgnores) / sizeof(defaultIgnores[0]);

    FileEventWatcher *watcher = FileEventWatcher(resolved, FILE_EVENT_ALL, &props, onWatchEvent, &app);
    if (!watcher) {
        fputs("difference: failed to create file watcher\n", stderr);
        if (useTui)
            DifferenceTui_free(&tui);
        SnapshotStore_free(&app.store);
        Memory_freeAll();
        return 1;
    }

    if (!FileEventWatcher_start(watcher)) {
        fputs("difference: failed to start file watcher\n", stderr);
        FileEventWatcher_destroy(watcher);
        if (useTui)
            DifferenceTui_free(&tui);
        SnapshotStore_free(&app.store);
        Memory_freeAll();
        return 1;
    }

    i32 rc = 0;
    if (useTui) {
        if (SnapshotSeeder_begin(&app.seeder, &app.store, resolved, watcher))
            DifferenceTui_setIdleHook(&tui, onIdleSeed, &app);
        rc = DifferenceTui_run(&tui, watcher);
        SnapshotSeeder_finish(&app.seeder);
    } else {
        (discard) SnapshotStore_seedDir(&app.store, resolved, watcher);
        signal(SIGINT, onSignal);
        signal(SIGTERM, onSignal);
        fprint(stdout, "difference: watching ", resolved, " (", (size_t) SnapshotStore_count(&app.store), " files tracked; Ctrl-C to stop)");
        fflush(stdout);

        const struct timespec nap = { 0, 50000000L };
        while (!atomic_load_explicit(&g_stopRequested, memory_order_acquire)) {
            FileEventWatcher_pump(watcher);
            nanosleep(&nap, nullptr);
        }
    }

    FileEventWatcher_stop(watcher);
    FileEventWatcher_pump(watcher);
    FileEventWatcher_destroy(watcher);
    if (useTui)
        DifferenceTui_free(&tui);
    SnapshotStore_free(&app.store);
    Memory_freeAll();
    return rc;
}
