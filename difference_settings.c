#include "vex.h"
#include "difference_settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hotcwap/manifest.h"
#include "re/primitive/vex-string.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: DifferenceSettings
 * ============================================================================
 * Persisted settings record for difference stored under the R1 manifest
 * app-data root (MANIFEST(MANIFEST_APP_DATA, "vexgraph", "difference")  to 
 * ~/Library/Application Support/vexgraph/difference/settings). Load is
 * all-or-nothing: a missing file keeps defaults, while any malformed line or
 * out-of-range value rejects without applying a partial update. Save is atomic
 * via temp file + fsync + rename.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DifferenceSettings (difference/difference_settings.c)
 * ============================================================================
 * Persists all 8 interactive TUI options:
 *   - collapse_mode      (bool, default true)
 *   - diff_layout        (auto | split | unified, default auto)
 *   - follow_tail        (bool, default true)
 *   - color              (bool, default true)
 *   - diff_icons         (bool, default true)
 *   - context_lines      (u32 0..12, default 3)
 *   - workspace_limit_mb (u32 1..65536, default 1024)
 *   - max_edits          (u32 100..1024, default 1024)
 * ============================================================================
 */

;;INTENTION("difference settings persist under the R1 manifest app-data root, not a private hardcoded path")

#define DIFFERENCE_SETTINGS_BYTES_MAX 4096u

/** Returns default DifferenceSettings matching DifferenceTui initial values. */
DifferenceSettings DifferenceSettings_0() {
    DifferenceSettings settings;
    settings.collapseMode = true;
    settings.diffLayout = TERMINAL_DIFF_LAYOUT_AUTO;
    settings.followTail = true;
    settings.color = true;
    settings.diffIcons = true;
    settings.contextLines = (u32) DIFFERENCE_SETTINGS_CONTEXT_DEFAULT;
    settings.workspaceLimitMb = (u32) DIFFERENCE_SETTINGS_WORKSPACE_MB_DEFAULT;
    settings.maxEdits = (u32) DIFFERENCE_SETTINGS_MAX_EDITS_DEFAULT;
    return settings;
}

;;SETTER
fn DifferenceSettings_setCollapseMode(DifferenceSettings *self, bool value) {
    if (self != nullptr)
        (*self).collapseMode = value;
}

;;SETTER
bool DifferenceSettings_setDiffLayout(DifferenceSettings *self, TerminalDiffLayoutMode value) {
    if (self == nullptr)
        return false;
    if (value != TERMINAL_DIFF_LAYOUT_AUTO &&
        value != TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE &&
        value != TERMINAL_DIFF_LAYOUT_UNIFIED)
        return false;
    (*self).diffLayout = value;
    return true;
}

;;SETTER
fn DifferenceSettings_setFollowTail(DifferenceSettings *self, bool value) {
    if (self != nullptr)
        (*self).followTail = value;
}

;;SETTER
fn DifferenceSettings_setColor(DifferenceSettings *self, bool value) {
    if (self != nullptr)
        (*self).color = value;
}

;;SETTER
fn DifferenceSettings_setDiffIcons(DifferenceSettings *self, bool value) {
    if (self != nullptr)
        (*self).diffIcons = value;
}

;;SETTER
bool DifferenceSettings_setContextLines(DifferenceSettings *self, u32 value) {
    if (self == nullptr ||
        value < (u32) DIFFERENCE_SETTINGS_CONTEXT_MIN ||
        value > (u32) DIFFERENCE_SETTINGS_CONTEXT_MAX)
        return false;
    (*self).contextLines = value;
    return true;
}

;;SETTER
bool DifferenceSettings_setWorkspaceLimitMb(DifferenceSettings *self, u32 value) {
    if (self == nullptr ||
        value < (u32) DIFFERENCE_SETTINGS_WORKSPACE_MB_MIN ||
        value > (u32) DIFFERENCE_SETTINGS_WORKSPACE_MB_MAX)
        return false;
    (*self).workspaceLimitMb = value;
    return true;
}

;;SETTER
bool DifferenceSettings_setMaxEdits(DifferenceSettings *self, u32 value) {
    if (self == nullptr ||
        value < (u32) DIFFERENCE_SETTINGS_MAX_EDITS_MIN ||
        value > (u32) DIFFERENCE_SETTINGS_MAX_EDITS_MAX)
        return false;
    (*self).maxEdits = value;
    return true;
}

;;GETTER
bool DifferenceSettings_isCollapseMode(const DifferenceSettings *self) {
    return self == nullptr ? true : (*self).collapseMode;
}

;;GETTER
TerminalDiffLayoutMode DifferenceSettings_getDiffLayout(const DifferenceSettings *self) {
    return self == nullptr ? TERMINAL_DIFF_LAYOUT_AUTO : (*self).diffLayout;
}

;;GETTER
bool DifferenceSettings_isFollowTail(const DifferenceSettings *self) {
    return self == nullptr ? true : (*self).followTail;
}

;;GETTER
bool DifferenceSettings_isColor(const DifferenceSettings *self) {
    return self == nullptr ? true : (*self).color;
}

;;GETTER
bool DifferenceSettings_isDiffIcons(const DifferenceSettings *self) {
    return self == nullptr ? true : (*self).diffIcons;
}

;;GETTER
u32 DifferenceSettings_getContextLines(const DifferenceSettings *self) {
    return self == nullptr ? (u32) DIFFERENCE_SETTINGS_CONTEXT_DEFAULT : (*self).contextLines;
}

;;GETTER
u32 DifferenceSettings_getWorkspaceLimitMb(const DifferenceSettings *self) {
    return self == nullptr ? (u32) DIFFERENCE_SETTINGS_WORKSPACE_MB_DEFAULT : (*self).workspaceLimitMb;
}

;;GETTER
u32 DifferenceSettings_getMaxEdits(const DifferenceSettings *self) {
    return self == nullptr ? (u32) DIFFERENCE_SETTINGS_MAX_EDITS_DEFAULT : (*self).maxEdits;
}

/** Mounts and ensures the R1 manifest root for vexgraph/difference once. */
static bool ensureManifest() {
    static i32 state = 0;
    if (state == 0)
        state = (MANIFEST(MANIFEST_APP_DATA, "vexgraph", "difference") && MANIFEST_ENSURE()) ? 1 : -1;
    return state == 1;
}

/** Resolves <manifest app-data root>/settings into caller buffer dest. */
bool DifferenceSettings_resolvePath(utf8 *dest, bytes cap) {
    if (dest == nullptr || cap == 0u || !ensureManifest())
        return false;
    string root = MANIFEST_ROOT();
    if (root == nullptr)
        return false;
    i32 written = snprintf(dest, cap, "%s/settings", root);
    return written > 0 && (bytes) written < cap;
}

/** Parses a strict boolean token ("0", "1", "false", "true"). */
static bool parseBool(string value, bool *out) {
    string token = value;
    if (string_equals(token, "1")
        || string_equals(token, "true")) {
        *out = true;
        return true;
    }
    if (string_equals(token, "0")
        || string_equals(token, "false")) {
        *out = false;
        return true;
    }
    return false;
}

/** Parses a strict unsigned decimal integer with no trailing junk. */
static bool parseU32(string value, u32 *out) {
    if (value == nullptr || value[0] < '0' || value[0] > '9')
        return false;
    errno = 0;
    utf8 *end = nullptr;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > 0xFFFFFFFFul)
        return false;
    *out = (u32) parsed;
    return true;
}

/** Applies one "name=value" line to parsed; returns false on unknown key or invalid value. */
static bool applyLine(DifferenceSettings *parsed, string name, bytes nameLen, string value) {
    bool flag = false;
    u32 num = 0u;

    if (nameLen == 13u && memcmp(name, "collapse_mode", 13u) == 0) {
        if (!parseBool(value, &flag))
            return false;
        DifferenceSettings_setCollapseMode(parsed, flag);
        return true;
    }
    if (nameLen == 11u && memcmp(name, "diff_layout", 11u) == 0) {
        string token = value;
        if (string_equals(token, "auto"))
            return DifferenceSettings_setDiffLayout(parsed, TERMINAL_DIFF_LAYOUT_AUTO);
        if (string_equals(token, "split"))
            return DifferenceSettings_setDiffLayout(parsed, TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE);
        if (string_equals(token, "unified"))
            return DifferenceSettings_setDiffLayout(parsed, TERMINAL_DIFF_LAYOUT_UNIFIED);
        return false;
    }
    if (nameLen == 11u && memcmp(name, "follow_tail", 11u) == 0) {
        if (!parseBool(value, &flag))
            return false;
        DifferenceSettings_setFollowTail(parsed, flag);
        return true;
    }
    if (nameLen == 5u && memcmp(name, "color", 5u) == 0) {
        if (!parseBool(value, &flag))
            return false;
        DifferenceSettings_setColor(parsed, flag);
        return true;
    }
    if (nameLen == 10u && memcmp(name, "diff_icons", 10u) == 0) {
        if (!parseBool(value, &flag))
            return false;
        DifferenceSettings_setDiffIcons(parsed, flag);
        return true;
    }
    if (nameLen == 13u && memcmp(name, "context_lines", 13u) == 0) {
        if (!parseU32(value, &num))
            return false;
        return DifferenceSettings_setContextLines(parsed, num);
    }
    if (nameLen == 18u && memcmp(name, "workspace_limit_mb", 18u) == 0) {
        if (!parseU32(value, &num))
            return false;
        return DifferenceSettings_setWorkspaceLimitMb(parsed, num);
    }
    if (nameLen == 9u && memcmp(name, "max_edits", 9u) == 0) {
        if (!parseU32(value, &num))
            return false;
        return DifferenceSettings_setMaxEdits(parsed, num);
    }
    return false;
}

/** Loads persisted settings all-or-nothing; missing file keeps defaults and returns true. */
bool DifferenceSettings_load(DifferenceSettings *self, string path) {
    if (self == nullptr || path == nullptr || path[0] == '\0')
        return false;
    i32 fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT;

    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        close(fd);
        return false;
    }

    utf8 buffer[DIFFERENCE_SETTINGS_BYTES_MAX + 1u];
    bytes total = 0u;
    while (total < DIFFERENCE_SETTINGS_BYTES_MAX + 1u) {
        i64 got = read(fd, buffer + total, (DIFFERENCE_SETTINGS_BYTES_MAX + 1u) - total);
        if (got < 0) {
            close(fd);
            return false;
        }
        if (got == 0)
            break;
        total += (bytes) got;
    }
    close(fd);
    if (total > DIFFERENCE_SETTINGS_BYTES_MAX)
        return false;
    buffer[total] = '\0';

    DifferenceSettings parsed = DifferenceSettings_0();
    bytes at = 0u;
    while (at < total) {
        bytes end = at;
        while (end < total && buffer[end] != '\n')
            ++end;
        bytes start = at;
        bytes lineEnd = end;
        if (lineEnd > start && buffer[lineEnd - 1u] == '\r')
            --lineEnd;
        at = end + 1u;
        while (start < lineEnd && (buffer[start] == ' ' || buffer[start] == '\t'))
            ++start;
        while (lineEnd > start && (buffer[lineEnd - 1u] == ' ' || buffer[lineEnd - 1u] == '\t'))
            --lineEnd;
        buffer[lineEnd] = '\0';
        if (start == lineEnd || buffer[start] == '#')
            continue;
        bytes equals = start;
        while (equals < lineEnd && buffer[equals] != '=')
            ++equals;
        if (equals == lineEnd)
            return false;
        buffer[equals] = '\0';
        if (!applyLine(&parsed, buffer + start, equals - start, buffer + equals + 1u))
            return false;
    }
    *self = parsed;
    return true;
}

/** Formats TerminalDiffLayoutMode as a stable lowercase settings token. */
static string layoutToken(TerminalDiffLayoutMode mode) {
    if (mode == TERMINAL_DIFF_LAYOUT_SIDE_BY_SIDE)
        return "split";
    if (mode == TERMINAL_DIFF_LAYOUT_UNIFIED)
        return "unified";
    return "auto";
}

/** Atomically persists DifferenceSettings to path via <path>.tmp + fsync + rename. */
bool DifferenceSettings_save(const DifferenceSettings *self, string path) {
    if (self == nullptr || path == nullptr || path[0] == '\0')
        return false;
    utf8 temporary[1040];
    i32 written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written <= 0 || (bytes) written >= sizeof(temporary))
        return false;

    i32 fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;

    utf8 body[DIFFERENCE_SETTINGS_BYTES_MAX];
    i32 length = snprintf(body, sizeof(body),
                          "collapse_mode=%d\n"
                          "diff_layout=%s\n"
                          "follow_tail=%d\n"
                          "color=%d\n"
                          "diff_icons=%d\n"
                          "context_lines=%u\n"
                          "workspace_limit_mb=%u\n"
                          "max_edits=%u\n",
                          DifferenceSettings_isCollapseMode(self) ? 1 : 0,
                          layoutToken(DifferenceSettings_getDiffLayout(self)),
                          DifferenceSettings_isFollowTail(self) ? 1 : 0,
                          DifferenceSettings_isColor(self) ? 1 : 0,
                          DifferenceSettings_isDiffIcons(self) ? 1 : 0,
                          (unsigned) DifferenceSettings_getContextLines(self),
                          (unsigned) DifferenceSettings_getWorkspaceLimitMb(self),
                          (unsigned) DifferenceSettings_getMaxEdits(self));
    if (length <= 0 || (bytes) length >= sizeof(body)) {
        close(fd);
        unlink(temporary);
        return false;
    }

    bytes offset = 0u;
    while (offset < (bytes) length) {
        i64 put = write(fd, body + offset, (bytes) length - offset);
        if (put < 0) {
            close(fd);
            unlink(temporary);
            return false;
        }
        offset += (bytes) put;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        unlink(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        return false;
    }
    return true;
}
