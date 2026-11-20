// Consolidated automated owner test suite for vex/difference.
// Usage: difference_test [engine|printer|snapshot]

#include "vex.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/file/file.h"
#include "nio/mem.h"
#include "difference/diff_engine.h"
#include "difference/difference_printer.h"
#include "difference/difference_tui.h"
#include "difference/snapshot_store.h"

;;GETTER
static inline const char *SnapEntry_data(const SnapshotEntry *e) { return (*e).data; }
;;GETTER
static inline bytes SnapEntry_length(const SnapshotEntry *e) { return (*e).length; }

/** Proves line diffing, hunk grouping, and inline word/letter span detection. */
static fn suite_engine() {
    assert(Memory_init(0));
    const char *oldText = "alpha\ni32 count = 10;\nomega\n";
    const char *newText = "alpha\ni32 total = 10;\nomega\nextra\n";

    DiffResult diff;
    assert(DiffEngine_compute(oldText, strlen(oldText), newText, strlen(newText), 1u, &diff));
    assert(diff.hunkCount == 1u);
    assert(diff.linesAdded == 2u);
    assert(diff.linesRemoved == 1u);
    assert(diff.spanCount == 2u);
    assert(diff.spans[0].start == 4u && diff.spans[0].length == 5u);
    assert(diff.spans[1].start == 4u && diff.spans[1].length == 5u);
    DiffEngine_free(&diff);

    DiffResult same;
    assert(DiffEngine_compute(oldText, strlen(oldText), oldText, strlen(oldText), 3u, &same));
    assert(same.hunkCount == 0u && same.linesAdded == 0u && same.linesRemoved == 0u);
    DiffEngine_free(&same);
    Memory_freeAll();
}

/** Proves DifferencePrinter custom header, responsive split vs unified modes, and +/- stat bar. */
static fn suite_printer() {
    assert(Memory_init(0));
    const char *oldText = "line one\nhello world\nline three\n";
    const char *newText = "line one\nhello vexgraph\nline three\n";

    DiffResult diff;
    assert(DiffEngine_compute(oldText, strlen(oldText), newText, strlen(newText), 2u, &diff));

    FileWatchEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.path = "/tmp/demo/hello.c";
    ev.relativePath = "hello.c";
    ev.type = FILE_EVENT_MODIFIED;
    ev.epochSeconds = 1763611200LL;
    ev.fileSize = (i64) strlen(newText);

    DifferencePrinter printer;
    DifferencePrinter_init(&printer);
    DifferencePrinter_setColor(&printer, false);

    char wideBuf[4096];
    memset(wideBuf, 0, sizeof(wideBuf));
    FILE *wideOut = fmemopen(wideBuf, sizeof(wideBuf) - 1u, "wb");
    assert(wideOut != nullptr);
    DifferencePrinter_setColumns(&printer, 120u);
    assert(DifferencePrinter_print(&printer, &ev, &diff, wideOut));
    fclose(wideOut);
    assert(strstr(wideBuf, "#1 ") != nullptr);
    assert(strstr(wideBuf, " EDIT ") != nullptr);
    assert(strstr(wideBuf, "hello.c") != nullptr);
    assert(strstr(wideBuf, "+1 -1") != nullptr);
    assert(strstr(wideBuf, "[side-by-side 120c]") != nullptr);

    char narrowBuf[4096];
    memset(narrowBuf, 0, sizeof(narrowBuf));
    FILE *narrowOut = fmemopen(narrowBuf, sizeof(narrowBuf) - 1u, "wb");
    assert(narrowOut != nullptr);
    DifferencePrinter_setColumns(&printer, 72u);
    assert(DifferencePrinter_print(&printer, &ev, &diff, narrowOut));
    fclose(narrowOut);
    assert(strstr(narrowBuf, "#2 ") != nullptr);
    assert(strstr(narrowBuf, "[unified 72c]") != nullptr);

    DiffEngine_free(&diff);
    Memory_freeAll();
}

/** Proves SnapshotStore seeding, lookup, update, binary skip, and removal. */
static fn suite_snapshot() {
    assert(Memory_init(0));
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/vex_diff_snap_%d", base, getpid());
    File_delete(dir);
    assert(File_mkdirs(dir));

    char txtPath[320];
    char binPath[320];
    snprintf(txtPath, sizeof(txtPath), "%s/sample.c", dir);
    snprintf(binPath, sizeof(binPath), "%s/blob.bin", dir);

    FILE *tf = fopen(txtPath, "wb");
    assert(tf != nullptr);
    fwrite("int x = 1;\n", 1, 11, tf);
    fclose(tf);

    FILE *bf = fopen(binPath, "wb");
    assert(bf != nullptr);
    const u8 raw[4] = { 0x7F, 'E', 'L', 0x00 };
    fwrite(raw, 1, 4, bf);
    fclose(bf);

    SnapshotStore store;
    SnapshotStore_init(&store, 1u << 20);
    assert(SnapshotStore_seedDir(&store, dir, nullptr));
    assert(SnapshotStore_count(&store) == 1u);
    const SnapshotEntry *e = SnapshotStore_find(&store, txtPath);
    assert(e != nullptr && SnapEntry_length(e) == 11u);
    assert(SnapshotStore_find(&store, binPath) == nullptr);

    assert(SnapshotStore_put(&store, txtPath, "int x = 2;\n", 11u));
    e = SnapshotStore_find(&store, txtPath);
    assert(e != nullptr && memcmp(SnapEntry_data(e), "int x = 2;\n", 11u) == 0);
    assert(SnapshotStore_remove(&store, txtPath));
    assert(SnapshotStore_count(&store) == 0u);

    SnapshotStore_free(&store);
    File_delete(txtPath);
    File_delete(binPath);
    Memory_freeAll();
}

/** Proves DifferenceTui headless push, wide-to-narrow resize reflow, and keybind handling. */
static fn suite_tui() {
    assert(Memory_init(0));
    DifferenceTui tui;
    assert(DifferenceTui_init(&tui, "/tmp/demo", TERMINAL_DIFF_LAYOUT_AUTO, true));
    tui.outFd = -1;

    FileWatchEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.path = "/tmp/demo/app.c";
    ev.relativePath = "app.c";
    ev.type = FILE_EVENT_MODIFIED;
    ev.epochSeconds = 1763611200LL;
    ev.fileSize = 32;

    const char *a = "alpha\nhello world\nomega\n";
    const char *b = "alpha\nhello vexgraph\nomega\n";
    assert(DifferenceTui_pushEdit(&tui, &ev, a, strlen(a), b, strlen(b)));

    assert(DifferenceTui_resize(&tui, 130u, 32u));
    DifferenceTui_renderFrame(&tui);
    assert(tui.totalRows > 0u);

    assert(DifferenceTui_resize(&tui, 64u, 16u));
    DifferenceTui_renderFrame(&tui);
    assert(tui.totalRows > 0u);

    DifferenceTui_handleInputByte(&tui, 's');
    DifferenceTui_renderFrame(&tui);
    DifferenceTui_handleInputByte(&tui, 'k');
    DifferenceTui_handleInputByte(&tui, 'G');
    DifferenceTui_handleInputByte(&tui, 'c');
    assert(tui.recordCount == 0u);

    DifferenceTui_free(&tui);
    Memory_freeAll();
}

typedef struct SuiteEntry {
    const char *name;
    fn (*run)();
} SuiteEntry;

/** Runs all automated vex/difference suites or a single named suite. */
i32 main(i32 argc, char **argv) {
    const SuiteEntry suites[] = {
        { "engine", suite_engine },
        { "printer", suite_printer },
        { "snapshot", suite_snapshot },
        { "tui", suite_tui },
    };
    const char *filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && strcmp(filter, suites[i].name) != 0)
            continue;
        suites[i].run();
        ran++;
    }
    if (filter && ran == 0) {
        fprintf(stderr, "difference_test: unknown suite '%s'\n", filter);
        return 2;
    }
    puts("difference_test: PASS");
    return 0;
}
