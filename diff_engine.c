#include "vex.h"
#include "diff_engine.h"

#include <string.h>
#include "nio/mem.h"
#include "type/re_type.h"

;;DEFINITION
#define TYPE_DIFF_ENGINE Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 40u)
/**
 * ============================================================================
 * DEFINITION: DiffEngine
 * ============================================================================
 * Computes line-level hunks with configurable context lines and pairs adjacent
 * removed/added lines to compute inline word/letter highlight spans.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: DiffEngine (difference/diff_engine.c)
 * ============================================================================
 * Splits old/new buffers into borrowed line slices, trims common prefix/suffix,
 * runs bounded LCS on the changed middle, groups edits into TerminalDiffHunks,
 * and computes character/word diff spans inside an owned MemoryArena.
 * ============================================================================
 */

typedef struct LineSlice {
    const char *text;
    bytes len;
} LineSlice;

typedef struct EditOp {
    TerminalDiffLineKind kind;
    u32 oldLine;
    u32 newLine;
    const char *text;
    bytes len;
} EditOp;

enum {
    DIFF_MAX_DP_CELLS = 4000000u
};

;;SETTER
static inline fn Span_setStart(TerminalDiffSpan *s, bytes v) { (*s).start = v; }
;;SETTER
static inline fn Span_setLength(TerminalDiffSpan *s, bytes v) { (*s).length = v; }

;;GETTER
static inline MemoryArena *DiffRes_arena(const DiffResult *r) { return (*r).arena; }
;;SETTER
static inline fn DiffRes_setArena(DiffResult *r, MemoryArena *a) { (*r).arena = a; }
;;SETTER
static inline fn DiffRes_setHunks(DiffResult *r, TerminalDiffHunk *h) { (*r).hunks = h; }
;;SETTER
static inline fn DiffRes_setHunkCount(DiffResult *r, bytes n) { (*r).hunkCount = n; }
;;SETTER
static inline fn DiffRes_setLines(DiffResult *r, TerminalDiffLine *l) { (*r).lines = l; }
;;SETTER
static inline fn DiffRes_setLineCount(DiffResult *r, bytes n) { (*r).lineCount = n; }
;;SETTER
static inline fn DiffRes_setSpans(DiffResult *r, TerminalDiffSpan *s) { (*r).spans = s; }
;;SETTER
static inline fn DiffRes_setSpanCount(DiffResult *r, bytes n) { (*r).spanCount = n; }
;;SETTER
static inline fn DiffRes_setAdded(DiffResult *r, u32 n) { (*r).linesAdded = n; }
;;SETTER
static inline fn DiffRes_setRemoved(DiffResult *r, u32 n) { (*r).linesRemoved = n; }

/** Splits text into line slices (without trailing newline) allocated in arena. */
static LineSlice *splitLines(MemoryArena *arena, const char *text, bytes len, bytes *outCount) {
    *outCount = 0;
    if (!arena || !text || len == 0)
        return nullptr;

    bytes count = 0;
    for (bytes i = 0; i < len; i++)
        if (text[i] == '\n')
            count++;
    if (text[len - 1u] != '\n')
        count++;

    LineSlice *slices = (LineSlice*) MemoryArena_alloc(arena, TYPE_DIFF_ENGINE, count * sizeof(LineSlice));
    if (!slices)
        return nullptr;

    bytes idx = 0;
    bytes start = 0;
    for (bytes i = 0; i < len; i++) {
        if (text[i] == '\n') {
            bytes lineLen = i - start;
            if (lineLen > 0 && text[start + lineLen - 1u] == '\r')
                lineLen--;
            slices[idx].text = text + start;
            slices[idx].len = lineLen;
            idx++;
            start = i + 1u;
        }
    }
    if (start < len && idx < count) {
        bytes lineLen = len - start;
        if (lineLen > 0 && text[start + lineLen - 1u] == '\r')
            lineLen--;
        slices[idx].text = text + start;
        slices[idx].len = lineLen;
        idx++;
    }

    *outCount = idx;
    return slices;
}

/** Compares two line slices for exact byte equality. */
static bool lineEq(LineSlice a, LineSlice b) {
    if (a.len != b.len)
        return false;
    if (a.len == 0)
        return true;
    return memcmp(a.text, b.text, a.len) == 0;
}

/** Computes exact changed inline character/word span on a paired old/new line. */
static fn computeInlineSpan(const char *oldText, bytes oldLen,
                            const char *newText, bytes newLen,
                            TerminalDiffSpan *outOldSpan, bool *hasOld,
                            TerminalDiffSpan *outNewSpan, bool *hasNew) {
    *hasOld = false;
    *hasNew = false;
    bytes prefix = 0;
    bytes minLen = oldLen < newLen ? oldLen : newLen;
    while (prefix < minLen && oldText[prefix] == newText[prefix])
        prefix++;

    bytes oldTail = oldLen;
    bytes newTail = newLen;
    while (oldTail > prefix && newTail > prefix && oldText[oldTail - 1u] == newText[newTail - 1u]) {
        oldTail--;
        newTail--;
    }

    if (oldTail > prefix) {
        Span_setStart(outOldSpan, prefix);
        Span_setLength(outOldSpan, oldTail - prefix);
        *hasOld = true;
    }
    if (newTail > prefix) {
        Span_setStart(outNewSpan, prefix);
        Span_setLength(outNewSpan, newTail - prefix);
        *hasNew = true;
    }
}

/** Builds flat edit script via common prefix/suffix trimming and LCS DP on the middle range. */
static EditOp *buildEditOps(MemoryArena *arena,
                            const LineSlice *oldLines, bytes oldCount,
                            const LineSlice *newLines, bytes newCount,
                            bytes *outOpCount) {
    *outOpCount = 0;
    bytes prefix = 0;
    while (prefix < oldCount && prefix < newCount && lineEq(oldLines[prefix], newLines[prefix]))
        prefix++;

    bytes suffix = 0;
    while (suffix < (oldCount - prefix) && suffix < (newCount - prefix) &&
           lineEq(oldLines[oldCount - 1u - suffix], newLines[newCount - 1u - suffix]))
        suffix++;

    bytes midOld = oldCount - prefix - suffix;
    bytes midNew = newCount - prefix - suffix;
    bytes maxOps = oldCount + newCount;
    if (maxOps == 0)
        return nullptr;

    EditOp *ops = (EditOp*) MemoryArena_alloc(arena, TYPE_DIFF_ENGINE, maxOps * sizeof(EditOp));
    if (!ops)
        return nullptr;
    bytes opCount = 0;

    for (bytes i = 0; i < prefix; i++) {
        ops[opCount].kind = TERMINAL_DIFF_CONTEXT;
        ops[opCount].oldLine = (u32) (i + 1u);
        ops[opCount].newLine = (u32) (i + 1u);
        ops[opCount].text = oldLines[i].text;
        ops[opCount].len = oldLines[i].len;
        opCount++;
    }

    bool useDp = (midOld > 0 && midNew > 0 &&
                  (midOld + 1u) * (midNew + 1u) <= (bytes) DIFF_MAX_DP_CELLS);

    if (useDp) {
        bytes rows = midOld + 1u;
        bytes cols = midNew + 1u;
        u32 *dp = (u32*) MemoryArena_alloc(arena, TYPE_DIFF_ENGINE, rows * cols * sizeof(u32));
        if (dp) {
            memset(dp, 0, rows * cols * sizeof(u32));
            for (bytes i = midOld; i > 0; i--) {
                for (bytes j = midNew; j > 0; j--) {
                    bytes r = i - 1u;
                    bytes c = j - 1u;
                    if (lineEq(oldLines[prefix + r], newLines[prefix + c]))
                        dp[r * cols + c] = dp[(r + 1u) * cols + (c + 1u)] + 1u;
                    else {
                        u32 down = dp[(r + 1u) * cols + c];
                        u32 right = dp[r * cols + (c + 1u)];
                        dp[r * cols + c] = down >= right ? down : right;
                    }
                }
            }

            bytes r = 0;
            bytes c = 0;
            while (r < midOld || c < midNew) {
                if (r < midOld && c < midNew && lineEq(oldLines[prefix + r], newLines[prefix + c])) {
                    ops[opCount].kind = TERMINAL_DIFF_CONTEXT;
                    ops[opCount].oldLine = (u32) (prefix + r + 1u);
                    ops[opCount].newLine = (u32) (prefix + c + 1u);
                    ops[opCount].text = oldLines[prefix + r].text;
                    ops[opCount].len = oldLines[prefix + r].len;
                    opCount++;
                    r++;
                    c++;
                } else if (r < midOld && (c == midNew || dp[(r + 1u) * cols + c] >= dp[r * cols + (c + 1u)])) {
                    ops[opCount].kind = TERMINAL_DIFF_REMOVED;
                    ops[opCount].oldLine = (u32) (prefix + r + 1u);
                    ops[opCount].newLine = 0u;
                    ops[opCount].text = oldLines[prefix + r].text;
                    ops[opCount].len = oldLines[prefix + r].len;
                    opCount++;
                    r++;
                } else {
                    ops[opCount].kind = TERMINAL_DIFF_ADDED;
                    ops[opCount].oldLine = 0u;
                    ops[opCount].newLine = (u32) (prefix + c + 1u);
                    ops[opCount].text = newLines[prefix + c].text;
                    ops[opCount].len = newLines[prefix + c].len;
                    opCount++;
                    c++;
                }
            }
            MemoryArena_free(arena, dp);
        } else
            useDp = false;
    }

    if (!useDp) {
        for (bytes r = 0; r < midOld; r++) {
            ops[opCount].kind = TERMINAL_DIFF_REMOVED;
            ops[opCount].oldLine = (u32) (prefix + r + 1u);
            ops[opCount].newLine = 0u;
            ops[opCount].text = oldLines[prefix + r].text;
            ops[opCount].len = oldLines[prefix + r].len;
            opCount++;
        }
        for (bytes c = 0; c < midNew; c++) {
            ops[opCount].kind = TERMINAL_DIFF_ADDED;
            ops[opCount].oldLine = 0u;
            ops[opCount].newLine = (u32) (prefix + c + 1u);
            ops[opCount].text = newLines[prefix + c].text;
            ops[opCount].len = newLines[prefix + c].len;
            opCount++;
        }
    }

    for (bytes i = 0; i < suffix; i++) {
        bytes oi = oldCount - suffix + i;
        bytes ni = newCount - suffix + i;
        ops[opCount].kind = TERMINAL_DIFF_CONTEXT;
        ops[opCount].oldLine = (u32) (oi + 1u);
        ops[opCount].newLine = (u32) (ni + 1u);
        ops[opCount].text = oldLines[oi].text;
        ops[opCount].len = oldLines[oi].len;
        opCount++;
    }

    *outOpCount = opCount;
    return ops;
}

/** Computes grouped hunks and inline word/letter spans between oldText and newText. */
bool DiffEngine_compute(const char *oldText, bytes oldLen,
                        const char *newText, bytes newLen,
                        u32 contextLines,
                        DiffResult *out) {
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));

    MemoryArena *arena = MemoryArena_create(0);
    if (!arena)
        return false;

    bytes oldCount = 0;
    bytes newCount = 0;
    LineSlice *oldLines = splitLines(arena, oldText, oldLen, &oldCount);
    LineSlice *newLines = splitLines(arena, newText, newLen, &newCount);

    bytes opCount = 0;
    EditOp *ops = buildEditOps(arena, oldLines, oldCount, newLines, newCount, &opCount);
    MemoryArena_free(arena, oldLines);
    MemoryArena_free(arena, newLines);

    if (!ops || opCount == 0) {
        MemoryArena_destroy(arena);
        return true;
    }

    bytes hunkCap = 0;
    bytes i = 0;
    while (i < opCount) {
        if (ops[i].kind == TERMINAL_DIFF_CONTEXT) {
            i++;
            continue;
        }
        hunkCap++;
        while (i < opCount) {
            if (ops[i].kind != TERMINAL_DIFF_CONTEXT) {
                i++;
                continue;
            }
            bytes ctxRun = 0;
            while (i + ctxRun < opCount && ops[i + ctxRun].kind == TERMINAL_DIFF_CONTEXT)
                ctxRun++;
            if (i + ctxRun < opCount && ctxRun <= (bytes) contextLines * 2u)
                i += ctxRun;
            else
                break;
        }
    }

    if (hunkCap == 0) {
        MemoryArena_destroy(arena);
        return true;
    }

    TerminalDiffHunk *hunks = (TerminalDiffHunk*) MemoryArena_alloc(arena, TYPE_DIFF_ENGINE, hunkCap * sizeof(TerminalDiffHunk));
    TerminalDiffLine *lines = (TerminalDiffLine*) MemoryArena_alloc(arena, TYPE_DIFF_ENGINE, opCount * sizeof(TerminalDiffLine));
    TerminalDiffSpan *spans = (TerminalDiffSpan*) MemoryArena_alloc(arena, TYPE_DIFF_ENGINE, opCount * sizeof(TerminalDiffSpan));
    if (!hunks || !lines || !spans) {
        MemoryArena_destroy(arena);
        return false;
    }

    bytes hunkIdx = 0;
    bytes lineIdx = 0;
    bytes spanIdx = 0;
    u32 totalAdded = 0;
    u32 totalRemoved = 0;

    i = 0;
    while (i < opCount) {
        if (ops[i].kind == TERMINAL_DIFF_CONTEXT) {
            i++;
            continue;
        }
        bytes start = i > (bytes) contextLines ? i - (bytes) contextLines : 0u;
        bytes end = i;
        while (end < opCount) {
            if (ops[end].kind != TERMINAL_DIFF_CONTEXT) {
                end++;
                continue;
            }
            bytes ctxRun = 0;
            while (end + ctxRun < opCount && ops[end + ctxRun].kind == TERMINAL_DIFF_CONTEXT)
                ctxRun++;
            if (end + ctxRun < opCount && ctxRun <= (bytes) contextLines * 2u)
                end += ctxRun;
            else {
                bytes tail = ctxRun < (bytes) contextLines ? ctxRun : (bytes) contextLines;
                end += tail;
                break;
            }
        }

        bytes hunkLineStart = lineIdx;
        u32 oldStart = 0;
        u32 newStart = 0;
        u32 oldCnt = 0;
        u32 newCnt = 0;

        for (bytes k = start; k < end; k++) {
            lines[lineIdx].kind = ops[k].kind;
            lines[lineIdx].oldLine = ops[k].oldLine;
            lines[lineIdx].newLine = ops[k].newLine;
            lines[lineIdx].text = ops[k].text;
            lines[lineIdx].length = ops[k].len;
            lines[lineIdx].spans = nullptr;
            lines[lineIdx].spanCount = 0;
            if (ops[k].oldLine > 0) {
                if (oldStart == 0)
                    oldStart = ops[k].oldLine;
                oldCnt++;
            }
            if (ops[k].newLine > 0) {
                if (newStart == 0)
                    newStart = ops[k].newLine;
                newCnt++;
            }
            if (ops[k].kind == TERMINAL_DIFF_ADDED)
                totalAdded++;
            else if (ops[k].kind == TERMINAL_DIFF_REMOVED)
                totalRemoved++;
            lineIdx++;
        }

        bytes hLen = lineIdx - hunkLineStart;
        bytes p = 0;
        while (p < hLen) {
            if (lines[hunkLineStart + p].kind != TERMINAL_DIFF_REMOVED) {
                p++;
                continue;
            }
            bytes delStart = p;
            while (p < hLen && lines[hunkLineStart + p].kind == TERMINAL_DIFF_REMOVED)
                p++;
            bytes delCount = p - delStart;
            bytes addStart = p;
            while (p < hLen && lines[hunkLineStart + p].kind == TERMINAL_DIFF_ADDED)
                p++;
            bytes addCount = p - addStart;
            bytes pairs = delCount < addCount ? delCount : addCount;
            for (bytes q = 0; q < pairs; q++) {
                bytes di = hunkLineStart + delStart + q;
                bytes ai = hunkLineStart + addStart + q;
                TerminalDiffSpan oldSp = { 0, 0 };
                TerminalDiffSpan newSp = { 0, 0 };
                bool hasOld = false;
                bool hasNew = false;
                computeInlineSpan(lines[di].text, lines[di].length,
                                  lines[ai].text, lines[ai].length,
                                  &oldSp, &hasOld, &newSp, &hasNew);
                if (hasOld && spanIdx < opCount) {
                    spans[spanIdx] = oldSp;
                    lines[di].spans = &spans[spanIdx];
                    lines[di].spanCount = 1u;
                    spanIdx++;
                }
                if (hasNew && spanIdx < opCount) {
                    spans[spanIdx] = newSp;
                    lines[ai].spans = &spans[spanIdx];
                    lines[ai].spanCount = 1u;
                    spanIdx++;
                }
            }
        }

        hunks[hunkIdx].oldStart = oldStart;
        hunks[hunkIdx].oldCount = oldCnt;
        hunks[hunkIdx].newStart = newStart;
        hunks[hunkIdx].newCount = newCnt;
        hunks[hunkIdx].lines = &lines[hunkLineStart];
        hunks[hunkIdx].lineCount = hLen;
        hunkIdx++;
        i = end;
    }

    MemoryArena_free(arena, ops);
    DiffRes_setArena(out, arena);
    DiffRes_setHunks(out, hunks);
    DiffRes_setHunkCount(out, hunkIdx);
    DiffRes_setLines(out, lines);
    DiffRes_setLineCount(out, lineIdx);
    DiffRes_setSpans(out, spans);
    DiffRes_setSpanCount(out, spanIdx);
    DiffRes_setAdded(out, totalAdded);
    DiffRes_setRemoved(out, totalRemoved);
    return true;
}

/** Releases the isolated MemoryArena owned by a DiffResult. */
fn DiffEngine_free(DiffResult *result) {
    if (!result)
        return;
    MemoryArena *arena = DiffRes_arena(result);
    if (arena)
        MemoryArena_destroy(arena);
    memset(result, 0, sizeof(*result));
}
