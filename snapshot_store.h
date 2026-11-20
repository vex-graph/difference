#ifndef DIFFERENCE_SNAPSHOT_STORE_H
#define DIFFERENCE_SNAPSHOT_STORE_H

#include "vex.h"
#include "io/file/file.h"
#include "nio/mem.h"

// snapshot_store.h — in-memory file content snapshots for realtime edit diffing.

typedef struct SnapshotEntry {
    char *path;
    char *data;
    bytes length;
} SnapshotEntry;

typedef struct SnapshotStore {
    MemoryArena *arena;
    MemoryArena *scratch;
    bytes arenaBytes;
    SnapshotEntry *entries;
    bytes count;
    bytes capacity;
    bytes maxFileBytes;
} SnapshotStore;

fn SnapshotStore_init(SnapshotStore *store, bytes maxFileBytes);
fn SnapshotStore_free(SnapshotStore *store);
bool SnapshotStore_seedDir(SnapshotStore *store, const char *rootDir, const FileEventWatcher *filter);
const SnapshotEntry *SnapshotStore_find(const SnapshotStore *store, const char *path);
bool SnapshotStore_put(SnapshotStore *store, const char *path, const char *data, bytes len);
bool SnapshotStore_remove(SnapshotStore *store, const char *path);
char *SnapshotStore_readFile(SnapshotStore *store, const char *path, bytes *outLen, bool *outBinary);
fn SnapshotStore_freeBuf(SnapshotStore *store, char *buf);
bytes SnapshotStore_count(const SnapshotStore *store);

#endif
