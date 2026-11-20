#include "vex.h"
#include "snapshot_store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "nio/mem.h"
#include "type/re_type.h"

;;DEFINITION
#define TYPE_SNAPSHOT_STORE Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 41u)
/**
 * ============================================================================
 * DEFINITION: SnapshotStore
 * ============================================================================
 * In-memory snapshot map tracking previous UTF-8/text file contents for every
 * watched file so live FileWatchEvents can be diffed against the prior version.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SnapshotStore (difference/snapshot_store.c)
 * ============================================================================
 * Separates ephemeral file reads (resettable scratch MemoryArena) from live
 * file snapshots (auto-compacting persistent MemoryArena) so watching large
 * repositories never exhausts bump storage.
 * ============================================================================
 */

enum {
    SNAPSHOT_DEFAULT_MAX_FILE = 256u * 1024u,
    SNAPSHOT_PERSIST_ARENA_BYTES = 128u * 1024u * 1024u,
    SNAPSHOT_SCRATCH_ARENA_BYTES = 64u * 1024u * 1024u
};

;;GETTER
static inline MemoryArena *Store_arena(const SnapshotStore *s) { return (*s).arena; }
;;SETTER
static inline fn Store_setArena(SnapshotStore *s, MemoryArena *a) { (*s).arena = a; }
;;GETTER
static inline MemoryArena *Store_scratch(const SnapshotStore *s) { return (*s).scratch; }
;;SETTER
static inline fn Store_setScratch(SnapshotStore *s, MemoryArena *a) { (*s).scratch = a; }
;;GETTER
static inline bytes Store_arenaBytes(const SnapshotStore *s) { return (*s).arenaBytes; }
;;SETTER
static inline fn Store_setArenaBytes(SnapshotStore *s, bytes n) { (*s).arenaBytes = n; }
;;GETTER
static inline SnapshotEntry *Store_entries(const SnapshotStore *s) { return (*s).entries; }
;;SETTER
static inline fn Store_setEntries(SnapshotStore *s, SnapshotEntry *e) { (*s).entries = e; }
;;GETTER
static inline bytes Store_count(const SnapshotStore *s) { return (*s).count; }
;;SETTER
static inline fn Store_setCount(SnapshotStore *s, bytes c) { (*s).count = c; }
;;GETTER
static inline bytes Store_capacity(const SnapshotStore *s) { return (*s).capacity; }
;;SETTER
static inline fn Store_setCapacity(SnapshotStore *s, bytes c) { (*s).capacity = c; }
;;GETTER
static inline bytes Store_maxBytes(const SnapshotStore *s) { return (*s).maxFileBytes; }
;;SETTER
static inline fn Store_setMaxBytes(SnapshotStore *s, bytes m) { (*s).maxFileBytes = m; }
;;GETTER
static inline const char *Dirent_name(const struct dirent *d) { return (*d).d_name; }

/** Compares two NUL-terminated strings for exact byte equality. */
static bool snapStrEq(const char *a, const char *b) {
    if (!a || !b)
        return false;
    bytes la = strlen(a);
    bytes lb = strlen(b);
    if (la != lb)
        return false;
    return la == 0u || memcmp(a, b, la) == 0;
}

/** Returns true if str begins with prefix. */
static bool snapStartsWith(const char *str, const char *prefix) {
    if (!str || !prefix)
        return false;
    bytes plen = strlen(prefix);
    if (strlen(str) < plen)
        return false;
    return plen == 0u || memcmp(str, prefix, plen) == 0;
}

/** Skips common build, cache, and VCS directories during initial recursive seeding. */
static bool isSkippedDirName(const char *name) {
    if (!name || name[0] == '\0' || name[0] == '.')
        return true;
    if (snapStartsWith(name, "cmake-build-"))
        return true;
    if (snapStrEq(name, "node_modules") || snapStrEq(name, "build") ||
        snapStrEq(name, "target") || snapStrEq(name, "dist") ||
        snapStrEq(name, "__pycache__") || snapStrEq(name, "DerivedData"))
        return true;
    return false;
}

/** Checks the first 4 KiB for NUL bytes to avoid diffing binary files. */
static bool looksBinary(const char *buf, bytes len) {
    bytes probe = len < 4096u ? len : 4096u;
    for (bytes i = 0; i < probe; i++)
        if (buf[i] == '\0')
            return true;
    return false;
}

/** Compacts all live snapshot entries into a fresh arena so bump space is reclaimed. */
static bool snapshotCompact(SnapshotStore *store, bytes minExtraBytes) {
    if (!store)
        return false;
    MemoryArena *oldArena = Store_arena(store);
    SnapshotEntry *oldEntries = Store_entries(store);
    bytes count = Store_count(store);
    bytes cap = Store_capacity(store);

    bytes liveBytes = cap * sizeof(SnapshotEntry) + minExtraBytes;
    for (bytes i = 0; i < count; i++) {
        if (oldEntries[i].path)
            liveBytes += strlen(oldEntries[i].path) + 64u;
        liveBytes += oldEntries[i].length + 64u;
    }

    bytes nextSize = Store_arenaBytes(store);
    if (nextSize == 0u)
        nextSize = SNAPSHOT_PERSIST_ARENA_BYTES;
    while (nextSize < MemoryArena_capacityFor(liveBytes * 2u))
        nextSize *= 2u;

    MemoryArena *nextArena = MemoryArena_create(nextSize);
    if (!nextArena)
        return false;

    SnapshotEntry *nextEntries = nullptr;
    if (cap > 0u) {
        nextEntries = (SnapshotEntry*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, cap * sizeof(SnapshotEntry));
        if (!nextEntries) {
            MemoryArena_destroy(nextArena);
            return false;
        }
        for (bytes i = 0; i < count; i++) {
            bytes plen = strlen(oldEntries[i].path);
            char *pCopy = (char*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, plen + 1u);
            char *dCopy = (char*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, oldEntries[i].length + 1u);
            if (!pCopy || !dCopy) {
                MemoryArena_destroy(nextArena);
                return false;
            }
            memcpy(pCopy, oldEntries[i].path, plen + 1u);
            if (oldEntries[i].length > 0u && oldEntries[i].data)
                memcpy(dCopy, oldEntries[i].data, oldEntries[i].length);
            dCopy[oldEntries[i].length] = '\0';
            nextEntries[i].path = pCopy;
            nextEntries[i].data = dCopy;
            nextEntries[i].length = oldEntries[i].length;
        }
    }

    Store_setArena(store, nextArena);
    Store_setArenaBytes(store, nextSize);
    Store_setEntries(store, nextEntries);
    if (oldArena)
        MemoryArena_destroy(oldArena);
    return true;
}

/** Allocates from the persistent arena, compacting and retrying once on exhaustion. */
static any persistAlloc(SnapshotStore *store, bytes numBytes) {
    MemoryArena *arena = Store_arena(store);
    if (!arena)
        return nullptr;
    any p = MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, numBytes);
    if (p)
        return p;
    if (!snapshotCompact(store, numBytes))
        return nullptr;
    return MemoryArena_alloc(Store_arena(store), TYPE_SNAPSHOT_STORE, numBytes);
}

/** Initializes an empty SnapshotStore with persistent and scratch MemoryArenas. */
fn SnapshotStore_init(SnapshotStore *store, bytes maxFileBytes) {
    if (!store)
        return;
    memset(store, 0, sizeof(*store));
    Store_setArenaBytes(store, SNAPSHOT_PERSIST_ARENA_BYTES);
    Store_setArena(store, MemoryArena_create(SNAPSHOT_PERSIST_ARENA_BYTES));
    Store_setScratch(store, MemoryArena_create(SNAPSHOT_SCRATCH_ARENA_BYTES));
    Store_setMaxBytes(store, maxFileBytes > 0u ? maxFileBytes : (bytes) SNAPSHOT_DEFAULT_MAX_FILE);
}

/** Destroys the store's persistent and scratch arenas and resets all fields. */
fn SnapshotStore_free(SnapshotStore *store) {
    if (!store)
        return;
    MemoryArena *arena = Store_arena(store);
    MemoryArena *scratch = Store_scratch(store);
    if (arena)
        MemoryArena_destroy(arena);
    if (scratch)
        MemoryArena_destroy(scratch);
    memset(store, 0, sizeof(*store));
}

/** Reads a text file into the resettable scratch arena; sets outBinary on NUL bytes. */
char *SnapshotStore_readFile(SnapshotStore *store, const char *path, bytes *outLen, bool *outBinary) {
    if (outLen)
        *outLen = 0;
    if (outBinary)
        *outBinary = false;
    if (!store || !path)
        return nullptr;
    MemoryArena *scratch = Store_scratch(store);
    if (!scratch)
        return nullptr;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return nullptr;
    bytes sz = (bytes) st.st_size;
    bytes maxBytes = Store_maxBytes(store);
    if (maxBytes > 0u && sz > maxBytes)
        return nullptr;

    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;

    MemoryArena_freeAll(scratch);
    char *buf = (char*) MemoryArena_alloc(scratch, TYPE_SNAPSHOT_STORE, sz + 1u);
    if (!buf) {
        fclose(f);
        return nullptr;
    }
    bytes n = sz > 0u ? fread(buf, 1u, sz, f) : 0u;
    fclose(f);
    buf[n] = '\0';

    if (looksBinary(buf, n)) {
        if (outBinary)
            *outBinary = true;
        MemoryArena_freeAll(scratch);
        return nullptr;
    }
    if (outLen)
        *outLen = n;
    return buf;
}

/** Resets the scratch arena after finishing with a buffer from SnapshotStore_readFile. */
fn SnapshotStore_freeBuf(SnapshotStore *store, char *buf) {
    (discard) buf;
    if (!store)
        return;
    MemoryArena *scratch = Store_scratch(store);
    if (scratch)
        MemoryArena_freeAll(scratch);
}

/** Finds an existing snapshot by exact path, or returns nullptr. */
const SnapshotEntry *SnapshotStore_find(const SnapshotStore *store, const char *path) {
    if (!store || !path)
        return nullptr;
    SnapshotEntry *entries = Store_entries(store);
    bytes count = Store_count(store);
    for (bytes i = 0; i < count; i++)
        if (entries[i].path && snapStrEq(entries[i].path, path))
            return &entries[i];
    return nullptr;
}

/** Inserts or replaces a file's snapshot content with an owned copy in the persistent arena. */
bool SnapshotStore_put(SnapshotStore *store, const char *path, const char *data, bytes len) {
    if (!store || !path)
        return false;

    SnapshotEntry *entries = Store_entries(store);
    bytes count = Store_count(store);
    for (bytes i = 0; i < count; i++) {
        if (entries[i].path && snapStrEq(entries[i].path, path)) {
            char *dataCopy = (char*) persistAlloc(store, len + 1u);
            if (!dataCopy)
                return false;
            if (len > 0u && data)
                memcpy(dataCopy, data, len);
            dataCopy[len] = '\0';
            entries = Store_entries(store);
            MemoryArena_free(Store_arena(store), entries[i].data);
            entries[i].data = dataCopy;
            entries[i].length = len;
            return true;
        }
    }

    bytes pathLen = strlen(path);
    for (u32 attempt = 0; attempt < 2u; attempt++) {
        MemoryArena *arena = Store_arena(store);
        if (!arena)
            return false;
        entries = Store_entries(store);
        count = Store_count(store);
        bytes cap = Store_capacity(store);
        bytes nextCap = (count == cap) ? (cap ? cap * 2u : 64u) : cap;
        bytes needExtra = (count == cap ? nextCap * sizeof(SnapshotEntry) : 0u) + pathLen + len + 512u;

        SnapshotEntry *grown = nullptr;
        if (count == cap) {
            grown = (SnapshotEntry*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, nextCap * sizeof(SnapshotEntry));
            if (!grown) {
                if (attempt == 0u && snapshotCompact(store, needExtra))
                    continue;
                return false;
            }
        }

        char *pathCopy = (char*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, pathLen + 1u);
        char *dataCopy = pathCopy ? (char*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, len + 1u) : nullptr;
        if (!pathCopy || !dataCopy) {
            MemoryArena_free(arena, grown);
            MemoryArena_free(arena, pathCopy);
            MemoryArena_free(arena, dataCopy);
            if (attempt == 0u && snapshotCompact(store, needExtra))
                continue;
            return false;
        }

        if (grown) {
            if (entries && count > 0u) {
                memcpy(grown, entries, count * sizeof(SnapshotEntry));
                MemoryArena_free(arena, entries);
            }
            entries = grown;
            Store_setEntries(store, entries);
            Store_setCapacity(store, nextCap);
        }

        memcpy(pathCopy, path, pathLen + 1u);
        if (len > 0u && data)
            memcpy(dataCopy, data, len);
        dataCopy[len] = '\0';

        entries[count].path = pathCopy;
        entries[count].data = dataCopy;
        entries[count].length = len;
        Store_setCount(store, count + 1u);
        return true;
    }
    return false;
}

/** Removes a file's snapshot entry on deletion. */
bool SnapshotStore_remove(SnapshotStore *store, const char *path) {
    if (!store || !path)
        return false;
    MemoryArena *arena = Store_arena(store);
    if (!arena)
        return false;
    SnapshotEntry *entries = Store_entries(store);
    bytes count = Store_count(store);
    for (bytes i = 0; i < count; i++) {
        if (entries[i].path && snapStrEq(entries[i].path, path)) {
            MemoryArena_free(arena, entries[i].path);
            MemoryArena_free(arena, entries[i].data);
            if (i + 1u < count)
                entries[i] = entries[count - 1u];
            Store_setCount(store, count - 1u);
            return true;
        }
    }
    return false;
}

/** Recursively walks dir and seeds initial snapshots matching filter. */
static fn seedWalk(SnapshotStore *store, const char *dir, const FileEventWatcher *filter, i32 depth) {
    if (depth > 16)
        return;
    DIR *dp = opendir(dir);
    if (!dp)
        return;

    struct dirent *ent;
    while ((ent = readdir(dp)) != nullptr) {
        const char *name = Dirent_name(ent);
        if (snapStrEq(name, ".") || snapStrEq(name, ".."))
            continue;
        char full[FILE_PATH_MAX];
        i32 n = snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (n <= 0 || (bytes) n >= sizeof(full))
            continue;

        struct stat st;
        if (lstat(full, &st) != 0)
            continue;
        if (S_ISLNK(st.st_mode))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (isSkippedDirName(name))
                continue;
            seedWalk(store, full, filter, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;
        if (filter && !FileEventWatcher_matchesPath(filter, full, false))
            continue;

        bytes len = 0;
        bool isBin = false;
        char *buf = SnapshotStore_readFile(store, full, &len, &isBin);
        if (buf) {
            (discard) SnapshotStore_put(store, full, buf, len);
            SnapshotStore_freeBuf(store, buf);
        }
    }
    closedir(dp);
}

/** Seeds the store with initial text file snapshots under rootDir. */
bool SnapshotStore_seedDir(SnapshotStore *store, const char *rootDir, const FileEventWatcher *filter) {
    if (!store || !rootDir || !rootDir[0])
        return false;
    seedWalk(store, rootDir, filter, 0);
    return true;
}

/** Returns the number of tracked file snapshots. */
bytes SnapshotStore_count(const SnapshotStore *store) {
    return store ? Store_count(store) : 0u;
}
