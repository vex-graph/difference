#ifndef DIFFERENCE_SNAPSHOT_STORE_H
#define DIFFERENCE_SNAPSHOT_STORE_H

#include <dirent.h>
#include "vex.h"
#include "io/file/file.h"
#include "nio/mem.h"

/** snapshot_store.h, in-memory file content snapshots for realtime edit diffing. */

enum {
    SNAPSHOT_SEEDER_MAX_DEPTH = 17u
};

#define SNAPSHOT_DEFAULT_MAX_WORKSPACE_BYTES (1024ULL * 1024ULL * 1024ULL)

typedef struct SnapshotEntry {
    utf8 *path;
    utf8 *data;
    bytes pathLen;
    bytes length;
    u64 hash;
} SnapshotEntry;

typedef struct SnapshotStore {
    MemoryArena *arena;
    MemoryArena *scratch;
    bytes arenaBytes;
    SnapshotEntry *entries;
    bytes *buckets;
    bytes count;
    bytes capacity;
    bytes bucketCap;
    bytes tombstones;
    bytes maxFileBytes;
    bytes maxWorkspaceBytes;
    bytes totalDataBytes;
} SnapshotStore;

typedef struct SnapshotSeedFrame {
    DIR *dp;
    utf8 dirPath[FILE_PATH_MAX];
} SnapshotSeedFrame;

typedef struct SnapshotSeeder {
    SnapshotStore *store;
    const FileEventWatcher *filter;
    SnapshotSeedFrame stack[SNAPSHOT_SEEDER_MAX_DEPTH];
    bytes depth;
    bool active;
} SnapshotSeeder;

fn SnapshotStore_init(SnapshotStore *store, bytes maxFileBytes);
fn SnapshotStore_free(SnapshotStore *store);
fn SnapshotStore_setMaxWorkspaceBytes(SnapshotStore *store, bytes maxBytes);
bytes SnapshotStore_maxWorkspaceBytes(const SnapshotStore *store);
bytes SnapshotStore_totalDataBytes(const SnapshotStore *store);
bool SnapshotStore_seedDir(SnapshotStore *store, string rootDir, const FileEventWatcher *filter);
bool SnapshotSeeder_begin(SnapshotSeeder *seeder, SnapshotStore *store,
                          string rootDir, const FileEventWatcher *filter);
bool SnapshotSeeder_step(SnapshotSeeder *seeder, bytes maxFiles);
fn SnapshotSeeder_finish(SnapshotSeeder *seeder);
const SnapshotEntry *SnapshotStore_find(const SnapshotStore *store, string path);
bool SnapshotStore_put(SnapshotStore *store, string path, string data, bytes len);
bool SnapshotStore_remove(SnapshotStore *store, string path);
utf8 *SnapshotStore_readFile(SnapshotStore *store, string path, bytes *outLen, bool *outBinary);
fn SnapshotStore_freeBuf(SnapshotStore *store, utf8 *buf);
bytes SnapshotStore_count(const SnapshotStore *store);

#endif
