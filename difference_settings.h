#ifndef DIFFERENCE_SETTINGS_H
#define DIFFERENCE_SETTINGS_H

#include "vex.h"
#include "ui/panel/terminal_diff_panel.h"

// difference_settings.h — persisted difference TUI settings under the R1 manifest.
//
// One bounded key=value record stored at the manifest app-data root
// (~/Library/Application Support/vexgraph/difference/settings). A missing file
// yields defaults; a malformed line rejects WITHOUT applying a partial record.

enum {
    DIFFERENCE_SETTINGS_CONTEXT_MIN = 0u,
    DIFFERENCE_SETTINGS_CONTEXT_MAX = 12u,
    DIFFERENCE_SETTINGS_CONTEXT_DEFAULT = 3u,
    DIFFERENCE_SETTINGS_WORKSPACE_MB_MIN = 1u,
    DIFFERENCE_SETTINGS_WORKSPACE_MB_MAX = 65536u,
    DIFFERENCE_SETTINGS_WORKSPACE_MB_DEFAULT = 1024u,
    DIFFERENCE_SETTINGS_MAX_EDITS_MIN = 100u,
    DIFFERENCE_SETTINGS_MAX_EDITS_MAX = 1024u,
    DIFFERENCE_SETTINGS_MAX_EDITS_DEFAULT = 1024u
};

typedef struct DifferenceSettings {
    bool collapseMode;
    TerminalDiffLayoutMode diffLayout;
    bool followTail;
    bool color;
    bool diffIcons;
    u32 contextLines;
    u32 workspaceLimitMb;
    u32 maxEdits;
} DifferenceSettings;

DifferenceSettings DifferenceSettings_0();

fn DifferenceSettings_setCollapseMode(DifferenceSettings *self, bool value);
bool DifferenceSettings_setDiffLayout(DifferenceSettings *self, TerminalDiffLayoutMode value);
fn DifferenceSettings_setFollowTail(DifferenceSettings *self, bool value);
fn DifferenceSettings_setColor(DifferenceSettings *self, bool value);
fn DifferenceSettings_setDiffIcons(DifferenceSettings *self, bool value);
bool DifferenceSettings_setContextLines(DifferenceSettings *self, u32 value);
bool DifferenceSettings_setWorkspaceLimitMb(DifferenceSettings *self, u32 value);
bool DifferenceSettings_setMaxEdits(DifferenceSettings *self, u32 value);

bool DifferenceSettings_isCollapseMode(const DifferenceSettings *self);
TerminalDiffLayoutMode DifferenceSettings_getDiffLayout(const DifferenceSettings *self);
bool DifferenceSettings_isFollowTail(const DifferenceSettings *self);
bool DifferenceSettings_isColor(const DifferenceSettings *self);
bool DifferenceSettings_isDiffIcons(const DifferenceSettings *self);
u32 DifferenceSettings_getContextLines(const DifferenceSettings *self);
u32 DifferenceSettings_getWorkspaceLimitMb(const DifferenceSettings *self);
u32 DifferenceSettings_getMaxEdits(const DifferenceSettings *self);

// Resolve <app-data root>/settings via MANIFEST(MANIFEST_APP_DATA, "vexgraph", "difference").
bool DifferenceSettings_resolvePath(char *dest, bytes cap);

// Load into self. Missing file leaves defaults and returns true; malformed file returns false and applies nothing.
bool DifferenceSettings_load(DifferenceSettings *self, const char *path);

// Atomic save (temp + fsync + rename under the same directory).
bool DifferenceSettings_save(const DifferenceSettings *self, const char *path);

#endif
