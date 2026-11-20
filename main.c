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
#include "difference_tui.h"
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

typedef struct WatchApp {
    SnapshotStore store;
    DifferencePrinter printer;
    DifferenceTui *tui;
    FILE *out;
} WatchApp;

;;GETTER
static inline SnapshotStore *App_storeMut(WatchApp *a) { return &(*a).store; }
;;GETTER
static inline DifferencePrinter *App_printerMut(WatchApp *a) { return &(*a).printer; }
;;GETTER
static inline DifferenceTui *App_tui(const WatchApp *a) { return (*a).tui; }
;;GETTER
static inline FILE *App_out(const WatchApp *a) { return (*a).out; }
;;GETTER
static inline const char *Ev_path(const FileWatchEvent *e) { return (*e).path; }
;;GETTER
static inline FileEventType Ev_type(const FileWatchEvent *e) { return (*e).type; }
;;GETTER
static inline const char *Snap_data(const SnapshotEntry *s) { return (*s).data; }
;;GETTER
static inline bytes Snap_length(const SnapshotEntry *s) { return (*s).length; }

/** Compares a CLI argument against an expected flag string. */
static bool argEq(const char *a, const char *b) {
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
                   const char *oldData, bytes oldLen,
                   const char *newData, bytes newLen) {
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
    const char *path = Ev_path(event);
    FileEventType type = Ev_type(event);
    if (!path || !app)
        return;

    SnapshotStore *store = App_storeMut(app);
    const SnapshotEntry *prev = SnapshotStore_find(store, path);
    const char *oldData = prev ? Snap_data(prev) : "";
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
    char *newData = SnapshotStore_readFile(store, path, &newLen, &isBinary);
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

/** Prints CLI usage summary to stderr. */
static fn printUsage() {
    fputs("Usage: difference [dir] [--tui|--stream] [--unified|--split] [--cols N] [--no-color] [--all]\n", stderr);
}

/** Watches the target directory (default cwd) in TUI mode (TTY) or stream mode. */
i32 main(i32 argc, char **argv) {
    const char *targetArg = ".";
    TerminalDiffLayoutMode layout = TERMINAL_DIFF_LAYOUT_AUTO;
    u32 overrideCols = 0u;
    bool isTty = isatty(STDOUT_FILENO) != 0 && isatty(STDIN_FILENO) != 0;
    bool useColor = isatty(STDOUT_FILENO) != 0;
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
        else if (argEq(argv[i], "--unified"))
            layout = TERMINAL_DIFF_LAYOUT_UNIFIED;
        else if (argEq(argv[i], "--split"))
            layout = TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE;
        else if (argEq(argv[i], "--no-color"))
            useColor = false;
        else if (argEq(argv[i], "--all"))
            includeHidden = true;
        else if (argEq(argv[i], "--cols") && i + 1 < argc)
            overrideCols = (u32) atoi(argv[++i]);
        else if (argv[i][0] != '-')
            targetArg = argv[i];
        else {
            fprintf(stderr, "difference: unknown option '%s'\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    char resolved[PATH_MAX];
    if (!realpath(targetArg, resolved)) {
        fprintf(stderr, "difference: cannot resolve path '%s'\n", targetArg);
        return 1;
    }

    if (!Memory_init(0)) {
        fputs("difference: failed to initialize engine memory\n", stderr);
        return 1;
    }

    WatchApp app;
    memset(&app, 0, sizeof(app));
    app.out = stdout;
    SnapshotStore_init(&app.store, 256u * 1024u);
    DifferencePrinter_init(&app.printer);
    DifferencePrinter_setLayoutMode(&app.printer, layout);
    DifferencePrinter_setColumns(&app.printer, overrideCols);
    DifferencePrinter_setColor(&app.printer, useColor);

    DifferenceTui tui;
    if (useTui) {
        if (!DifferenceTui_init(&tui, resolved, layout, useColor)) {
            fputs("difference: failed to initialize TUI\n", stderr);
            SnapshotStore_free(&app.store);
            Memory_freeAll();
            return 1;
        }
        app.tui = &tui;
    }

    const char *defaultIgnores[] = {
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

    FileEventWatcher *watcher = FileEventWatcher_createWithProps(resolved, FILE_EVENT_ALL, &props, onWatchEvent, &app);
    if (!watcher) {
        fputs("difference: failed to create file watcher\n", stderr);
        if (useTui)
            DifferenceTui_free(&tui);
        SnapshotStore_free(&app.store);
        Memory_freeAll();
        return 1;
    }

    (discard) SnapshotStore_seedDir(&app.store, resolved, watcher);
    if (useTui)
        DifferenceTui_setTrackedFiles(&tui, SnapshotStore_count(&app.store));

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
    if (useTui)
        rc = DifferenceTui_run(&tui, watcher);
    else {
        signal(SIGINT, onSignal);
        signal(SIGTERM, onSignal);
        fprintf(stdout, "difference: watching %s (%zu files tracked; Ctrl-C to stop)\n",
                resolved, (size_t) SnapshotStore_count(&app.store));
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
