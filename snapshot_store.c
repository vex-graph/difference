#include "vex.h"
#include "snapshot_store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "nio/mem.h"
#include "type/re_type.h"

#define TYPE_SNAPSHOT_STORE Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 41u)

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: SnapshotStore
 * ============================================================================
 * Hash-indexed in-memory snapshot map tracking previous UTF-8/text file
 * contents for every watched file so live FileWatchEvents can be diffed
 * against the prior version without quadratic startup scans.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SnapshotStore (difference/snapshot_store.c)
 * ============================================================================
 * Separates ephemeral file reads (O(1)-freed scratch MemoryArena) from live
 * file snapshots (auto-compacting persistent MemoryArena with open-addressed
 * FNV-1a path hashing) and provides an iterative non-blocking SnapshotSeeder.
 * ============================================================================
 */

enum {
    SNAPSHOT_DEFAULT_MAX_FILE = 256u * 1024u,
    SNAPSHOT_PERSIST_ARENA_BYTES = 128u * 1024u * 1024u,
    SNAPSHOT_SCRATCH_ARENA_BYTES = 64u * 1024u * 1024u,
    SNAPSHOT_PROBE_BYTES = 4096u
};

#define SNAPSHOT_TOMBSTONE (~((bytes) 0u))

;;GETTER
static inline MemoryArena *Store_arena(const SnapshotStore *s)
{
    return (*s).arena;
}
;;SETTER
static inline fn Store_setArena(SnapshotStore *s, MemoryArena *a)
{
    (*s).arena = a;
}
;;GETTER
static inline MemoryArena *Store_scratch(const SnapshotStore *s)
{
    return (*s).scratch;
}
;;SETTER
static inline fn Store_setScratch(SnapshotStore *s, MemoryArena *a)
{
    (*s).scratch = a;
}
;;GETTER
static inline bytes Store_arenaBytes(const SnapshotStore *s)
{
    return (*s).arenaBytes;
}
;;SETTER
static inline fn Store_setArenaBytes(SnapshotStore *s, bytes n)
{
    (*s).arenaBytes = n;
}
;;GETTER
static inline SnapshotEntry *Store_entries(const SnapshotStore *s)
{
    return (*s).entries;
}
;;SETTER
static inline fn Store_setEntries(SnapshotStore *s, SnapshotEntry *e)
{
    (*s).entries = e;
}
;;GETTER
static inline bytes *Store_buckets(const SnapshotStore *s)
{
    return (*s).buckets;
}
;;SETTER
static inline fn Store_setBuckets(SnapshotStore *s, bytes *b)
{
    (*s).buckets = b;
}
;;GETTER
static inline bytes Store_count(const SnapshotStore *s)
{
    return (*s).count;
}
;;SETTER
static inline fn Store_setCount(SnapshotStore *s, bytes c)
{
    (*s).count = c;
}
;;GETTER
static inline bytes Store_capacity(const SnapshotStore *s)
{
    return (*s).capacity;
}
;;SETTER
static inline fn Store_setCapacity(SnapshotStore *s, bytes c)
{
    (*s).capacity = c;
}
;;GETTER
static inline bytes Store_bucketCap(const SnapshotStore *s)
{
    return (*s).bucketCap;
}
;;SETTER
static inline fn Store_setBucketCap(SnapshotStore *s, bytes c)
{
    (*s).bucketCap = c;
}
;;GETTER
static inline bytes Store_tombstones(const SnapshotStore *s)
{
    return (*s).tombstones;
}
;;SETTER
static inline fn Store_setTombstones(SnapshotStore *s, bytes t)
{
    (*s).tombstones = t;
}
;;GETTER
static inline bytes Store_maxBytes(const SnapshotStore *s)
{
    return (*s).maxFileBytes;
}
;;SETTER
static inline fn Store_setMaxBytes(SnapshotStore *s, bytes m)
{
    (*s).maxFileBytes = m;
}
;;GETTER
static inline bytes Store_maxWorkspaceBytes(const SnapshotStore *s)
{
    return (*s).maxWorkspaceBytes;
}
;;SETTER
static inline fn Store_setMaxWorkspaceBytes(SnapshotStore *s, bytes m)
{
    (*s).maxWorkspaceBytes = m;
}
;;GETTER
static inline bytes Store_totalDataBytes(const SnapshotStore *s)
{
    return (*s).totalDataBytes;
}
;;SETTER
static inline fn Store_setTotalDataBytes(SnapshotStore *s, bytes n)
{
    (*s).totalDataBytes = n;
}
;;GETTER
static inline string Dirent_name(const struct dirent *d)
{
    return (*d).d_name;
}

;;GETTER
static inline SnapshotStore *Seeder_store(const SnapshotSeeder *s)
{
    return (*s).store;
}
;;SETTER
static inline fn Seeder_setStore(SnapshotSeeder *s, SnapshotStore *st)
{
    (*s).store = st;
}
;;GETTER
static inline const FileEventWatcher *Seeder_filter(const SnapshotSeeder *s)
{
    return (*s).filter;
}
;;SETTER
static inline fn Seeder_setFilter(SnapshotSeeder *s, const FileEventWatcher *f)
{
    (*s).filter = f;
}
;;GETTER
static inline SnapshotSeedFrame *Seeder_stackMut(SnapshotSeeder *s)
{
    return (*s).stack;
}
;;GETTER
static inline bytes Seeder_depth(const SnapshotSeeder *s)
{
    return (*s).depth;
}
;;SETTER
static inline fn Seeder_setDepth(SnapshotSeeder *s, bytes d)
{
    (*s).depth = d;
}
;;GETTER
static inline bool Seeder_active(const SnapshotSeeder *s)
{
    return (*s).active;
}
;;SETTER
static inline fn Seeder_setActive(SnapshotSeeder *s, bool a)
{
    (*s).active = a;
}

/** Computes 64-bit FNV-1a hash of a byte slice. */
static u64 snapHash(string s, bytes len) {
    u64 h = 14695981039346656037ULL;
    for (bytes i = 0u; i < len; i++) {
        h ^= (u64) (u8) s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/** Returns the smallest power of two >= n (minimum 128). */
static bytes nextPow2(bytes n) {
    bytes p = 128u;
    while (p < n && p <= (SIZE_MAX >> 1u))
        p <<= 1u;
    return p;
}

/** Compares two NUL-terminated strings for exact byte equality. */
static bool snapStrEq(string a, string b) {
    if (!a || !b)
        return false;
    bytes la = strlen(a);
    bytes lb = strlen(b);
    if (la != lb)
        return false;
    return la == 0u || memcmp(a, b, la) == 0;
}

/** Returns true if str begins with prefix. */
static bool snapStartsWith(string str, string prefix) {
    if (!str || !prefix)
        return false;
    bytes plen = strlen(prefix);
    if (strlen(str) < plen)
        return false;
    return plen == 0u || memcmp(str, prefix, plen) == 0;
}

/** Skips common build, cache, and VCS directories during initial recursive seeding. */
static bool isSkippedDirName(string name) {
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
static bool looksBinary(string buf, bytes len) {
    bytes probe = len < SNAPSHOT_PROBE_BYTES ? len : SNAPSHOT_PROBE_BYTES;
    for (bytes i = 0u; i < probe; i++)
        if (buf[i] == '\0')
            return true;
    return false;
}

/** Inserts an entry index into an open-addressed bucket array, returning true if reusing a tombstone. */
static bool bucketInsertRaw(bytes *buckets, bytes bucketCap,
                            const SnapshotEntry *entries, bytes entryIdx) {
    bytes mask = bucketCap - 1u;
    bytes slot = (bytes) entries[entryIdx].hash & mask;
    while (buckets[slot] != 0u && buckets[slot] != SNAPSHOT_TOMBSTONE)
        slot = (slot + 1u) & mask;
    bool reusedTombstone = (buckets[slot] == SNAPSHOT_TOMBSTONE);
    buckets[slot] = entryIdx + 1u;
    return reusedTombstone;
}

/** Looks up a path in the hash table; returns entry index or SNAPSHOT_TOMBSTONE if absent. */
static bytes storeLookup(const SnapshotStore *store, string path,
                         bytes pathLen, u64 hash, bytes *outBucket) {
    bytes *buckets = Store_buckets(store);
    bytes bucketCap = Store_bucketCap(store);
    SnapshotEntry *entries = Store_entries(store);
    if (!buckets || !entries || bucketCap == 0u)
        return SNAPSHOT_TOMBSTONE;

    bytes mask = bucketCap - 1u;
    bytes slot = (bytes) hash & mask;
    for (bytes probe = 0u; probe < bucketCap; probe++) {
        bytes val = buckets[slot];
        if (val == 0u)
            return SNAPSHOT_TOMBSTONE;
        if (val != SNAPSHOT_TOMBSTONE) {
            bytes idx = val - 1u;
            if (entries[idx].hash == hash && entries[idx].pathLen == pathLen &&
                (pathLen == 0u || memcmp(entries[idx].path, path, pathLen) == 0)) {
                if (outBucket)
                    *outBucket = slot;
                return idx;
            }
        }
        slot = (slot + 1u) & mask;
    }
    return SNAPSHOT_TOMBSTONE;
}

/** Compacts all live snapshot entries and rebuilds the hash table in a fresh arena. */
static bool snapshotCompact(SnapshotStore *store, bytes minExtraBytes) {
    if (!store)
        return false;
    MemoryArena *oldArena = Store_arena(store);
    SnapshotEntry *oldEntries = Store_entries(store);
    bytes count = Store_count(store);
    bytes cap = Store_capacity(store);
    bytes nextBucketCap = nextPow2(cap > 0u ? cap * 2u : 128u);

    bytes liveBytes = cap * sizeof(SnapshotEntry) + nextBucketCap * sizeof(bytes) + minExtraBytes;
    for (bytes i = 0u; i < count; i++) {
        liveBytes += oldEntries[i].pathLen + 64u;
        liveBytes += oldEntries[i].length + 64u;
    }

    bytes nextSize = Store_arenaBytes(store);
    if (nextSize == 0u)
        nextSize = SNAPSHOT_PERSIST_ARENA_BYTES;
    while (nextSize < MemoryArena_capacityFor(liveBytes * 2u))
        nextSize *= 2u;

    MemoryArena *nextArena = MemoryArena(nextSize);
    if (!nextArena)
        return false;

    SnapshotEntry *nextEntries = nullptr;
    bytes *nextBuckets = nullptr;
    if (cap > 0u) {
        nextEntries = (SnapshotEntry*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, cap * sizeof(SnapshotEntry));
        nextBuckets = (bytes*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, nextBucketCap * sizeof(bytes));
        if (!nextEntries || !nextBuckets) {
            MemoryArena_destroy(nextArena);
            return false;
        }
        memset(nextBuckets, 0, nextBucketCap * sizeof(bytes));
        for (bytes i = 0u; i < count; i++) {
            bytes plen = oldEntries[i].pathLen;
            utf8 *pCopy = (utf8*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, plen + 1u);
            utf8 *dCopy = (utf8*) MemoryArena_alloc(nextArena, TYPE_SNAPSHOT_STORE, oldEntries[i].length + 1u);
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
            nextEntries[i].pathLen = plen;
            nextEntries[i].length = oldEntries[i].length;
            nextEntries[i].hash = oldEntries[i].hash;
            (discard) bucketInsertRaw(nextBuckets, nextBucketCap, nextEntries, i);
        }
    }

    Store_setArena(store, nextArena);
    Store_setArenaBytes(store, nextSize);
    Store_setEntries(store, nextEntries);
    Store_setBuckets(store, nextBuckets);
    Store_setBucketCap(store, nextBuckets ? nextBucketCap : 0u);
    Store_setTombstones(store, 0u);
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
    Store_setArena(store, MemoryArena(SNAPSHOT_PERSIST_ARENA_BYTES));
    Store_setScratch(store, MemoryArena(SNAPSHOT_SCRATCH_ARENA_BYTES));
    Store_setMaxBytes(store, maxFileBytes > 0u ? maxFileBytes : (bytes) SNAPSHOT_DEFAULT_MAX_FILE);
    Store_setMaxWorkspaceBytes(store, (bytes) SNAPSHOT_DEFAULT_MAX_WORKSPACE_BYTES);
    Store_setTotalDataBytes(store, 0u);
}

/** Updates the total workspace snapshot memory cap in bytes (0 = unlimited). */
fn SnapshotStore_setMaxWorkspaceBytes(SnapshotStore *store, bytes maxBytes) {
    if (store)
        Store_setMaxWorkspaceBytes(store, maxBytes);
}

/** Returns the configured workspace snapshot memory cap in bytes. */
bytes SnapshotStore_maxWorkspaceBytes(const SnapshotStore *store) {
    return store ? Store_maxWorkspaceBytes(store) : 0u;
}

/** Returns the total stored snapshot bytes currently tracked in the workspace. */
bytes SnapshotStore_totalDataBytes(const SnapshotStore *store) {
    return store ? Store_totalDataBytes(store) : 0u;
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

/** Reads a text file into the scratch arena, probing the first 4 KiB before full allocation. */
static utf8 *readFileKnownSize(SnapshotStore *store, string path,
                               bytes knownSize, bool hasSize,
                               bytes *outLen, bool *outBinary) {
    if (outLen)
        *outLen = 0u;
    if (outBinary)
        *outBinary = false;
    if (!store || !path)
        return nullptr;
    MemoryArena *scratch = Store_scratch(store);
    if (!scratch)
        return nullptr;

    bytes sz = knownSize;
    if (!hasSize) {
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            return nullptr;
        sz = (bytes) st.st_size;
    }
    bytes maxBytes = Store_maxBytes(store);
    if (maxBytes > 0u && sz > maxBytes)
        return nullptr;

    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;

    utf8 head[SNAPSHOT_PROBE_BYTES];
    bytes probeLen = sz < SNAPSHOT_PROBE_BYTES ? sz : SNAPSHOT_PROBE_BYTES;
    bytes nHead = probeLen > 0u ? fread(head, 1u, probeLen, f) : 0u;
    if (looksBinary(head, nHead)) {
        fclose(f);
        if (outBinary)
            *outBinary = true;
        return nullptr;
    }

    utf8 *buf = (utf8*) MemoryArena_alloc(scratch, TYPE_SNAPSHOT_STORE, sz + 1u);
    if (!buf) {
        MemoryArena_freeAll(scratch);
        buf = (utf8*) MemoryArena_alloc(scratch, TYPE_SNAPSHOT_STORE, sz + 1u);
        if (!buf) {
            fclose(f);
            return nullptr;
        }
    }

    if (nHead > 0u)
        memcpy(buf, head, nHead);
    bytes nTail = 0u;
    if (sz > nHead)
        nTail = fread(buf + nHead, 1u, sz - nHead, f);
    fclose(f);

    bytes total = nHead + nTail;
    buf[total] = '\0';
    if (outLen)
        *outLen = total;
    return buf;
}

/** Reads a text file into the scratch arena; sets outBinary on NUL bytes. */
utf8 *SnapshotStore_readFile(SnapshotStore *store, string path, bytes *outLen, bool *outBinary) {
    return readFileKnownSize(store, path, 0u, false, outLen, outBinary);
}

/** Releases a single scratch buffer in O(1) without rebuilding slab freelists. */
fn SnapshotStore_freeBuf(SnapshotStore *store, utf8 *buf) {
    if (!store || !buf)
        return;
    MemoryArena *scratch = Store_scratch(store);
    if (scratch)
        MemoryArena_free(scratch, buf);
}

/** Finds an existing snapshot by exact path in O(1) average time, or returns nullptr. */
const SnapshotEntry *SnapshotStore_find(const SnapshotStore *store, string path) {
    if (!store || !path)
        return nullptr;
    bytes pathLen = strlen(path);
    u64 hash = snapHash(path, pathLen);
    bytes idx = storeLookup(store, path, pathLen, hash, nullptr);
    if (idx == SNAPSHOT_TOMBSTONE)
        return nullptr;
    return &Store_entries(store)[idx];
}

/** Inserts or replaces a file's snapshot content with an owned copy in the persistent arena. */
bool SnapshotStore_put(SnapshotStore *store, string path, string data, bytes len) {
    if (!store || !path)
        return false;

    bytes pathLen = strlen(path);
    u64 hash = snapHash(path, pathLen);
    bytes maxWs = Store_maxWorkspaceBytes(store);
    bytes curTotal = Store_totalDataBytes(store);
    bytes existing = storeLookup(store, path, pathLen, hash, nullptr);
    if (existing != SNAPSHOT_TOMBSTONE) {
        SnapshotEntry *entries = Store_entries(store);
        bytes oldLen = entries[existing].length;
        bytes nextTotal = (curTotal >= oldLen ? curTotal - oldLen : 0u) + len;
        if (maxWs > 0u && nextTotal > maxWs)
            return false;
        utf8 *dataCopy = (utf8*) persistAlloc(store, len + 1u);
        if (!dataCopy)
            return false;
        if (len > 0u && data)
            memcpy(dataCopy, data, len);
        dataCopy[len] = '\0';
        entries = Store_entries(store);
        MemoryArena_free(Store_arena(store), entries[existing].data);
        entries[existing].data = dataCopy;
        entries[existing].length = len;
        Store_setTotalDataBytes(store, nextTotal);
        return true;
    }

    bytes addBytes = pathLen + len;
    if (maxWs > 0u && curTotal + addBytes > maxWs)
        return false;

    for (u32 attempt = 0u; attempt < 2u; attempt++) {
        MemoryArena *arena = Store_arena(store);
        if (!arena)
            return false;
        SnapshotEntry *entries = Store_entries(store);
        bytes *buckets = Store_buckets(store);
        bytes count = Store_count(store);
        bytes cap = Store_capacity(store);
        bytes bucketCap = Store_bucketCap(store);
        bytes tombstones = Store_tombstones(store);

        bool needEntries = (count == cap);
        bytes nextCap = needEntries ? (cap ? cap * 2u : 64u) : cap;
        bool needBuckets = (bucketCap == 0u || (count + tombstones + 1u) * 10u >= bucketCap * 7u);
        bytes nextBucketCap = needBuckets ? nextPow2((count + 1u) * 2u) : bucketCap;

        bytes needExtra = (needEntries ? nextCap * sizeof(SnapshotEntry) : 0u)
                          + (needBuckets ? nextBucketCap * sizeof(bytes) : 0u)
                          + pathLen + len + 512u;

        SnapshotEntry *grownEntries = nullptr;
        if (needEntries) {
            grownEntries = (SnapshotEntry*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, nextCap * sizeof(SnapshotEntry));
            if (!grownEntries) {
                if (attempt == 0u && snapshotCompact(store, needExtra))
                    continue;
                return false;
            }
        }

        bytes *grownBuckets = nullptr;
        if (needBuckets) {
            grownBuckets = (bytes*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, nextBucketCap * sizeof(bytes));
            if (!grownBuckets) {
                MemoryArena_free(arena, grownEntries);
                if (attempt == 0u && snapshotCompact(store, needExtra))
                    continue;
                return false;
            }
        }

        utf8 *pathCopy = (utf8*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, pathLen + 1u);
        utf8 *dataCopy = pathCopy ? (utf8*) MemoryArena_alloc(arena, TYPE_SNAPSHOT_STORE, len + 1u) : nullptr;
        if (!pathCopy || !dataCopy) {
            MemoryArena_free(arena, grownEntries);
            MemoryArena_free(arena, grownBuckets);
            MemoryArena_free(arena, pathCopy);
            MemoryArena_free(arena, dataCopy);
            if (attempt == 0u && snapshotCompact(store, needExtra))
                continue;
            return false;
        }

        if (grownEntries) {
            if (entries && count > 0u) {
                memcpy(grownEntries, entries, count * sizeof(SnapshotEntry));
                MemoryArena_free(arena, entries);
            }
            entries = grownEntries;
            Store_setEntries(store, entries);
            Store_setCapacity(store, nextCap);
        }

        memcpy(pathCopy, path, pathLen + 1u);
        if (len > 0u && data)
            memcpy(dataCopy, data, len);
        dataCopy[len] = '\0';

        entries[count].path = pathCopy;
        entries[count].data = dataCopy;
        entries[count].pathLen = pathLen;
        entries[count].length = len;
        entries[count].hash = hash;

        if (grownBuckets) {
            memset(grownBuckets, 0, nextBucketCap * sizeof(bytes));
            for (bytes i = 0u; i <= count; i++)
                (discard) bucketInsertRaw(grownBuckets, nextBucketCap, entries, i);
            if (buckets)
                MemoryArena_free(arena, buckets);
            Store_setBuckets(store, grownBuckets);
            Store_setBucketCap(store, nextBucketCap);
            Store_setTombstones(store, 0u);
        } else if (bucketInsertRaw(buckets, bucketCap, entries, count) && tombstones > 0u)
            Store_setTombstones(store, tombstones - 1u);

        Store_setCount(store, count + 1u);
        Store_setTotalDataBytes(store, Store_totalDataBytes(store) + addBytes);
        return true;
    }
    return false;
}

/** Removes a file's snapshot entry on deletion while preserving O(1) hash indices. */
bool SnapshotStore_remove(SnapshotStore *store, string path) {
    if (!store || !path)
        return false;
    MemoryArena *arena = Store_arena(store);
    if (!arena)
        return false;

    bytes pathLen = strlen(path);
    u64 hash = snapHash(path, pathLen);
    bytes bucketSlot = 0u;
    bytes idx = storeLookup(store, path, pathLen, hash, &bucketSlot);
    if (idx == SNAPSHOT_TOMBSTONE)
        return false;

    SnapshotEntry *entries = Store_entries(store);
    bytes *buckets = Store_buckets(store);
    bytes count = Store_count(store);

    bytes freedBytes = entries[idx].pathLen + entries[idx].length;
    bytes curTotal = Store_totalDataBytes(store);
    Store_setTotalDataBytes(store, curTotal >= freedBytes ? curTotal - freedBytes : 0u);
    MemoryArena_free(arena, entries[idx].path);
    MemoryArena_free(arena, entries[idx].data);
    buckets[bucketSlot] = SNAPSHOT_TOMBSTONE;
    Store_setTombstones(store, Store_tombstones(store) + 1u);

    if (idx + 1u < count) {
        SnapshotEntry last = entries[count - 1u];
        bytes lastBucket = 0u;
        bytes foundLast = storeLookup(store, last.path, last.pathLen, last.hash, &lastBucket);
        entries[idx] = last;
        if (foundLast != SNAPSHOT_TOMBSTONE)
            buckets[lastBucket] = idx + 1u;
    }
    Store_setCount(store, count - 1u);
    return true;
}

/** Begins an incremental directory walk for non-blocking TUI snapshot seeding. */
bool SnapshotSeeder_begin(SnapshotSeeder *seeder, SnapshotStore *store,
                          string rootDir, const FileEventWatcher *filter) {
    if (!seeder || !store || !rootDir || !rootDir[0])
        return false;
    memset(seeder, 0, sizeof(*seeder));
    DIR *dp = opendir(rootDir);
    if (!dp)
        return false;
    SnapshotSeedFrame *stack = Seeder_stackMut(seeder);
    stack[0].dp = dp;
    snprintf(stack[0].dirPath, sizeof(stack[0].dirPath), "%s", rootDir);
    Seeder_setStore(seeder, store);
    Seeder_setFilter(seeder, filter);
    Seeder_setDepth(seeder, 1u);
    Seeder_setActive(seeder, true);
    return true;
}

/** Processes up to maxFiles regular files; returns true while more work remains. */
bool SnapshotSeeder_step(SnapshotSeeder *seeder, bytes maxFiles) {
    if (!seeder || !Seeder_active(seeder))
        return false;
    SnapshotStore *store = Seeder_store(seeder);
    const FileEventWatcher *filter = Seeder_filter(seeder);
    SnapshotSeedFrame *stack = Seeder_stackMut(seeder);
    bytes limit = maxFiles > 0u ? maxFiles : 32u;
    bytes processed = 0u;

    bytes maxWs = Store_maxWorkspaceBytes(store);
    while (Seeder_depth(seeder) > 0u && processed < limit) {
        if (maxWs > 0u && Store_totalDataBytes(store) >= maxWs) {
            SnapshotSeeder_finish(seeder);
            return false;
        }
        bytes depth = Seeder_depth(seeder);
        bytes top = depth - 1u;
        struct dirent *ent = readdir(stack[top].dp);
        if (!ent) {
            closedir(stack[top].dp);
            stack[top].dp = nullptr;
            Seeder_setDepth(seeder, top);
            continue;
        }

        string name = Dirent_name(ent);
        if (snapStrEq(name, ".") || snapStrEq(name, ".."))
            continue;

        utf8 full[FILE_PATH_MAX];
        i32 n = snprintf(full, sizeof(full), "%s/%s", stack[top].dirPath, name);
        if (n <= 0 || (bytes) n >= sizeof(full))
            continue;

        struct stat st;
        if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (isSkippedDirName(name) || depth >= (bytes) SNAPSHOT_SEEDER_MAX_DEPTH)
                continue;
            DIR *sub = opendir(full);
            if (sub) {
                stack[depth].dp = sub;
                snprintf(stack[depth].dirPath, sizeof(stack[depth].dirPath), "%s", full);
                Seeder_setDepth(seeder, depth + 1u);
            }
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;
        if (filter && !FileEventWatcher_matchesPath(filter, full, false))
            continue;

        processed++;
        if (SnapshotStore_find(store, full))
            continue;

        bytes len = 0u;
        bool isBin = false;
        utf8 *buf = readFileKnownSize(store, full, (bytes) st.st_size, true, &len, &isBin);
        if (buf) {
            (discard) SnapshotStore_put(store, full, buf, len);
            SnapshotStore_freeBuf(store, buf);
        }
    }

    if (Seeder_depth(seeder) == 0u) {
        Seeder_setActive(seeder, false);
        return false;
    }
    return true;
}

/** Closes any open directory handles owned by an active or interrupted SnapshotSeeder. */
fn SnapshotSeeder_finish(SnapshotSeeder *seeder) {
    if (!seeder)
        return;
    SnapshotSeedFrame *stack = Seeder_stackMut(seeder);
    bytes depth = Seeder_depth(seeder);
    for (bytes i = 0u; i < depth; i++) {
        if (stack[i].dp) {
            closedir(stack[i].dp);
            stack[i].dp = nullptr;
        }
    }
    Seeder_setDepth(seeder, 0u);
    Seeder_setActive(seeder, false);
}

/** Seeds the store synchronously with all text file snapshots under rootDir. */
bool SnapshotStore_seedDir(SnapshotStore *store, string rootDir, const FileEventWatcher *filter) {
    if (!store || !rootDir || !rootDir[0])
        return false;
    SnapshotSeeder seeder;
    if (!SnapshotSeeder_begin(&seeder, store, rootDir, filter))
        return false;
    while (SnapshotSeeder_step(&seeder, 256u))
        ;
    SnapshotSeeder_finish(&seeder);
    return true;
}

/** Returns the number of tracked file snapshots. */
bytes SnapshotStore_count(const SnapshotStore *store) {
    return store ? Store_count(store) : 0u;
}
