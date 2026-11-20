#ifndef DIFFERENCE_DIFF_ENGINE_H
#define DIFFERENCE_DIFF_ENGINE_H

#include "vex.h"
#include "nio/mem.h"
#include "ui/panel/terminal_diff_panel.h"

// diff_engine.h — line-level + inline word/letter diff engine for difference.

typedef struct DiffResult {
    MemoryArena *arena;
    bool ownsArena;
    TerminalDiffHunk *hunks;
    bytes hunkCount;
    TerminalDiffLine *lines;
    bytes lineCount;
    TerminalDiffSpan *spans;
    bytes spanCount;
    u32 linesAdded;
    u32 linesRemoved;
} DiffResult;

bool DiffEngine_computeIn(MemoryArena *arena, MemoryArena *scratch,
                          const char *oldText, bytes oldLen,
                          const char *newText, bytes newLen,
                          u32 contextLines,
                          DiffResult *out);

bool DiffEngine_compute(const char *oldText, bytes oldLen,
                        const char *newText, bytes newLen,
                        u32 contextLines,
                        DiffResult *out);

fn DiffEngine_free(DiffResult *result);

#endif
