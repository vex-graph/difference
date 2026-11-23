/**
 * Consolidated automated owner test suite for vex/difference.
 * Usage: difference_test [engine|printer|snapshot]
 */

#include "vex.h"
#include "re/primitive/vex-string.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/file/file.h"
#include "nio/mem.h"
#include "difference/diff_engine.h"
#include "difference/difference_printer.h"
#include "difference/difference_settings.h"
#include "difference/difference_tui.h"
#include "difference/snapshot_store.h"
#include "difference_test.h"

;;GETTER
static inline string SnapEntry_data(const SnapshotEntry *e)
{
    return (*e).data;
}
;;GETTER
static inline bytes SnapEntry_length(const SnapshotEntry *e)
{
    return (*e).length;
}

/** Proves line diffing, hunk grouping, and inline word/letter span detection. */
static fn suite_engine() {
    assert(Memory_init(0));
    string oldText = "alpha\ni32 count = 10;\nomega\n";
    string newText = "alpha\ni32 total = 10;\nomega\nextra\n";

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

    string nlOld = "alpha\nomega\n";
    string nlNew = "alpha\n\nomega\n";
    DiffResult nlDiff;
    assert(DiffEngine_compute(nlOld, strlen(nlOld), nlNew, strlen(nlNew), 1u, &nlDiff));
    assert(nlDiff.linesAdded == 1u && nlDiff.linesRemoved == 0u);
    assert(nlDiff.hunkCount == 1u);
    bool sawBlankAdd = false;
    for (bytes i = 0u; i < nlDiff.hunks[0].lineCount; i++) {
        if (nlDiff.hunks[0].lines[i].kind == TERMINAL_DIFF_ADDED) {
            assert(nlDiff.hunks[0].lines[i].length == 0u);
            assert(nlDiff.hunks[0].lines[i].text[0] == '\0');
            sawBlankAdd = true;
        }
    }
    assert(sawBlankAdd);
    DiffEngine_free(&nlDiff);
    Memory_freeAll();
}

/** Proves DifferencePrinter custom header, responsive split vs unified modes, and +/- stat bar. */
static fn suite_printer() {
    assert(Memory_init(0));
    string oldText = "line one\nhello world\nline three\n";
    string newText = "line one\nhello vexgraph\nline three\n";

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

    utf8 wideBuf[4096];
    memset(wideBuf, 0, sizeof(wideBuf));
    FILE *wideOut = fmemopen(wideBuf, sizeof(wideBuf) - 1u, "wb");
    assert(wideOut != nullptr);
    DifferencePrinter_setColumns(&printer, 120u);
    assert(DifferencePrinter_print(&printer, &ev, &diff, wideOut));
    fclose(wideOut);
    assert(string_contains(wideBuf, "#1 "));
    assert(string_contains(wideBuf, " EDIT "));
    assert(string_contains(wideBuf, "hello.c"));
    assert(string_contains(wideBuf, "+1 -1"));
    assert(string_contains(wideBuf, "[side-by-side 120c]"));

    utf8 narrowBuf[4096];
    memset(narrowBuf, 0, sizeof(narrowBuf));
    FILE *narrowOut = fmemopen(narrowBuf, sizeof(narrowBuf) - 1u, "wb");
    assert(narrowOut != nullptr);
    DifferencePrinter_setColumns(&printer, 72u);
    assert(DifferencePrinter_print(&printer, &ev, &diff, narrowOut));
    fclose(narrowOut);
    assert(string_contains(narrowBuf, "#2 "));
    assert(string_contains(narrowBuf, "[unified 72c]"));

    DiffEngine_free(&diff);
    Memory_freeAll();
}

/** Proves SnapshotStore seeding, lookup, update, binary skip, and removal. */
static fn suite_snapshot() {
    assert(Memory_init(0));
    string base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    utf8 dir[256];
    snprintf(dir, sizeof(dir), "%s/vex_diff_snap_%d", base, getpid());
    File_delete(dir);
    assert(File_mkdirs(dir));

    utf8 txtPath[320];
    utf8 binPath[320];
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

    for (u32 i = 0u; i < 300u; i++) {
        utf8 k[64];
        utf8 v[32];
        snprintf(k, sizeof(k), "/virtual/file_%u.c", (unsigned) i);
        snprintf(v, sizeof(v), "val=%u", (unsigned) i);
        assert(SnapshotStore_put(&store, k, v, strlen(v)));
    }
    assert(SnapshotStore_count(&store) == 300u);
    for (u32 i = 0u; i < 150u; i++) {
        utf8 k[64];
        snprintf(k, sizeof(k), "/virtual/file_%u.c", (unsigned) (i * 2u));
        assert(SnapshotStore_remove(&store, k));
        assert(SnapshotStore_find(&store, k) == nullptr);
    }
    assert(SnapshotStore_count(&store) == 150u);
    for (u32 i = 0u; i < 150u; i++) {
        utf8 k[64];
        utf8 v[32];
        snprintf(k, sizeof(k), "/virtual/file_%u.c", (unsigned) (i * 2u + 1u));
        snprintf(v, sizeof(v), "val=%u", (unsigned) (i * 2u + 1u));
        e = SnapshotStore_find(&store, k);
        assert(e != nullptr && SnapEntry_length(e) == strlen(v));
        assert(memcmp(SnapEntry_data(e), v, strlen(v)) == 0);
    }

    SnapshotStore_setMaxWorkspaceBytes(&store, SnapshotStore_totalDataBytes(&store) + 16u);
    assert(!SnapshotStore_put(&store, "/virtual/over_cap.c", "0123456789abcdef0123456789", 26u));
    SnapshotStore_free(&store);

    utf8 txt2Path[320];
    snprintf(txt2Path, sizeof(txt2Path), "%s/second.c", dir);
    FILE *tf2 = fopen(txt2Path, "wb");
    assert(tf2 != nullptr);
    fwrite("int y = 9;\n", 1, 11, tf2);
    fclose(tf2);

    SnapshotStore incStore;
    SnapshotStore_init(&incStore, 1u << 20);
    assert(SnapshotStore_put(&incStore, txtPath, "live edit\n", 10u));
    SnapshotSeeder seeder;
    assert(SnapshotSeeder_begin(&seeder, &incStore, dir, nullptr));
    u32 steps = 0u;
    while (SnapshotSeeder_step(&seeder, 1u))
        steps++;
    SnapshotSeeder_finish(&seeder);
    assert(steps >= 1u);
    assert(SnapshotStore_count(&incStore) == 2u);
    e = SnapshotStore_find(&incStore, txtPath);
    assert(e != nullptr && SnapEntry_length(e) == 10u && memcmp(SnapEntry_data(e), "live edit\n", 10u) == 0);
    e = SnapshotStore_find(&incStore, txt2Path);
    assert(e != nullptr && SnapEntry_length(e) == 11u);
    SnapshotStore_free(&incStore);

    File_delete(txtPath);
    File_delete(txt2Path);
    File_delete(binPath);
    Memory_freeAll();
}

/** Proves DifferenceTui collapse-by-default, click/key overlays, +/- steppers, horizontal scroll, and Mode-2048 resize. */
static fn suite_tui() {
    assert(Memory_init(0));
    i32 pipeFds[2] = { -1, -1 };
    assert(pipe(pipeFds) == 0);

    DifferenceTui tui;
    assert(DifferenceTui_init(&tui, "/tmp/demo", TERMINAL_DIFF_LAYOUT_AUTO, true));
    DifferenceTui_setOutputFd(&tui, pipeFds[1]);
    DifferenceTui_setTrackedFiles(&tui, 42u);

    FileWatchEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.path = "/tmp/demo/app.c";
    ev.relativePath = "app.c";
    ev.type = FILE_EVENT_MODIFIED;
    ev.epochSeconds = 1763611200LL;
    ev.fileSize = 2048;

    string a = "alpha\nhello world with a very long line of code for horizontal scrolling\nomega\n";
    string b = "alpha\nhello vexgraph with a very long line of code for horizontal scrolling\nomega\n";
    assert(DifferenceTui_pushEdit(&tui, &ev, a, strlen(a), b, strlen(b)));
    assert(tui.collapseMode == true);
    assert(tui.records[0].expanded == false);
    assert(tui.totalAdded == 1u && tui.totalRemoved == 1u);

    assert(DifferenceTui_resize(&tui, 130u, 32u));
    DifferenceTui_renderFrame(&tui);
    assert(tui.totalRows == 1u);
    assert(TerminalSurface_presentCount(&tui.surface) == 1u);

    utf8 outBuf[4096];
    i64 emitted = read(pipeFds[0], outBuf, sizeof(outBuf));
    assert(emitted > 0);

    DifferenceTui_setOutputFd(&tui, -1);

    DifferenceTui_handleInputByte(&tui, '\r');
    assert(tui.records[0].expanded == true);
    DifferenceTui_renderFrame(&tui);
    assert(tui.totalRows > 1u);

    DifferenceTui_handleKeySeq(&tui, "\x1b[C", 3u);
    assert(TerminalDiffPanel_scrollX(&tui.panel) == 4u);
    DifferenceTui_handleInputByte(&tui, '0');
    assert(TerminalDiffPanel_scrollX(&tui.panel) == 0u);

    DifferenceTui_handleKeySeq(&tui, "\x1b[48;24;90;0;0t", 15u);
    assert(tui.width == 90u && tui.height == 24u);

    DifferenceTui_handleInputByte(&tui, '?');
    assert(tui.overlay == DIFFERENCE_OVERLAY_HELP);
    DifferenceTui_renderFrame(&tui);
    DifferenceTui_handleKeySeq(&tui, "\x1b", 1u);
    assert(tui.overlay == DIFFERENCE_OVERLAY_NONE);

    DifferenceTui_handleInputByte(&tui, 15);
    assert(tui.overlay == DIFFERENCE_OVERLAY_SETTINGS);
    DifferenceTui_renderFrame(&tui);
    utf8 clickPlus[32];
    snprintf(clickPlus, sizeof(clickPlus), "\x1b[<0;%zu;%zuM",
             (size_t) (tui.ctxPlusX0 + 1u), (size_t) (tui.ctxRowY + 1u));
    for (bytes i = 0u; clickPlus[i]; i++)
        DifferenceTui_handleInputByte(&tui, (u8) clickPlus[i]);
    assert(tui.contextLines == 4u);
    utf8 clickMinus[32];
    snprintf(clickMinus, sizeof(clickMinus), "\x1b[<0;%zu;%zuM",
             (size_t) (tui.ctxMinusX0 + 1u), (size_t) (tui.ctxRowY + 1u));
    for (bytes i = 0u; clickMinus[i]; i++)
        DifferenceTui_handleInputByte(&tui, (u8) clickMinus[i]);
    assert(tui.contextLines == 3u);
    assert(tui.maxEdits == 1024u);
    assert(tui.workspaceLimitMb == 1024u);

    utf8 clickWs[32];
    snprintf(clickWs, sizeof(clickWs), "\x1b[<0;%zu;%zuM",
             (size_t) (tui.wsEditX0 + 1u), (size_t) (tui.wsRowY + 1u));
    for (bytes i = 0u; clickWs[i]; i++)
        DifferenceTui_handleInputByte(&tui, (u8) clickWs[i]);
    assert(tui.editingSetting == 6);
    DifferenceTui_renderFrame(&tui);
    DifferenceTui_handleInputByte(&tui, 0x7F);
    DifferenceTui_handleInputByte(&tui, 0x7F);
    DifferenceTui_handleInputByte(&tui, 0x7F);
    DifferenceTui_handleInputByte(&tui, 0x7F);
    DifferenceTui_handleInputByte(&tui, '5');
    DifferenceTui_handleInputByte(&tui, '1');
    DifferenceTui_handleInputByte(&tui, '2');
    DifferenceTui_handleKeySeq(&tui, "\x1b[D", 3u);
    DifferenceTui_handleKeySeq(&tui, "\x1b[C", 3u);
    DifferenceTui_handleInputByte(&tui, '\r');
    assert(tui.editingSetting == -1);
    assert(tui.workspaceLimitMb == 512u);

    DifferenceTui_handleInputByte(&tui, 15);
    assert(tui.overlay == DIFFERENCE_OVERLAY_NONE);

    DifferenceTui_handleKeySeq(&tui, "\x1bo", 2u);
    assert(tui.collapseMode == false);
    assert(tui.records[0].expanded == true);
    DifferenceTui_handleInputByte(&tui, 0xC3);
    DifferenceTui_handleInputByte(&tui, 0xB8);
    assert(tui.collapseMode == true);
    assert(tui.records[0].expanded == false);

    DifferenceTui_setMaxEdits(&tui, 100u);
    assert(tui.maxEdits == 100u);
    for (u32 k = 0u; k < 105u; k++)
        assert(DifferenceTui_pushEdit(&tui, &ev, a, strlen(a), b, strlen(b)));
    assert(tui.recordCount == 100u);
    assert(tui.records[0].sequence == 7u);
    assert(tui.records[99].sequence == 106u);
    DifferenceTui_handleInputByte(&tui, 'G');
    DifferenceTui_renderFrame(&tui);
    bytes bodyH = tui.height - 2u;
    bytes firstVisRow = 1u * tui.width;
    bytes lastVisRow = (1u + bodyH - 1u) * tui.width;
    assert(tui.backCells[firstVisRow + 2u] == ' ');
    assert(tui.backCells[firstVisRow + 3u] == '#');
    assert(tui.backCells[lastVisRow] == 0x25CFu);
    assert(tui.backCells[lastVisRow + 2u] == '#');
    assert(tui.backCells[lastVisRow + 3u] == '1');
    assert(tui.backCells[lastVisRow + 4u] == '0');
    assert(tui.backCells[lastVisRow + 5u] == '6');

    DifferenceTui_handleInputByte(&tui, 'c');
    assert(tui.recordCount == 0u);
    assert(tui.totalAdded == 0u && tui.totalRemoved == 0u);

    DifferenceTui_free(&tui);
    close(pipeFds[0]);
    close(pipeFds[1]);
    Memory_freeAll();
}

static u32 g_settingsHookCalls = 0u;
static DifferenceSettings g_lastHookSettings;

/** Captures live TUI setting-change notifications during unit testing. */
static fn onTestSettingsChanged(const DifferenceSettings *s, any userdata) {
    (discard) userdata;
    g_settingsHookCalls++;
    if (s)
        g_lastHookSettings = *s;
}

/** Proves manifest path resolution, atomic save/reload, malformed rejection, and TUI hook wiring. */
static fn suite_settings() {
    assert(Memory_init(0));
    string base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    utf8 manifestRoot[256];
    snprintf(manifestRoot, sizeof(manifestRoot), "%s/vex_diff_manifest_%d", base, getpid());
    assert(setenv("VEX_MANIFEST", manifestRoot, 1) == 0);

    utf8 resolvedPath[512];
    assert(DifferenceSettings_resolvePath(resolvedPath, sizeof(resolvedPath)));
    assert(string_contains(resolvedPath, "/vexgraph/difference/settings"));

    DifferenceSettings s = DifferenceSettings_0();
    assert(DifferenceSettings_load(&s, resolvedPath));
    assert(DifferenceSettings_isCollapseMode(&s) == true);
    assert(DifferenceSettings_getDiffLayout(&s) == TERMINAL_DIFF_LAYOUT_AUTO);
    assert(DifferenceSettings_isFollowTail(&s) == true);
    assert(DifferenceSettings_isColor(&s) == true);
    assert(DifferenceSettings_isDiffIcons(&s) == true);
    assert(DifferenceSettings_getContextLines(&s) == 3u);
    assert(DifferenceSettings_getWorkspaceLimitMb(&s) == 1024u);
    assert(DifferenceSettings_getMaxEdits(&s) == 1024u);

    DifferenceSettings_setCollapseMode(&s, false);
    assert(DifferenceSettings_setDiffLayout(&s, TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE));
    DifferenceSettings_setFollowTail(&s, false);
    DifferenceSettings_setColor(&s, false);
    DifferenceSettings_setDiffIcons(&s, false);
    assert(DifferenceSettings_setContextLines(&s, 7u));
    assert(DifferenceSettings_setWorkspaceLimitMb(&s, 512u));
    assert(DifferenceSettings_setMaxEdits(&s, 400u));
    assert(DifferenceSettings_save(&s, resolvedPath));

    DifferenceSettings reloaded = DifferenceSettings_0();
    assert(DifferenceSettings_load(&reloaded, resolvedPath));
    assert(DifferenceSettings_isCollapseMode(&reloaded) == false);
    assert(DifferenceSettings_getDiffLayout(&reloaded) == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE);
    assert(DifferenceSettings_isFollowTail(&reloaded) == false);
    assert(DifferenceSettings_isColor(&reloaded) == false);
    assert(DifferenceSettings_isDiffIcons(&reloaded) == false);
    assert(DifferenceSettings_getContextLines(&reloaded) == 7u);
    assert(DifferenceSettings_getWorkspaceLimitMb(&reloaded) == 512u);
    assert(DifferenceSettings_getMaxEdits(&reloaded) == 400u);

    utf8 badPath[512];
    snprintf(badPath, sizeof(badPath), "%s.bad", resolvedPath);
    FILE *bf = fopen(badPath, "wb");
    assert(bf != nullptr);
    fputs("collapse_mode=0\nmax_edits=42\n", bf);
    fclose(bf);
    assert(!DifferenceSettings_load(&reloaded, badPath));
    assert(DifferenceSettings_getMaxEdits(&reloaded) == 400u);
    unlink(badPath);

    DifferenceTui tui;
    assert(DifferenceTui_init(&tui, "/tmp/demo", TERMINAL_DIFF_LAYOUT_AUTO, true));
    DifferenceTui_setOutputFd(&tui, -1);
    g_settingsHookCalls = 0u;
    DifferenceTui_setSettingsChangedHook(&tui, onTestSettingsChanged, nullptr);
    DifferenceTui_applySettings(&tui, &reloaded);
    assert(g_settingsHookCalls == 0u);

    DifferenceSettings exported;
    DifferenceTui_exportSettings(&tui, &exported);
    assert(DifferenceSettings_isCollapseMode(&exported) == false);
    assert(DifferenceSettings_getDiffLayout(&exported) == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE);
    assert(DifferenceSettings_isFollowTail(&exported) == false);
    assert(DifferenceSettings_isColor(&exported) == false);
    assert(DifferenceSettings_isDiffIcons(&exported) == false);
    assert(DifferenceSettings_getContextLines(&exported) == 7u);
    assert(DifferenceSettings_getWorkspaceLimitMb(&exported) == 512u);
    assert(DifferenceSettings_getMaxEdits(&exported) == 400u);

    DifferenceTui_handleInputByte(&tui, 's');
    assert(g_settingsHookCalls == 1u);
    assert(DifferenceSettings_getDiffLayout(&g_lastHookSettings) == TERMINAL_DIFF_LAYOUT_UNIFIED);
    DifferenceTui_handleInputByte(&tui, 'f');
    assert(g_settingsHookCalls == 2u);
    assert(DifferenceSettings_isFollowTail(&g_lastHookSettings) == true);
    DifferenceTui_setMaxEdits(&tui, 300u);
    assert(g_settingsHookCalls == 3u);
    assert(DifferenceSettings_getMaxEdits(&g_lastHookSettings) == 300u);
    assert(DifferenceSettings_save(&g_lastHookSettings, resolvedPath));

    DifferenceTui_free(&tui);
    unlink(resolvedPath);
    Memory_freeAll();
}



/** Runs all automated vex/difference suites or a single named suite. */
i32 main(i32 argc, utf8 **argv) {
    const SuiteEntry suites[] = {
        { "engine", suite_engine },
        { "printer", suite_printer },
        { "snapshot", suite_snapshot },
        { "tui", suite_tui },
        { "settings", suite_settings },
    };
    string filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && !string_equals(filter, suites[i].name))
            continue;
        suites[i].run();
        ran++;
    }
    if (filter && ran == 0) {
        fprint(stderr, "difference_test: unknown suite '", filter, "'");
        return 2;
    }
    puts("difference_test: PASS");
    return 0;
}
