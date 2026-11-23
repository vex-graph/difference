#ifndef DIFFERENCE_DIFF_ENGINE_H
#define DIFFERENCE_DIFF_ENGINE_H

#include "vex.h"
#include "nio/mem.h"
#include "ui/panel/terminal_diff_panel.h"

/** diff_engine.h, line-level + inline word/letter diff engine for difference. */

/**
 * Borrowed single-line slice produced when splitting a text buffer.
 *
 * string text - pointer to the first byte of the line
 * bytes len        - byte length of the line excluding line endings
 */
typedef struct LineSlice {
    string text;
    bytes len;
} LineSlice;

/**
 * Single diff edit operation in the flat LCS edit script.
 *
 * TerminalDiffLineKind kind - context, added, or removed line classification
 * u32 oldLine               - 1-based line number in the old buffer (0 if added)
 * u32 newLine               - 1-based line number in the new buffer (0 if removed)
 * string text          - borrowed pointer to the line text
 * bytes len                 - byte length of the line text
 */
typedef struct EditOp {
    TerminalDiffLineKind kind;
    u32 oldLine;
    u32 newLine;
    string text;
    bytes len;
} EditOp;

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
                          string oldText, bytes oldLen,
                          string newText, bytes newLen,
                          u32 contextLines,
                          DiffResult *out);

bool DiffEngine_compute(string oldText, bytes oldLen,
                        string newText, bytes newLen,
                        u32 contextLines,
                        DiffResult *out);

fn DiffEngine_free(DiffResult *result);

#endif
