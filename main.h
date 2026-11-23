#ifndef DIFFERENCE_MAIN_H
#define DIFFERENCE_MAIN_H

#include "vex.h"
#include <stdio.h>
#include "difference_printer.h"
#include "difference_tui.h"
#include "snapshot_store.h"

/**
 * Runtime state container for the difference CLI watch loop.
 *
 * SnapshotStore store         - in-memory previous file contents map
 * SnapshotSeeder seeder       - incremental directory seeder for TUI startup
 * DifferencePrinter printer   - stream-mode diff formatter
 * DifferenceTui *tui          - active fullscreen TUI instance (nullptr in stream mode)
 * FILE *out                   - output stream for stream mode
 * bytes lastReportedFiles     - last tracked-file count pushed to the TUI header
 * utf8 settingsPath[1024]     - resolved manifest settings file path
 * bool haveSettingsPath       - true when settingsPath was resolved successfully
 */
typedef struct WatchApp {
    SnapshotStore store;
    SnapshotSeeder seeder;
    DifferencePrinter printer;
    DifferenceTui *tui;
    FILE *out;
    bytes lastReportedFiles;
    utf8 settingsPath[1024];
    bool haveSettingsPath;
} WatchApp;

#endif
